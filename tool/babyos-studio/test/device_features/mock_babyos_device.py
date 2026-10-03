#!/usr/bin/env python3
"""
mock_babyos_device — virtual BabyOS device endpoint for host-side E2E tests.

Authority:
  - origin/master:tool/README.md          (BabyOS 协议说明)
  - origin/master:tool/b_protocol.py      (frame pack/parse)
  - bos/modules/inc/b_mod_protocol.h      (cmd structs / PROTOCOL_NEED_DEFAULT_ACK)
  - bos/modules/b_mod_protocol.c          (parse accept + default ACK + TEA)
  - bos/modules/b_mod_param.c             (param shell text)
  - bos/algorithm/algo_crc.c              (CRC32 口径 — via device.crc_util)

This module provides:
  1. ByteChannel          — pty pair (preferred) or socketpair fallback
  2. MockBabyOSDevice     — device-side protocol responder (real frame I/O)
  3. XmodemReceiver       — Xmodem-128 receiver (SOH/ACK/NAK/EOT)
  4. YmodemReceiver       — Ymodem-1K receiver (block0 name+size + STX 1K)
  5. open_api_over_pty    — DeviceManager ↔ pty host ↔ mock device link
                            (FastAPI TestClient / uvicorn walk
                             API → UART → pty → Mock device)

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

TEA (_PROTO_ENCRYPT_ENABLE):
  Host ProtocolClient(encrypt=True) TEA-encrypts whole TX frames.
  When device.encrypt=True the mock decrypts RX frames before parse
  (firmware _bProtocolDecrypt). Device replies stay plaintext — host
  ProtocolClient encrypt flag only affects TX; RX parse is plaintext.

Shell text (b_mod_param.c):
  "param\r"                 -> ": name\\r\\n" each
  "param <name>\\r"         -> "<name>:<value>\\r\\n"
  "param <name> <value>\\r" -> atoi set, no success line

Mode switching (tests must switch explicitly when reusing the port):
  switch_to_protocol() / switch_to_shell() / switch_to_xmodem() /
  switch_to_ymodem()

Assertion surface:
  received_fw / received_file / received_file_name / shell_log /
  shell_params / written_cmds / last_utc / last_sn / last_ota_result

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
from typing import Any, Callable, Dict, List, Optional, Tuple

# Make python/ importable when executed from test/device_features/
_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
if _PY_ROOT not in sys.path:
    sys.path.insert(0, _PY_ROOT)

from device import b_protocol as bp  # noqa: E402
from device.crc_util import crc32  # noqa: E402
from device.xmodem_ydmodem import (  # noqa: E402
    SOH, STX, EOT, ACK, NAK, CAN, CRCPKT,
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

# Default shell params (mirror test/selftest/test_modules.c + studio UI)
DEFAULT_SHELL_PARAMS = {
    'g_param_test_val': 12345,
    'g_param_test_val2': -999,
    'g_volume': 50,
}


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
        self.host_uart = host_uart       # UartService | SocketUart | None
        self.device_obj = device_obj     # fd or socket for DeviceEndpoint
        self._close_fn = close_fn
        self.host_port = host_port       # pty slave path when kind=='pty'

    def device_endpoint(self) -> DeviceEndpoint:
        return DeviceEndpoint(self.device_obj)

    def disconnect_device_side(self) -> None:
        """Close only the device end (simulate device unplug / broken pty master)."""
        try:
            if isinstance(self.device_obj, socket.socket):
                self.device_obj.close()
            else:
                os.close(self.device_obj)
        except Exception:
            pass

    def close(self) -> None:
        try:
            if self.host_uart is not None and hasattr(self.host_uart, 'close'):
                self.host_uart.close()
        except Exception:
            pass
        try:
            self._close_fn()
        except Exception:
            pass


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


def _create_pty_channel(open_host_uart: bool = True) -> ByteChannel:
    """
    Open a pty pair.

    open_host_uart=True  → host end opened via studio UartService (pyserial)
    open_host_uart=False → host end left for DeviceManager / TestClient to
                           open on host_port (API-over-pty path)
    """
    master_fd, slave_fd = pty_open()
    for fd in (master_fd, slave_fd):
        _set_raw_nonblock(fd)
    slave_name = os.ttyname(slave_fd)

    uart = None  # type: Any
    if open_host_uart:
        from device.uart_service import UartService
        uart = UartService()
        ok = uart.open(slave_name, 115200, timeout=0.02, write_timeout=1.0)
        if not ok:
            for fd in (master_fd, slave_fd):
                try:
                    os.close(fd)
                except Exception:
                    pass
            raise RuntimeError('UartService.open failed on pty slave %s'
                               % slave_name)

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


def create_byte_channel(prefer: str = 'pty',
                        open_host_uart: bool = True,
                        require_pty: bool = False) -> ByteChannel:
    """
    Create a host↔device byte channel.

    prefer='pty' tries pty first (studio UartService path), then socketpair.
    prefer='socketpair' skips pty.
    prefer='auto' tries pty then socketpair.

    open_host_uart:
      True  → open UartService on the pty slave now (ProtocolClient path)
      False → leave host_port for DeviceManager/TestClient to open

    require_pty=True:
      Raise if a real pty pair cannot be created (acceptance tests).
      kind will always be 'pty' on success.

    Returns a ByteChannel. Raises if both transports fail.
    """
    errors = []  # type: List[str]
    if require_pty:
        try:
            return _create_pty_channel(open_host_uart=open_host_uart)
        except Exception as exc:
            raise RuntimeError('require_pty: pty unavailable: %s' % exc)
    if prefer in ('pty', 'auto'):
        try:
            return _create_pty_channel(open_host_uart=open_host_uart)
        except Exception as exc:
            errors.append('pty: %s' % exc)
    if prefer in ('socketpair', 'auto', 'pty'):
        try:
            return _create_socketpair_channel()
        except Exception as exc:
            errors.append('socketpair: %s' % exc)
    raise RuntimeError('no byte channel available: %s' % '; '.join(errors))


# ---------------------------------------------------------------------------
# Xmodem-128 receiver (device side)
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

    Fault injection (edge/exception testing):
      send_start_byte=False  — never emit initial 'C' (host waits / times out)
      nak_on_block=N         — NAK the N-th data block once (mid-transfer NAK)
      reject_eot=True        — NAK every EOT instead of ACK (host retries / aborts)
      suppress_acks=False    — when True, drop ACK/NAK entirely (link silence)
    """

    BLOCK_SIZE = 128
    FRAME_LEN = 1 + 1 + 1 + 128 + 2  # SOH + blk + ~blk + data + crc16

    def __init__(self, write_fn: Callable[[bytes], None],
                 log_fn: Optional[Callable[[str], None]] = None,
                 send_start_byte: bool = True,
                 nak_on_block: Optional[int] = None,
                 reject_eot: bool = False,
                 suppress_acks: bool = False) -> None:
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
        self.send_start_byte = bool(send_start_byte)
        self.nak_on_block = nak_on_block
        self.reject_eot = bool(reject_eot)
        self.suppress_acks = bool(suppress_acks)
        self._nak_injected = False
        self.eot_nak_count = 0

    def start(self) -> None:
        """Request CRC-16 mode (standard receiver init)."""
        if not self.send_start_byte:
            self._log('[mock-device] xmodem: start byte suppressed')
            return
        self._write_fn(bytes([CRCPKT]))
        self._log('[mock-device] xmodem: sent C (CRC-16 request)')

    def feed(self, data: bytes) -> None:
        if not data:
            return
        if self.done:
            return
        self.rx.extend(data)
        self._process()

    def _write(self, b: int) -> None:
        if self.suppress_acks:
            return
        self._write_fn(bytes([b]))

    def _nak(self, reason: str) -> None:
        self.nak_count += 1
        self._log('[mock-device] xmodem NAK: %s' % reason)
        self._write(NAK)

    def _ack(self) -> None:
        self.ack_count += 1
        self._write(ACK)

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
                # mid-transfer NAK injection (once per configured block)
                if (self.nak_on_block is not None and not self._nak_injected
                        and blk == (self.nak_on_block & 0xFF)):
                    self._nak_injected = True
                    del self.rx[0:self.FRAME_LEN]
                    self._nak('injected NAK on blk=%d' % blk)
                    continue
                del self.rx[0:self.FRAME_LEN]
                self.blocks[blk] = payload
                self.received_blocks += 1
                while self.next_block in self.blocks:
                    self.data.extend(self.blocks.pop(self.next_block))
                    self.next_block += 1
                self._ack()
                self._log('[mock-device] xmodem ACK blk=%d total=%d'
                          % (blk, len(self.data)))
            elif b0 == EOT:
                del self.rx[0]
                if self.reject_eot:
                    self.eot_nak_count += 1
                    self._nak('EOT rejected (fault inject)')
                    continue
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
                del self.rx[0]
                if self.next_block == 1 and not self.data:
                    self.start()
            else:
                del self.rx[0]


