#!/usr/bin/env python3
"""
mock_babyos_device — virtual BabyOS device endpoint for host-side E2E tests.

Authority:
  - origin/master:tool/README.md          (BabyOS 协议说明)
  - origin/master:tool/b_protocol.py      (frame pack/parse)
  - bos/modules/inc/b_mod_protocol.h      (cmd structs / PROTOCOL_NEED_DEFAULT_ACK)
  - bos/modules/b_mod_protocol.c          (parse accept + default ACK list)
  - bos/algorithm/algo_crc.c              (CRC32 口径 — via device.crc_util)

This module provides:
  1. ByteChannel          — pty pair (preferred) or socketpair fallback
  2. MockBabyOSDevice     — device-side protocol responder (real frame I/O)
  3. XmodemReceiver       — simplified Xmodem-128 receiver (SOH/ACK/NAK/EOT)

Device semantics (firmware-faithful):
  0x1 TEST         -> empty ACK
  0x2 UTC          -> empty ACK (stores utc)
  0x3 FW_INFO      -> empty ACK, then FDATA seq=0.. stream, then OTA_RESULT
  0x6 TRANS_FILE   -> empty ACK, then FDATA seq=0.. stream, then OTA_RESULT
  0x4 FDATA        -> accumulate chunk; request next or finish + CRC check
  0x5 OTA_RESULT   -> record host ACK, no further reply
  0x7 GET_UID      -> len(1) + uid(n)
  0x8 WRITE_SN     -> empty ACK (stores sn)
  0xA DEVICEINFO   -> version(16) + name(16)

Python 3.8 compatible. Real byte I/O only — no stubs.
"""

from __future__ import print_function

import fcntl
import os
import socket
import struct
import sys
import termios
import threading
import time
import tty
from typing import Any, Callable, List, Optional, Tuple

# Make python/ importable when executed from test/device_features/
_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
if _PY_ROOT not in sys.path:
    sys.path.insert(0, _PY_ROOT)

from device import b_protocol as bp  # noqa: E402
from device.crc_util import crc32  # noqa: E402
from device.xmodem_ydmodem import (  # noqa: E402
    SOH, EOT, ACK, NAK, CAN, CRCPKT,
    crc16_ccitt,
)

# Command aliases (same values as b_protocol)
CMD_TEST = bp.CMD_TEST
CMD_UTC = bp.CMD_UTC
CMD_FW_INFO = bp.CMD_FW_INFO
CMD_FDATA = bp.CMD_FDATA
CMD_OTA_RESULT = bp.CMD_OTA_RESULT
CMD_TRANS_FILE = bp.CMD_TRANS_FILE
CMD_GET_UID = bp.CMD_GET_UID
CMD_WRITE_SN = bp.CMD_WRITE_SN
CMD_DEVICEINFO = bp.CMD_DEVICEINFO

OTA_RESULT_OK = bp.OTA_RESULT_OK
OTA_RESULT_CRC_ERROR = bp.OTA_RESULT_CRC_ERROR
OTA_RESULT_NAME_MISMATCH = bp.OTA_RESULT_NAME_MISMATCH
OTA_RESULT_LEN_INVALID = bp.OTA_RESULT_LEN_INVALID
OTA_RESULT_TIMEOUT = bp.OTA_RESULT_TIMEOUT

FDATA_CHUNK = bp.FDATA_CHUNK_SIZE  # 512
FW_NAME_SIZE = bp.FW_NAME_SIZE    # 64
DEVINFO_FIELD = bp.DEVINFO_FIELD_SIZE  # 16

_MAX_FRAME = 2048  # hard cap matching protocol_client


# ---------------------------------------------------------------------------
# Device-side endpoint (fd or socket)
# ---------------------------------------------------------------------------

