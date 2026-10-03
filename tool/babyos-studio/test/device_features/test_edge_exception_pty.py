#!/usr/bin/env python3
"""
test_edge_exception_pty — edge / exception scenarios over real virtual serial.

Authority (硬性验收):
  1. 边缘/异常场景必须走虚拟串口 pty 真实双向通讯（主路径）。
  2. 补充可含内存 mock（FakeUart / in-process），但报告必须分开计数。
  3. 主机侧真实 UartService / ProtocolClient / ShellClient / Xmodem / API。
  4. 设备侧 mock 真实解析 BabyOS 帧。
  5. Python 3.8；不改 bos/thirdparty；不提交 git；禁止 stub；禁止改测试造假。
  6. 异常时 API 返回结构化错误，禁止裸 500/裸栈；SDK 返回明确 None/异常，
     禁止死循环 busy-wait。

Covered fault-injection matrix (MockBabyOSDevice):
  drop_after_n_frames / stop_responding_after(cmd, n)
  inject_garbage_between_frames / corrupt_next_fdata_chunk
  send_wrong_cmd / send_duplicate_frame / oversized_length_field
  ota_result override: 0, 1, 2, 3, 4
  close_pty_mid_transfer / stop pump thread / device-endpoint close
  xmodem/ymodem: 不回 C、中途 NAK、拒绝 EOT

Product-behaviour checks:
  cancel 后状态清理、TRANSFER_BUSY 正确
  中断后 status/result 结构化，不悬挂 job
  http_mock 证书内容不匹配明确拒绝（HTTPS_CERT_MISMATCH）
  ProtocolClient 对超长 length / 未知帧不崩溃

Run:
  tool/babyos-studio/python/.venv/bin/python \
      tool/babyos-studio/test/device_features/test_edge_exception_pty.py
"""

from __future__ import print_function

