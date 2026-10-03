#!/usr/bin/env python3
"""
Device business-service tests for BabyOS Studio.

Covers:
  - device.protocol_client.ProtocolClient  (real BabyOS protocol frames)
  - device.shell_client.ShellClient        (param shell text commands)
  - device.http_mock.HttpMock              (real ThreadingHTTPServer)
  - device.device_manager.DeviceManager    (singleton service registry)

Authority:
  - origin/master:tool/README.md (BabyOS 协议说明)
  - origin/master:tool/b_protocol.py / tool/mainwindow.py
  - bos/modules/b_mod_protocol.h / b_mod_protocol.c
  - bos/modules/b_mod_param.c  ("param" shell command)
  - bos/algorithm/algo_crc.c   (CRC32 口径)

Run:
  tool/babyos-studio/python/.venv/bin/python \
      tool/babyos-studio/test/device_features/test_device_services.py
  or: pytest tool/babyos-studio/test/device_features/test_device_services.py
"""

from __future__ import print_function

import json
import os
import struct
import sys
import tempfile
import threading
import time
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
if _PY_ROOT not in sys.path:
    sys.path.insert(0, _PY_ROOT)

from device import b_protocol as bp  # noqa: E402
from device.b_protocol import (  # noqa: E402
    TEA_KEY,
    tea_encrypt,
    tea_decrypt,
)
from device.crc_util import crc32  # noqa: E402
from device.sn_util import sn_bytes, sn_hex  # noqa: E402
from device.protocol_client import (  # noqa: E402
    ProtocolClient,
    CMD_TEST,
    CMD_UTC,
    CMD_FW_INFO,
    CMD_FDATA,
    CMD_OTA_RESULT,
    CMD_TRANS_FILE,
    CMD_GET_UID,
    CMD_WRITE_SN,
    CMD_DEVICEINFO,
    OTA_RESULT_OK,
    OTA_RESULT_CRC_ERROR,
    DEVICE_ID_HOST,
    INVALID_ID,
)
from device.shell_client import ShellClient  # noqa: E402
from device.http_mock import HttpMock  # noqa: E402
from device.device_manager import DeviceManager, get_device_manager  # noqa: E402


# ---------------------------------------------------------------------------
# Fake UART + protocol-faithful fake device
# ---------------------------------------------------------------------------

class FakeUart:
    """In-memory UART: host writes captured, device responses injected."""

    def __init__(self) -> None:
        self.written = bytearray()
        self._rx = bytearray()
        self._is_open = True
        self.port = 'FAKE0'
        self.baudrate = 115200
        self.write_fail = False
        self._lock = threading.Lock()
        self.on_write = None  # optional hook(bytes)

    def open(self, port, baudrate=115200, **kwargs):
        self.port = port
        self.baudrate = baudrate
        self._is_open = True
        return True

    @property
    def is_open(self):
        return self._is_open

    @is_open.setter
    def is_open(self, value):
        self._is_open = bool(value)

    def write(self, data) -> int:
        if self.write_fail:
            return -1
        if isinstance(data, bytearray):
            data = bytes(data)
        if not isinstance(data, (bytes, bytearray)):
            return -1
        with self._lock:
            self.written.extend(data)
        if self.on_write is not None:
            self.on_write(bytes(data))
        return len(data)

    def read_available(self) -> bytes:
        with self._lock:
            out = bytes(self._rx)
            self._rx.clear()
            return out

    def inject(self, data) -> None:
        with self._lock:
            self._rx.extend(data)

    def close(self) -> None:
        self._is_open = False

    def all_written(self) -> bytes:
        with self._lock:
            return bytes(self.written)

    def clear_written(self) -> None:
        with self._lock:
            self.written.clear()


class FakeDevice:
    """
    Protocol-faithful BabyOS device simulator.

    Parses host frames from UART writes and injects device frames:
      - default ACK (empty same-cmd) for TEST/UTC/FW_INFO/TRANS_FILE/WRITE_SN
      - GET_UID  → len+uid
      - DEVICEINFO → version(16)+name(16)
      - OTA / file transfer: after 0x3/0x6, request FDATA chunks, then 0x5
    """

    def __init__(self, uart: FakeUart, device_id: int = 0x0000ABCD,
                 uid: bytes = b'\x11\x22\x33\x44\x55\x66\x77\x88',
                 version: str = 'v1.2.3', model: str = 'BabyOS-MCU',
                 ota_result: int = 0) -> None:
        self.uart = uart
        self.device_id = device_id
        self.uid = uid
        self.version = version
        self.model = model
        self.ota_result = ota_result
        self.rx_buf = bytearray()
        self.fw_info = None          # (size, crc, name) from 0x3
        self.trans_file = None       # (size, crc, dev_no, offset) from 0x6
        self.received_chunks = []    # list of (seq, data512)
        self.received_fw_bytes = bytearray()
        self.host_acks = []          # (cmd, param) of host ACK frames
        self.written_cmds = []       # host cmd numbers seen
        uart.on_write = self._on_host_write

    # -- frame helpers ----------------------------------------------------

    @staticmethod
    def _pack(device_id: int, cmd: int, param: bytes = b'') -> bytes:
        return bp.pack_frame(device_id, cmd, param)

    def _reply(self, cmd: int, param: bytes = b'') -> None:
        self.uart.inject(self._pack(self.device_id, cmd, param))

    # -- host write hook --------------------------------------------------

    def _on_host_write(self, data: bytes) -> None:
        self.rx_buf.extend(data)
        while True:
            frame = self._extract()
            if frame is None:
                break
            dev_id, cmd, param = frame
            self.written_cmds.append(cmd)
            self._handle(cmd, param)

    def _extract(self):
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
        if frame_len < 1 or total_len > 4096:
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

    def _handle(self, cmd: int, param: bytes) -> None:
        if cmd == CMD_TEST:
            # firmware: empty ACK (PROTOCOL_NEED_DEFAULT_ACK on device role)
            self._reply(CMD_TEST, b'')
        elif cmd == CMD_UTC:
            self._reply(CMD_UTC, b'')
        elif cmd == CMD_FW_INFO:
            parsed = bp.parse_fw_info_param(param)
            self.fw_info = parsed
            # device ACK + start FDATA request seq=0 (if size > 0)
            self._reply(CMD_FW_INFO, b'')
            if parsed is not None and parsed[0] > 0:
                self._reply(CMD_FDATA, bp.build_fdata_req_param(0))
        elif cmd == CMD_TRANS_FILE:
            parsed = bp.parse_trans_file_param(param)
            self.trans_file = parsed
            self._reply(CMD_TRANS_FILE, b'')
            if parsed is not None and parsed[0] > 0:
                self._reply(CMD_FDATA, bp.build_fdata_req_param(0))
        elif cmd == CMD_FDATA:
            parsed = bp.parse_fdata_param(param)
            if parsed is None:
                return
            seq, data = parsed
            self.received_chunks.append((seq, data))
            # accumulate payload using announced size
            size = 0
            if self.fw_info is not None:
                size = self.fw_info[0]
            elif self.trans_file is not None:
                size = self.trans_file[0]
            base = seq * 512
            # keep only the real bytes
            self.received_fw_bytes.extend(data[:max(0, size - base)] if size else data)
            next_seq = seq + 1
            if size and next_seq * 512 < size:
                self._reply(CMD_FDATA, bp.build_fdata_req_param(next_seq))
            else:
                # verify crc if we have the full payload
                ok = True
                if size and self.fw_info is not None:
                    crc_local = crc32(bytes(self.received_fw_bytes[:size]))
                    ok = (crc_local == self.fw_info[1])
                elif size and self.trans_file is not None:
                    crc_local = crc32(bytes(self.received_fw_bytes[:size]))
                    ok = (crc_local == self.trans_file[1])
                result = self.ota_result if ok else 1  # 1 = crc_error
                self._reply(CMD_OTA_RESULT, bytes([result & 0xFF]))
        elif cmd == CMD_OTA_RESULT:
            # host ACK — record, no further reply
            self.host_acks.append((cmd, param))
        elif cmd == CMD_GET_UID:
            self._reply(CMD_GET_UID, bytes([len(self.uid)]) + self.uid)
        elif cmd == CMD_WRITE_SN:
            self.host_acks.append((cmd, param))
            self._reply(CMD_WRITE_SN, b'')
        elif cmd == CMD_DEVICEINFO:
            v = self.version.encode('utf-8')[:16].ljust(16, b'\x00')
            n = self.model.encode('utf-8')[:16].ljust(16, b'\x00')
            self._reply(CMD_DEVICEINFO, v + n)
        else:
            # unknown: still ACK empty like firmware default for host-role cmds
            self._reply(cmd, b'')