class DeviceEndpoint:
    """Uniform non-blocking read/write over an os fd or a socket."""

    def __init__(self, obj: Any) -> None:
        self._obj = obj
        self._is_sock = isinstance(obj, socket.socket)
        if not self._is_sock:
            try:
                fl = fcntl.fcntl(obj, fcntl.F_GETFL)
                fcntl.fcntl(obj, fcntl.F_SETFL, fl | os.O_NONBLOCK)
            except Exception:
                pass
        else:
            try:
                obj.setblocking(False)
            except Exception:
                pass

    def read(self, n: int = 4096) -> bytes:
        if self._is_sock:
            try:
                return self._obj.recv(n)
            except (BlockingIOError, socket.error):
                return b''
        try:
            return os.read(self._obj, n)
        except (BlockingIOError, OSError):
            return b''

    def write(self, data: bytes) -> int:
        if isinstance(data, bytearray):
            data = bytes(data)
        if self._is_sock:
            try:
                return int(self._obj.send(data))
            except (BlockingIOError, socket.error):
                return 0
        try:
            return int(os.write(self._obj, data))
        except OSError:
            return 0

    def close(self) -> None:
        try:
            if self._is_sock:
                self._obj.close()
            else:
                os.close(self._obj)
        except Exception:
            pass


class SocketUart:
    """
    Host-side uart adapter over a socket (socketpair fallback).

    Same interface ProtocolClient needs: write / read_available / is_open.
    """

    def __init__(self, sock: socket.socket) -> None:
        self._sock = sock
        try:
            sock.setblocking(False)
        except Exception:
            pass
        self.port = 'SOCKETPAIR'
        self.baudrate = 115200

    def write(self, data: bytes) -> int:
        if data is None:
            return -1
        if isinstance(data, bytearray):
            data = bytes(data)
        if not isinstance(data, (bytes, bytearray)):
            return -1
        try:
            return int(self._sock.send(data))
        except (BlockingIOError, socket.error):
            return -1

    def read_available(self) -> bytes:
        chunks = []  # type: List[bytes]
        while True:
            try:
                d = self._sock.recv(4096)
            except (BlockingIOError, socket.error):
                break
            if not d:
                break
            chunks.append(d)
            if len(d) < 4096:
                break
        return b''.join(chunks)

    @property
    def is_open(self) -> bool:
        return True

    def close(self) -> None:
        try:
            self._sock.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Byte channel: pty (preferred) or socketpair
# ---------------------------------------------------------------------------

class ByteChannel:
    """One end for the host uart client, one end for the mock device."""

    def __init__(self, kind: str, host_uart: Any, device_obj: Any,
                 close_fn: Callable[[], None],
                 host_port: str = '') -> None:
        self.kind = kind                 # 'pty' | 'socketpair'
        self.host_uart = host_uart       # UartService or SocketUart
        self.device_obj = device_obj     # fd or socket for DeviceEndpoint
        self._close_fn = close_fn
        self.host_port = host_port       # pty slave path when kind=='pty'

    def device_endpoint(self) -> DeviceEndpoint:
        return DeviceEndpoint(self.device_obj)

    def close(self) -> None:
        try:
            if hasattr(self.host_uart, 'close'):
                self.host_uart.close()
        except Exception:
            pass
        try:
            self._close_fn()
        except Exception:
            pass


def _create_pty_channel() -> ByteChannel:
    """Open a pty pair; host side via studio UartService (pyserial)."""
    from device.uart_service import UartService

    master_fd, slave_fd = pty_open()
    for fd in (master_fd, slave_fd):
        _set_raw_nonblock(fd)
    slave_name = os.ttyname(slave_fd)
    uart = UartService()
    ok = uart.open(slave_name, 115200, timeout=0.02, write_timeout=1.0)
    if not ok:
        try:
            os.close(master_fd)
        except Exception:
            pass
        try:
            os.close(slave_fd)
        except Exception:
            pass
        raise RuntimeError('UartService.open failed on pty slave %s' % slave_name)

    def _close() -> None:
        for fd in (master_fd, slave_fd):
            try:
                os.close(fd)
            except Exception:
                pass

    return ByteChannel('pty', uart, master_fd, _close, host_port=slave_name)


def _create_socketpair_channel() -> ByteChannel:
    """Fallback when pty is unavailable: AF_UNIX socketpair byte pipe."""
    a, b = socket.socketpair()
    uart = SocketUart(a)

    def _close() -> None:
        try:
            a.close()
        except Exception:
            pass
        try:
            b.close()
        except Exception:
            pass

    return ByteChannel('socketpair', uart, b, _close, host_port='socketpair')