class YmodemReceiver:
    """
    Ymodem-1K receiver matching studio YmodemSender framing.

    Block 0:  SOH + 00 + FF + "name\\0size\\0" + pad(128) + CRC16
    Data:     STX + blk + ~blk + 1024B + CRC16
    Tail pad: SOH + blk + ~blk + zeros(128) + CRC16   (when size % 1024 != 0)
    EOT:      ACK (sender completes on first ACK)

    Always CRC-16 (receiver starts with 'C'). Filename + declared size are
    captured from block 0 and used to trim delivered data.

    Fault injection mirrors XmodemReceiver (send_start_byte / nak_on_block /
    reject_eot / suppress_acks).
    """

    BLOCK0_LEN = 1 + 1 + 1 + 128 + 2      # SOH + blk + ~blk + 128 + crc16
    STX_LEN = 1 + 1 + 1 + 1024 + 2        # STX + blk + ~blk + 1024 + crc16

    def __init__(self, write_fn: Callable[[bytes], None],
                 log_fn: Optional[Callable[[str], None]] = None,
                 send_start_byte: bool = True,
                 nak_on_block: Optional[int] = None,
                 reject_eot: bool = False,
                 suppress_acks: bool = False) -> None:
        self._write_fn = write_fn
        self._log = log_fn or (lambda *_: None)
        self.rx = bytearray()
        self.filename = ''
        self.file_size = 0
        self.data = bytearray()
        self.blocks = {}                 # type: Dict[int, bytes]
        self.next_block = 1
        self.block0_seen = False
        self.done = False
        self.error = None                # type: Optional[str]
        self.nak_count = 0
        self.ack_count = 0
        self.received_blocks = 0
        self.send_start_byte = bool(send_start_byte)
        self.nak_on_block = nak_on_block
        self.reject_eot = bool(reject_eot)
        self.suppress_acks = bool(suppress_acks)
        self._nak_injected = False
        self.eot_nak_count = 0

    def start(self) -> None:
        """Request CRC-16 mode (standard Ymodem receiver init)."""
        if not self.send_start_byte:
            self._log('[mock-device] ymodem: start byte suppressed')
            return
        self._write_fn(bytes([CRCPKT]))
        self._log('[mock-device] ymodem: sent C (CRC-16 request)')

    def feed(self, data: bytes) -> None:
        if not data or self.done:
            return
        self.rx.extend(data)
        self._process()

    def _write(self, b: int) -> None:
        if self.suppress_acks:
            return
        self._write_fn(bytes([b]))

    def _nak(self, reason: str) -> None:
        self.nak_count += 1
        self._log('[mock-device] ymodem NAK: %s' % reason)
        self._write(NAK)

    def _ack(self) -> None:
        self.ack_count += 1
        self._write(ACK)

    @staticmethod
    def _parse_block0(payload: bytes) -> Tuple[str, int]:
        """Parse Ymodem block0 payload → (filename, size)."""
        parts = payload.split(b'\x00')
        name = parts[0].decode('latin-1', errors='replace') if parts else ''
        size = 0
        if len(parts) > 1 and parts[1]:
            try:
                size = int(parts[1].decode('ascii', errors='replace'))
            except ValueError:
                size = 0
        return name, size

    def _deliver_ordered(self) -> None:
        while self.next_block in self.blocks:
            self.data.extend(self.blocks.pop(self.next_block))
            self.next_block += 1

    def _trim_to_size(self) -> bytes:
        if self.file_size and self.file_size <= len(self.data):
            return bytes(self.data[:self.file_size])
        return bytes(self.data)

    def _process(self) -> None:
        while not self.done:
            if not self.rx:
                return
            b0 = self.rx[0]
            if b0 == SOH:
                if len(self.rx) < self.BLOCK0_LEN:
                    return
                blk = self.rx[1]
                nblk = self.rx[2]
                if ((blk + nblk) & 0xFF) != 0xFF:
                    del self.rx[0]
                    self._nak('bad SOH complement blk=%d ~blk=%d' % (blk, nblk))
                    continue
                payload = bytes(self.rx[3:3 + 128])
                crc_recv = struct.unpack_from('>H', self.rx, 3 + 128)[0]
                crc_calc = crc16_ccitt(payload)
                if crc_recv != crc_calc:
                    del self.rx[0]
                    self._nak('SOH crc mismatch blk=%d' % blk)
                    continue
                del self.rx[0:self.BLOCK0_LEN]
                if not self.block0_seen:
                    if blk != 0:
                        self._nak('expected block0, got blk=%d' % blk)
                        continue
                    self.filename, self.file_size = self._parse_block0(payload)
                    self.block0_seen = True
                    self._ack()
                    self._log('[mock-device] ymodem block0 name=%r size=%d'
                              % (self.filename, self.file_size))
                else:
                    # tail padding block (128 zeros) — ACK and continue
                    if (self.nak_on_block is not None and not self._nak_injected
                            and blk == (self.nak_on_block & 0xFF)):
                        self._nak_injected = True
                        self._nak('injected NAK on ymodem blk=%d' % blk)
                        continue
                    self.received_blocks += 1
                    self._ack()
                    self._log('[mock-device] ymodem pad ACK blk=%d' % blk)
            elif b0 == STX:
                if not self.block0_seen:
                    del self.rx[0]
                    self._nak('STX before block0')
                    continue
                if len(self.rx) < self.STX_LEN:
                    return
                blk = self.rx[1]
                nblk = self.rx[2]
                if ((blk + nblk) & 0xFF) != 0xFF:
                    del self.rx[0]
                    self._nak('bad STX complement blk=%d ~blk=%d' % (blk, nblk))
                    continue
                payload = bytes(self.rx[3:3 + 1024])
                crc_recv = struct.unpack_from('>H', self.rx, 3 + 1024)[0]
                crc_calc = crc16_ccitt(payload)
                if crc_recv != crc_calc:
                    del self.rx[0]
                    self._nak('STX crc mismatch blk=%d' % blk)
                    continue
                if (self.nak_on_block is not None and not self._nak_injected
                        and blk == (self.nak_on_block & 0xFF)):
                    self._nak_injected = True
                    del self.rx[0:self.STX_LEN]
                    self._nak('injected NAK on ymodem STX blk=%d' % blk)
                    continue
                del self.rx[0:self.STX_LEN]
                self.blocks[blk] = payload
                self.received_blocks += 1
                self._deliver_ordered()
                self._ack()
                self._log('[mock-device] ymodem ACK blk=%d total=%d'
                          % (blk, len(self.data)))
            elif b0 == EOT:
                del self.rx[0]
                if not self.block0_seen:
                    self._nak('EOT before block0')
                    continue
                if self.reject_eot:
                    self.eot_nak_count += 1
                    self._nak('ymodem EOT rejected (fault inject)')
                    continue
                self._ack()
                self.done = True
                got = self._trim_to_size()
                self.data = bytearray(got)
                self._log('[mock-device] ymodem EOT done name=%r size=%d bytes=%d'
                          % (self.filename, self.file_size, len(self.data)))
                return
            elif b0 == CAN:
                del self.rx[0]
                self.done = True
                self.error = 'cancelled'
                self._log('[mock-device] ymodem CAN from host')
                return
            elif b0 == CRCPKT:
                del self.rx[0]
                if not self.block0_seen:
                    self.start()
            else:
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
                 encrypt: bool = False,
                 shell_params: Optional[Dict[str, int]] = None,
                 log_fn: Optional[Callable[[str], None]] = None) -> None:
        self.endpoint = (device_obj if isinstance(device_obj, DeviceEndpoint)
                         else DeviceEndpoint(device_obj))
        self.device_id = device_id & 0xFFFFFFFF
        self.uid = bytes(uid or b'')
        self.version = version or ''
        self.model = model or ''
        self.force_crc_error = bool(force_crc_error)
        self.corrupt_fdata = bool(corrupt_fdata)
        # When True, decrypt RX frames (host ProtocolClient encrypt=True)
        self.encrypt = bool(encrypt)
        self._log_fn = log_fn or (lambda *_: None)

        # ---- fault injection (edge / exception scenarios) ----------------
        # drop_after_n_frames: after N frames handled, stop all responses
        self.drop_after_n_frames = None            # type: Optional[int]
        self._frames_handled_limit = None          # type: Optional[int]
        # stop_responding_after(cmd, n): after n frames of cmd, silence that cmd
        self.stop_responding_after_cmd = None      # type: Optional[int]
        self.stop_responding_after_n = None        # type: Optional[int]
        self._cmd_silent_counts = {}               # type: Dict[int, int]
        # inject_garbage_between_frames: device TX inserts junk between frames
        self.inject_garbage_between_frames = False
        # corrupt_next_fdata_chunk: corrupt the next received FDATA chunk
        self.corrupt_next_fdata_chunk = False
        self._corrupt_next_fdata = False
        # send_wrong_cmd: reply with this cmd instead of the expected one
        self.send_wrong_cmd = None                 # type: Optional[int]
        # send_duplicate_frame: emit every TX frame twice
        self.send_duplicate_frame = False
        # oversized_length_field: next device TX frame lies about length
        self.oversized_length_field = False
        # ota_result override: force OTA_RESULT_* codes 0..4 on transfer end
        self.ota_result_override = None            # type: Optional[int]
        # close_pty_mid_transfer / stop-read: simulate link break
        self.link_broken = False
        # xmodem/ymodem fault knobs forwarded to receivers
        self.xm_fault = {}                         # type: Dict[str, Any]
        self.ym_fault = {}                         # type: Dict[str, Any]

        self.rx_buf = bytearray()
        self.mode = 'protocol'           # protocol|shell|xmodem|ymodem
        self.xmodem = None               # type: Optional[XmodemReceiver]
        self.ymodem = None               # type: Optional[YmodemReceiver]

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

        # shell state (b_mod_param.c)
        self.shell_params = dict(shell_params if shell_params is not None
                                 else DEFAULT_SHELL_PARAMS)
        self.shell_log = []              # type: List[Tuple[str, str]]
        self._shell_rx = bytearray()

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
        """Read available bytes and dispatch by mode."""
        if self.link_broken:
            if timeout > 0:
                time.sleep(min(timeout, 0.02))
            return
        data = self.endpoint.read(8192)
        if not data:
            if timeout > 0:
                time.sleep(min(timeout, 0.02))
            return
        if self.mode == 'xmodem':
            if self.xmodem is not None:
                self.xmodem.feed(data)
            return
        if self.mode == 'ymodem':
            if self.ymodem is not None:
                self.ymodem.feed(data)
            return
        if self.mode == 'shell':
            self._feed_shell(data)
            return
        self._feed_protocol(data)

    # -- fault-injection helpers ------------------------------------------

    def stop_responding_after(self, cmd: int, n: int) -> None:
        """After n frames of `cmd`, stop replying to that command."""
        self.stop_responding_after_cmd = cmd & 0xFF
        self.stop_responding_after_n = max(0, int(n))
        self._cmd_silent_counts = {}

    def clear_faults(self) -> None:
        """Reset all fault-injection knobs to normal operation."""
        self.drop_after_n_frames = None
        self._frames_handled_limit = None
        self.stop_responding_after_cmd = None
        self.stop_responding_after_n = None
        self._cmd_silent_counts = {}
        self.inject_garbage_between_frames = False
        self.corrupt_next_fdata_chunk = False
        self._corrupt_next_fdata = False
        self.send_wrong_cmd = None
        self.send_duplicate_frame = False
        self.oversized_length_field = False
        self.ota_result_override = None
        self.link_broken = False
        self.force_crc_error = False
        self.corrupt_fdata = False
        self.xm_fault = {}
        self.ym_fault = {}

    def _should_silence(self, cmd: int) -> bool:
        """Return True when fault injection says this cmd must not reply."""
        if self.link_broken:
            return True
        if self._frames_handled_limit is not None:
            if self.frames_handled > self._frames_handled_limit:
                return True
        if (self.stop_responding_after_cmd is not None
                and self.stop_responding_after_n is not None):
            if (cmd & 0xFF) == (self.stop_responding_after_cmd & 0xFF):
                cnt = self._cmd_silent_counts.get(cmd & 0xFF, 0)
                if cnt >= self.stop_responding_after_n:
                    return True
                self._cmd_silent_counts[cmd & 0xFF] = cnt + 1
        return False

    def _emit_frame(self, cmd: int, param: bytes, force: bool = False) -> None:
        """Send one device→host frame honouring fault-injection knobs."""
        if not force and self._should_silence(cmd):
            self._log('[mock-device] tx suppressed cmd=0x%02X (fault)' % (cmd & 0xFF))
            return
        if self.inject_garbage_between_frames:
            junk = bytes([0x00, 0xFF, 0x5A, 0xA5, 0x13, 0x37])
            self.endpoint.write(junk)
        tx_cmd = cmd
        if self.send_wrong_cmd is not None:
            tx_cmd = self.send_wrong_cmd & 0xFF
        if self.oversized_length_field:
            # Lie about length so host parser must reject/resync without crashing.
            frame_len = 0xFFFF
            body = bytes([tx_cmd & 0xFF]) + (param or b'')
            total = bp.FRAME_HEADER_SIZE + frame_len
            buf = bytearray(total)
            buf[0] = bp.PROTOCOL_HEAD
            buf[1:5] = (self.device_id & 0xFFFFFFFF).to_bytes(4, 'little')
            buf[5:7] = frame_len.to_bytes(2, 'little')
            buf[7] = tx_cmd & 0xFF
            # put real param at offset 8 but claim huge length; checksum over
            # whatever we filled so host sees a length-field failure first.
            if param:
                buf[8:8 + len(param)] = param[:min(len(param), 64)]
            buf[total - 1] = 0x00
            frame = bytes(buf[:min(len(buf), 32)])  # truncated lie
            self.endpoint.write(frame)
            self._log('[mock-device] tx oversized_length cmd=0x%02X'
                      % (tx_cmd & 0xFF))
            return
        frame = bp.pack_frame(self.device_id, tx_cmd, param or b'', encrypt=False)
        if self.send_duplicate_frame:
            self.endpoint.write(frame)
        self.endpoint.write(frame)
        self._log('[mock-device] tx cmd=0x%02X len=%d%s' % (
            tx_cmd & 0xFF, len(frame),
            ' (dup)' if self.send_duplicate_frame else ''))

    def _send_frame(self, cmd: int, param: bytes = b'') -> None:
        # Device replies stay plaintext so host ProtocolClient(encrypt=True)
        # can parse RX (encrypt flag only affects host TX).
        self._emit_frame(cmd, param or b'')

    def close_pty_mid_transfer(self, stop_thread: bool = True) -> None:
        """
        Simulate a broken link: stop the device pump (optionally stop the
        thread) and mark link_broken so TX/RX are dropped. Host sees silence
        / EIO depending on whether the pty master fd is also closed.
        """
        self.link_broken = True
        if stop_thread:
            self._stop.set()
            t = self._thread
            if t is not None and t.is_alive():
                t.join(timeout=1.0)
            self._thread = None

    def break_link_and_close_endpoint(self) -> None:
        """Hard break: stop thread + close device endpoint (pty master)."""
        self.link_broken = True
        self._stop.set()
        t = self._thread
        if t is not None and t.is_alive():
            t.join(timeout=1.0)
        self._thread = None
        try:
            self.endpoint.close()
        except Exception:
            pass

    # -- protocol RX ------------------------------------------------------

    def _feed_protocol(self, raw: bytes) -> None:
        if self.encrypt and raw:
            # Firmware _bProtocolDecrypt whole incoming buffer when enabled.
            raw = bp.tea_decrypt(raw)
        self.rx_buf.extend(raw)
        while True:
            frame = self._extract_one()
            if frame is None:
                break
            dev_id, cmd, param = frame
            self.frames_handled += 1
            if self.drop_after_n_frames is not None:
                if self._frames_handled_limit is None:
                    self._frames_handled_limit = int(self.drop_after_n_frames)
            with self._lock:
                self.written_cmds.append(cmd)
            self._log('[mock-device] rx cmd=0x%02X id=0x%08X plen=%d'
                      % (cmd & 0xFF, dev_id & 0xFFFFFFFF, len(param)))
            self._handle(cmd, param)

    def _extract_one(self) -> Optional[Tuple[int, int, bytes]]:
        """
        Pull one valid frame from rx_buf. Corrupt heads (bad length /
        bad checksum) are dropped and the scan continues so a later valid
        frame in the same buffer is still parsed — mirrors firmware
        _bProtocolParse resync and host ProtocolClient._extract_one_frame.
        Returns None only when the buffer is clean or incomplete.
        """
        buf = self.rx_buf
        while True:
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
                continue
            if total_len > len(buf):
                return None
            if bp.calc_checksum(bytes(buf[:total_len - 1])) != buf[total_len - 1]:
                del buf[0]
                continue
            param = bytes(buf[8:total_len - 1])
            del buf[:total_len]
            return device_id, cmd, param

    # -- command handlers -------------------------------------------------

    def _handle(self, cmd: int, param: bytes) -> None:
        if cmd == CMD_TEST:
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
                # zero / invalid size — firmware reports LEN_INVALID
                self.last_ota_result = OTA_RESULT_LEN_INVALID
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
                self.last_ota_result = OTA_RESULT_LEN_INVALID
                self._send_frame(CMD_OTA_RESULT,
                                 bytes([OTA_RESULT_LEN_INVALID & 0xFF]))
        elif cmd == CMD_FDATA:
            self._handle_fdata(param)
        elif cmd == CMD_OTA_RESULT:
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
            chunk[0] = (chunk[0] ^ 0xFF) & 0xFF
        if self.corrupt_next_fdata_chunk and chunk:
            self.corrupt_next_fdata_chunk = False
            chunk[0] = (chunk[0] ^ 0xFF) & 0xFF
        self.received_fw_bytes.extend(chunk)

        next_seq = seq + 1
        if size and next_seq * FDATA_CHUNK < size:
            self._send_frame(CMD_FDATA, bp.build_fdata_req_param(next_seq))
            return

        got = bytes(self.received_fw_bytes[:size]) if size else bytes(self.received_fw_bytes)
        crc_local = crc32(got)
        if self.ota_result_override is not None:
            result = int(self.ota_result_override) & 0xFF
        elif self.force_crc_error:
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

    # -- shell text (b_mod_param.c) --------------------------------------

    def _feed_shell(self, raw: bytes) -> None:
        """Accumulate shell text; process CR/LF-terminated commands."""
        self._shell_rx.extend(raw)
        while True:
            idx_cr = self._shell_rx.find(b'\r')
            idx_lf = self._shell_rx.find(b'\n')
            cands = [i for i in (idx_cr, idx_lf) if i >= 0]
            if not cands:
                break
            idx = min(cands)
            line = bytes(self._shell_rx[:idx]).decode('utf-8', errors='replace').strip()
            del self._shell_rx[:idx + 1]
            resp = self._handle_shell_line(line)
            if resp:
                self.endpoint.write(resp.encode('utf-8'))
            with self._lock:
                self.shell_log.append((line, resp))

    def _handle_shell_line(self, text: str) -> str:
        """
        Firmware-faithful 'param' shell (b_mod_param.c _ShellParamHandle).

        param                  -> ": name\\r\\n" for each
        param <name>           -> "<name>:<value>\\r\\n"
        param <name> <value>   -> atoi set; prints nothing

        Lines that do not start with a letter are discarded (binary leftovers
        from a previous protocol/xfer session must not glue onto the next
        shell command).
        """
        if not text:
            return ''
        # drop leading non-letters (e.g. 0xFE frame head residue)
        i = 0
        while i < len(text) and not text[i].isalpha():
            i += 1
        if i:
            text = text[i:]
        if not text:
            return ''
        parts = text.split()
        if parts[0] != 'param':
            return ''
        if len(parts) == 1:
            out = []
            for name in self.shell_params:
                out.append(': %s\r\n' % name)
            return ''.join(out)
        name = parts[1]
        if len(parts) == 2:
            if name in self.shell_params:
                return '%s:%d\r\n' % (name, self.shell_params[name])
            return ''
        # set — firmware-faithful (bos/modules/b_mod_param.c _ShellParamHandle):
        # only params already registered in the b_mod_param section are written;
        # unknown names are silent no-ops (no auto-create).
        try:
            value = int(parts[2], 10)
        except (ValueError, IndexError):
            return ''
        if name in self.shell_params:
            self.shell_params[name] = value
        return ''

    def _drain_endpoint(self, budget_s: float = 0.05) -> int:
        """Read and discard bytes already queued on the wire (mode switch)."""
        total = 0
        deadline = time.time() + max(0.0, budget_s)
        idle_rounds = 0
        while time.time() < deadline:
            data = self.endpoint.read(4096)
            if data:
                total += len(data)
                idle_rounds = 0
            else:
                idle_rounds += 1
                if idle_rounds >= 3:
                    break
                time.sleep(0.005)
        return total

    # -- mode switching ---------------------------------------------------

    def switch_to_protocol(self) -> None:
        """Return to BabyOS binary protocol mode; drop xfer/shell buffers."""
        self._drain_endpoint(0.03)
        self.mode = 'protocol'
        self.xmodem = None
        self.ymodem = None
        self.rx_buf = bytearray()
        self._shell_rx = bytearray()

    def switch_to_shell(self,
                        params: Optional[Dict[str, int]] = None) -> None:
        """Enter shell-text mode (param commands)."""
        # Drain in the OLD mode first so a late protocol ACK is consumed
        # as protocol (recorded) rather than glued onto the next shell line.
        self._drain_endpoint(0.05)
        self.mode = 'shell'
        self.xmodem = None
        self.ymodem = None
        self.rx_buf = bytearray()
        self._shell_rx = bytearray()
        if params is not None:
            self.shell_params = dict(params)

    def switch_to_xmodem(self) -> XmodemReceiver:
        """Enter Xmodem-128 receive mode and request CRC-16 ('C')."""
        return self.start_xmodem_receive()

    def switch_to_ymodem(self) -> YmodemReceiver:
        """Enter Ymodem-1K receive mode and request CRC-16 ('C')."""
        self._drain_endpoint(0.03)
        self.mode = 'ymodem'
        self.xmodem = None
        self.rx_buf = bytearray()
        self._shell_rx = bytearray()
        self.ymodem = YmodemReceiver(self.endpoint.write, log_fn=self._log,
                                     **self.ym_fault)
        self.ymodem.start()
        return self.ymodem

    def start_xmodem_receive(self,
                             log_fn: Optional[Callable[[str], None]] = None,
                             **fault_kwargs
                             ) -> XmodemReceiver:
        """Switch to xmodem mode and request CRC-16 ('C')."""
        self._drain_endpoint(0.03)
        self.mode = 'xmodem'
        self.ymodem = None
        self.rx_buf = bytearray()
        self._shell_rx = bytearray()
        faults = dict(self.xm_fault)
        faults.update(fault_kwargs or {})
        self.xm_fault = faults
        self.xmodem = XmodemReceiver(self.endpoint.write,
                                     log_fn=log_fn or self._log,
                                     **faults)
        self.xmodem.start()
        return self.xmodem

    def start_ymodem_receive(self,
                             log_fn: Optional[Callable[[str], None]] = None,
                             **fault_kwargs
                             ) -> YmodemReceiver:
        """Switch to ymodem mode and request CRC-16 ('C')."""
        self._drain_endpoint(0.03)
        self.mode = 'ymodem'
        self.xmodem = None
        self.rx_buf = bytearray()
        self._shell_rx = bytearray()
        faults = dict(self.ym_fault)
        faults.update(fault_kwargs or {})
        self.ym_fault = faults
        self.ymodem = YmodemReceiver(self.endpoint.write,
                                     log_fn=log_fn or self._log,
                                     **faults)
        self.ymodem.start()
        return self.ymodem

    # -- introspection ----------------------------------------------------

    @property
    def received_fw(self) -> bytes:
        """Bytes received via OTA (0x3) or file transfer (0x6)."""
        return bytes(self.received_fw_bytes)

    @property
    def received_file(self) -> bytes:
        """
        Bytes received via Ymodem (trimmed to declared size), or via
        protocol file-transfer (0x6) when ymodem is idle.
        """
        if self.ymodem is not None:
            return bytes(self.ymodem.data)
        if self.trans_file is not None:
            return self.received_fw
        return b''

    @property
    def received_file_name(self) -> str:
        """Filename captured from Ymodem block 0 (empty if none)."""
        if self.ymodem is not None:
            return self.ymodem.filename
        return ''

    @property
    def received_xmodem(self) -> bytes:
        if self.xmodem is not None:
            return bytes(self.xmodem.data)
        return b''

    def snapshot(self) -> dict:
        with self._lock:
            return {
                'kind': 'mock_babyos_device',
                'device_id': self.device_id,
                'mode': self.mode,
                'encrypt': self.encrypt,
                'frames_handled': self.frames_handled,
                'written_cmds': list(self.written_cmds),
                'host_acks': list(self.host_acks),
                'fw_info': self.fw_info,
                'trans_file': self.trans_file,
                'received_chunks': len(self.received_chunks),
                'received_fw_len': len(self.received_fw_bytes),
                'received_file_len': len(self.received_file),
                'received_file_name': self.received_file_name,
                'last_utc': self.last_utc,
                'last_sn': self.last_sn,
                'last_ota_result': self.last_ota_result,
                'xmodem_done': bool(self.xmodem is not None and self.xmodem.done),
                'xmodem_bytes': len(self.xmodem.data) if self.xmodem else 0,
                'ymodem_done': bool(self.ymodem is not None and self.ymodem.done),
                'ymodem_bytes': len(self.ymodem.data) if self.ymodem else 0,
                'ymodem_name': self.ymodem.filename if self.ymodem else '',
                'shell_params': dict(self.shell_params),
                'shell_log_len': len(self.shell_log),
                'faults': {
                    'drop_after_n_frames': self.drop_after_n_frames,
                    'stop_responding_after_cmd': self.stop_responding_after_cmd,
                    'stop_responding_after_n': self.stop_responding_after_n,
                    'inject_garbage_between_frames': self.inject_garbage_between_frames,
                    'corrupt_next_fdata_chunk': self.corrupt_next_fdata_chunk,
                    'send_wrong_cmd': self.send_wrong_cmd,
                    'send_duplicate_frame': self.send_duplicate_frame,
                    'oversized_length_field': self.oversized_length_field,
                    'ota_result_override': self.ota_result_override,
                    'link_broken': self.link_broken,
                    'xm_fault': dict(self.xm_fault),
                    'ym_fault': dict(self.ym_fault),
                },
            }