class FakeShellDevice:
    """Shell-text device: responds to 'param ...' commands like b_mod_param.c."""

    def __init__(self, uart: FakeUart,
                 params: dict = None) -> None:
        self.uart = uart
        self.params = dict(params or {
            'g_param_test_val': 12345,
            'g_param_test_val2': -999,
            'g_volume': 50,
        })
        self.rx = bytearray()
        self.commands = []
        self.echo = True   # host-side terminal often echoes
        uart.on_write = self._on_write

    def _on_write(self, data: bytes) -> None:
        self.rx.extend(data)
        # commands are CR-terminated
        while True:
            idx = self.rx.find(b'\r')
            if idx < 0:
                break
            raw = bytes(self.rx[:idx])
            del self.rx[:idx + 1]
            # strip LF leftovers
            text = raw.decode('utf-8', errors='replace').strip('\n')
            self.commands.append(text)
            self._handle(text)

    def _emit(self, text: str) -> None:
        payload = text.encode('utf-8')
        if self.echo:
            # echo the command line (typical interactive shell)
            pass  # not required; firmware b_log only prints results
        self.uart.inject(payload)

    def _handle(self, cmd: str) -> None:
        parts = cmd.split()
        if not parts:
            return
        if parts[0] != 'param':
            return
        if len(parts) == 1:
            # list: firmware b_log(": %s\r\n", name)
            for name in self.params:
                self._emit(': %s\r\n' % name)
            return
        name = parts[1]
        if len(parts) == 2:
            if name in self.params:
                self._emit('%s:%d\r\n' % (name, self.params[name]))
            return
        if len(parts) >= 3:
            try:
                value = int(parts[2], 10)
            except ValueError:
                return
            if name in self.params:
                self.params[name] = value
            else:
                self.params[name] = value
            # firmware prints nothing on set


# ---------------------------------------------------------------------------
# ProtocolClient
# ---------------------------------------------------------------------------

class TestProtocolClientFrames(unittest.TestCase):
    """send_cmd 帧布局 / feed_bytes 解析"""

    def setUp(self):
        self.uart = FakeUart()
        self.pc = ProtocolClient(self.uart, log_fn=lambda m: None)

    def test_host_id_bound_to_0x1314(self):
        self.assertEqual(self.pc.host_id, DEVICE_ID_HOST)
        self.assertEqual(DEVICE_ID_HOST, 0x1314)

    def test_send_cmd_returns_packed_frame(self):
        frame = self.pc.send_cmd(CMD_TEST, bp.build_test_param())
        self.assertEqual(frame[0], bp.PROTOCOL_HEAD)
        # host→device default id = INVALID_ID (master mainwindow)
        self.assertEqual(struct.unpack_from('<I', frame, 1)[0], INVALID_ID)
        self.assertEqual(struct.unpack_from('<H', frame, 5)[0], 1 + 7)
        self.assertEqual(frame[7], CMD_TEST)
        self.assertEqual(frame[8:15], b'BabyOS\x00')
        self.assertEqual(frame[-1], bp.calc_checksum(frame[:-1]))
        # written to uart
        self.assertEqual(bytes(self.uart.written), frame)

    def test_send_cmd_with_explicit_device_id(self):
        frame = self.pc.pack_cmd(CMD_TEST, b'BabyOS\x00', device_id=0x11223344)
        self.assertEqual(struct.unpack_from('<I', frame, 1)[0], 0x11223344)

    def test_feed_bytes_parses_single_frame(self):
        raw = bp.pack_frame(0xABCD, CMD_TEST, b'BabyOS\x00')
        got = self.pc.feed_bytes(raw)
        self.assertEqual(len(got), 1)
        dev_id, cmd, param = got[0]
        self.assertEqual(dev_id, 0xABCD)
        self.assertEqual(cmd, CMD_TEST)
        self.assertEqual(param, b'BabyOS\x00')

    def test_feed_bytes_partial_then_complete(self):
        raw = bp.pack_frame(0xABCD, CMD_GET_UID, b'')
        got = self.pc.feed_bytes(raw[:5])
        self.assertEqual(got, [])
        got = self.pc.feed_bytes(raw[5:])
        self.assertEqual(len(got), 1)
        self.assertEqual(got[0][1], CMD_GET_UID)

    def test_feed_bytes_rejects_bad_checksum(self):
        raw = bytearray(bp.pack_frame(0xABCD, CMD_TEST, b'BabyOS\x00'))
        raw[-1] ^= 0xFF
        got = self.pc.feed_bytes(bytes(raw))
        self.assertEqual(got, [])

    def test_feed_bytes_resyncs_after_garbage(self):
        raw = bp.pack_frame(0xABCD, CMD_TEST, b'BabyOS\x00')
        got = self.pc.feed_bytes(b'\x00\x11\x22' + raw)
        self.assertEqual(len(got), 1)
        self.assertEqual(got[0][1], CMD_TEST)

    def test_feed_bytes_multiple_frames(self):
        a = bp.pack_frame(1, CMD_TEST, b'BabyOS\x00')
        b = bp.pack_frame(2, CMD_GET_UID, b'')
        got = self.pc.feed_bytes(a + b)
        self.assertEqual(len(got), 2)
        self.assertEqual(got[0][1], CMD_TEST)
        self.assertEqual(got[1][1], CMD_GET_UID)