def create_byte_channel(prefer: str = 'pty') -> ByteChannel:
    """
    Create a host↔device byte channel.

    prefer='pty' tries pty first (studio UartService path), then socketpair.
    prefer='socketpair' skips pty.
    Returns a ByteChannel. Raises if both transports fail.
    """
    errors = []  # type: List[str]
    if prefer in ('pty', 'auto'):
        try:
            return _create_pty_channel()
        except Exception as exc:
            errors.append('pty: %s' % exc)
    if prefer in ('socketpair', 'auto', 'pty'):
        try:
            return _create_socketpair_channel()
        except Exception as exc:
            errors.append('socketpair: %s' % exc)
    raise RuntimeError('no byte channel available: %s' % '; '.join(errors))


def pty_open() -> Tuple[int, int]:
    import pty
    return pty.openpty()


def _set_raw_nonblock(fd: int) -> None:
    try:
        tty.setraw(fd)
    except Exception:
        pass
    try:
        fl = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)
    except Exception:
        pass


# ---------------------------------------------------------------------------
# Simplified Xmodem-128 receiver (device side)
# ---------------------------------------------------------------------------

class XmodemReceiver:
    """
    Standard-framing Xmodem-128 receiver.

    Framing: SOH(0x01) + blk + ~blk + 128B + CRC16(2B BE)
             EOT(0x04) -> ACK
             bad CRC / bad complement -> NAK(0x15)
             CAN(0x18) -> abort

    CRC16 uses device.xmodem_ydmodem.crc16_ccitt — the same BabyOS form
    XmodemSender uses — so host and device agree on the check value.
    """

    BLOCK_SIZE = 128
    FRAME_LEN = 1 + 1 + 1 + 128 + 2  # SOH + blk + ~blk + data + crc16

    def __init__(self, write_fn: Callable[[bytes], None],
                 log_fn: Optional[Callable[[str], None]] = None) -> None:
        self._write_fn = write_fn
        self._log = log_fn or (lambda *_: None)
        self.rx = bytearray()
        self.data = bytearray()          # delivered payload (ordered)
        self.blocks = {}                 # type: dict
        self.next_block = 1
        self.done = False
        self.error = None                # type: Optional[str]
        self.nak_count = 0
        self.ack_count = 0
        self.received_blocks = 0

    def start(self) -> None:
        """Request CRC-16 mode (standard receiver init)."""
        self._write_fn(bytes([CRCPKT]))
        self._log('[mock-device] xmodem: sent C (CRC-16 request)')

    def feed(self, data: bytes) -> None:
        if not data:
            return
        if self.done:
            return
        self.rx.extend(data)
        self._process()

    def _nak(self, reason: str) -> None:
        self.nak_count += 1
        self._log('[mock-device] xmodem NAK: %s' % reason)
        self._write_fn(bytes([NAK]))

    def _ack(self) -> None:
        self.ack_count += 1
        self._write_fn(bytes([ACK]))

    def _process(self) -> None:
        while not self.done:
            if not self.rx:
                return
            b0 = self.rx[0]
            if b0 == SOH:
                if len(self.rx) < self.FRAME_LEN:
                    return
                blk = self.rx[1]
                nblk = self.rx[2]
                if ((blk + nblk) & 0xFF) != 0xFF:
                    # drop SOH and resync
                    del self.rx[0]
                    self._nak('bad block complement blk=%d ~blk=%d' % (blk, nblk))
                    continue
                payload = bytes(self.rx[3:3 + self.BLOCK_SIZE])
                crc_recv = struct.unpack_from('>H', self.rx, 3 + self.BLOCK_SIZE)[0]
                crc_calc = crc16_ccitt(payload)
                if crc_recv != crc_calc:
                    del self.rx[0]
                    self._nak('crc mismatch blk=%d recv=0x%04X calc=0x%04X'
                              % (blk, crc_recv, crc_calc))
                    continue
                del self.rx[0:self.FRAME_LEN]
                self.blocks[blk] = payload
                self.received_blocks += 1
                # deliver in order
                while self.next_block in self.blocks:
                    self.data.extend(self.blocks.pop(self.next_block))
                    self.next_block += 1
                self._ack()
                self._log('[mock-device] xmodem ACK blk=%d total=%d'
                          % (blk, len(self.data)))
            elif b0 == EOT:
                del self.rx[0]
                self._ack()
                self.done = True
                self._log('[mock-device] xmodem EOT, done bytes=%d' % len(self.data))
                return
            elif b0 == CAN:
                del self.rx[0]
                self.done = True
                self.error = 'cancelled'
                self._log('[mock-device] xmodem CAN from host')
                return
            elif b0 == CRCPKT:
                # host re-requesting CRC mode mid-stream — ACK start again
                del self.rx[0]
                if self.next_block == 1 and not self.data:
                    self.start()
            else:
                # unknown byte — discard (resync)
                del self.rx[0]


