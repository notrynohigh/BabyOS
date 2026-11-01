"""
protocol_client — BabyOS host-side protocol business client.

Authority:
  - origin/master:tool/README.md  (BabyOS 协议说明)
  - origin/master:tool/b_protocol.py, tool/mainwindow.py
  - bos/modules/b_mod_protocol.h / b_mod_protocol.c
  - bos/algorithm/algo_crc.c (CRC32 口径)

Frame: HEAD(0xFE) + DeviceID(4B LE) + Length(2B LE=1+param) + CMD(1B)
       + Params + Checksum(1B = sum of all except checksum)

Host identity: DEVICE_ID_HOST = 0x1314.
Host→device frames default to device_id=INVALID_ID (0xFFFFFFFF),
matching origin/master:tool/mainwindow.py and firmware parse acceptance
(frame id == device id || frame id == INVALID_ID || device id == INVALID_ID).

OTA / file-transfer host flow (README §1.3–§1.5, task spec):
  1. host → CMD_FW_INFO 0x3  (size+crc32+filename[64])   [OTA]
     or   → CMD_TRANS_FILE 0x6 (size+crc32+dev_no+offset) [file xfer]
  2. device → CMD_FW_INFO 0x3 ACK (empty)  — also auto-ACKed by firmware
  3. device → CMD_FDATA 0x4  seq(2B)       — requests chunk
  4. host   → CMD_FDATA 0x4  seq(2B)+data[512]  (pad 0)
  5. device → CMD_OTA_RESULT 0x5  result(1B)
  6. host   → CMD_OTA_RESULT 0x5  ACK (empty)

Python 3.8 compatible. No stubs — real UART I/O via uart.write /
uart.read_available.
"""

from __future__ import print_function

import os
import threading
import time
from typing import Any, Callable, List, Optional, Tuple

from . import b_protocol as bp
from .crc_util import crc32
from .sn_util import sn_bytes as _sn_bytes

DEVICE_ID_HOST = bp.DEVICE_ID_HOST
INVALID_ID = bp.INVALID_ID
PROTOCOL_HEAD = bp.PROTOCOL_HEAD

CMD_TEST = bp.CMD_TEST
CMD_UTC = bp.CMD_UTC
CMD_FW_INFO = bp.CMD_FW_INFO
CMD_FDATA = bp.CMD_FDATA
CMD_OTA_RESULT = bp.CMD_OTA_RESULT
CMD_TRANS_FILE = bp.CMD_TRANS_FILE
CMD_GET_UID = bp.CMD_GET_UID
CMD_WRITE_SN = bp.CMD_WRITE_SN
CMD_TSL_INVOKE = bp.CMD_TSL_INVOKE
CMD_DEVICEINFO = bp.CMD_DEVICEINFO
CMD_SETCFGNET_MODE = bp.CMD_SETCFGNET_MODE
CMD_GET_NETINFO = bp.CMD_GET_NETINFO
CMD_SET_VOICE_SWITCH = bp.CMD_SET_VOICE_SWITCH
CMD_SET_VOICE_VOLUME = bp.CMD_SET_VOICE_VOLUME
CMD_GET_VOICE_VOLUME = bp.CMD_GET_VOICE_VOLUME
CMD_GET_VOICE_STAT = bp.CMD_GET_VOICE_STAT
CMD_TTS_CONTENT = bp.CMD_TTS_CONTENT
CMD_HTTP_REQUEST = bp.CMD_HTTP_REQUEST
CMD_HTTP_RESPONSE = bp.CMD_HTTP_RESPONSE
CMD_HTTP_INIT = bp.CMD_HTTP_INIT
CMD_HTTP_DEINIT = bp.CMD_HTTP_DEINIT

OTA_RESULT_OK = bp.OTA_RESULT_OK
OTA_RESULT_CRC_ERROR = bp.OTA_RESULT_CRC_ERROR
OTA_RESULT_NAME_MISMATCH = bp.OTA_RESULT_NAME_MISMATCH
OTA_RESULT_LEN_INVALID = bp.OTA_RESULT_LEN_INVALID
OTA_RESULT_TIMEOUT = bp.OTA_RESULT_TIMEOUT