class TestProtocolClientSimpleCommands(unittest.TestCase):
    """test_link / set_time / get_uid / write_sn / get_device_info"""

    def setUp(self):
        self.uart = FakeUart()
        self.device = FakeDevice(self.uart)
        self.pc = ProtocolClient(self.uart, log_fn=lambda m: None)
        self.progress = []
        self.logs = []
        self.results = []
        self.pc.on_progress = lambda p: self.progress.append(p)
        self.pc.on_log = lambda m: self.logs.append(m)
        self.pc.on_result = lambda ok, c: self.results.append((ok, c))

    def test_test_link(self):
        resp = self.pc.test_link(timeout=2.0)
        self.assertIsNotNone(resp)
        dev_id, cmd, param = resp
        self.assertEqual(cmd, CMD_TEST)
        self.assertEqual(param, b'')
        self.assertEqual(dev_id, self.device.device_id)
        # host sent b'BabyOS\x00'
        self.assertIn(b'BabyOS\x00', self.uart.all_written())

    def test_set_time_utc(self):
        utc = 1700000000
        resp = self.pc.set_time(utc, timeout=2.0)
        self.assertIsNotNone(resp)
        self.assertEqual(resp[1], CMD_UTC)
        # host frame carries 4B LE utc on the wire
        expected_param = struct.pack('<I', utc)
        frame = bp.pack_frame(INVALID_ID, CMD_UTC, expected_param)
        self.assertIn(frame, self.uart.all_written())

    def test_get_uid(self):
        uid = self.pc.get_uid(timeout=2.0)
        self.assertEqual(uid, self.device.uid)

    def test_write_sn_from_uid(self):
        uid = self.device.uid
        expected = sn_bytes(uid, 0)
        resp = self.pc.write_sn(uid=uid, orval=0, timeout=2.0)
        self.assertIsNotNone(resp)
        self.assertEqual(resp[1], CMD_WRITE_SN)
        self.assertEqual(resp[2], b'')  # device empty ACK
        # SN param was on the wire
        self.assertIn(expected, self.uart.all_written())
        # length prefix + md5|orval layout
        self.assertEqual(expected[0], 16)
        self.assertEqual(len(expected), 17)

    def test_write_sn_from_sn_bytes(self):
        sn = sn_bytes(self.device.uid, 0x5A)
        resp = self.pc.write_sn(sn_bytes=sn, timeout=2.0)
        self.assertIsNotNone(resp)
        self.assertIn(sn, self.uart.all_written())
        # orval applied
        self.assertEqual(sn[0], 16)
        body = sn[1:]
        self.assertEqual(body[0] | 0x5A, body[0])

    def test_get_device_info(self):
        info = self.pc.get_device_info(timeout=2.0)
        self.assertIsNotNone(info)
        version, model = info
        self.assertEqual(version, 'v1.2.3')
        self.assertEqual(model, 'BabyOS-MCU')

    def test_request_response_timeout(self):
        # no device attached
        uart2 = FakeUart()
        pc2 = ProtocolClient(uart2, log_fn=lambda m: None)
        resp = pc2.request_response(CMD_TEST, bp.build_test_param(),
                                    expect_cmd=CMD_TEST, timeout=0.15)
        self.assertIsNone(resp)