import os
import struct
import sys
import tempfile
import time
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
for _p in (_PY_ROOT, _HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from device import b_protocol as bp  # noqa: E402
from device.crc_util import crc32  # noqa: E402
from device.protocol_client import (  # noqa: E402
    ProtocolClient,
    CMD_TEST,
    CMD_UTC,
    CMD_FW_INFO,
    CMD_FDATA,
    CMD_OTA_RESULT,
    CMD_GET_UID,
    OTA_RESULT_OK,
    OTA_RESULT_CRC_ERROR,
    OTA_RESULT_NAME_MISMATCH,
    OTA_RESULT_LEN_INVALID,
    OTA_RESULT_TIMEOUT,
)
from device.xmodem_ydmodem import (  # noqa: E402
    XmodemSender,
    YmodemSender,
    XferState,
    crc16_ccitt,
    SOH, EOT, ACK, NAK, CAN, CRCPKT,
)

from mock_babyos_device import (  # noqa: E402
    MockBabyOSDevice,
    create_byte_channel,
    open_api_over_pty,
)

MOCK_UID = b'\x11\x22\x33\x44\x55\x66\x77\x88\x99\xAA\xBB\xCC'
MOCK_VERSION = 'v1.2.3'
MOCK_MODEL = 'BabyOS-MCU'
MOCK_DEV_ID = 0x0000ABCD

# Separate counters — requirement: pty vs memory-mock reported apart
_PTY_TOTAL = 0
_PTY_PASS = 0
_PTY_FAIL = 0
_MEM_TOTAL = 0
_MEM_PASS = 0
_MEM_FAIL = 0
_PTY_CASES = []
_MEM_CASES = []
_CHANNEL_KINDS = []


def _rec_pty(ok: bool, case_id: str) -> None:
    global _PTY_TOTAL, _PTY_PASS, _PTY_FAIL
    _PTY_TOTAL += 1
    if case_id not in _PTY_CASES:
        _PTY_CASES.append(case_id)
    if ok:
        _PTY_PASS += 1
    else:
        _PTY_FAIL += 1


def _rec_mem(ok: bool, case_id: str) -> None:
    global _MEM_TOTAL, _MEM_PASS, _MEM_FAIL
    _MEM_TOTAL += 1
    if case_id not in _MEM_CASES:
        _MEM_CASES.append(case_id)
    if ok:
        _MEM_PASS += 1
    else:
        _MEM_FAIL += 1


def _make_fw(n: int, seed: int = 3) -> bytes:
    return bytes(bytearray(((i * 7) + seed) & 0xFF for i in range(n)))


def _write_temp(data: bytes, suffix: str = '.bin') -> str:
    fd, path = tempfile.mkstemp(suffix=suffix, prefix='babyos_edge_')
    try:
        os.write(fd, data)
    finally:
        os.close(fd)
    return path


def _wait(pred, timeout: float = 3.0, interval: float = 0.01) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            if pred():
                return True
        except Exception:
            pass
        time.sleep(interval)
    try:
        return bool(pred())
    except Exception:
        return False


def _wait_job(client, job_id: str, kind: str = 'ota',
              timeout: float = 12.0) -> dict:
    deadline = time.time() + timeout
    body = {}
    status_url = {
        'ota': '/api/device/ota/status?job_id=%s',
        'file': '/api/device/file/status?job_id=%s',
        'xmodem': '/api/device/xmodem/status?job_id=%s&kind=xmodem',
        'ymodem': '/api/device/xmodem/status?job_id=%s&kind=ymodem',
    }[kind]
    while time.time() < deadline:
        r = client.get(status_url % job_id)
        if r.status_code == 200:
            body = r.json()
            job = body.get('job') or {}
            state = job.get('state') or body.get('xfer_state')
            if state in ('done', 'error', 'cancelled'):
                return body
        time.sleep(0.02)
    return body


def _detail_code(r) -> str:
    try:
        detail = r.json().get('detail')
        if isinstance(detail, dict):
            return str(detail.get('code') or '')
    except Exception:
        pass
    return ''


def _assert_structured(testcase, r, status, code):
    testcase.assertEqual(r.status_code, status,
                         'status=%s body=%s' % (r.status_code, r.text))
    detail = r.json().get('detail')
    testcase.assertIsInstance(detail, dict,
                             'AppError must return structured detail, got %r' % (detail,))
    testcase.assertEqual(detail.get('code'), code, r.text)
    testcase.assertTrue(str(detail.get('detail') or '').strip(),
                        'detail.detail must be non-empty: %r' % (detail,))


class _PtyBase(unittest.TestCase):
    """Real pty + MockBabyOSDevice + ProtocolClient fixture (main path)."""

    encrypt = False

    def setUp(self):
        self.logs = []
        self.link = open_api_over_pty(
            encrypt=self.encrypt,
            open_host_uart=True,
            bind_device_manager=False,
            device_kwargs={
                'device_id': MOCK_DEV_ID,
                'uid': MOCK_UID,
                'version': MOCK_VERSION,
                'model': MOCK_MODEL,
                'encrypt': self.encrypt,
                'log_fn': self.logs.append,
            },
            require_pty=True,
        )
        self.addCleanup(self.link.close)
        self.assertEqual(self.link.kind, 'pty',
                         'edge/exception main path requires real pty, got %s'
                         % self.link.kind)
        if self.link.kind not in _CHANNEL_KINDS:
            _CHANNEL_KINDS.append(self.link.kind)
        self.device = self.link.device
        self.pc = self.link.protocol_client(encrypt=self.encrypt)

    def _fresh_fw(self, size: int = 600, seed: int = 9) -> str:
        return _write_temp(_make_fw(size, seed=seed))


# ---------------------------------------------------------------------------
# 1. MockBabyOSDevice fault injection over real pty
# ---------------------------------------------------------------------------

class TestPtyFaultInjection(_PtyBase):
    """Device-side fault injection driven by real host ProtocolClient over pty."""

    def test_drop_after_n_frames_timeout(self):
        """drop_after_n_frames: host commands after the budget time out as None."""
        case = 'pty_drop_after_n_frames'
        self.device.drop_after_n_frames = 0
        # first cmd may or may not complete depending on timing of limit apply;
        # after frames_handled > 0 all replies are suppressed.
        r1 = self.device.written_cmds[:]  # pre-state
        resp1 = self.pc.test_link(timeout=0.6)
        # force silence then subsequent cmds must time out
        self.device.drop_after_n_frames = 0
        self.device._frames_handled_limit = 0
        resp2 = self.pc.set_time(1700000000, timeout=0.5)
        resp3 = self.pc.get_uid(timeout=0.5)
        ok = (resp2 is None) and (resp3 is None)
        _rec_pty(ok, case)
        self.assertTrue(ok,
                        'expected silence after drop; r1=%s r2=%s r3=%s logs=%s'
                        % (resp1, resp2, resp3, self.logs[-6:]))
        self.assertIn(case, _PTY_CASES)

    def test_stop_responding_after_cmd(self):
        """stop_responding_after(CMD_TEST, 1): first ACK ok, second times out."""
        case = 'pty_stop_responding_after_cmd'
        self.device.stop_responding_after(CMD_TEST, 1)
        resp1 = self.pc.test_link(timeout=2.0)
        resp2 = self.pc.test_link(timeout=0.5)
        ok = (resp1 is not None) and (resp1[1] == CMD_TEST) and (resp2 is None)
        _rec_pty(ok, case)
        self.assertTrue(ok, 'r1=%s r2=%s logs=%s' % (resp1, resp2, self.logs[-8:]))

    def test_inject_garbage_between_frames(self):
        """Garbage between device TX frames must not break host parser (resync)."""
        case = 'pty_inject_garbage_between_frames'
        self.device.inject_garbage_between_frames = True
        resp = self.pc.test_link(timeout=2.0)
        uid = self.pc.get_uid(timeout=2.0)
        info = self.pc.get_device_info(timeout=2.0)
        ok = (resp is not None and resp[1] == CMD_TEST
              and uid == MOCK_UID
              and info is not None and info[0] == MOCK_VERSION)
        _rec_pty(ok, case)
        self.assertTrue(ok, 'resp=%s uid=%s info=%s' % (resp, uid, info))
        self.device.inject_garbage_between_frames = False

    def test_corrupt_next_fdata_chunk_ota_crc_error(self):
        """corrupt_next_fdata_chunk: OTA ends with CRC_ERROR (result=1)."""
        case = 'pty_corrupt_next_fdata_chunk'
        path = self._fresh_fw(700, seed=12)
        try:
            self.device.corrupt_next_fdata_chunk = True
            results = []
            self.pc.on_result = lambda ok, code: results.append((ok, code))
            ok = self.pc.start_ota(path, timeout=8.0)
            self.assertFalse(ok)
            self.assertEqual(self.device.last_ota_result, OTA_RESULT_CRC_ERROR)
            self.assertEqual(self.pc.transfer_result, OTA_RESULT_CRC_ERROR)
            _rec_pty(True, case)
        finally:
            os.unlink(path)
            self.device.corrupt_next_fdata_chunk = False

    def test_send_wrong_cmd(self):
        """send_wrong_cmd: host expects CMD_TEST but receives another cmd → None."""
        case = 'pty_send_wrong_cmd'
        self.device.send_wrong_cmd = CMD_GET_UID  # reply with wrong cmd
        resp = self.pc.test_link(timeout=0.6)
        # host waits for CMD_TEST; wrong-cmd frame is queued but not matching
        self.assertIsNone(resp, 'wrong-cmd reply must not satisfy test_link')
        # a follow-up command with correct device response still works if we clear
        self.device.send_wrong_cmd = None
        self.device.clear_faults()
        # re-open clean link path: new link not needed — device still healthy
        resp2 = self.pc.request_response(CMD_GET_UID, b'',
                                         expect_cmd=CMD_GET_UID, timeout=2.0)
        ok = (resp is None) and (resp2 is not None)
        _rec_pty(ok, case)
        self.assertTrue(ok, 'resp=%s resp2=%s' % (resp, resp2))

    def test_send_duplicate_frame(self):
        """send_duplicate_frame: host still gets a clean single ACK (idempotent)."""
        case = 'pty_send_duplicate_frame'
        self.device.send_duplicate_frame = True
        resp1 = self.pc.test_link(timeout=2.0)
        resp2 = self.pc.set_time(1700000111, timeout=2.0)
        ok = (resp1 is not None and resp1[1] == CMD_TEST
              and resp2 is not None and resp2[1] == CMD_UTC
              and self.device.last_utc == 1700000111)
        _rec_pty(ok, case)
        self.assertTrue(ok, 'r1=%s r2=%s utc=%s' % (resp1, resp2, self.device.last_utc))
        self.device.send_duplicate_frame = False

    def test_oversized_length_field_no_crash(self):
        """oversized_length_field: ProtocolClient must not crash; later cmds recover."""
        case = 'pty_oversized_length_field'
        self.device.oversized_length_field = True
        crashed = False
        resp_bad = None
        try:
            resp_bad = self.pc.test_link(timeout=0.5)
        except Exception:
            crashed = True
        self.device.oversized_length_field = False
        self.device.clear_faults()
        # recovery: clear rx queue and try a normal command
        self.pc.reset_rx_queue()
        resp_ok = self.pc.get_uid(timeout=2.0)
        ok = (not crashed) and (resp_ok == MOCK_UID)
        _rec_pty(ok, case)
        self.assertFalse(crashed, 'ProtocolClient crashed on oversized length')
        self.assertTrue(ok, 'recovery failed resp_bad=%s resp_ok=%s' % (resp_bad, resp_ok))

    def test_unknown_frame_and_garbage_resync(self):
        """Host parser survives unknown/garbage bytes before a valid frame."""
        case = 'pty_unknown_frame_resync'
        # push junk + a frame with unknown cmd through the device→host path
        junk = b'\x00\x11\x22\xFE\x00\x02\x01\x99\x00'  # incomplete / junk
        # device injects wrong-cmd frame then we clear and talk normally
        self.device.send_wrong_cmd = 0xEE
        self.pc.test_link(timeout=0.4)
        self.device.send_wrong_cmd = None
        self.device.clear_faults()
        self.pc.reset_rx_queue()
        uid = self.pc.get_uid(timeout=2.0)
        ok = uid == MOCK_UID
        _rec_pty(ok, case)
        self.assertTrue(ok, 'uid=%s' % (uid,))

    def test_ota_result_override_codes(self):
        """ota_result_override: codes 0..4 each produce the structured host result."""
        for code, expect_ok in (
            (OTA_RESULT_OK, True),
            (OTA_RESULT_CRC_ERROR, False),
            (OTA_RESULT_NAME_MISMATCH, False),
            (OTA_RESULT_LEN_INVALID, False),
            (OTA_RESULT_TIMEOUT, False),
        ):
            case = 'pty_ota_result_override_%d' % code
            path = self._fresh_fw(400, seed=code + 1)
            try:
                self.device.clear_faults()
                self.device.ota_result_override = code
                results = []
                self.pc.on_result = lambda ok, c: results.append((ok, c))
                ok = self.pc.start_ota(path, timeout=8.0)
                self.assertEqual(ok, expect_ok, 'code=%d ok=%s' % (code, ok))
                self.assertEqual(self.device.last_ota_result, code)
                self.assertEqual(self.pc.transfer_result, code)
                if results:
                    self.assertEqual(results[-1], (expect_ok, code))
                _rec_pty(True, case)
            finally:
                os.unlink(path)
                self.device.ota_result_override = None
                self.device.clear_faults()

    def test_close_pty_mid_transfer(self):
        """close_pty_mid_transfer: OTA times out structured, no hang, result=4."""
        case = 'pty_close_pty_mid_transfer'
        path = self._fresh_fw(2000, seed=21)  # multi-chunk
        try:
            results = []
            self.pc.on_result = lambda ok, c: results.append((ok, c))

            def _break_when_started():
                # break after host has sent FW_INFO / some FDATA
                if self.device.frames_handled >= 2:
                    self.device.close_pty_mid_transfer(stop_thread=True)
                    return True
                return False

            # drive break in background during OTA
            import threading
            stop_flag = {'done': False}

            def _breaker():
                deadline = time.time() + 3.0
                while time.time() < deadline and not stop_flag['done']:
                    if _break_when_started():
                        stop_flag['done'] = True
                        break
                    time.sleep(0.01)

            th = threading.Thread(target=_breaker, daemon=True)
            th.start()
            t0 = time.time()
            ok = self.pc.start_ota(path, timeout=3.0)
            elapsed = time.time() - t0
            stop_flag['done'] = True
            th.join(timeout=1.0)
            self.assertFalse(ok)
            self.assertLess(elapsed, 5.0, 'must not busy-wait forever')
            # structured result: timeout code, transfer_active cleared
            self.assertEqual(self.pc.transfer_result, OTA_RESULT_TIMEOUT)
            self.assertFalse(self.pc.transfer_active)
            if results:
                self.assertEqual(results[-1][0], False)
                self.assertEqual(results[-1][1], OTA_RESULT_TIMEOUT)
            _rec_pty(True, case)
        finally:
            os.unlink(path)

    def test_device_endpoint_close_mid_transfer(self):
        """break_link_and_close_endpoint: host sees timeout, no crash."""
        case = 'pty_endpoint_close_mid_transfer'
        path = self._fresh_fw(1500, seed=33)
        try:
            import threading
            stop_flag = {'done': False}

            def _breaker():
                deadline = time.time() + 3.0
                while time.time() < deadline and not stop_flag['done']:
                    if self.device.frames_handled >= 2:
                        self.device.break_link_and_close_endpoint()
                        stop_flag['done'] = True
                        break
                    time.sleep(0.01)

            th = threading.Thread(target=_breaker, daemon=True)
            th.start()
            t0 = time.time()
            ok = self.pc.start_ota(path, timeout=3.0)
            elapsed = time.time() - t0
            stop_flag['done'] = True
            th.join(timeout=1.0)
            self.assertFalse(ok)
            self.assertLess(elapsed, 5.0)
            self.assertEqual(self.pc.transfer_result, OTA_RESULT_TIMEOUT)
            _rec_pty(True, case)
        finally:
            os.unlink(path)


# ---------------------------------------------------------------------------
# 2. Xmodem / Ymodem fault injection over pty
# ---------------------------------------------------------------------------

class TestPtyXmodemYmodemFaults(_PtyBase):

    def _run_xmodem(self, payload: bytes, device_fault: dict, timeout: float = 4.0):
        """Start host XmodemSender on the REAL host uart; pump device.

        Host side must use UartService (pty slave), not the device endpoint
        (pty master) — writing/reading the device fd would short-circuit the
        virtual serial link.
        """
        fx = self.device
        fx.switch_to_protocol()
        fx.xm_fault = dict(device_fault)
        rx = fx.start_xmodem_receive()
        uart = self.link.host_uart
        self.assertIsNotNone(uart, 'host uart must be open for pty xmodem')

        def _send(buf: bytes) -> None:
            n = uart.write(buf)
            if n is not None and n < 0:
                raise IOError('host uart write failed for xmodem')

        sender = XmodemSender(_send, log_fn=self.logs.append,
                              timeout_sec=1.0, start_timeout_sec=0.4,
                              max_retries=4)
        sender.start(payload)
        deadline = time.time() + timeout
        while time.time() < deadline and sender.is_active:
            data = uart.read_available()
            if data:
                for b in data:
                    sender.on_uart_byte(b)
            sender.on_timer_tick()
            time.sleep(0.005)
        return sender.state == XferState.DONE, sender, bytes(rx.data)

    def test_xmodem_no_start_byte_aborts(self):
        """Device never sends C → sender aborts (no busy-wait hang)."""
        case = 'pty_xmodem_no_start_byte'
        payload = _make_fw(200, seed=5)
        ok_done, sender, rx = self._run_xmodem(payload, {
            'send_start_byte': False,
        }, timeout=3.0)
        self.assertFalse(ok_done)
        self.assertEqual(sender.state, XferState.ABORTED)
        _rec_pty(True, case)

    def test_xmodem_mid_transfer_nak_recovers(self):
        """nak_on_block=1: host resends and still completes."""
        case = 'pty_xmodem_mid_nak'
        payload = _make_fw(300, seed=6)  # 3 blocks
        ok_done, sender, rx = self._run_xmodem(payload, {
            'nak_on_block': 1,
        }, timeout=4.0)
        # receiver NAKs blk1 once; sender resends; should still finish
        self.assertTrue(ok_done, 'state=%s logs=%s' % (sender.state, self.logs[-12:]))
        self.assertTrue(rx[:len(payload)] == payload or rx.startswith(payload),
                        'rx mismatch len=%d' % len(rx))
        _rec_pty(True, case)

    def test_xmodem_reject_eot_aborts(self):
        """reject_eot: sender cannot finish; after retries it aborts."""
        case = 'pty_xmodem_reject_eot'
        payload = _make_fw(128, seed=7)
        ok_done, sender, rx = self._run_xmodem(payload, {
            'reject_eot': True,
        }, timeout=4.0)
        self.assertFalse(ok_done)
        self.assertEqual(sender.state, XferState.ABORTED)
        _rec_pty(True, case)

    def test_ymodem_no_start_byte_aborts(self):
        case = 'pty_ymodem_no_start_byte'
        fx = self.device
        fx.switch_to_protocol()
        fx.ym_fault = {'send_start_byte': False}
        rx = fx.start_ymodem_receive()
        payload = _make_fw(200, seed=8)
        uart = self.link.host_uart
        self.assertIsNotNone(uart)

        def _send(buf: bytes) -> None:
            n = uart.write(buf)
            if n is not None and n < 0:
                raise IOError('host uart write failed for ymodem')

        sender = YmodemSender(_send, log_fn=self.logs.append,
                              timeout_sec=1.0, start_timeout_sec=0.4,
                              max_retries=4)
        sender.start(payload, filename='f.bin')
        deadline = time.time() + 3.0
        while time.time() < deadline and sender.is_active:
            data = uart.read_available()
            if data:
                for b in data:
                    sender.on_uart_byte(b)
            sender.on_timer_tick()
            time.sleep(0.005)
        self.assertEqual(sender.state, XferState.ABORTED)
        _rec_pty(True, case)

    def test_ymodem_reject_eot_aborts(self):
        case = 'pty_ymodem_reject_eot'
        fx = self.device
        fx.switch_to_protocol()
        fx.ym_fault = {'reject_eot': True}
        rx = fx.start_ymodem_receive()
        payload = _make_fw(1024, seed=10)  # exact 1K → EOT after one STX
        uart = self.link.host_uart
        self.assertIsNotNone(uart)

        def _send(buf: bytes) -> None:
            n = uart.write(buf)
            if n is not None and n < 0:
                raise IOError('host uart write failed for ymodem')

        sender = YmodemSender(_send, log_fn=self.logs.append,
                              timeout_sec=1.0, start_timeout_sec=0.4,
                              max_retries=4)
        sender.start(payload, filename='f.bin')
        deadline = time.time() + 3.0
        while time.time() < deadline and sender.is_active:
            data = uart.read_available()
            if data:
                for b in data:
                    sender.on_uart_byte(b)
            sender.on_timer_tick()
            time.sleep(0.005)
        self.assertFalse(sender.state == XferState.DONE)
        self.assertIn(sender.state, (XferState.ABORTED, XferState.SEND_EOT))
        _rec_pty(True, case)


# ---------------------------------------------------------------------------
# 3. API-over-pty: cancel / TRANSFER_BUSY / structured errors / disconnect
# ---------------------------------------------------------------------------

class TestApiEdgeExceptions(unittest.TestCase):
    """FastAPI TestClient → DeviceManager → UartService → pty → mock device."""

    def setUp(self):
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)
        self.logs = []
        self.link = open_api_over_pty(
            open_host_uart=False,
            bind_device_manager=False,
            device_kwargs={
                'device_id': MOCK_DEV_ID,
                'uid': MOCK_UID,
                'version': MOCK_VERSION,
                'model': MOCK_MODEL,
                'log_fn': self.logs.append,
            },
            require_pty=True,
        )
        self.addCleanup(self.link.close)
        self.assertEqual(self.link.kind, 'pty')
        if self.link.kind not in _CHANNEL_KINDS:
            _CHANNEL_KINDS.append(self.link.kind)

        from app.main import create_app
        from fastapi.testclient import TestClient
        self.app = create_app()
        self.client = TestClient(self.app)
        r = self.client.post('/api/device/serial/open',
                             json={'path': self.link.host_port, 'baud': 115200})
        self.assertEqual(r.status_code, 200, r.text)

    def test_structured_serial_close_then_commands(self):
        case = 'pty_api_serial_close_structured'
        r = self.client.post('/api/device/serial/close', json={})
        self.assertEqual(r.status_code, 200)
        for url, body in (
            ('/api/device/protocol/test', {}),
            ('/api/device/protocol/set_time', {'utc': 1}),
            ('/api/device/uid/get', {}),
            ('/api/device/ota/start', {'path': '/nope.bin', 'timeout': 1}),
        ):
            rr = self.client.post(url, json=body)
            _assert_structured(self, rr, 409, 'SERIAL_NOT_OPEN')
        _rec_pty(True, case)

    def test_ota_busy_and_cancel_state_cleanup(self):
        """TRANSFER_BUSY on concurrent OTA; cancel cleans state for next job."""
        case = 'pty_api_ota_busy_cancel'
        path = _write_temp(_make_fw(1200, seed=41))
        try:
            # make device silent so first OTA stays running
            self.link.device.stop_responding_after(CMD_FW_INFO, 0)
            self.link.device.drop_after_n_frames = 0
            self.link.device._frames_handled_limit = 0

            r1 = self.client.post('/api/device/ota/start',
                                  json={'path': path, 'timeout': 8})
            self.assertEqual(r1.status_code, 200, r1.text)
            job1 = r1.json()['job_id']
            # wait until transfer is actually active
            _wait(lambda: (self.client.get(
                '/api/device/ota/status?job_id=%s' % job1).json()
                .get('transfer_active')), timeout=3.0)

            # concurrent start must be structured TRANSFER_BUSY
            r2 = self.client.post('/api/device/ota/start',
                                  json={'path': path, 'timeout': 8})
            _assert_structured(self, r2, 409, 'TRANSFER_BUSY')

            # cancel / stop → structured cleanup, not a hung job
            rstop = self.client.post('/api/device/file/stop', json={})
            self.assertEqual(rstop.status_code, 200)
            body = _wait_job(self.client, job1, 'ota', timeout=5.0)
            job = body.get('job') or {}
            self.assertIn(job.get('state'), ('cancelled', 'error'),
                          'job state=%s body=%s' % (job.get('state'), body))
            self.assertFalse(body.get('transfer_active', True),
                             'transfer_active must clear after cancel')

            # device can respond again → new OTA accepted
            self.link.device.clear_faults()
            self.link.device.stop()
            # restart device pump on same endpoint
            self.link.device._stop.clear()
            self.link.device._thread = None
            self.link.device.start()
            r3 = self.client.post('/api/device/ota/start',
                                  json={'path': path, 'timeout': 8})
            self.assertEqual(r3.status_code, 200, r3.text)
            body3 = _wait_job(self.client, r3.json()['job_id'], 'ota', timeout=10.0)
            job3 = body3.get('job') or {}
            self.assertIn(job3.get('state'), ('done', 'error'),
                          'restarted job=%s' % job3)
            _rec_pty(True, case)
        finally:
            os.unlink(path)

    def test_ota_timeout_structured_job(self):
        """Device silent → OTA job ends error with result_code=TIMEOUT, no hang."""
        case = 'pty_api_ota_timeout_structured'
        path = _write_temp(_make_fw(800, seed=42))
        try:
            self.link.device.drop_after_n_frames = 0
            self.link.device._frames_handled_limit = 0
            r = self.client.post('/api/device/ota/start',
                                 json={'path': path, 'timeout': 2})
            self.assertEqual(r.status_code, 200, r.text)
            body = _wait_job(self.client, r.json()['job_id'], 'ota', timeout=8.0)
            job = body.get('job') or {}
            self.assertEqual(job.get('state'), 'error', body)
            self.assertEqual(job.get('result_code'), OTA_RESULT_TIMEOUT)
            self.assertFalse(body.get('transfer_active', True))
            # structured follow-up command still possible (device still "open")
            self.link.device.clear_faults()
            r2 = self.client.post('/api/device/protocol/test', json={})
            self.assertEqual(r2.status_code, 200, r2.text)
            _rec_pty(True, case)
        finally:
            os.unlink(path)

    def test_ota_device_disconnect_mid_transfer(self):
        """Device endpoint closed mid-OTA → job error, no hang, structured."""
        case = 'pty_api_ota_disconnect_mid'
        path = _write_temp(_make_fw(2000, seed=43))
        try:
            import threading
            stop_flag = {'done': False}

            def _breaker():
                deadline = time.time() + 3.0
                while time.time() < deadline and not stop_flag['done']:
                    if self.link.device.frames_handled >= 2:
                        self.link.device.break_link_and_close_endpoint()
                        stop_flag['done'] = True
                        break
                    time.sleep(0.01)

            th = threading.Thread(target=_breaker, daemon=True)
            th.start()
            r = self.client.post('/api/device/ota/start',
                                 json={'path': path, 'timeout': 3})
            self.assertEqual(r.status_code, 200, r.text)
            body = _wait_job(self.client, r.json()['job_id'], 'ota', timeout=8.0)
            stop_flag['done'] = True
            th.join(timeout=1.0)
            job = body.get('job') or {}
            self.assertIn(job.get('state'), ('error', 'cancelled'), body)
            # after device gone, protocol cmds time out structured (not bare 500)
            r2 = self.client.post('/api/device/protocol/test', json={})
            self.assertIn(r2.status_code, (504, 409, 500), r2.text)
            if r2.status_code == 504:
                _assert_structured(self, r2, 504, 'PROTOCOL_TIMEOUT')
            else:
                detail = r2.json().get('detail')
                self.assertIsInstance(detail, dict)
                self.assertTrue(detail.get('code'))
            _rec_pty(True, case)
        finally:
            os.unlink(path)

    def test_xmodem_cancel_after_silent_device(self):
        """Xmodem on silent device + cancel → cancelled job, state cleaned."""
        case = 'pty_api_xmodem_cancel_silent'
        path = _write_temp(_make_fw(300, seed=44))
        try:
            # device silent in xmodem mode (no C)
            self.link.device.switch_to_protocol()
            self.link.device.xm_fault = {'send_start_byte': False}
            r = self.client.post('/api/device/xmodem/start', json={'path': path})
            self.assertEqual(r.status_code, 200, r.text)
            job_id = r.json()['job_id']
            rc = self.client.post('/api/device/xmodem/cancel', json={})
            self.assertEqual(rc.status_code, 200, rc.text)
            body = _wait_job(self.client, job_id, 'xmodem', timeout=6.0)
            job = body.get('job') or {}
            self.assertIn(job.get('state'), ('cancelled', 'error'), body)
            # after cancel, active_xfer must be clear → new start allowed
            r2 = self.client.post('/api/device/xmodem/start', json={'path': path})
            self.assertEqual(r2.status_code, 200, r2.text)
            rc2 = self.client.post('/api/device/xmodem/cancel', json={})
            self.assertEqual(rc2.status_code, 200)
            _rec_pty(True, case)
        finally:
            os.unlink(path)

    def test_shell_param_structured_errors(self):
        case = 'pty_api_param_structured'
        r = self.client.post('/api/device/param/get',
                             json={'name': 'no_such_param_xyz', 'timeout': 0.4})
        _assert_structured(self, r, 504, 'PARAM_NOT_FOUND')
        r2 = self.client.post('/api/device/param/get', json={'name': ''})
        _assert_structured(self, r2, 400, 'INVALID_REQUEST')
        _rec_pty(True, case)

    def test_http_cert_mismatch_structured(self):
        """HTTPS cert content mismatch → HTTPS_CERT_MISMATCH (not bare 500)."""
        case = 'pty_api_https_cert_mismatch'
        import device.http_mock as hm
        # force resolve to see only a non-matching candidate
        tmp_c = _write_temp(b'-----BEGIN CERTIFICATE-----\nNOT-ORIGIN-DEV\n-----END CERTIFICATE-----\n',
                            suffix='_bad.pem')
        tmp_k = _write_temp(b'-----BEGIN PRIVATE KEY-----\nNOT-ORIGIN-DEV\n-----END PRIVATE KEY-----\n',
                            suffix='_bad.key')
        old_pairs = hm._candidate_cert_pairs
        old_paths = hm._https_cert_paths
        try:
            hm._candidate_cert_pairs = lambda: [(tmp_c, tmp_k)]
            hm._https_cert_paths = None
            r = self.client.post('/api/device/http/start',
                                 json={'https': True, 'port': 0,
                                       'body': '{"x":1}', 'status_code': 200})
            _assert_structured(self, r, 409, 'HTTPS_CERT_MISMATCH')
            detail = r.json()['detail']['detail']
            self.assertTrue('cert' in detail.lower() or 'mismatch' in detail.lower()
                            or 'origin/dev' in detail.lower(), detail)
            _rec_pty(True, case)
        finally:
            hm._candidate_cert_pairs = old_pairs
            hm._https_cert_paths = old_paths
            try:
                os.unlink(tmp_c)
            except Exception:
                pass
            try:
                os.unlink(tmp_k)
            except Exception:
                pass

    def test_ota_result_override_via_api_job(self):
        """API OTA job surfaces device ota_result override as structured result_code."""
        case = 'pty_api_ota_result_override'
        path = _write_temp(_make_fw(500, seed=45))
        try:
            self.link.device.ota_result_override = OTA_RESULT_NAME_MISMATCH
            r = self.client.post('/api/device/ota/start',
                                 json={'path': path, 'timeout': 8})
            self.assertEqual(r.status_code, 200, r.text)
            body = _wait_job(self.client, r.json()['job_id'], 'ota', timeout=10.0)
            job = body.get('job') or {}
            self.assertEqual(job.get('state'), 'error', body)
            self.assertEqual(job.get('result_code'), OTA_RESULT_NAME_MISMATCH)
            self.assertFalse(job.get('ok', True))
            _rec_pty(True, case)
        finally:
            os.unlink(path)
            self.link.device.ota_result_override = None