# ---------------------------------------------------------------------------
# Mock BabyOS device
# ---------------------------------------------------------------------------

class MockBabyOSDevice:
    """
    Device-side BabyOS protocol responder over a byte endpoint.

    Runs a background pump thread. Parses host frames and replies with
    firmware-faithful semantics (see module docstring).
    """

    def __init__(self,
                 device_obj: Any,
                 device_id: int = 0x0000ABCD,
                 uid: bytes = b'\x11\x22\x33\x44\x55\x66\x77\x88\x99\xAA\xBB\xCC',
                 version: str = 'v1.2.3',
                 model: str = 'BabyOS-MCU',
                 force_crc_error: bool = False,
                 corrupt_fdata: bool = False,
                 log_fn: Optional[Callable[[str], None]] = None) -> None:
        self.endpoint = (device_obj if isinstance(device_obj, DeviceEndpoint)
                         else DeviceEndpoint(device_obj))
        self.device_id = device_id & 0xFFFFFFFF
        self.uid = bytes(uid or b'')
        self.version = version or ''
        self.model = model or ''
        self.force_crc_error = bool(force_crc_error)
        self.corrupt_fdata = bool(corrupt_fdata)
        self._log_fn = log_fn or (lambda *_: None)

        self.rx_buf = bytearray()
        self.mode = 'protocol'           # 'protocol' | 'xmodem'
        self.xmodem = None               # type: Optional[XmodemReceiver]

        # observable state for tests
        self.written_cmds = []           # type: List[int]
        self.host_acks = []              # type: List[Tuple[int, bytes]]
        self.fw_info = None              # type: Optional[Tuple[int, int, str]]
        self.trans_file = None           # type: Optional[Tuple[int, int, int, int]]
        self.received_chunks = []        # type: List[Tuple[int, bytes]]
        self.received_fw_bytes = bytearray()
        self.last_utc = None             # type: Optional[int]
        self.last_sn = None              # type: Optional[bytes]
        self.last_ota_result = None      # type: Optional[int]
        self.frames_handled = 0

        self._stop = threading.Event()
        self._thread = None              # type: Optional[threading.Thread]
        self._lock = threading.Lock()

    # -- lifecycle --------------------------------------------------------

    def start(self) -> None:
        if self._thread is not None:
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, name='MockBabyOSDevice')
        self._thread.daemon = True
        self._thread.start()

    def stop(self, timeout: float = 2.0) -> None:
        self._stop.set()
        t = self._thread
        if t is not None and t.is_alive():
            t.join(timeout=timeout)
        self._thread = None
        try:
            self.endpoint.close()
        except Exception:
            pass

    def _log(self, msg: str) -> None:
        try:
            self._log_fn(msg)
        except Exception:
            pass

    def _run(self) -> None:
        while not self._stop.is_set():
            self.pump(timeout=0.02)

    def pump(self, timeout: float = 0.02) -> None:
        """Read available bytes and dispatch (protocol or xmodem)."""
        # brief wait via select-like poll: non-blocking read + sleep
        data = self.endpoint.read(8192)
        if not data:
            if timeout > 0:
                time.sleep(min(timeout, 0.02))
            return
        if self.mode == 'xmodem':
            if self.xmodem is not None:
                self.xmodem.feed(data)
            return
        self._feed_protocol(data)

    # -- tx helpers -------------------------------------------------------

    def _send_frame(self, cmd: int, param: bytes = b'') -> None:
        frame = bp.pack_frame(self.device_id, cmd, param or b'')
        self.endpoint.write(frame)
        self._log('[mock-device] tx cmd=0x%02X len=%d' % (cmd & 0xFF, len(frame)))

    # -- protocol RX ------------------------------------------------------

    def _feed_protocol(self, raw: bytes) -> None:
        self.rx_buf.extend(raw)
        while True:
            frame = self._extract_one()
            if frame is None:
                break
            dev_id, cmd, param = frame
            self.frames_handled += 1
            with self._lock:
                self.written_cmds.append(cmd)
            self._log('[mock-device] rx cmd=0x%02X id=0x%08X plen=%d'
                      % (cmd & 0xFF, dev_id & 0xFFFFFFFF, len(param)))
            self._handle(cmd, param)

    def _extract_one(self) -> Optional[Tuple[int, int, bytes]]:
        buf = self.rx_buf
        start = 0
        n = len(buf)
        while start < n and buf[start] != bp.PROTOCOL_HEAD:
            start += 1
        if start:
            del buf[:start]
            n = len(buf)
        if n < bp.FRAME_HEADER_SIZE + 1:
            return None
        device_id = int.from_bytes(buf[1:5], 'little')
        frame_len = int.from_bytes(buf[5:7], 'little')
        cmd = buf[7]
        total_len = bp.FRAME_HEADER_SIZE + frame_len
        if frame_len < 1 or total_len > _MAX_FRAME:
            del buf[0]
            return None
        if total_len > len(buf):
            return None
        if bp.calc_checksum(bytes(buf[:total_len - 1])) != buf[total_len - 1]:
            del buf[0]
            return None
        param = bytes(buf[8:total_len - 1])
        del buf[:total_len]
        return device_id, cmd, param

    # -- command handlers -------------------------------------------------

    def _handle(self, cmd: int, param: bytes) -> None:
        if cmd == CMD_TEST:
            # firmware device-role PROTOCOL_NEED_DEFAULT_ACK includes TEST
            self._send_frame(CMD_TEST, b'')
        elif cmd == CMD_UTC:
            utc = None
            if param is not None and len(param) >= 4:
                utc = struct.unpack_from('<I', param, 0)[0]
            self.last_utc = utc
            self._send_frame(CMD_UTC, b'')
        elif cmd == CMD_FW_INFO:
            parsed = bp.parse_fw_info_param(param)
            self.fw_info = parsed
            self._reset_transfer_state()
            self._send_frame(CMD_FW_INFO, b'')
            if parsed is not None and parsed[0] > 0:
                self._send_frame(CMD_FDATA, bp.build_fdata_req_param(0))
            else:
                # zero-size: report len_invalid like firmware would
                self._send_frame(CMD_OTA_RESULT,
                                 bytes([OTA_RESULT_LEN_INVALID & 0xFF]))
        elif cmd == CMD_TRANS_FILE:
            parsed = bp.parse_trans_file_param(param)
            self.trans_file = parsed
            self._reset_transfer_state()
            self._send_frame(CMD_TRANS_FILE, b'')
            if parsed is not None and parsed[0] > 0:
                self._send_frame(CMD_FDATA, bp.build_fdata_req_param(0))
            else:
                self._send_frame(CMD_OTA_RESULT,
                                 bytes([OTA_RESULT_LEN_INVALID & 0xFF]))
        elif cmd == CMD_FDATA:
            self._handle_fdata(param)
        elif cmd == CMD_OTA_RESULT:
            # host ACK — record, no reply
            with self._lock:
                self.host_acks.append((cmd, bytes(param)))
        elif cmd == CMD_GET_UID:
            body = bytes([len(self.uid) & 0xFF]) + bytes(self.uid)
            self._send_frame(CMD_GET_UID, body)
        elif cmd == CMD_WRITE_SN:
            with self._lock:
                self.host_acks.append((cmd, bytes(param)))
                self.last_sn = bytes(param)
            self._send_frame(CMD_WRITE_SN, b'')
        elif cmd == CMD_DEVICEINFO:
            self._send_frame(CMD_DEVICEINFO,
                             bp.build_devinfo_param(self.version, self.model))
        else:
            # unknown: empty ACK (firmware default for many cmds)
            self._send_frame(cmd, b'')

    def _reset_transfer_state(self) -> None:
        self.received_chunks = []
        self.received_fw_bytes = bytearray()
        self.last_ota_result = None

    def _transfer_size_crc(self) -> Tuple[int, int]:
        if self.fw_info is not None:
            return self.fw_info[0], self.fw_info[1]
        if self.trans_file is not None:
            return self.trans_file[0], self.trans_file[1]
        return 0, 0

    def _handle_fdata(self, param: bytes) -> None:
        parsed = bp.parse_fdata_param(param)
        if parsed is None:
            # maybe a device-role style 2-byte request leaked through — ignore
            return
        seq, data = parsed
        size, expected_crc = self._transfer_size_crc()
        self.received_chunks.append((seq, bytes(data)))
        base = seq * FDATA_CHUNK
        if size:
            take = max(0, size - base)
            chunk = bytearray(data[:take])
        else:
            chunk = bytearray(data)
        if self.corrupt_fdata and chunk and seq == 0:
            # simulate line corruption on the first chunk only so the
            # device-side CRC check genuinely fails against announced crc
            chunk[0] = (chunk[0] ^ 0xFF) & 0xFF
        self.received_fw_bytes.extend(chunk)

        next_seq = seq + 1
        if size and next_seq * FDATA_CHUNK < size:
            self._send_frame(CMD_FDATA, bp.build_fdata_req_param(next_seq))
            return

        # transfer complete — firmware CRC verify + OTA_RESULT
        got = bytes(self.received_fw_bytes[:size]) if size else bytes(self.received_fw_bytes)
        crc_local = crc32(got)
        if self.force_crc_error:
            result = OTA_RESULT_CRC_ERROR
        elif size and crc_local != expected_crc:
            result = OTA_RESULT_CRC_ERROR
        else:
            result = OTA_RESULT_OK
        self.last_ota_result = result
        self._log('[mock-device] transfer done size=%d crc_local=0x%08X '
                  'expected=0x%08X result=%d'
                  % (size, crc_local & 0xFFFFFFFF, expected_crc & 0xFFFFFFFF, result))
        self._send_frame(CMD_OTA_RESULT, bytes([result & 0xFF]))

    # -- xmodem mode ------------------------------------------------------

    def start_xmodem_receive(self,
                             log_fn: Optional[Callable[[str], None]] = None
                             ) -> XmodemReceiver:
        """Switch to xmodem mode and request CRC-16 ('C')."""
        self.mode = 'xmodem'
        self.xmodem = XmodemReceiver(self.endpoint.write, log_fn=log_fn or self._log)
        self.xmodem.start()
        return self.xmodem

    def switch_to_protocol(self) -> None:
        self.mode = 'protocol'
        self.xmodem = None
        self.rx_buf = bytearray()

    # -- introspection ----------------------------------------------------

    @property
    def received_fw(self) -> bytes:
        return bytes(self.received_fw_bytes)

    def snapshot(self) -> dict:
        with self._lock:
            return {
                'kind': 'mock_babyos_device',
                'device_id': self.device_id,
                'mode': self.mode,
                'frames_handled': self.frames_handled,
                'written_cmds': list(self.written_cmds),
                'host_acks': list(self.host_acks),
                'fw_info': self.fw_info,
                'trans_file': self.trans_file,
                'received_chunks': len(self.received_chunks),
                'received_fw_len': len(self.received_fw_bytes),
                'last_utc': self.last_utc,
                'last_sn': self.last_sn,
                'last_ota_result': self.last_ota_result,
                'xmodem_done': bool(self.xmodem is not None and self.xmodem.done),
                'xmodem_bytes': len(self.xmodem.data) if self.xmodem else 0,
            }