class TestProtocolClientOTA(unittest.TestCase):
    """OTA: 0x3 → 0x4 loop → 0x5 result → host 0x5 ACK"""

    def _make_fw(self, size: int) -> str:
        data = bytes((i * 7 + 3) & 0xFF for i in range(size))
        fd, path = tempfile.mkstemp(suffix='.bin')
        os.write(fd, data)
        os.close(fd)
        self.addCleanup(lambda: os.path.exists(path) and os.remove(path))
        return path

    def test_ota_single_chunk_success(self):
        path = self._make_fw(100)
        with open(path, 'rb') as f:
            fw = f.read()
        uart = FakeUart()
        dev = FakeDevice(uart, ota_result=OTA_RESULT_OK)
        pc = ProtocolClient(uart, log_fn=lambda m: None)
        progress, results, logs = [], [], []
        pc.on_progress = lambda p: progress.append(p)
        pc.on_result = lambda ok, c: results.append((ok, c))
        pc.on_log = lambda m: logs.append(m)

        ok = pc.start_ota(path, name='test_fw.bin', timeout=5.0, poll_interval=0.002)
        self.assertTrue(ok)
        self.assertEqual(results, [(True, OTA_RESULT_OK)])
        self.assertEqual(dev.fw_info[0], len(fw))
        self.assertEqual(dev.fw_info[1], crc32(fw))
        self.assertEqual(dev.fw_info[2], 'test_fw.bin')
        self.assertEqual(len(dev.received_chunks), 1)
        seq, data = dev.received_chunks[0]
        self.assertEqual(seq, 0)
        self.assertEqual(data[:len(fw)], fw)
        self.assertEqual(data[len(fw):], b'\x00' * (512 - len(fw)))
        self.assertEqual(progress[-1], 100)
        # host ACKed 0x5
        self.assertTrue(any(c == CMD_OTA_RESULT and p == b''
                            for c, p in dev.host_acks))

    def test_ota_multi_chunk(self):
        size = 512 * 2 + 50  # 3 chunks
        path = self._make_fw(size)
        with open(path, 'rb') as f:
            fw = f.read()
        uart = FakeUart()
        dev = FakeDevice(uart, ota_result=OTA_RESULT_OK)
        pc = ProtocolClient(uart, log_fn=lambda m: None)
        results = []
        pc.on_result = lambda ok, c: results.append((ok, c))

        ok = pc.start_ota(path, timeout=8.0, poll_interval=0.002)
        self.assertTrue(ok)
        self.assertEqual(results, [(True, OTA_RESULT_OK)])
        self.assertEqual(len(dev.received_chunks), 3)
        # reassemble and compare
        rebuilt = b''.join(c[1] for c in sorted(dev.received_chunks,
                                                 key=lambda x: x[0]))
        self.assertEqual(rebuilt[:size], fw)
        self.assertEqual(bytes(dev.received_fw_bytes[:size]), fw)
        # seqs 0,1,2 in order
        self.assertEqual([c[0] for c in dev.received_chunks], [0, 1, 2])

    def test_ota_crc_error_reported(self):
        path = self._make_fw(200)
        uart = FakeUart()
        dev = FakeDevice(uart, ota_result=OTA_RESULT_OK)
        # force device-side crc check failure by lying about crc in fw_info
        # easier: set ota_result after corrupting — patch device to report crc_error
        dev.ota_result = OTA_RESULT_OK
        orig_handle = dev._handle

        def bad_handle(cmd, param):
            if cmd == CMD_FW_INFO:
                # corrupt crc32 so device reports crc_error
                size, _crc, name = bp.parse_fw_info_param(param)
                bad = bp.build_fw_info_param(size, 0xDEADBEEF, name)
                dev.fw_info = (size, 0xDEADBEEF, name)
                dev._reply(CMD_FW_INFO, b'')
                dev._reply(CMD_FDATA, bp.build_fdata_req_param(0))
                return
            orig_handle(cmd, param)

        dev._handle = bad_handle
        pc = ProtocolClient(uart, log_fn=lambda m: None)
        results = []
        pc.on_result = lambda ok, c: results.append((ok, c))
        ok = pc.start_ota(path, timeout=5.0, poll_interval=0.002)
        self.assertFalse(ok)
        self.assertEqual(results, [(False, OTA_RESULT_CRC_ERROR)])

    def test_ota_timeout(self):
        path = self._make_fw(64)
        uart = FakeUart()  # no device → no responses
        pc = ProtocolClient(uart, log_fn=lambda m: None)
        results = []
        pc.on_result = lambda ok, c: results.append((ok, c))
        ok = pc.start_ota(path, timeout=0.2, poll_interval=0.005)
        self.assertFalse(ok)
        self.assertEqual(results, [(False, 4)])  # OTA_RESULT_TIMEOUT
        # FW_INFO was sent on the wire
        parsed = bp.parse_frame(bytes(uart.written))
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed[1], CMD_FW_INFO)

    def test_file_transfer_success(self):
        path = self._make_fw(300)
        with open(path, 'rb') as f:
            payload = f.read()
        uart = FakeUart()
        dev = FakeDevice(uart, ota_result=OTA_RESULT_OK)
        pc = ProtocolClient(uart, log_fn=lambda m: None)
        results = []
        pc.on_result = lambda ok, c: results.append((ok, c))

        ok = pc.start_file_transfer(path, dev_no=3, offset=0x1000,
                                    timeout=5.0, poll_interval=0.002)
        self.assertTrue(ok)
        self.assertEqual(results, [(True, OTA_RESULT_OK)])
        self.assertEqual(dev.trans_file[0], len(payload))
        self.assertEqual(dev.trans_file[1], crc32(payload))
        self.assertEqual(dev.trans_file[2], 3)
        self.assertEqual(dev.trans_file[3], 0x1000)
        self.assertEqual(bytes(dev.received_fw_bytes[:len(payload)]), payload)

    def test_ota_rejects_missing_file(self):
        uart = FakeUart()
        pc = ProtocolClient(uart, log_fn=lambda m: None)
        with self.assertRaises(IOError):
            pc.start_ota('/no/such/firmware.bin', timeout=0.2)

    def test_ota_rejects_empty_file(self):
        fd, path = tempfile.mkstemp(suffix='.bin')
        os.close(fd)
        self.addCleanup(lambda: os.path.exists(path) and os.remove(path))
        uart = FakeUart()
        FakeDevice(uart)
        pc = ProtocolClient(uart, log_fn=lambda m: None)
        results = []
        pc.on_result = lambda ok, c: results.append((ok, c))
        ok = pc.start_ota(path, timeout=0.5, poll_interval=0.002)
        self.assertFalse(ok)
        self.assertEqual(results, [(False, 3)])  # LEN_INVALID


class TestProtocolClientCallbacks(unittest.TestCase):
    """progress / log / result 回调"""

    def test_callbacks_fired_on_ota(self):
        fd, path = tempfile.mkstemp(suffix='.bin')
        os.write(fd, b'A' * 600)  # 2 chunks
        os.close(fd)
        self.addCleanup(lambda: os.path.exists(path) and os.remove(path))

        uart = FakeUart()
        FakeDevice(uart)
        pc = ProtocolClient(uart, log_fn=lambda m: None)
        progress, logs, results = [], [], []
        pc.on_progress = lambda p: progress.append(p)
        pc.on_log = lambda m: logs.append(m)
        pc.on_result = lambda ok, c: results.append((ok, c))

        ok = pc.start_ota(path, timeout=5.0, poll_interval=0.002)
        self.assertTrue(ok)
        self.assertTrue(progress, 'progress callbacks expected')
        self.assertEqual(progress[-1], 100)
        self.assertTrue(any('OTA start' in m for m in logs))
        self.assertTrue(any('transfer result' in m for m in logs))
        self.assertEqual(results, [(True, OTA_RESULT_OK)])


# ---------------------------------------------------------------------------
# ShellClient
# ---------------------------------------------------------------------------