# ---------------------------------------------------------------------------
# API-over-pty: DeviceManager ↔ pty host ↔ MockBabyOSDevice
# ---------------------------------------------------------------------------

class PtyLink:
    """
    Real virtual-serial link for API / DeviceManager tests.

    Walks: FastAPI DeviceManager → UartService → pty slave →
           MockBabyOSDevice on pty master.
    """

    def __init__(self, chan: ByteChannel, device: MockBabyOSDevice,
                 dm: Any = None, owns_dm: bool = False) -> None:
        self.chan = chan
        self.device = device
        self.dm = dm                  # DeviceManager or None
        self.owns_dm = owns_dm
        self._closed = False

    @property
    def kind(self) -> str:
        return self.chan.kind

    @property
    def host_port(self) -> str:
        return self.chan.host_port

    @property
    def host_uart(self) -> Any:
        return self.chan.host_uart

    def protocol_client(self, encrypt: bool = False) -> Any:
        """Bind a ProtocolClient on the host uart (if already open)."""
        from device.protocol_client import ProtocolClient
        uart = self.chan.host_uart
        if uart is None:
            raise RuntimeError('host uart not open on this link')
        return ProtocolClient(uart, encrypt=encrypt)

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        if self.dm is not None:
            try:
                self.dm.close_all()
            except Exception:
                pass
            if self.owns_dm:
                try:
                    from device.device_manager import DeviceManager
                    DeviceManager.reset()
                except Exception:
                    pass
        try:
            self.device.stop()
        except Exception:
            pass
        try:
            self.chan.close()
        except Exception:
            pass

    def __enter__(self) -> 'PtyLink':
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()