FRAME_HEADER_SIZE = bp.FRAME_HEADER_SIZE
FDATA_CHUNK_SIZE = bp.FDATA_CHUNK_SIZE
FW_NAME_SIZE = bp.FW_NAME_SIZE

# Hard cap for a single host↔device frame (cmd+param) to avoid unbounded
# allocation if length field is corrupt. Max payload used is FDATA 514B.
_MAX_PARAM_SIZE = 2048
_MAX_FRAME_SIZE = FRAME_HEADER_SIZE + 1 + _MAX_PARAM_SIZE + 1

# Ring caps for RX/TX frame logs. Unknown-cmd frames still land here when no
# waiter is active — without a cap a long-running host session grows without
# bound on stray device traffic.
_RX_LOG_MAX = 256
_RX_FRAMES_MAX = 64
_TX_LOG_MAX = 256

# Callback types
# on_progress(pct: int 0..100)
# on_log(msg: str)
# on_result(ok: bool, code: int)
ProgressFn = Callable[[int], None]
LogFn = Callable[[str], None]
ResultFn = Callable[[bool, int], None]


def _default_log(msg: str) -> None:
    pass


class ProtocolClient:
    """
    Host-side BabyOS protocol client bound to a UART object.

    The uart object must provide:
      write(data: bytes) -> int
      read_available() -> bytes
      is_open -> bool   (optional)

    Thread-safe frame RX; synchronous request/response and OTA pump.
    """

    def __init__(self, uart: Any, host_id: int = DEVICE_ID_HOST,
                 encrypt: bool = False,
                 log_fn: Optional[LogFn] = None) -> None:
        if uart is None:
            raise ValueError('uart is required')
        self._uart = uart
        self.host_id = host_id & 0xFFFFFFFF
        self._encrypt = bool(encrypt)
        self.on_progress: Optional[ProgressFn] = None
        self.on_log: Optional[LogFn] = None
        self.on_result: Optional[ResultFn] = None

        self._rx_buf = bytearray()
        self._cv = threading.Condition()
        # frames waiting to be consumed by request_response()
        self._rx_frames: List[Tuple[int, int, bytes]] = []
        self._log = log_fn or _default_log

        # active transfer context (OTA / file xfer)
        self._xfer_data = b''
        self._xfer_size = 0
        self._xfer_crc = 0
        self._xfer_name = ''
        self._xfer_kind = ''          # 'ota' | 'file'
        self._xfer_dev_no = 0
        self._xfer_offset = 0
        self._xfer_done = False
        self._xfer_result: Optional[int] = None
        self._xfer_chunks_sent = 0
        self._xfer_busy = False

        self._tx_log: List[bytes] = []   # packed frames actually written
        self._rx_log: List[Tuple[int, int, bytes]] = []

    # ------------------------------------------------------------------
    # logging helpers
    # ------------------------------------------------------------------

    def _emit_log(self, msg: str) -> None:
        try:
            self._log(msg)
        except Exception:
            pass
        fn = self.on_log
        if fn is not None:
            try:
                fn(msg)
            except Exception:
                pass

    def _emit_progress(self, pct: int) -> None:
        if pct < 0:
            pct = 0
        if pct > 100:
            pct = 100
        fn = self.on_progress
        if fn is not None:
            try:
                fn(int(pct))
            except Exception:
                pass

    def _emit_result(self, ok: bool, code: int) -> None:
        fn = self.on_result
        if fn is not None:
            try:
                fn(bool(ok), int(code))
            except Exception:
                pass

    # ------------------------------------------------------------------
    # frame TX
    # ------------------------------------------------------------------

    @property
    def encrypt(self) -> bool:
        return self._encrypt

    @encrypt.setter
    def encrypt(self, value: bool) -> None:
        self._encrypt = bool(value)

    @property
    def tx_log(self) -> List[bytes]:
        """Packed frames written by this client (for tests / diagnostics)."""
        return list(self._tx_log)

    @property
    def rx_log(self) -> List[Tuple[int, int, bytes]]:
        """Parsed (device_id, cmd, param) frames received."""
        return list(self._rx_log)

    def pack_cmd(self, cmd: int, param: Optional[bytes] = None,
                 device_id: int = INVALID_ID) -> bytes:
        """Pack a host→device frame (does not write to UART)."""
        if param is None:
            param = b''
        return bp.pack_frame(device_id, cmd, param, encrypt=self._encrypt)

    def send_cmd(self, cmd: int, param: Optional[bytes] = None,
                 device_id: int = INVALID_ID) -> bytes:
        """
        Pack and write one protocol frame to UART.
        Returns the packed frame bytes (what was sent).
        """
        frame = self.pack_cmd(cmd, param, device_id=device_id)
        if not frame:
            raise ValueError('pack failed for cmd=0x%02X' % (cmd & 0xFF))
        n = self._uart.write(frame)
        if n is not None and n < 0:
            raise IOError('uart write failed')
        self._tx_log.append(frame)
        if len(self._tx_log) > _TX_LOG_MAX:
            del self._tx_log[:-_TX_LOG_MAX]
        self._emit_log('s-> cmd:0x%02X len=%d data=%s' % (
            cmd & 0xFF, len(frame), ' '.join('%02X' % b for b in frame[:min(len(frame), 24)])))
        return frame

    # ------------------------------------------------------------------
    # frame RX / dispatch
    # ------------------------------------------------------------------

    def feed_bytes(self, raw: Optional[bytes]) -> List[Tuple[int, int, bytes]]:
        """
        Feed raw UART bytes into the frame parser.

        Returns the list of frames completed by this call
        (each = (device_id, cmd, param_bytes)).

        Invalid bytes are discarded with head resync (0xFE scan).
        Frames belonging to an active OTA/file transfer are dispatched
        internally (FDATA / OTA_RESULT); they are still returned.
        """
        if not raw:
            return []
        if isinstance(raw, bytearray):
            raw = bytes(raw)

        completed: List[Tuple[int, int, bytes]] = []
        with self._cv:
            self._rx_buf.extend(raw)
            while True:
                frame = self._extract_one_frame()
                if frame is None:
                    break
                dev_id, cmd, param = frame
                self._rx_log.append((dev_id, cmd, param))
                if len(self._rx_log) > _RX_LOG_MAX:
                    del self._rx_log[:-_RX_LOG_MAX]
                self._emit_log('r-> id:0x%08X cmd:0x%02X param_len=%d' % (
                    dev_id & 0xFFFFFFFF, cmd & 0xFF, len(param)))
                completed.append((dev_id, cmd, param))
                # active transfer takes priority over generic queue
                self._dispatch_transfer_frame(cmd, param)
                # generic waiter queue
                self._rx_frames.append((dev_id, cmd, param))
                if len(self._rx_frames) > _RX_FRAMES_MAX:
                    del self._rx_frames[:-_RX_FRAMES_MAX]
            self._cv.notify_all()
        return completed

    def _extract_one_frame(self) -> Optional[Tuple[int, int, bytes]]:
        """
        Pull one valid frame from _rx_buf. Caller holds self._cv.

        Corrupt heads (bad length / bad checksum) are dropped and the scan
        continues so a later valid frame in the same buffer is still parsed.
        Returns None only when the buffer is clean or incomplete (wait more).
        """
        buf = self._rx_buf
        while True:
            # resync to HEAD
            start = 0
            n = len(buf)
            while start < n and buf[start] != PROTOCOL_HEAD:
                start += 1
            if start:
                del buf[:start]
                n = len(buf)
            if n < FRAME_HEADER_SIZE + 1:
                return None

            device_id = int.from_bytes(buf[1:5], 'little')
            frame_len = int.from_bytes(buf[5:7], 'little')  # 1 + param
            cmd = buf[7]
            # total = header(8) + frame_len(=1+param)   [checksum is last of frame_len]
            # Layout: head+id+len+cmd (8) + params + checksum → total = 8 + frame_len
            total_len = FRAME_HEADER_SIZE + frame_len
            if frame_len < 1 or total_len > _MAX_FRAME_SIZE:
                # corrupt length — drop the head byte and keep scanning
                del buf[0]
                continue
            if total_len > len(buf):
                return None  # incomplete — wait for more data

            expected = bp.calc_checksum(bytes(buf[:total_len - 1]))
            if expected != buf[total_len - 1]:
                # checksum fail — drop head, keep scanning
                del buf[0]
                continue

            param = bytes(buf[8:total_len - 1])
            del buf[:total_len]
            return device_id, cmd, param

    def _dispatch_transfer_frame(self, cmd: int, param: bytes) -> None:
        """Handle FDATA request / OTA_RESULT while a transfer is active."""
        if not self._xfer_busy:
            return
        if cmd == CMD_FDATA:
            seq = bp.parse_fdata_req_param(param)
            if seq is None:
                return
            self._send_fdata_chunk(seq)
        elif cmd == CMD_OTA_RESULT:
            result = bp.parse_ota_result_param(param)
            if result is None:
                result = 0
            # host ACK (README §1.5 上位机==>设备 无参数)
            try:
                self.send_cmd(CMD_OTA_RESULT, b'')
            except Exception:
                pass
            self._xfer_result = result
            self._xfer_done = True
            ok = (result == OTA_RESULT_OK)
            name = {
                OTA_RESULT_OK: 'success',
                OTA_RESULT_CRC_ERROR: 'crc_error',
                OTA_RESULT_NAME_MISMATCH: 'name_mismatch',
                OTA_RESULT_LEN_INVALID: 'len_invalid',
                OTA_RESULT_TIMEOUT: 'timeout',
            }.get(result, 'unknown(%d)' % result)
            self._emit_log('transfer result: %s (%d)' % (name, result))
            self._emit_progress(100 if ok else self._last_pct())
            self._emit_result(ok, result)

    def _last_pct(self) -> int:
        if self._xfer_size <= 0:
            return 0
        sent = self._xfer_chunks_sent * FDATA_CHUNK_SIZE
        return min(99, int(sent * 100 / self._xfer_size))

    def _send_fdata_chunk(self, seq: int) -> None:
        """Answer a device FDATA request with seq + 512B chunk."""
        index = seq * FDATA_CHUNK_SIZE
        if index >= self._xfer_size:
            self._emit_log('FDATA seq %d beyond size %d — ignored' % (seq, self._xfer_size))
            return
        chunk = self._xfer_data[index:index + FDATA_CHUNK_SIZE]
        param = bp.build_fdata_param(seq, chunk)
        try:
            self.send_cmd(CMD_FDATA, param)
        except (IOError, OSError) as exc:
            # Link died mid-transfer: surface structured timeout result.
            self._emit_log('FDATA send failed: %s' % exc)
            self._xfer_done = True
            if self._xfer_result is None:
                self._xfer_result = OTA_RESULT_TIMEOUT
            self._emit_result(False, self._xfer_result)
            return
        self._xfer_chunks_sent += 1
        sent = min(self._xfer_size, index + FDATA_CHUNK_SIZE)
        pct = int(sent * 100 / self._xfer_size) if self._xfer_size else 100
        self._emit_progress(pct)

    # ------------------------------------------------------------------
    # synchronous request/response
    # ------------------------------------------------------------------

    def _pump_uart(self, timeout: float, poll_interval: float,
                   predicate: Optional[Callable[[], bool]] = None) -> bool:
        """
        Read UART until predicate() is True or timeout.
        Returns True if predicate became True.
        """
        deadline = time.time() + max(0.0, timeout)
        while True:
            if predicate is not None and predicate():
                return True
            if time.time() >= deadline:
                return predicate() is True if predicate else False
            try:
                data = self._uart.read_available()
            except Exception:
                data = b''
            if data:
                self.feed_bytes(data)
            else:
                time.sleep(poll_interval)

    def _wait_frame(self, expect_cmd: int, timeout: float,
                    poll_interval: float,
                    pred: Optional[Callable[[int, int, bytes], bool]] = None
                    ) -> Optional[Tuple[int, int, bytes]]:
        """Pump UART until a frame with expect_cmd (and optional pred) arrives."""
        found: List[Tuple[int, int, bytes]] = []

        def _check() -> bool:
            # single pass over the current queue — avoids infinite
            # append/pop loop when only non-matching frames are present
            if not self._rx_frames:
                return False
            n = len(self._rx_frames)
            for _ in range(n):
                item = self._rx_frames.pop(0)
                if item[1] != (expect_cmd & 0xFF):
                    self._rx_frames.append(item)
                    continue
                if pred is not None and not pred(item[0], item[1], item[2]):
                    self._rx_frames.append(item)
                    continue
                found.append(item)
                return True
            return False

        with self._cv:
            if _check():
                return found[0]
        # pump may consume the matching frame into `found` via _check
        self._pump_uart(timeout, poll_interval, _check)
        if found:
            return found[0]
        with self._cv:
            if _check():
                return found[0]
        return None

    def request_response(self, cmd: int, param: Optional[bytes] = None,
                         expect_cmd: Optional[int] = None,
                         timeout: float = 2.0,
                         device_id: int = INVALID_ID,
                         poll_interval: float = 0.005,
                         pred: Optional[Callable[[int, int, bytes], bool]] = None
                         ) -> Optional[Tuple[int, int, bytes]]:
        """
        Send cmd and wait for a response frame.

        Returns (device_id, cmd, param) on success, None on timeout.
        expect_cmd defaults to cmd (firmware ACKs / replies with same cmd
        for TEST / UTC / FW_INFO / GET_UID / WRITE_SN / DEVICEINFO / TRANS_FILE).
        """
        expect = cmd if expect_cmd is None else expect_cmd
        try:
            self.send_cmd(cmd, param, device_id=device_id)
        except (IOError, OSError) as exc:
            # Device unplugged / pty broken: treat as no response so the API
            # can return a structured timeout instead of a bare 500.
            self._emit_log('send_cmd failed (0x%02X): %s' % (cmd & 0xFF, exc))
            return None
        return self._wait_frame(expect, timeout, poll_interval, pred=pred)

    # ------------------------------------------------------------------
    # simple command APIs
    # ------------------------------------------------------------------

    def test_link(self, timeout: float = 2.0) -> Optional[Tuple[int, int, bytes]]:
        """
        CMD 0x1 test — param = b'BabyOS\\x00' (firmware bProtoTestParam_t.str[7]).
        Device replies empty ACK with same cmd.
        """
        return self.request_response(CMD_TEST, bp.build_test_param(),
                                     expect_cmd=CMD_TEST, timeout=timeout)

    def set_time(self, utc: int, timeout: float = 2.0) -> Optional[Tuple[int, int, bytes]]:
        """CMD 0x2 — 4B LE Unix timestamp. Device replies empty ACK."""
        return self.request_response(CMD_UTC, bp.build_utc_param(utc),
                                     expect_cmd=CMD_UTC, timeout=timeout)

    def get_uid(self, timeout: float = 2.0) -> Optional[bytes]:
        """CMD 0x7 — device replies len(1)+uid(n). Returns raw uid bytes or None."""
        resp = self.request_response(CMD_GET_UID, b'', expect_cmd=CMD_GET_UID,
                                     timeout=timeout)
        if resp is None:
            return None
        uid = bp.parse_uid_response(resp[2])
        return uid

    def write_sn(self, sn_bytes: Optional[bytes] = None,
                 uid: Optional[bytes] = None, orval: int = 0,
                 timeout: float = 2.0) -> Optional[Tuple[int, int, bytes]]:
        """
        CMD 0x8 — write SN.

        Prefer sn_bytes = full length-prefixed param from sn_util.sn_bytes()
        (bytes([16]) + md5(uid)|orval). If only uid is given, SN is generated
        as md5(uid)[:16] each byte | orval with 1-byte length prefix.
        Device replies empty ACK.
        """
        if sn_bytes is None:
            if uid is None:
                raise ValueError('write_sn requires sn_bytes or uid')
            sn_bytes = _sn_bytes(uid, orval)
        return self.request_response(CMD_WRITE_SN, sn_bytes,
                                     expect_cmd=CMD_WRITE_SN, timeout=timeout)

    def get_device_info(self, timeout: float = 2.0
                        ) -> Optional[Tuple[str, str]]:
        """
        CMD 0xA — device replies version(16)+name(16).
        Returns (version_str, model_str) with NULs stripped.
        """
        resp = self.request_response(CMD_DEVICEINFO, b'',
                                     expect_cmd=CMD_DEVICEINFO, timeout=timeout)
        if resp is None:
            return None
        parsed = bp.parse_devinfo_response(resp[2])
        if parsed is None:
            return None
        version_b, name_b = parsed
        version = version_b.split(b'\x00', 1)[0].decode('utf-8', errors='replace')
        model = name_b.split(b'\x00', 1)[0].decode('utf-8', errors='replace')
        return version, model

    # ------------------------------------------------------------------
    # OTA / file transfer
    # ------------------------------------------------------------------

    @staticmethod
    def _load_file(path: str) -> Tuple[bytes, int, str]:
        if not path or not os.path.isfile(path):
            raise IOError('firmware/file not found: %r' % (path,))
        with open(path, 'rb') as f:
            data = f.read()
        crc = crc32(data)
        name = os.path.basename(path)
        return data, crc, name

    def _run_transfer(self, kind: str, data: bytes, crc: int, name: str,
                      dev_no: int, offset: int, timeout: float,
                      poll_interval: float) -> bool:
        if self._xfer_busy:
            # Busy rejection: surface via on_result only. Do NOT touch
            # _xfer_result — that belongs to the in-flight transfer, which
            # will set its own code on completion / cancel / timeout.
            self._emit_log('transfer busy — rejected')
            self._emit_result(False, OTA_RESULT_LEN_INVALID)
            return False
        size = len(data)
        if size <= 0:
            self._emit_log('empty file — rejected')
            self._xfer_result = OTA_RESULT_LEN_INVALID
            self._emit_result(False, OTA_RESULT_LEN_INVALID)
            return False

        self._xfer_data = data
        self._xfer_size = size
        self._xfer_crc = crc
        self._xfer_name = name
        self._xfer_kind = kind
        self._xfer_dev_no = dev_no
        self._xfer_offset = offset
        self._xfer_done = False
        self._xfer_result = None
        self._xfer_chunks_sent = 0
        self._xfer_busy = True

        try:
            if kind == 'ota':
                param = bp.build_fw_info_param(size, crc, name)
                self._emit_log('OTA start: size=%d crc32=0x%08X name=%s' % (
                    size, crc & 0xFFFFFFFF, name))
                self.send_cmd(CMD_FW_INFO, param)
            else:
                param = bp.build_trans_file_param(size, crc, dev_no, offset)
                self._emit_log('FILE xfer start: size=%d crc32=0x%08X dev_no=%d offset=0x%X'
                               % (size, crc & 0xFFFFFFFF, dev_no, offset))
                self.send_cmd(CMD_TRANS_FILE, param)

            deadline = time.time() + max(0.0, timeout)
            while time.time() < deadline and not self._xfer_done:
                try:
                    raw = self._uart.read_available()
                except Exception:
                    raw = b''
                if raw:
                    self.feed_bytes(raw)
                else:
                    time.sleep(poll_interval)

            if not self._xfer_done:
                # Structured result for interrupt/timeout — never leave
                # transfer_result as None when the job ends unsuccessfully.
                self._xfer_done = True
                self._xfer_result = OTA_RESULT_TIMEOUT
                self._emit_log('transfer timeout after %.1fs' % timeout)
                self._emit_progress(self._last_pct())
                self._emit_result(False, OTA_RESULT_TIMEOUT)
                return False
            return self._xfer_result == OTA_RESULT_OK
        except (IOError, OSError) as exc:
            # Link broken mid-transfer (pty closed / uart write failed).
            self._emit_log('transfer aborted (io): %s' % exc)
            self._xfer_done = True
            if self._xfer_result is None:
                self._xfer_result = OTA_RESULT_TIMEOUT
            self._emit_result(False, self._xfer_result)
            return False
        finally:
            self._xfer_busy = False
            self._xfer_data = b''

    def start_ota(self, firmware_path: str, name: Optional[str] = None,
                  timeout: float = 30.0, poll_interval: float = 0.005) -> bool:
        """
        Full OTA host flow:
          0x3 FW_INFO → device ACK / 0x4 FDATA loop → 0x5 result → 0x5 ACK.
        Returns True iff device reported success (result==0).
        """
        data, crc, auto_name = self._load_file(firmware_path)
        fw_name = name if name else auto_name
        return self._run_transfer('ota', data, crc, fw_name, 0, 0,
                                  timeout, poll_interval)

    def start_file_transfer(self, path: str, dev_no: int = 0, offset: int = 0,
                            name: Optional[str] = None,
                            timeout: float = 30.0,
                            poll_interval: float = 0.005) -> bool:
        """
        Full FLASH file-transfer host flow (CMD 0x6):
          0x6 TRANS_FILE(size+crc+dev_no+offset) → device ACK
          → 0x4 FDATA loop → 0x5 result → 0x5 ACK.

        Matches origin/dev tool `_on_trans_file` + README §1.6.
        """
        data, crc, auto_name = self._load_file(path)
        fw_name = name if name else auto_name
        return self._run_transfer('file', data, crc, fw_name, dev_no, offset,
                                  timeout, poll_interval)

    def stop_transfer(self, notify_device: bool = True) -> None:
        """
        Abort in-flight transfer from the host side.

        When notify_device=True and UART is open, also send CMD 0x6 with
        all-zero size/crc/dev_no/offset — same as origin/dev tool
        `_on_stop_trans_file` (host tells device to drop the transfer).
        """
        if notify_device:
            try:
                self.send_cmd(CMD_TRANS_FILE, bp.build_trans_file_stop_param())
                self._emit_log('stop-transfer: sent 0x6 zeros to device')
            except (IOError, OSError, RuntimeError) as exc:
                self._emit_log('stop-transfer device notify failed: %s' % exc)
        if not self._xfer_busy:
            # idle transfer — device may still need the zero-frame; do not
            # rewrite a completed result as timeout.
            return
        self._xfer_busy = False
        self._xfer_done = True
        if self._xfer_result is None:
            self._xfer_result = OTA_RESULT_TIMEOUT
        self._emit_result(False, OTA_RESULT_TIMEOUT)

    @property
    def transfer_active(self) -> bool:
        return self._xfer_busy

    @property
    def transfer_result(self) -> Optional[int]:
        return self._xfer_result

    # ------------------------------------------------------------------
    # network / voice / TSL / HTTP debug (dev tool + README + firmware)
    # ------------------------------------------------------------------

    def set_cfgnet_mode(self, cfg_type: int = 0, ssid: str = '',
                        passwd: str = '', timeout: float = 2.0,
                        wait_ack: bool = True) -> Optional[Tuple[int, int, bytes]]:
        """
        CMD 0x30 SETCFGNET_MODE: type(1)+ssid(32)+passwd(64).
        type 0=AP 1=BLE. Device ACK empty per README §2.1 (firmware may
        require app callback — wait_ack=False skips the ACK wait).
        """
        param = bp.build_set_cfgnet_param(cfg_type, ssid, passwd)
        if not wait_ack:
            self.send_cmd(CMD_SETCFGNET_MODE, param)
            return None
        return self.request_response(CMD_SETCFGNET_MODE, param,
                                     expect_cmd=CMD_SETCFGNET_MODE,
                                     timeout=timeout)

    def get_netinfo(self, timeout: float = 2.0) -> Optional[dict]:
        """
        CMD 0x31 GET_NETINFO — device replies ssid(32)+ip+gw+mask.
        Returns dict with dotted IPs, or None on timeout/malformed.
        """
        resp = self.request_response(CMD_GET_NETINFO, b'',
                                     expect_cmd=CMD_GET_NETINFO,
                                     timeout=timeout)
        if resp is None:
            return None
        return bp.netinfo_response_dict(resp[2])

    def set_voice_switch(self, on: int, timeout: float = 2.0,
                         wait_ack: bool = True) -> Optional[Tuple[int, int, bytes]]:
        """CMD 0x40 — voice on/off (0/1)."""
        param = bp.build_voice_switch_param(on)
        if not wait_ack:
            self.send_cmd(CMD_SET_VOICE_SWITCH, param)
            return None
        return self.request_response(CMD_SET_VOICE_SWITCH, param,
                                     expect_cmd=CMD_SET_VOICE_SWITCH,
                                     timeout=timeout)

    def set_voice_volume(self, volume: int, timeout: float = 2.0,
                         wait_ack: bool = True) -> Optional[Tuple[int, int, bytes]]:
        """CMD 0x41 — volume 0~100."""
        param = bp.build_voice_volume_param(volume)
        if not wait_ack:
            self.send_cmd(CMD_SET_VOICE_VOLUME, param)
            return None
        return self.request_response(CMD_SET_VOICE_VOLUME, param,
                                     expect_cmd=CMD_SET_VOICE_VOLUME,
                                     timeout=timeout)

    def get_voice_volume(self, timeout: float = 2.0) -> Optional[int]:
        """CMD 0x42 — device replies 1-byte volume."""
        resp = self.request_response(CMD_GET_VOICE_VOLUME, b'',
                                     expect_cmd=CMD_GET_VOICE_VOLUME,
                                     timeout=timeout)
        if resp is None or not resp[2]:
            return None
        return int(resp[2][0])

    def get_voice_stat(self, timeout: float = 2.0) -> Optional[dict]:
        """
        CMD 0x43 — device replies 1-byte state.
        0=idle 1=listening 2=playing (README §3.3).
        """
        resp = self.request_response(CMD_GET_VOICE_STAT, b'',
                                     expect_cmd=CMD_GET_VOICE_STAT,
                                     timeout=timeout)
        if resp is None or not resp[2]:
            return None
        code = int(resp[2][0])
        return {
            'code': code,
            'state': code,
            'name': bp.VOICE_STAT_NAMES.get(code, 'unknown'),
        }

    def send_tts_content(self, content: str, timeout: float = 2.0,
                         wait_ack: bool = True) -> Optional[Tuple[int, int, bytes]]:
        """CMD 0x44 TTS — host pushes play content to device."""
        param = bp.build_tts_content_param(content)
        if not wait_ack:
            self.send_cmd(CMD_TTS_CONTENT, param)
            return None
        return self.request_response(CMD_TTS_CONTENT, param,
                                     expect_cmd=CMD_TTS_CONTENT,
                                     timeout=timeout)

    def invoke_tsl(self, content: str, timeout: float = 2.0) -> Optional[Tuple[int, int, bytes]]:
        """CMD 0x9 物模型方法调用 — host content → device empty ACK (README §1.9)."""
        param = bp.build_tsl_invoke_param(content)
        return self.request_response(CMD_TSL_INVOKE, param,
                                     expect_cmd=CMD_TSL_INVOKE,
                                     timeout=timeout)

    def http_init(self, timeout: float = 2.0) -> Optional[Tuple[int, int, bytes]]:
        """CMD 0x52 HTTP_INIT — initialize device HTTP client."""
        return self.request_response(CMD_HTTP_INIT, bp.build_http_init_param(),
                                     expect_cmd=CMD_HTTP_INIT,
                                     timeout=timeout)

    def http_deinit(self, timeout: float = 2.0) -> Optional[Tuple[int, int, bytes]]:
        """CMD 0x53 HTTP_DEINIT — deinit device HTTP client."""
        return self.request_response(CMD_HTTP_DEINIT, bp.build_http_deinit_param(),
                                     expect_cmd=CMD_HTTP_DEINIT,
                                     timeout=timeout)

    def http_request(self, method: Any, url: str, headers: str = '',
                     body: Any = b'', timeout: float = 5.0) -> Optional[dict]:
        """
        CMD 0x50 HTTP_REQUEST — trigger device HTTP client request.
        Param: method(1)+url_len(2LE)+url+hdr_len(2LE)+headers+body
        (origin/dev tool/mainwindow.py `_on_http_send`).
        Device replies CMD 0x51: status(2 LE)+body.
        Returns {method,url,status,body,device_id} or None on timeout.
        """
        param = bp.build_http_request_param(method, url, headers, body)
        self.send_cmd(CMD_HTTP_REQUEST, param)
        self._emit_log('HTTP request %s %s -> device' % (
            str(method).upper(), url))
        resp = self._wait_frame(CMD_HTTP_RESPONSE, timeout=timeout,
                                poll_interval=0.005)
        if resp is None:
            return None
        device_id, cmd, rparam = resp
        parsed = bp.parse_http_response_param(rparam)
        if parsed is None:
            return None
        status, body_text = parsed
        return {
            'device_id': device_id,
            'cmd': cmd,
            'status': status,
            'body': body_text,
            'method': str(method).upper(),
            'url': url,
        }

    # ------------------------------------------------------------------
    # misc
    # ------------------------------------------------------------------

    def reset_rx_queue(self) -> None:
        with self._cv:
            self._rx_frames.clear()
            self._rx_buf.clear()

    def status(self) -> dict:
        return {
            'host_id': self.host_id,
            'encrypt': self._encrypt,
            'transfer_active': self._xfer_busy,
            'transfer_result': self._xfer_result,
            'tx_frames': len(self._tx_log),
            'rx_frames': len(self._rx_log),
        }