class TestShellClient(unittest.TestCase):
    """param shell 文本命令 — 对齐 b_mod_param.c"""

    def setUp(self):
        self.uart = FakeUart()
        self.dev = FakeShellDevice(self.uart)
        self.sh = ShellClient(self.uart, default_timeout=1.0)

    def test_write_appends_cr(self):
        self.sh.write('param')
        self.assertEqual(self.uart.all_written(), b'param\r')

    def test_write_keeps_existing_ending(self):
        self.sh.write('param\r')
        self.assertEqual(self.uart.all_written(), b'param\r')

    def test_param_list(self):
        names = self.sh.param_list(timeout=1.0)
        self.assertIn('g_param_test_val', names)
        self.assertIn('g_param_test_val2', names)
        self.assertIn('g_volume', names)
        self.assertEqual(self.dev.commands[0], 'param')

    def test_param_get(self):
        val = self.sh.param_get('g_param_test_val', timeout=1.0)
        self.assertEqual(val, 12345)
        self.assertEqual(self.dev.commands[-1], 'param g_param_test_val')

    def test_param_get_negative(self):
        val = self.sh.param_get('g_param_test_val2', timeout=1.0)
        self.assertEqual(val, -999)

    def test_param_get_missing(self):
        val = self.sh.param_get('no_such_param', timeout=0.4)
        self.assertIsNone(val)

    def test_param_set_and_verify(self):
        ok = self.sh.param_set('g_volume', 80, timeout=1.0, verify=True)
        self.assertTrue(ok)
        self.assertEqual(self.dev.params['g_volume'], 80)
        self.assertEqual(self.dev.commands[-2], 'param g_volume 80')
        # re-read
        self.assertEqual(self.sh.param_get('g_volume', timeout=1.0), 80)

    def test_param_set_negative(self):
        ok = self.sh.param_set('g_param_test_val', -777, verify=True)
        self.assertTrue(ok)
        self.assertEqual(self.dev.params['g_param_test_val'], -777)

    def test_param_set_missing_param_creates_and_verifies(self):
        ok = self.sh.param_set('g_new_param', 42, verify=True)
        self.assertTrue(ok)
        self.assertEqual(self.dev.params['g_new_param'], 42)

    def test_parse_list_format(self):
        text = ': alpha\r\n: beta\r\n: gamma\r\n'
        self.assertEqual(ShellClient._parse_list(text), ['alpha', 'beta', 'gamma'])

    def test_parse_get_format(self):
        self.assertEqual(ShellClient._parse_get('foo:123\r\n', 'foo'), 123)
        self.assertEqual(ShellClient._parse_get('foo:-1\r\n', 'foo'), -1)
        self.assertIsNone(ShellClient._parse_get('bar:9\r\n', 'foo'))

    def test_send_command_raw(self):
        # non-param shell command still goes out as text
        self.sh.send_command('bos -v', timeout=0.4)
        self.assertEqual(self.dev.commands[-1], 'bos -v')
        self.assertIn(b'bos -v\r', self.uart.all_written())

    def test_status(self):
        st = self.sh.status()
        self.assertEqual(st['line_ending'], '\r')
        self.assertGreaterEqual(st['commands_sent'], 0)


# ---------------------------------------------------------------------------
# HttpMock
# ---------------------------------------------------------------------------

class TestHttpMock(unittest.TestCase):
    """真实 ThreadingHTTPServer — 非 stub"""

    def setUp(self):
        self.mock = HttpMock()
        self.addCleanup(self.mock.stop)

    def test_start_records_port(self):
        port = self.mock.start(port=0, body=b'hello', content_type='text/plain',
                               status_code=200)
        self.assertGreater(port, 0)
        self.assertTrue(self.mock.is_running)
        self.assertEqual(self.mock.base_url, 'http://127.0.0.1:%d' % port)
        self.addCleanup(self.mock.stop)

    def test_get_records_and_returns_body(self):
        import urllib.request
        port = self.mock.start(port=0, body=b'{"ok":true}',
                               content_type='application/json', status_code=200)
        url = 'http://127.0.0.1:%d/api/status' % port
        with urllib.request.urlopen(url, timeout=2.0) as resp:
            body = resp.read()
            self.assertEqual(resp.status, 200)
            self.assertEqual(resp.headers.get('Content-Type'), 'application/json')
            self.assertEqual(body, b'{"ok":true}')
        entry = self.mock.wait_for_request(path_only='/api/status', timeout=1.0)
        self.assertIsNotNone(entry)
        self.assertEqual(entry['method'], 'GET')
        self.assertEqual(entry['path'], '/api/status')

    def test_post_records_body_and_headers(self):
        import urllib.request
        port = self.mock.start(port=0, body=b'{"r":1}',
                               content_type='application/json', status_code=201)
        url = 'http://127.0.0.1:%d/api/wifi' % port
        payload = json.dumps({'ssid': 'baby', 'pass': '12345678'}).encode('utf-8')
        req = urllib.request.Request(url, data=payload, method='POST')
        req.add_header('Content-Type', 'application/json')
        with urllib.request.urlopen(req, timeout=2.0) as resp:
            self.assertEqual(resp.status, 201)
            self.assertEqual(resp.read(), b'{"r":1}')
        entry = self.mock.wait_for_request(path_only='/api/wifi', method='POST',
                                           timeout=1.0)
        self.assertIsNotNone(entry)
        self.assertEqual(entry['body'], '{"ssid": "baby", "pass": "12345678"}')
        self.assertEqual(entry['body_len'], len(payload))
        self.assertIn('Content-Type', entry['headers'])

    def test_requests_endpoint_returns_log(self):
        import urllib.request
        port = self.mock.start(port=0, body=b'x', content_type='text/plain')
        # trigger a request first
        with urllib.request.urlopen('http://127.0.0.1:%d/ping' % port,
                                    timeout=2.0) as resp:
            resp.read()
        with urllib.request.urlopen('http://127.0.0.1:%d/_requests' % port,
                                    timeout=2.0) as resp:
            data = json.loads(resp.read().decode('utf-8'))
        self.assertIsInstance(data, list)
        paths = [e['path_only'] for e in data]
        self.assertIn('/ping', paths)
        self.assertIn('/_requests', paths)
        # log entries are serialisable
        for e in data:
            self.assertIn('method', e)
            self.assertIn('path', e)

    def test_set_path_response(self):
        import urllib.request
        port = self.mock.start(port=0, body=b'default',
                               content_type='text/plain', status_code=200)
        self.mock.set_path_response('/api/eth', b'{"eth":"up"}',
                                    content_type='application/json',
                                    status_code=200)
        with urllib.request.urlopen('http://127.0.0.1:%d/api/eth' % port,
                                    timeout=2.0) as resp:
            self.assertEqual(resp.read(), b'{"eth":"up"}')
        with urllib.request.urlopen('http://127.0.0.1:%d/other' % port,
                                    timeout=2.0) as resp:
            self.assertEqual(resp.read(), b'default')

    def test_custom_status_code(self):
        import urllib.request
        import urllib.error
        port = self.mock.start(port=0, body=b'nope',
                               content_type='text/plain', status_code=404)
        try:
            urllib.request.urlopen('http://127.0.0.1:%d/x' % port, timeout=2.0)
            self.fail('expected HTTPError 404')
        except urllib.error.HTTPError as e:
            self.assertEqual(e.code, 404)
            self.assertEqual(e.read(), b'nope')

    def test_stop_is_idempotent(self):
        self.mock.start(port=0)
        self.mock.stop()
        self.assertFalse(self.mock.is_running)
        self.mock.stop()  # second stop must not raise
        self.assertEqual(self.mock.port, 0)

    def test_clear_requests(self):
        import urllib.request
        port = self.mock.start(port=0, body=b'z')
        urllib.request.urlopen('http://127.0.0.1:%d/a' % port, timeout=2.0).read()
        self.assertGreaterEqual(self.mock.request_count, 1)
        self.mock.clear_requests()
        self.assertEqual(self.mock.request_count, 0)

    def test_status_dict(self):
        self.mock.start(port=0, body=b'abc', content_type='text/plain',
                        status_code=202)
        st = self.mock.status()
        self.assertTrue(st['running'])
        self.assertGreater(st['port'], 0)
        self.assertEqual(st['status_code'], 202)
        self.assertEqual(st['body_len'], 3)