# ---------------------------------------------------------------------------
# CLI smoke (manual): python mock_babyos_device.py
# ---------------------------------------------------------------------------

def _cli_smoke() -> int:
    """Open a pty, run mock device, drive ProtocolClient through test_link."""
    import tempfile
    from device.protocol_client import ProtocolClient

    chan = create_byte_channel(prefer='auto')
    logs = []
    device = MockBabyOSDevice(chan.device_endpoint(), log_fn=logs.append)
    device.start()
    pc = ProtocolClient(chan.host_uart, log_fn=logs.append)
    try:
        resp = pc.test_link(timeout=2.0)
        print('channel=%s test_link=%s' % (chan.kind, resp is not None))
        print('device_cmds=%s' % device.written_cmds)
        ok = resp is not None and resp[1] == CMD_TEST
        # OTA smoke
        with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
            payload = bytes(bytearray((i * 13 + 5) & 0xFF for i in range(600)))
            f.write(payload)
            path = f.name
        try:
            ota_ok = pc.start_ota(path, timeout=5.0)
            print('ota_ok=%s received=%d expected=%d'
                  % (ota_ok, len(device.received_fw), len(payload)))
            ok = ok and ota_ok and device.received_fw[:len(payload)] == payload
        finally:
            try:
                os.unlink(path)
            except Exception:
                pass
        return 0 if ok else 1
    finally:
        device.stop()
        chan.close()


if __name__ == '__main__':
    sys.exit(_cli_smoke())