def open_api_over_pty(encrypt: bool = False,
                      baudrate: int = 115200,
                      open_host_uart: bool = True,
                      device_kwargs: Optional[dict] = None,
                      require_pty: bool = True,
                      bind_device_manager: bool = True
                      ) -> PtyLink:
    """
    Create pty + MockBabyOSDevice (+ optional DeviceManager bound to pty).

    open_host_uart=True:
      Opens UartService on the pty slave immediately (ProtocolClient path).
      DeviceManager, if bound, reuses this UART via _bind_clients when
      open_host_uart was used with the same process singleton — for the
      DeviceManager/TestClient path prefer open_host_uart=False and let
      dm.open_port / POST /api/device/serial/open open host_port.

    open_host_uart=False:
      Returns a link whose host_port is ready for:
        DeviceManager.open_port(link.host_port, baudrate, encrypt=...)
        POST /api/device/serial/open {"path": link.host_port, ...}

    require_pty=True:
      Fail hard if a real pty pair cannot be created (acceptance default).

    Returns PtyLink. Caller must close() (or use as context manager).
    """
    kwargs = dict(device_kwargs or {})
    if 'encrypt' not in kwargs:
        kwargs['encrypt'] = encrypt
    chan = create_byte_channel(prefer='pty',
                               open_host_uart=open_host_uart,
                               require_pty=require_pty)
    if require_pty and chan.kind != 'pty':
        chan.close()
        raise RuntimeError('open_api_over_pty requires pty, got %s' % chan.kind)
    logs = []  # type: List[str]
    kwargs.setdefault('log_fn', logs.append)
    device = MockBabyOSDevice(chan.device_endpoint(), **kwargs)
    device.start()

    dm = None
    owns_dm = False
    if bind_device_manager:
        from device.device_manager import DeviceManager
        dm = DeviceManager.get()
        owns_dm = True
        if open_host_uart and chan.host_uart is not None:
            # Bind clients onto the already-open host UartService instance
            # by pointing DeviceManager.uart at it (same object).
            dm.uart = chan.host_uart
            dm._bind_clients(encrypt=encrypt)
        # else: caller / TestClient opens host_port via dm.open_port / API
    return PtyLink(chan, device, dm=dm, owns_dm=owns_dm)


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