# ---------------------------------------------------------------------------
# DeviceManager
# ---------------------------------------------------------------------------

class TestDeviceManager(unittest.TestCase):
    """单例 DeviceManager — 绑定 uart / protocol / shell / http_mock / xmodem 状态"""

    def setUp(self):
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

    def test_singleton_identity(self):
        a = DeviceManager.get()
        b = DeviceManager.get()
        self.assertIs(a, b)
        c = get_device_manager()
        self.assertIs(a, c)

    def test_reset_creates_new_instance(self):
        a = DeviceManager.get()
        DeviceManager.reset()
        b = DeviceManager.get()
        self.assertIsNot(a, b)

    def test_open_port_binds_clients(self):
        mgr = DeviceManager.get()
        fake = FakeUart()
        mgr.uart = fake
        FakeDevice(fake)
        ok = mgr.open_port('FAKE0', 115200)
        self.assertTrue(ok)
        self.assertTrue(mgr.is_open())
        self.assertIsNotNone(mgr.protocol_client)
        self.assertIsNotNone(mgr.shell_client)
        self.assertEqual(mgr.protocol_client.host_id, DEVICE_ID_HOST)

    def test_protocol_apis_via_manager(self):
        mgr = DeviceManager.get()
        fake = FakeUart()
        mgr.uart = fake
        FakeDevice(fake, uid=b'\x01\x02\x03\x04')
        mgr.uart.is_open = True
        mgr._bind_clients()

        resp = mgr.test_link(timeout=2.0)
        self.assertIsNotNone(resp)
        self.assertEqual(resp[1], CMD_TEST)

        uid = mgr.get_uid(timeout=2.0)
        self.assertEqual(uid, b'\x01\x02\x03\x04')
        self.assertEqual(mgr.last_uid, uid)

        info = mgr.get_device_info(timeout=2.0)
        self.assertEqual(info[0], 'v1.2.3')
        self.assertEqual(mgr.last_devinfo['model'], 'BabyOS-MCU')

        sn_resp = mgr.write_sn(timeout=2.0)  # uses last_uid
        self.assertIsNotNone(sn_resp)
        self.assertEqual(mgr.last_sn, b'')

    def test_shell_apis_via_manager(self):
        mgr = DeviceManager.get()
        fake = FakeUart()
        mgr.uart = fake
        FakeShellDevice(fake, params={'g_volume': 30})
        mgr.uart.is_open = True
        mgr._bind_clients()

        names = mgr.param_list(timeout=1.0)
        self.assertIn('g_volume', names)
        self.assertEqual(mgr.param_get('g_volume', timeout=1.0), 30)
        self.assertTrue(mgr.param_set('g_volume', 77, timeout=1.0, verify=True))
        self.assertEqual(mgr.param_get('g_volume', timeout=1.0), 77)
        self.assertIn('g_volume', mgr.last_param_list)

    def test_http_mock_via_manager(self):
        mgr = DeviceManager.get()
        port = mgr.start_http_mock(port=0, body=b'{"mock":1}',
                                   content_type='application/json',
                                   status_code=200)
        self.addCleanup(mgr.stop_http_mock)
        self.assertGreater(port, 0)
        import urllib.request
        with urllib.request.urlopen('http://127.0.0.1:%d/api/x' % port,
                                    timeout=2.0) as resp:
            self.assertEqual(resp.read(), b'{"mock":1}')
        self.assertGreaterEqual(mgr.http_mock.request_count, 1)
        mgr.stop_http_mock()
        self.assertFalse(mgr.http_mock.is_running)

    def test_ota_via_manager(self):
        mgr = DeviceManager.get()
        fake = FakeUart()
        mgr.uart = fake
        dev = FakeDevice(fake)
        mgr.uart.is_open = True
        mgr._bind_clients()

        fd, path = tempfile.mkstemp(suffix='.bin')
        os.write(fd, b'BabyOS-OTA-Payload-0123456789')
        os.close(fd)
        self.addCleanup(lambda: os.path.exists(path) and os.remove(path))

        ok = mgr.start_ota(path, name='mgr_fw.bin', timeout=5.0)
        # start_ota doesn't pass poll_interval — ProtocolClient default is 0.005
        # which is fine; FakeDevice replies synchronously on write
        self.assertTrue(ok)
        self.assertEqual(dev.fw_info[2], 'mgr_fw.bin')

    def test_status_dict_shape(self):
        mgr = DeviceManager.get()
        st = mgr.status()
        self.assertIn('uart', st)
        self.assertIn('protocol', st)
        self.assertIn('shell', st)
        self.assertIn('http_mock', st)
        self.assertIn('xmodem', st)
        self.assertIn('open', st['uart'])
        self.assertIn('running', st['http_mock'])

    def test_logs_captured(self):
        mgr = DeviceManager.get()
        fake = FakeUart()
        mgr.uart = fake
        FakeDevice(fake)
        mgr.uart.is_open = True
        mgr._bind_clients()
        mgr.test_link(timeout=2.0)
        logs = mgr.get_logs(tail=50)
        self.assertTrue(any('cmd:0x01' in line or 'OTA' in line or 'open' in line
                            or 'clients bound' in line or 's->' in line
                            for line in logs))

    def test_ensure_clients_raises_when_closed(self):
        mgr = DeviceManager.get()
        mgr.uart = FakeUart()
        mgr.uart.is_open = False
        mgr.protocol_client = None
        mgr.shell_client = None
        with self.assertRaises(RuntimeError):
            mgr.ensure_clients()
        with self.assertRaises(RuntimeError):
            mgr.ensure_shell()