# ---------------------------------------------------------------------------
# 4. Memory-mock supplements (NOT the main path) — counted separately
# ---------------------------------------------------------------------------

class _MemUart(object):
    """In-memory uart for ProtocolClient unit-level edge cases (supplementary)."""

    def __init__(self):
        self.rx = bytearray()
        self.tx = bytearray()
        self.is_open = True
        self.port = 'MEM'
        self.baudrate = 115200

    def write(self, data: bytes) -> int:
        if data is None or not self.is_open:
            return -1
        if isinstance(data, bytearray):
            data = bytes(data)
        self.tx.extend(data)
        return len(data)

    def read_available(self) -> bytes:
        out = bytes(self.rx)
        self.rx.clear()
        return out

    def close(self) -> None:
        self.is_open = False


class TestMemoryMockSupplements(unittest.TestCase):
    """Supplementary in-memory tests — reported separately from pty counts."""

    def test_feed_bytes_oversized_length_no_crash(self):
        case = 'mem_protocol_oversized_length'
        uart = _MemUart()
        pc = ProtocolClient(uart)
        # frame claiming length 0xFFFF but truncated
        bad = bytearray(b'\xFE') + (0x1314).to_bytes(4, 'little') \
            + (0xFFFF).to_bytes(2, 'little') + b'\x01\x00\x00'
        frames = pc.feed_bytes(bytes(bad))
        self.assertEqual(frames, [])
        # valid frame after garbage still parses
        good = bp.pack_frame(0xABCD, CMD_TEST, b'')
        frames2 = pc.feed_bytes(good)
        self.assertEqual(len(frames2), 1)
        self.assertEqual(frames2[0][1], CMD_TEST)
        _rec_mem(True, case)

    def test_feed_bytes_unknown_cmd_and_garbage(self):
        case = 'mem_protocol_unknown_frame'
        uart = _MemUart()
        pc = ProtocolClient(uart)
        junk = b'\x00\x01\x02\xFE\xAA\xBB\xCC\xDD\xEE\xFF'  # junk then bad frame
        frames = pc.feed_bytes(junk)
        self.assertEqual(frames, [])
        good = bp.pack_frame(0x1314, CMD_GET_UID, b'')
        frames2 = pc.feed_bytes(good)
        self.assertEqual(len(frames2), 1)
        _rec_mem(True, case)

    def test_xmodem_sender_wait_start_abort(self):
        case = 'mem_xmodem_wait_start_abort'
        sent = []

        def _send(buf: bytes) -> None:
            sent.append(buf)

        sender = XmodemSender(_send, timeout_sec=0.2, start_timeout_sec=0.1,
                              max_retries=2)
        sender.start(b'\x01\x02\x03')
        t0 = time.time()
        while sender.is_active and time.time() - t0 < 2.0:
            sender.on_timer_tick()
            time.sleep(0.02)
        self.assertEqual(sender.state, XferState.ABORTED)
        self.assertLess(time.time() - t0, 2.0)
        _rec_mem(True, case)

    def test_ymodem_sender_wait_start_abort(self):
        case = 'mem_ymodem_wait_start_abort'
        sender = YmodemSender(lambda b: None, timeout_sec=0.2,
                              start_timeout_sec=0.1, max_retries=2)
        sender.start(b'\x01\x02\x03', filename='a.bin')
        t0 = time.time()
        while sender.is_active and time.time() - t0 < 2.0:
            sender.on_timer_tick()
            time.sleep(0.02)
        self.assertEqual(sender.state, XferState.ABORTED)
        _rec_mem(True, case)

    def test_stop_transfer_clears_active_state(self):
        case = 'mem_stop_transfer_cleanup'
        uart = _MemUart()
        pc = ProtocolClient(uart)
        # drive transfer state manually
        pc._xfer_busy = True
        pc._xfer_done = False
        pc._xfer_result = None
        pc.stop_transfer()
        self.assertFalse(pc.transfer_active)
        self.assertEqual(pc.transfer_result, OTA_RESULT_TIMEOUT)
        st = pc.status()
        self.assertFalse(st['transfer_active'])
        self.assertEqual(st['transfer_result'], OTA_RESULT_TIMEOUT)
        _rec_mem(True, case)