# ---------------------------------------------------------------------------
# Cross-checks against master / firmware authorities
# ---------------------------------------------------------------------------

class TestAuthorityParity(unittest.TestCase):
    """与 origin/master tool / 固件口径交叉验证"""

    def test_host_id_matches_master(self):
        self.assertEqual(DEVICE_ID_HOST, 0x1314)
        self.assertEqual(bp.DEVICE_ID_HOST, 0x1314)

    def test_frame_layout_matches_readme(self):
        """HEAD + ID(4) + LEN(2)=1+param + CMD + param + CHECK"""
        param = b'\x01\x02'
        frame = bp.pack_frame(0x1314, 0x0A, param)
        self.assertEqual(frame[0], 0xFE)
        self.assertEqual(struct.unpack_from('<I', frame, 1)[0], 0x1314)
        self.assertEqual(struct.unpack_from('<H', frame, 5)[0], 1 + len(param))
        self.assertEqual(frame[7], 0x0A)
        self.assertEqual(frame[8:10], param)
        self.assertEqual(frame[10], sum(frame[:10]) & 0xFF)
        self.assertEqual(len(frame), 11)

    def test_sn_matches_master_algorithm(self):
        """md5(uid)[:16] 每字节 | orval，前缀 len — master mainwindow _on_set_sn"""
        import hashlib
        uid = b'\xDE\xAD\xBE\xEF'
        orval = 0x0F
        md5_val = hashlib.md5(uid).digest()
        expected = bytes([len(md5_val)]) + bytes(b | orval for b in md5_val)
        self.assertEqual(sn_bytes(uid, orval), expected)

    def test_crc32_matches_firmware_algo_crc(self):
        """CRC32 = zlib.crc32 — algo_crc.c ALGO_CRC32 (init FF, xorout FF)"""
        import zlib
        for data in (b'', b'123456789', b'BabyOS', bytes(range(64))):
            self.assertEqual(crc32(data), zlib.crc32(data) & 0xFFFFFFFF)

    def test_ota_result_codes_match_readme(self):
        self.assertEqual(OTA_RESULT_OK, 0)
        self.assertEqual(bp.OTA_RESULT_CRC_ERROR, 1)
        self.assertEqual(bp.OTA_RESULT_NAME_MISMATCH, 2)
        self.assertEqual(bp.OTA_RESULT_LEN_INVALID, 3)
        self.assertEqual(bp.OTA_RESULT_TIMEOUT, 4)

    def test_fw_info_param_layout(self):
        """0x3: size(4)+crc32(4)+name(64) — bProtoFWParam_t"""
        param = bp.build_fw_info_param(0x12345678, 0xAABBCCDD, 'app.bin')
        self.assertEqual(len(param), 4 + 4 + 64)
        size, crc, name = bp.parse_fw_info_param(param)
        self.assertEqual(size, 0x12345678)
        self.assertEqual(crc, 0xAABBCCDD)
        self.assertEqual(name, 'app.bin')

    def test_fdata_param_layout(self):
        """0x4 host→device: seq(2)+data(512, pad 0)"""
        payload = b'hello'
        param = bp.build_fdata_param(9, payload)
        self.assertEqual(len(param), 2 + 512)
        seq, data = bp.parse_fdata_param(param)
        self.assertEqual(seq, 9)
        self.assertEqual(data[:5], payload)
        self.assertEqual(data[5:], b'\x00' * (512 - 5))

    def test_trans_file_param_layout(self):
        """0x6: size+crc32+dev_no+offset (4×4B)"""
        param = bp.build_trans_file_param(100, 1, 2, 3)
        self.assertEqual(bp.parse_trans_file_param(param), (100, 1, 2, 3))

    def test_devinfo_param_layout(self):
        """0xA reply: version(16)+name(16)"""
        param = bp.build_devinfo_param('v1', 'm1')
        self.assertEqual(len(param), 32)
        ver, name = bp.parse_devinfo_response(param)
        self.assertTrue(ver.startswith(b'v1'))
        self.assertTrue(name.startswith(b'm1'))


# ---------------------------------------------------------------------------
# TEA encryption path (task item 7)
# ---------------------------------------------------------------------------