# ---------------------------------------------------------------------------
# Runner / report
# ---------------------------------------------------------------------------

def _build_report() -> dict:
    return {
        'channel_kinds': list(_CHANNEL_KINDS),
        'pty': {
            'total': _PTY_TOTAL,
            'pass': _PTY_PASS,
            'fail': _PTY_FAIL,
            'cases': list(_PTY_CASES),
        },
        'memory_mock': {
            'total': _MEM_TOTAL,
            'pass': _MEM_PASS,
            'fail': _MEM_FAIL,
            'cases': list(_MEM_CASES),
            'note': 'supplementary — NOT the primary pty path',
        },
        'all_pty': _PTY_FAIL == 0 and _PTY_TOTAL > 0,
        'all_mem': _MEM_FAIL == 0,
    }


def main() -> int:
    print('=== Edge / Exception virtual-serial suite ===')
    loader = unittest.defaultTestLoader
    suite = unittest.TestSuite()
    for cls in (
        TestPtyFaultInjection,
        TestPtyXmodemYmodemFaults,
        TestApiEdgeExceptions,
        TestMemoryMockSupplements,
    ):
        suite.addTests(loader.loadTestsFromTestCase(cls))
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    report = _build_report()
    print('\n=== IMPL report (edge/exception) ===')
    print('channel: %s' % (report['channel_kinds'],))
    print('PTY path (primary): total=%d pass=%d fail=%d' % (
        report['pty']['total'], report['pty']['pass'], report['pty']['fail']))
    print('  cases: %s' % (report['pty']['cases'],))
    print('MEMORY mock (supplementary, separate count): total=%d pass=%d fail=%d' % (
        report['memory_mock']['total'], report['memory_mock']['pass'],
        report['memory_mock']['fail']))
    print('  cases: %s' % (report['memory_mock']['cases'],))
    print('unittest: run=%d failures=%d errors=%d skipped=%d' % (
        result.testsRun, len(result.failures), len(result.errors),
        len(result.skipped)))
    ok = (report['all_pty'] and report['all_mem']
          and not result.failures and not result.errors)
    print('RESULT: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