class TestTEAEncryption(unittest.TestCase):
    """TEA 加密路径 — 固件 _PROTO_ENCRYPT_ENABLE 可选；至少解密兼容"""

    def test_key_matches_firmware_defaults(self):
        """TEA 密钥 = Kconfig SECRET_KEY1..4 默认值 (1,22,333,4444)"""
        self.assertEqual(TEA_KEY, (1, 22, 333, 4444))

    def test_roundtrip_various_sizes(self):
        """加解密往返 — 全块加密、尾部不动"""
        samples = [
            b'',
            b'\x01',
            b'1234567',
            b'12345678',
            bp.pack_frame(DEVICE_ID_HOST, CMD_TEST, b'BabyOS\x00'),
            bp.pack_frame(INVALID_ID, CMD_FW_INFO,
                          bp.build_fw_info_param(512, 0xDEADBEEF, 'fw.bin')),
            bytes(range(256)),
        ]
        for data in samples:
            enc = tea_encrypt(data)
            if len(data) >= 8:
                self.assertNotEqual(enc, data)
            dec = tea_decrypt(enc)
            self.assertEqual(dec, data)

    def test_encrypt_only_full_blocks(self):
        data = b'ABCDEFGHXXXXX'  # 13 bytes → only first 8 transformed
        enc = tea_encrypt(data)
        self.assertEqual(enc[8:], data[8:])
        self.assertNotEqual(enc[:8], data[:8])

    def test_tea_known_block_master_crosscheck(self):
        """单块向量 — 与 origin/master tool/b_protocol.py TEA 实现交叉验证"""
        DELTA = 0x9E3779B9
        K = (1, 22, 333, 4444)

        def master_encrypt_block(v0, v1):
            s = 0
            k0, k1, k2, k3 = K
            for _ in range(16):
                s = (s + DELTA) & 0xFFFFFFFF
                v0 = (v0 + ((((v1 << 4) + k0) ^ (v1 + s) ^ ((v1 >> 5) + k1)))) & 0xFFFFFFFF
                v1 = (v1 + ((((v0 << 4) + k2) ^ (v0 + s) ^ ((v0 >> 5) + k3)))) & 0xFFFFFFFF
            return v0, v1

        plain = b'\x01\x02\x03\x04\x05\x06\x07\x08'
        v0, v1 = struct.unpack('<II', plain)
        ev0, ev1 = master_encrypt_block(v0, v1)
        expected = struct.pack('<II', ev0, ev1)
        self.assertEqual(tea_encrypt(plain), expected)

    def test_tea_decrypt_compat_with_master_encrypted(self):
        """解密兼容 — master b_protocol.py 加密的数据本实现可解"""
        # Independently encrypt with master algorithm, then decrypt with ours
        DELTA = 0x9E3779B9
        K = (1, 22, 333, 4444)

        def master_encrypt_block(v0, v1):
            s = 0
            k0, k1, k2, k3 = K
            for _ in range(16):
                s = (s + DELTA) & 0xFFFFFFFF
                v0 = (v0 + ((((v1 << 4) + k0) ^ (v1 + s) ^ ((v1 >> 5) + k1)))) & 0xFFFFFFFF
                v1 = (v1 + ((((v0 << 4) + k2) ^ (v0 + s) ^ ((v0 >> 5) + k3)))) & 0xFFFFFFFF
            return v0, v1

        def master_encrypt(data: bytes) -> bytes:
            raw = bytearray(data)
            for i in range(len(raw) // 8):
                v0, v1 = struct.unpack_from('<II', raw, i * 8)
                ev0, ev1 = master_encrypt_block(v0, v1)
                struct.pack_into('<II', raw, i * 8, ev0, ev1)
            return bytes(raw)

        plain = bp.pack_frame(DEVICE_ID_HOST, CMD_TEST, b'BabyOS\x00')
        master_enc = master_encrypt(plain)
        # our decrypt must recover the plaintext
        self.assertEqual(tea_decrypt(master_enc), plain)
        # and our encrypt must match master's ciphertext
        self.assertEqual(tea_encrypt(plain), master_enc)

    def test_pack_frame_encrypt_flag(self):
        """pack_frame(encrypt=True) 输出密文帧；tea_decrypt 后可 parse"""
        plain = bp.pack_frame(INVALID_ID, CMD_TEST, b'BabyOS\x00')
        enc = bp.pack_frame(INVALID_ID, CMD_TEST, b'BabyOS\x00', encrypt=True)
        self.assertNotEqual(enc, plain)
        # raw parse of ciphertext fails (HEAD byte is encrypted)
        self.assertIsNone(bp.parse_frame(enc))
        # decrypt then parse succeeds
        dec = tea_decrypt(enc)
        parsed = bp.parse_frame(dec)
        self.assertIsNotNone(parsed)
        dev_id, cmd, param = parsed
        self.assertEqual(dev_id, INVALID_ID)
        self.assertEqual(cmd, CMD_TEST)
        self.assertEqual(param, b'BabyOS\x00')

    def test_protocol_client_encrypt_sends_ciphertext(self):
        """ProtocolClient(encrypt=True).send_cmd 发出 TEA 密文帧"""
        uart = FakeUart()
        pc = ProtocolClient(uart, encrypt=True, log_fn=lambda m: None)
        self.assertTrue(pc.encrypt)
        frame = pc.send_cmd(CMD_TEST, bp.build_test_param())
        # written bytes are ciphertext — HEAD is not 0xFE
        self.assertNotEqual(frame[0], bp.PROTOCOL_HEAD)
        # decrypt recovers a valid frame
        dec = tea_decrypt(frame)
        parsed = bp.parse_frame(dec)
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed[1], CMD_TEST)
        self.assertEqual(parsed[2], b'BabyOS\x00')

    def test_protocol_client_encrypt_ota_roundtrip(self):
        """加密 OTA：主机 encrypt=True 发出密文；设备端解密后走完整 0x3→0x4→0x5 流程"""
        fd, path = tempfile.mkstemp(suffix='.bin')
        os.write(fd, b'ENCRYPTED-OTA-PAYLOAD-0123456789ABCDEF' * 20)
        os.close(fd)
        self.addCleanup(lambda: os.path.exists(path) and os.remove(path))

        uart = FakeUart()
        pc = ProtocolClient(uart, encrypt=True, log_fn=lambda m: None)
        results = []
        pc.on_result = lambda ok, c: results.append((ok, c))

        # Device-side: decrypt every host frame before parsing (like firmware
        # with _PROTO_ENCRYPT_ENABLE). We wrap FakeDevice's extract step.
        dev = FakeDevice(uart, ota_result=OTA_RESULT_OK)
        # device replies stay plaintext — host with encrypt=True still parses
        # plaintext RX (encrypt flag only affects TX). Re-point on_write to
        # decrypt-then-handle.
        orig_extract = dev._extract

        def decrypting_extract():
            # decrypt any complete ciphertext frames sitting in rx_buf
            # simplest: decrypt whole buf if it looks encrypted, then parse
            if len(dev.rx_buf) >= bp.FRAME_MIN_SIZE:
                decrypted = tea_decrypt(bytes(dev.rx_buf))
                # keep only as much as we can confidently parse; FakeDevice
                # _extract resyncs on HEAD so feeding decrypted stream works
                dev.rx_buf = bytearray(decrypted)
            return orig_extract()

        uart.on_write = lambda data: (
            dev.rx_buf.extend(data),
            None,
        )
        # re-attach a proper on_write that decrypts then dispatches
        def on_write(data):
            dev.rx_buf.extend(data)
            while True:
                frame = decrypting_extract()
                if frame is None:
                    break
                _dev_id, cmd, param = frame
                dev.written_cmds.append(cmd)
                dev._handle(cmd, param)

        uart.on_write = on_write

        ok = pc.start_ota(path, name='enc_fw.bin', timeout=8.0, poll_interval=0.002)
        self.assertTrue(ok)
        self.assertEqual(results, [(True, OTA_RESULT_OK)])
        self.assertIsNotNone(dev.fw_info)
        self.assertEqual(dev.fw_info[2], 'enc_fw.bin')
        # device received at least one FDATA chunk
        self.assertGreaterEqual(len(dev.received_chunks), 1)
        # host TX frames were ciphertext (HEAD byte != 0xFE)
        self.assertTrue(any(f[0] != bp.PROTOCOL_HEAD for f in pc.tx_log))

    def test_tea_decrypt_null_and_empty(self):
        self.assertEqual(tea_decrypt(None), b'')
        self.assertEqual(tea_decrypt(b''), b'')
        self.assertEqual(tea_encrypt(None), b'')


if __name__ == '__main__':
    unittest.main(verbosity=2)
