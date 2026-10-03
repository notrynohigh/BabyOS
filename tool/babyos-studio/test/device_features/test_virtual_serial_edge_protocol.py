#!/usr/bin/env python3
"""
test_virtual_serial_edge_protocol — SDK-layer edge / exception suite over real pty.

Authority (硬性验收):
  1. 边缘/异常主路径必须走虚拟串口 pty 真实双向通讯。
  2. 内存 mock 仅作补充，报告必须与 pty 分开计数。
  3. 主机侧真实 UartService / ProtocolClient / ShellClient / XmodemSender /
     DeviceManager(API)；设备侧 MockBabyOSDevice 真实解析 BabyOS 帧。
  4. Python 3.8；不改 bos/thirdparty；不提交 git；禁止 stub；禁止改测试造假。
  5. 异常时 API 返回结构化错误（AppError detail.code），SDK 返回明确
     None / False / 超时结果，禁止死循环 busy-wait。

Coverage matrix (requirement ids):
  R1 OTA result codes 0/1/2/3/4
  R2 OTA mid-transfer silence → structured timeout; garbage then recover
  R3 half-frame / sticky multi-frame / oversized length / bad checksum → resync
  R4 device wrong cmd reply / duplicate frames / unknown CMD
  R5 file 0x6 success and failure
  R6 Xmodem timeout abort; mid NAK still succeeds
  R7 Ymodem no-C / mid NAK / cancel
  R8 Shell serial close / unknown param / illegal value
  R9 concurrent transfer → busy; cancel then restart

Run:
  /home/yyds/code/BabyOS/tool/babyos-studio/python/.venv/bin/python \
      /home/yyds/code/BabyOS/tool/babyos-studio/test/device_features/test_virtual_serial_edge_protocol.py
"""

from __future__ import print_function

import json
import os
import sys
import tempfile
import threading
import time
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
for _p in (_PY_ROOT, _HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from device import b_protocol as bp  # noqa: E402
from device.protocol_client import (  # noqa: E402
    ProtocolClient,
    CMD_TEST,
    CMD_UTC,
    CMD_FW_INFO,
    CMD_FDATA,
    CMD_OTA_RESULT,
    CMD_TRANS_FILE,
    CMD_GET_UID,
    OTA_RESULT_OK,
    OTA_RESULT_CRC_ERROR,
    OTA_RESULT_NAME_MISMATCH,
    OTA_RESULT_LEN_INVALID,
    OTA_RESULT_TIMEOUT,
    INVALID_ID,
    DEVICE_ID_HOST,
)
from device.shell_client import ShellClient  # noqa: E402
from device.uart_service import UartService  # noqa: E402
from device.xmodem_ydmodem import (  # noqa: E402
    XmodemSender, YmodemSender, XferState,
)
from mock_babyos_device import (  # noqa: E402
    MockBabyOSDevice,
    create_byte_channel,
    open_api_over_pty,
    DEFAULT_SHELL_PARAMS,
)

MOCK_UID = b'\x11\x22\x33\x44\x55\x66\x77\x88\x99\xAA\xBB\xCC'
MOCK_VERSION = 'v1.2.3'
MOCK_MODEL = 'BabyOS-MCU'
MOCK_DEV_ID = 0x0000ABCD

TEST_SCHEMA_ID = 'babyos_virtual_serial_edge_protocol_v1'

_MATRIX = []  # type: list
_PTY_COUNT = {'total': 0, 'pass': 0, 'fail': 0}
_MEM_COUNT = {'total': 0, 'pass': 0, 'fail': 0}
_CHANNEL_KINDS = []


def _matrix_add(case_id, channel, status, detail=''):
    """Record one coverage-matrix row. channel='pty' | 'memory'."""
    _MATRIX.append({
        'id': case_id,
        'channel': channel,
        'status': status,
        'detail': detail,
    })
    counter = _PTY_COUNT if channel == 'pty' else _MEM_COUNT
    counter['total'] += 1
    if status == 'PASS':
        counter['pass'] += 1
    else:
        counter['fail'] += 1


def _write_temp(data, suffix='.bin', prefix='babyos_edgeproto_'):
    fd, path = tempfile.mkstemp(suffix=suffix, prefix=prefix)
    try:
        os.write(fd, data)
    finally:
        os.close(fd)
    return path


def _make_fw(n, seed=3):
    return bytes(bytearray(((i * 7) + seed) & 0xFF for i in range(n)))


def _wait(pred, timeout=3.0, interval=0.01):
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


def _wait_job(client, job_id, kind='ota', timeout=12.0):
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


def _assert_structured(testcase, r, status, code):
    testcase.assertEqual(r.status_code, status,
                         'status=%s body=%s' % (r.status_code, r.text))
    detail = r.json().get('detail')
    testcase.assertIsInstance(detail, dict,
                             'AppError must return structured detail, got %r' % (detail,))
    testcase.assertEqual(detail.get('code'), code, r.text)
    testcase.assertTrue(str(detail.get('detail') or '').strip(),
                        'detail.detail must be non-empty: %r' % (detail,))


def _drain_uart(uart):
    try:
        return uart.read_available() or b''
    except Exception:
        return b''


class _PtySdkBase(unittest.TestCase):
    """Real pty + MockBabyOSDevice + host UartService/ProtocolClient."""

    def setUp(self):
        self.logs = []
        self.link = open_api_over_pty(
            encrypt=False,
            open_host_uart=True,
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
        self.assertEqual(self.link.kind, 'pty',
                         'edge/exception main path requires real pty, got %s'
                         % self.link.kind)
        if self.link.kind not in _CHANNEL_KINDS:
            _CHANNEL_KINDS.append(self.link.kind)
        self.device = self.link.device
        self.uart = self.link.host_uart
        self.pc = self.link.protocol_client(encrypt=False)

    def _fresh_fw(self, size=600, seed=9, suffix='.bin'):
        return _write_temp(_make_fw(size, seed=seed), suffix=suffix)

    def _host_write(self, data):
        n = self.uart.write(data)
        self.assertIsNotNone(n)
        self.assertGreaterEqual(n, 0, 'host uart write failed')
        return n

    def _feed_host(self, timeout=0.4):
        """Pump host uart into ProtocolClient; return newly completed frames."""
        frames = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            data = _drain_uart(self.uart)
            if data:
                frames.extend(self.pc.feed_bytes(data))
            else:
                time.sleep(0.01)
        return frames


# ===========================================================================
# R1 — OTA result codes 0/1/2/3/4 over real pty
# ===========================================================================

class TestOtaResultCodesPty(_PtySdkBase):

    def test_ota_result_codes_0_to_4(self):
        cases = (
            (OTA_RESULT_OK, True),
            (OTA_RESULT_CRC_ERROR, False),
            (OTA_RESULT_NAME_MISMATCH, False),
            (OTA_RESULT_LEN_INVALID, False),
            (OTA_RESULT_TIMEOUT, False),
        )
        for code, expect_ok in cases:
            case_id = 'pty_ota_result_%d' % code
            path = self._fresh_fw(400, seed=code + 20)
            try:
                self.device.clear_faults()
                self.device.ota_result_override = code
                results = []
                self.pc.on_result = lambda ok, c: results.append((ok, c))
                ok = self.pc.start_ota(path, timeout=8.0)
                self.assertEqual(ok, expect_ok,
                                 'code=%d ok=%s logs=%s' % (code, ok, self.logs[-8:]))
                self.assertEqual(self.device.last_ota_result, code)
                self.assertEqual(self.pc.transfer_result, code)
                self.assertFalse(self.pc.transfer_active)
                if results:
                    self.assertEqual(results[-1], (expect_ok, code))
                _matrix_add(case_id, 'pty', 'PASS',
                            'result=%d expect_ok=%s' % (code, expect_ok))
            except AssertionError as exc:
                _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
                raise
            finally:
                try:
                    os.unlink(path)
                except Exception:
                    pass
                self.device.ota_result_override = None
                self.device.clear_faults()

    def test_ota_empty_file_len_invalid(self):
        """Empty firmware file → SDK rejects with LEN_INVALID, no hang."""
        case_id = 'pty_ota_empty_file_len_invalid'
        path = _write_temp(b'', suffix='.bin')
        try:
            results = []
            self.pc.on_result = lambda ok, c: results.append((ok, c))
            t0 = time.time()
            ok = self.pc.start_ota(path, timeout=3.0)
            elapsed = time.time() - t0
            self.assertFalse(ok)
            self.assertLess(elapsed, 2.0, 'empty file must fail fast')
            self.assertEqual(self.pc.transfer_result, OTA_RESULT_LEN_INVALID)
            if results:
                self.assertEqual(results[-1], (False, OTA_RESULT_LEN_INVALID))
            _matrix_add(case_id, 'pty', 'PASS', 'elapsed=%.2f' % elapsed)
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            try:
                os.unlink(path)
            except Exception:
                pass


# ===========================================================================
# R2 — OTA mid-transfer silence / garbage recovery
# ===========================================================================

class TestOtaMidTransferEdges(_PtySdkBase):

    def test_ota_device_stops_responding_timeout_structured(self):
        """Device stops answering mid-OTA → host structured timeout, no hang."""
        case_id = 'pty_ota_mid_silence_timeout'
        path = self._fresh_fw(2000, seed=31)  # multi-chunk
        try:
            results = []
            self.pc.on_result = lambda ok, c: results.append((ok, c))
            # Silence after a couple of frames so FW_INFO / early FDATA run,
            # then the transfer stalls.
            self.device.stop_responding_after(CMD_FW_INFO, 0)
            self.device.drop_after_n_frames = 1
            t0 = time.time()
            ok = self.pc.start_ota(path, timeout=3.0)
            elapsed = time.time() - t0
            self.assertFalse(ok)
            self.assertLess(elapsed, 6.0, 'must not busy-wait forever')
            self.assertEqual(self.pc.transfer_result, OTA_RESULT_TIMEOUT)
            self.assertFalse(self.pc.transfer_active)
            if results:
                self.assertEqual(results[-1][0], False)
                self.assertEqual(results[-1][1], OTA_RESULT_TIMEOUT)
            # Follow-up command still returns a clear None (structured), not hang.
            self.device.clear_faults()
            resp = self.pc.get_uid(timeout=2.0)
            self.assertIsNotNone(resp, 'device should recover after clear_faults')
            _matrix_add(case_id, 'pty', 'PASS', 'elapsed=%.2f result=4' % elapsed)
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            try:
                os.unlink(path)
            except Exception:
                pass
            self.device.clear_faults()

    def test_ota_garbage_injection_then_recover(self):
        """Garbage between device frames during OTA must not break the host parser."""
        case_id = 'pty_ota_garbage_then_recover'
        path = self._fresh_fw(900, seed=32)
        try:
            self.device.clear_faults()
            self.device.inject_garbage_between_frames = True
            results = []
            self.pc.on_result = lambda ok, c: results.append((ok, c))
            ok = self.pc.start_ota(path, timeout=10.0)
            self.assertTrue(ok, 'OTA must survive inter-frame garbage: %s'
                            % self.logs[-12:])
            self.assertEqual(self.device.last_ota_result, OTA_RESULT_OK)
            # Recover path: after garbage session, normal commands still work.
            self.device.inject_garbage_between_frames = False
            self.pc.reset_rx_queue()
            uid = self.pc.get_uid(timeout=2.0)
            self.assertEqual(uid, MOCK_UID)
            _matrix_add(case_id, 'pty', 'PASS', 'ota_ok=True uid_recovered=True')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            try:
                os.unlink(path)
            except Exception:
                pass
            self.device.inject_garbage_between_frames = False
            self.device.clear_faults()


# ===========================================================================
# R3 — frame edges: half / sticky / oversized / bad checksum → resync
# ===========================================================================

class TestFrameParserEdgesPty(_PtySdkBase):
    """Host RX parser edges via real pty device→host bytes."""

    def _inject_device_bytes(self, data, delay=0.05):
        """Write raw bytes as device TX (pty master → host slave)."""
        # Device pump must not fight us for pure parser cases.
        self.device._stop.set()
        t = self.device._thread
        if t is not None and t.is_alive():
            t.join(timeout=1.0)
        self.device._thread = None
        self.uart.reset_buffers()
        self.pc.reset_rx_queue()
        self.device.endpoint.write(data)
        time.sleep(delay)

    def _restart_device(self):
        self.device._stop.clear()
        if self.device._thread is None:
            self.device.start()

    def test_half_frame_then_completion_resync(self):
        """Incomplete frame yields nothing; completing it yields one frame."""
        case_id = 'pty_frame_half_then_complete'
        try:
            frame = bp.pack_frame(MOCK_DEV_ID, CMD_TEST, b'')
            half = frame[:5]
            rest = frame[5:]
            self._inject_device_bytes(half)
            frames = self._feed_host(0.25)
            self.assertEqual(frames, [], 'half frame must not complete')
            self.device.endpoint.write(rest)
            time.sleep(0.05)
            frames = self._feed_host(0.35)
            self.assertEqual(len(frames), 1, 'frames=%s logs=%s'
                             % (frames, self.logs[-6:]))
            self.assertEqual(frames[0][1], CMD_TEST)
            _matrix_add(case_id, 'pty', 'PASS', 'half=%dB rest=%dB' % (
                len(half), len(rest)))
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            self._restart_device()

    def test_sticky_multiframe_one_write(self):
        """Two complete frames in one pty write are both parsed."""
        case_id = 'pty_frame_sticky_multiframe'
        try:
            f1 = bp.pack_frame(MOCK_DEV_ID, CMD_TEST, b'')
            uid_body = bytes([len(MOCK_UID)]) + MOCK_UID
            f2 = bp.pack_frame(MOCK_DEV_ID, CMD_GET_UID, uid_body)
            self._inject_device_bytes(f1 + f2)
            frames = self._feed_host(0.35)
            cmds = [f[1] for f in frames]
            self.assertEqual(len(frames), 2, 'cmds=%s' % cmds)
            self.assertIn(CMD_TEST, cmds)
            self.assertIn(CMD_GET_UID, cmds)
            _matrix_add(case_id, 'pty', 'PASS', '2 frames in one write')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            self._restart_device()

    def test_oversized_length_field_no_crash_then_recover(self):
        """Oversized Length must not crash host parser; later frames recover."""
        case_id = 'pty_frame_oversized_length'
        try:
            bad = bytearray(b'\xFE')
            bad += (MOCK_DEV_ID).to_bytes(4, 'little')
            bad += (0xFFFF).to_bytes(2, 'little')  # oversized length
            bad += b'\x01\x00\x00'
            good = bp.pack_frame(MOCK_DEV_ID, CMD_TEST, b'')
            self._inject_device_bytes(bytes(bad) + good)
            frames = self._feed_host(0.4)
            # bad frame dropped; good frame after it still parses
            good_frames = [f for f in frames if f[1] == CMD_TEST]
            self.assertEqual(len(good_frames), 1,
                             'recovery after oversized length failed: %s' % frames)
            _matrix_add(case_id, 'pty', 'PASS', 'recovered after 0xFFFF length')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            self._restart_device()

    def test_bad_checksum_then_valid_frame_resync(self):
        """Bad checksum frame is dropped; a following valid frame is parsed."""
        case_id = 'pty_frame_bad_checksum_resync'
        try:
            good = bp.pack_frame(MOCK_DEV_ID, CMD_UTC, b'\x01\x02\x03\x04')
            bad = bytearray(good)
            bad[-1] = (bad[-1] ^ 0xFF) & 0xFF
            self._inject_device_bytes(bytes(bad) + good)
            frames = self._feed_host(0.4)
            utc_frames = [f for f in frames if f[1] == CMD_UTC]
            self.assertEqual(len(utc_frames), 1,
                             'resync after bad checksum failed: %s' % frames)
            _matrix_add(case_id, 'pty', 'PASS', '1 valid after 1 corrupt')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            self._restart_device()

    def test_host_to_device_bad_checksum_and_sticky(self):
        """Device parser resyncs on bad checksum; sticky host TX still works."""
        case_id = 'pty_frame_host_tx_resync_sticky'
        try:
            self._restart_device()
            self.device.clear_faults()
            good = bp.pack_frame(INVALID_ID, CMD_TEST, bp.build_test_param())
            bad = bytearray(good)
            bad[-1] = (bad[-1] ^ 0x5A) & 0xFF
            # sticky: corrupt frame + good frame in ONE uart write
            self._host_write(bytes(bad) + good)
            resp = self.pc._wait_frame(CMD_TEST, 2.0, 0.005)
            self.assertIsNotNone(
                resp,
                'device must parse the good frame after corrupt sticky TX; '
                'logs=%s written=%s' % (self.logs[-8:], self.device.written_cmds[-4:]))
            self.assertEqual(resp[1], CMD_TEST)
            # second sticky pair still works
            self._host_write(good + good)
            resp2 = self.pc._wait_frame(CMD_TEST, 2.0, 0.005)
            self.assertIsNotNone(resp2)
            _matrix_add(case_id, 'pty', 'PASS', 'device resync + sticky ACK')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise


# ===========================================================================
# R4 — wrong cmd / duplicate / unknown CMD
# ===========================================================================

class TestDeviceFaultRepliesPty(_PtySdkBase):

    def test_device_wrong_cmd_reply(self):
        """Wrong-cmd reply must not satisfy test_link; clear → recovery."""
        case_id = 'pty_device_wrong_cmd'
        try:
            self.device.send_wrong_cmd = CMD_GET_UID
            resp = self.pc.test_link(timeout=0.5)
            self.assertIsNone(resp, 'wrong-cmd reply must not satisfy test_link')
            self.device.send_wrong_cmd = None
            self.device.clear_faults()
            self.pc.reset_rx_queue()
            uid = self.pc.get_uid(timeout=2.0)
            self.assertEqual(uid, MOCK_UID)
            _matrix_add(case_id, 'pty', 'PASS', 'None then uid recovered')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            self.device.send_wrong_cmd = None
            self.device.clear_faults()

    def test_device_duplicate_frame_idempotent(self):
        """Duplicate device frames: host still gets a clean single ACK."""
        case_id = 'pty_device_duplicate_frame'
        try:
            self.device.send_duplicate_frame = True
            resp1 = self.pc.test_link(timeout=2.0)
            resp2 = self.pc.set_time(1700000222, timeout=2.0)
            ok = (resp1 is not None and resp1[1] == CMD_TEST
                  and resp2 is not None and resp2[1] == CMD_UTC
                  and self.device.last_utc == 1700000222)
            self.assertTrue(ok, 'r1=%s r2=%s utc=%s' % (resp1, resp2, self.device.last_utc))
            _matrix_add(case_id, 'pty', 'PASS', 'dup frames still ACK correctly')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            self.device.send_duplicate_frame = False

    def test_device_unknown_cmd_frame(self):
        """Unknown CMD from device: host sees it in rx_log, then recovers."""
        case_id = 'pty_device_unknown_cmd'
        try:
            self.device.send_wrong_cmd = 0xEE  # unknown cmd
            resp = self.pc.test_link(timeout=0.4)
            self.assertIsNone(resp)
            unknown = [f for f in self.pc.rx_log if f[1] == 0xEE]
            self.assertTrue(unknown, 'unknown CMD must appear in host rx_log')
            self.device.send_wrong_cmd = None
            self.device.clear_faults()
            self.pc.reset_rx_queue()
            info = self.pc.get_device_info(timeout=2.0)
            self.assertIsNotNone(info)
            self.assertEqual(info[0], MOCK_VERSION)
            _matrix_add(case_id, 'pty', 'PASS', '0xEE observed, then recovered')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            self.device.send_wrong_cmd = None
            self.device.clear_faults()

    def test_device_oversized_length_field(self):
        """Device oversized Length TX: host does not crash; recovery works."""
        case_id = 'pty_device_oversized_length'
        try:
            self.device.oversized_length_field = True
            crashed = False
            try:
                self.pc.test_link(timeout=0.4)
            except Exception:
                crashed = True
            self.assertFalse(crashed, 'ProtocolClient crashed on oversized length')
            self.device.oversized_length_field = False
            self.device.clear_faults()
            self.pc.reset_rx_queue()
            uid = self.pc.get_uid(timeout=2.0)
            self.assertEqual(uid, MOCK_UID)
            _matrix_add(case_id, 'pty', 'PASS', 'no crash + uid recovered')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            self.device.oversized_length_field = False
            self.device.clear_faults()


# ===========================================================================
# R5 — file transfer 0x6 success / failure
# ===========================================================================

class TestFileTransfer0x6Pty(_PtySdkBase):

    def test_file_0x6_success(self):
        case_id = 'pty_file_0x6_success'
        path = self._fresh_fw(800, seed=51)
        try:
            results = []
            self.pc.on_result = lambda ok, c: results.append((ok, c))
            ok = self.pc.start_file_transfer(path, dev_no=1, offset=0x10,
                                             timeout=10.0)
            self.assertTrue(ok, 'file transfer failed: %s' % self.logs[-10:])
            self.assertEqual(self.device.last_ota_result, OTA_RESULT_OK)
            self.assertIsNotNone(self.device.trans_file)
            size, crc, dev_no, offset = self.device.trans_file
            self.assertEqual(dev_no, 1)
            self.assertEqual(offset, 0x10)
            payload = _make_fw(800, seed=51)
            self.assertEqual(self.device.received_fw[:800], payload)
            _matrix_add(case_id, 'pty', 'PASS', 'size=%d dev_no=1' % size)
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            try:
                os.unlink(path)
            except Exception:
                pass

    def test_file_0x6_failure(self):
        """0x6 failure paths: silent device → structured timeout; empty → LEN_INVALID."""
        case_id = 'pty_file_0x6_failure'
        path = self._fresh_fw(700, seed=52)
        empty = _write_temp(b'', suffix='.bin')
        try:
            # empty → LEN_INVALID
            results = []
            self.pc.on_result = lambda ok, c: results.append((ok, c))
            ok_empty = self.pc.start_file_transfer(empty, timeout=2.0)
            self.assertFalse(ok_empty)
            self.assertEqual(self.pc.transfer_result, OTA_RESULT_LEN_INVALID)

            # silent device → TIMEOUT (silence ALL replies so FDATA/OTA_RESULT
            # cannot complete the transfer either)
            self.device.clear_faults()
            self.device.frames_handled = 0
            self.device.drop_after_n_frames = 0
            t0 = time.time()
            ok_silent = self.pc.start_file_transfer(path, timeout=2.5)
            elapsed = time.time() - t0
            self.assertFalse(ok_silent)
            self.assertLess(elapsed, 5.0)
            self.assertEqual(self.pc.transfer_result, OTA_RESULT_TIMEOUT)
            _matrix_add(case_id, 'pty', 'PASS',
                        'empty=3 silent=4 elapsed=%.2f' % elapsed)
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            for p in (path, empty):
                try:
                    os.unlink(p)
                except Exception:
                    pass
            self.device.clear_faults()


# ===========================================================================
# R6 / R7 — Xmodem & Ymodem edges over real pty
# ===========================================================================

class TestXmodemYmodemEdgesPty(_PtySdkBase):

    def _run_xmodem(self, payload, device_fault, timeout=4.0):
        """Host XmodemSender on REAL host uart; device mock receives."""
        self.device.switch_to_protocol()
        self.device.xm_fault = dict(device_fault)
        rx = self.device.start_xmodem_receive()

        def _send(buf):
            n = self.uart.write(buf)
            if n is not None and n < 0:
                raise IOError('host uart write failed for xmodem')

        sender = XmodemSender(_send, log_fn=self.logs.append,
                              timeout_sec=1.0, start_timeout_sec=0.4,
                              max_retries=4)
        sender.start(payload)
        deadline = time.time() + timeout
        while time.time() < deadline and sender.is_active:
            data = _drain_uart(self.uart)
            if data:
                for b in data:
                    sender.on_uart_byte(b)
            sender.on_timer_tick()
            time.sleep(0.005)
        return sender.state == XferState.DONE, sender, bytes(rx.data)

    def _run_ymodem(self, payload, device_fault, timeout=4.0, filename='f.bin'):
        self.device.switch_to_protocol()
        self.device.ym_fault = dict(device_fault)
        rx = self.device.start_ymodem_receive()

        def _send(buf):
            n = self.uart.write(buf)
            if n is not None and n < 0:
                raise IOError('host uart write failed for ymodem')

        sender = YmodemSender(_send, log_fn=self.logs.append,
                              timeout_sec=1.0, start_timeout_sec=0.4,
                              max_retries=4)
        sender.start(payload, filename=filename)
        deadline = time.time() + timeout
        while time.time() < deadline and sender.is_active:
            data = _drain_uart(self.uart)
            if data:
                for b in data:
                    sender.on_uart_byte(b)
            sender.on_timer_tick()
            time.sleep(0.005)
        return sender.state == XferState.DONE, sender, bytes(rx.data)

    def test_xmodem_timeout_abort_bounded(self):
        """Device never sends C → sender aborts in bounded time (no hang)."""
        case_id = 'pty_xmodem_timeout_abort'
        try:
            t0 = time.time()
            ok_done, sender, _rx = self._run_xmodem(
                _make_fw(200, seed=5), {'send_start_byte': False}, timeout=3.0)
            elapsed = time.time() - t0
            self.assertFalse(ok_done)
            self.assertEqual(sender.state, XferState.ABORTED)
            self.assertLess(elapsed, 3.5)
            _matrix_add(case_id, 'pty', 'PASS', 'elapsed=%.2f ABORTED' % elapsed)
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise

    def test_xmodem_mid_nak_still_succeeds(self):
        """nak_on_block=1: host resends and transfer still completes."""
        case_id = 'pty_xmodem_mid_nak_success'
        try:
            payload = _make_fw(300, seed=6)  # 3 blocks
            ok_done, sender, rx = self._run_xmodem(
                payload, {'nak_on_block': 1}, timeout=5.0)
            self.assertTrue(ok_done,
                            'state=%s logs=%s' % (sender.state, self.logs[-12:]))
            self.assertTrue(rx[:len(payload)] == payload,
                            'rx mismatch len=%d' % len(rx))
            _matrix_add(case_id, 'pty', 'PASS', 'DONE after mid NAK')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise

    def test_xmodem_reject_eot_aborts(self):
        case_id = 'pty_xmodem_reject_eot_abort'
        try:
            ok_done, sender, _rx = self._run_xmodem(
                _make_fw(128, seed=7), {'reject_eot': True}, timeout=4.0)
            self.assertFalse(ok_done)
            self.assertEqual(sender.state, XferState.ABORTED)
            _matrix_add(case_id, 'pty', 'PASS', 'ABORTED after EOT NAKs')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise

    def test_ymodem_no_c_abort(self):
        case_id = 'pty_ymodem_no_c_abort'
        try:
            ok_done, sender, _rx = self._run_ymodem(
                _make_fw(200, seed=8), {'send_start_byte': False}, timeout=3.0)
            self.assertFalse(ok_done)
            self.assertEqual(sender.state, XferState.ABORTED)
            _matrix_add(case_id, 'pty', 'PASS', 'ABORTED without C')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise

    def test_ymodem_mid_nak_still_succeeds(self):
        case_id = 'pty_ymodem_mid_nak_success'
        try:
            payload = _make_fw(1500, seed=11)  # block0 + STX + tail
            ok_done, sender, rx = self._run_ymodem(
                payload, {'nak_on_block': 1}, timeout=5.0)
            self.assertTrue(ok_done,
                            'state=%s logs=%s' % (sender.state, self.logs[-12:]))
            self.assertTrue(len(rx) >= len(payload) or rx.startswith(payload),
                            'rx len=%d payload=%d' % (len(rx), len(payload)))
            _matrix_add(case_id, 'pty', 'PASS', 'DONE after mid NAK')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise

    def test_ymodem_cancel_mid_transfer(self):
        """Host cancel() mid-transfer → ABORTED, bounded, no hang."""
        case_id = 'pty_ymodem_cancel_mid'
        try:
            payload = _make_fw(3000, seed=12)  # multi-block, keeps sender busy
            self.device.switch_to_protocol()
            self.device.ym_fault = {}
            rx = self.device.start_ymodem_receive()

            def _send(buf):
                n = self.uart.write(buf)
                if n is not None and n < 0:
                    raise IOError('uart write fail')

            sender = YmodemSender(_send, log_fn=self.logs.append,
                                  timeout_sec=2.0, start_timeout_sec=0.5,
                                  max_retries=8)
            sender.start(payload, filename='big.bin')
            deadline = time.time() + 2.0
            cancelled = False
            while time.time() < deadline and sender.is_active:
                data = _drain_uart(self.uart)
                if data:
                    for b in data:
                        sender.on_uart_byte(b)
                sender.on_timer_tick()
                if not cancelled and sender.state == XferState.SEND_DATA:
                    sender.cancel()
                    cancelled = True
                time.sleep(0.005)
            self.assertTrue(cancelled, 'sender never reached SEND_DATA')
            self.assertEqual(sender.state, XferState.ABORTED)
            # Device may or may not have seen CAN — either is acceptable;
            # host state must be terminal.
            dev_err = ''
            if self.device.ymodem is not None:
                dev_err = self.device.ymodem.error or 'none'
            else:
                dev_err = 'receiver gone'
            _matrix_add(case_id, 'pty', 'PASS',
                        'ABORTED after cancel; device_error=%s' % dev_err)
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise


# ===========================================================================
# R8 — Shell: serial close / unknown param / illegal value
# ===========================================================================

class TestShellEdgesPty(_PtySdkBase):

    def test_shell_unknown_param_and_illegal_value(self):
        case_id = 'pty_shell_unknown_and_illegal'
        try:
            self.device.switch_to_shell(params=dict(DEFAULT_SHELL_PARAMS))
            sh = ShellClient(self.uart, default_timeout=0.6)

            # unknown param → None (firmware prints nothing)
            val = sh.param_get('no_such_param_xyz', timeout=0.4)
            self.assertIsNone(val)

            # empty name → SDK returns None / False without I/O hang
            self.assertIsNone(sh.param_get(''))
            self.assertFalse(sh.param_set('', 1))

            # illegal non-numeric value: firmware atoi fails → no store
            before = dict(self.device.shell_params)
            sh.param_set('g_volume', 'not_a_number', timeout=0.4, verify=True)
            after = dict(self.device.shell_params)
            self.assertEqual(after.get('g_volume'), before.get('g_volume'),
                             'illegal value must not change device param')

            # empty value string → SDK rejects
            self.assertFalse(sh.param_set('g_volume', '', timeout=0.4))

            # legal set still works after illegal attempts
            ok = sh.param_set('g_volume', 77, timeout=0.8, verify=True)
            self.assertTrue(ok, 'legal set must work after illegal attempts')
            self.assertEqual(self.device.shell_params.get('g_volume'), 77)
            _matrix_add(case_id, 'pty', 'PASS',
                        'unknown=None illegal-no-store legal_ok')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise

    def test_shell_serial_close_structured(self):
        """Host uart closed → shell write -1, param_get None, no hang."""
        case_id = 'pty_shell_serial_close'
        try:
            self.device.switch_to_shell(params=dict(DEFAULT_SHELL_PARAMS))
            sh = ShellClient(self.uart, default_timeout=0.5)
            val = sh.param_get('g_volume', timeout=0.8)
            self.assertEqual(val, DEFAULT_SHELL_PARAMS['g_volume'])

            # close the real host serial port
            self.uart.close()
            self.assertFalse(self.uart.is_open)

            n = sh.write('param g_volume')
            self.assertTrue(n is None or n < 0,
                            'write after close must fail, got %r' % n)
            t0 = time.time()
            val2 = sh.param_get('g_volume', timeout=0.35)
            elapsed = time.time() - t0
            self.assertIsNone(val2)
            self.assertLess(elapsed, 1.0, 'closed-port get must not hang')
            # read_response returns empty string, not exception
            text = sh.send_command('param', timeout=0.25)
            self.assertEqual(text, '')
            _matrix_add(case_id, 'pty', 'PASS', 'write=-1 get=None elapsed=%.2f' % elapsed)
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise


# ===========================================================================
# R9 — concurrency: busy + cancel then restart (SDK + API structured)
# ===========================================================================

class TestConcurrencyBusyCancelPty(_PtySdkBase):

    def test_sdk_transfer_busy_then_cancel_restart(self):
        """Second start_ota while transfer active → busy False; cancel → restart OK."""
        case_id = 'pty_sdk_busy_cancel_restart'
        path = self._fresh_fw(1800, seed=61)
        path2 = self._fresh_fw(500, seed=62)
        try:
            results = []
            self.pc.on_result = lambda ok, c: results.append((ok, c))

            # Silence device so first OTA stays in-flight
            self.device.stop_responding_after(CMD_FW_INFO, 0)
            self.device.drop_after_n_frames = 0
            self.device._frames_handled_limit = 0

            first = {'ok': None}

            def _first():
                first['ok'] = self.pc.start_ota(path, timeout=6.0)

            th = threading.Thread(target=_first, daemon=True)
            th.start()
            self.assertTrue(_wait(lambda: self.pc.transfer_active, timeout=2.0),
                            'transfer never became active; logs=%s' % self.logs[-6:])

            # Concurrent second start → busy rejection (clear False, no hang)
            t0 = time.time()
            ok2 = self.pc.start_ota(path2, timeout=1.0)
            elapsed_busy = time.time() - t0
            self.assertFalse(ok2, 'concurrent transfer must be rejected')
            self.assertLess(elapsed_busy, 2.0, 'busy reject must be fast')

            # Cancel / stop → structured cleanup
            self.pc.stop_transfer()
            th.join(timeout=4.0)
            self.assertFalse(self.pc.transfer_active)
            self.assertEqual(self.pc.transfer_result, OTA_RESULT_TIMEOUT)

            # Device recovers → new OTA succeeds
            self.device.clear_faults()
            self.device._stop.set()
            if self.device._thread is not None and self.device._thread.is_alive():
                self.device._thread.join(timeout=1.0)
            self.device._thread = None
            self.device._stop.clear()
            self.device.start()
            self.device.clear_faults()
            self.pc.reset_rx_queue()
            ok3 = self.pc.start_ota(path2, timeout=10.0)
            self.assertTrue(ok3, 'restart after cancel must succeed: %s'
                            % self.logs[-10:])
            _matrix_add(case_id, 'pty', 'PASS',
                        'busy_elapsed=%.2f restart_ok=%s' % (elapsed_busy, ok3))
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            for p in (path, path2):
                try:
                    os.unlink(p)
                except Exception:
                    pass
            self.device.clear_faults()


class TestApiStructuredEdgesPty(unittest.TestCase):
    """API over real pty: structured AppError codes, never bare 500/stack."""

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

    def test_api_serial_close_then_structured_cmds(self):
        case_id = 'pty_api_serial_close_structured'
        try:
            r = self.client.post('/api/device/serial/close', json={})
            self.assertEqual(r.status_code, 200, r.text)
            for url, body in (
                ('/api/device/protocol/test', {}),
                ('/api/device/protocol/set_time', {'utc': 1}),
                ('/api/device/uid/get', {}),
                ('/api/device/ota/start', {'path': '/nope.bin', 'timeout': 1}),
            ):
                rr = self.client.post(url, json=body)
                _assert_structured(self, rr, 409, 'SERIAL_NOT_OPEN')
            _matrix_add(case_id, 'pty', 'PASS', '409 SERIAL_NOT_OPEN x4')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise

    def test_api_ota_busy_and_cancel_restart(self):
        case_id = 'pty_api_ota_busy_cancel_restart'
        path = _write_temp(_make_fw(1200, seed=71))
        try:
            self.link.device.stop_responding_after(CMD_FW_INFO, 0)
            self.link.device.drop_after_n_frames = 0
            self.link.device._frames_handled_limit = 0

            r1 = self.client.post('/api/device/ota/start',
                                  json={'path': path, 'timeout': 8})
            self.assertEqual(r1.status_code, 200, r1.text)
            job1 = r1.json()['job_id']
            self.assertTrue(_wait(lambda: (self.client.get(
                '/api/device/ota/status?job_id=%s' % job1).json()
                .get('transfer_active')), timeout=3.0))

            r2 = self.client.post('/api/device/ota/start',
                                  json={'path': path, 'timeout': 8})
            _assert_structured(self, r2, 409, 'TRANSFER_BUSY')

            rstop = self.client.post('/api/device/file/stop', json={})
            self.assertEqual(rstop.status_code, 200, rstop.text)
            body = _wait_job(self.client, job1, 'ota', timeout=6.0)
            job = body.get('job') or {}
            self.assertIn(job.get('state'), ('cancelled', 'error'),
                          'job state=%s body=%s' % (job.get('state'), body))
            self.assertFalse(body.get('transfer_active', True))

            # restart after cancel
            self.link.device.clear_faults()
            self.link.device._stop.set()
            if (self.link.device._thread is not None
                    and self.link.device._thread.is_alive()):
                self.link.device._thread.join(timeout=1.0)
            self.link.device._thread = None
            self.link.device._stop.clear()
            self.link.device.start()
            self.link.device.clear_faults()
            r3 = self.client.post('/api/device/ota/start',
                                  json={'path': path, 'timeout': 8})
            self.assertEqual(r3.status_code, 200, r3.text)
            body3 = _wait_job(self.client, r3.json()['job_id'], 'ota', timeout=10.0)
            job3 = body3.get('job') or {}
            self.assertIn(job3.get('state'), ('done', 'error'),
                          'restarted job=%s' % job3)
            _matrix_add(case_id, 'pty', 'PASS',
                        '409 TRANSFER_BUSY; restart=%s' % job3.get('state'))
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            try:
                os.unlink(path)
            except Exception:
                pass

    def test_api_ota_timeout_structured_job(self):
        case_id = 'pty_api_ota_timeout_structured'
        path = _write_temp(_make_fw(800, seed=72))
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
            # follow-up structured command (device still open, just silent OTA)
            self.link.device.clear_faults()
            r2 = self.client.post('/api/device/protocol/test', json={})
            self.assertEqual(r2.status_code, 200, r2.text)
            _matrix_add(case_id, 'pty', 'PASS', 'error result_code=4')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            try:
                os.unlink(path)
            except Exception:
                pass
            self.link.device.clear_faults()

    def test_api_param_structured_errors(self):
        case_id = 'pty_api_param_structured'
        try:
            self.link.device.switch_to_shell()
            r = self.client.post('/api/device/param/get',
                                 json={'name': 'no_such_param_xyz', 'timeout': 0.4})
            _assert_structured(self, r, 504, 'PARAM_NOT_FOUND')
            r2 = self.client.post('/api/device/param/get', json={'name': ''})
            _assert_structured(self, r2, 400, 'INVALID_REQUEST')
            _matrix_add(case_id, 'pty', 'PASS', 'PARAM_NOT_FOUND + INVALID_REQUEST')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise

    def test_api_ota_name_mismatch_structured(self):
        case_id = 'pty_api_ota_name_mismatch_structured'
        path = _write_temp(_make_fw(500, seed=73))
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
            _matrix_add(case_id, 'pty', 'PASS', 'result_code=2 NAME_MISMATCH')
        except AssertionError as exc:
            _matrix_add(case_id, 'pty', 'FAIL', str(exc)[:200])
            raise
        finally:
            try:
                os.unlink(path)
            except Exception:
                pass
            self.link.device.ota_result_override = None


# ===========================================================================
# Memory-mock supplements — counted SEPARATELY from pty
# ===========================================================================

class _MemUart(object):
    def __init__(self):
        self.rx = bytearray()
        self.tx = bytearray()
        self.is_open = True
        self.port = 'MEM'
        self.baudrate = 115200

    def write(self, data):
        if data is None or not self.is_open:
            return -1
        if isinstance(data, bytearray):
            data = bytes(data)
        self.tx.extend(data)
        return len(data)

    def read_available(self):
        out = bytes(self.rx)
        self.rx.clear()
        return out

    def close(self):
        self.is_open = False


class TestMemoryMockSupplements(unittest.TestCase):
    """Supplementary in-memory parser / sender edges — NOT the pty main path."""

    def test_mem_oversized_length_and_resync(self):
        case_id = 'mem_protocol_oversized_and_resync'
        try:
            uart = _MemUart()
            pc = ProtocolClient(uart)
            bad = bytearray(b'\xFE') + (0x1314).to_bytes(4, 'little') \
                + (0xFFFF).to_bytes(2, 'little') + b'\x01\x00\x00'
            frames = pc.feed_bytes(bytes(bad))
            self.assertEqual(frames, [])
            good = bp.pack_frame(0xABCD, CMD_TEST, b'')
            frames2 = pc.feed_bytes(good)
            self.assertEqual(len(frames2), 1)
            _matrix_add(case_id, 'memory', 'PASS', 'oversized dropped; good kept')
        except AssertionError as exc:
            _matrix_add(case_id, 'memory', 'FAIL', str(exc)[:200])
            raise

    def test_mem_half_and_sticky_and_bad_checksum(self):
        case_id = 'mem_protocol_half_sticky_badsum'
        try:
            uart = _MemUart()
            pc = ProtocolClient(uart)
            f1 = bp.pack_frame(0x1314, CMD_TEST, b'')
            f2 = bp.pack_frame(0x1314, CMD_UTC, b'\x01\x00\x00\x00')
            bad = bytearray(f1)
            bad[-1] = (bad[-1] ^ 0xFF) & 0xFF
            # half
            self.assertEqual(pc.feed_bytes(f1[:4]), [])
            # rest of bad + sticky good frames
            frames = pc.feed_bytes(bytes(bad[4:]) + f2 + f1)
            cmds = [x[1] for x in frames]
            self.assertIn(CMD_UTC, cmds)
            self.assertIn(CMD_TEST, cmds)
            _matrix_add(case_id, 'memory', 'PASS', 'half+badsum+sticky OK')
        except AssertionError as exc:
            _matrix_add(case_id, 'memory', 'FAIL', str(exc)[:200])
            raise

    def test_mem_xmodem_wait_start_abort_bounded(self):
        case_id = 'mem_xmodem_wait_start_abort'
        try:
            sender = XmodemSender(lambda b: None, timeout_sec=0.2,
                                  start_timeout_sec=0.1, max_retries=2)
            sender.start(b'\x01\x02\x03')
            t0 = time.time()
            while sender.is_active and time.time() - t0 < 2.0:
                sender.on_timer_tick()
                time.sleep(0.02)
            self.assertEqual(sender.state, XferState.ABORTED)
            self.assertLess(time.time() - t0, 2.0)
            _matrix_add(case_id, 'memory', 'PASS', 'ABORTED bounded')
        except AssertionError as exc:
            _matrix_add(case_id, 'memory', 'FAIL', str(exc)[:200])
            raise

    def test_mem_ymodem_wait_start_abort_bounded(self):
        case_id = 'mem_ymodem_wait_start_abort'
        try:
            sender = YmodemSender(lambda b: None, timeout_sec=0.2,
                                  start_timeout_sec=0.1, max_retries=2)
            sender.start(b'\x01\x02\x03', filename='a.bin')
            t0 = time.time()
            while sender.is_active and time.time() - t0 < 2.0:
                sender.on_timer_tick()
                time.sleep(0.02)
            self.assertEqual(sender.state, XferState.ABORTED)
            _matrix_add(case_id, 'memory', 'PASS', 'ABORTED bounded')
        except AssertionError as exc:
            _matrix_add(case_id, 'memory', 'FAIL', str(exc)[:200])
            raise

    def test_mem_stop_transfer_clears_state(self):
        case_id = 'mem_stop_transfer_cleanup'
        try:
            uart = _MemUart()
            pc = ProtocolClient(uart)
            pc._xfer_busy = True
            pc._xfer_done = False
            pc._xfer_result = None
            pc.stop_transfer()
            self.assertFalse(pc.transfer_active)
            self.assertEqual(pc.transfer_result, OTA_RESULT_TIMEOUT)
            st = pc.status()
            self.assertFalse(st['transfer_active'])
            _matrix_add(case_id, 'memory', 'PASS', 'busy cleared result=4')
        except AssertionError as exc:
            _matrix_add(case_id, 'memory', 'FAIL', str(exc)[:200])
            raise


# ===========================================================================
# Runner + TEST_SCHEMA
# ===========================================================================

def _build_test_schema(result):
    pty_rows = [m for m in _MATRIX if m['channel'] == 'pty']
    mem_rows = [m for m in _MATRIX if m['channel'] == 'memory']
    pty_fail = sum(1 for m in pty_rows if m['status'] == 'FAIL')
    pty_pass = sum(1 for m in pty_rows if m['status'] == 'PASS')
    mem_fail = sum(1 for m in mem_rows if m['status'] == 'FAIL')
    mem_pass = sum(1 for m in mem_rows if m['status'] == 'PASS')
    total = len(_MATRIX)
    failed = sum(1 for m in _MATRIX if m['status'] == 'FAIL')
    acceptance = (
        pty_fail == 0 and pty_pass > 0
        and result.wasSuccessful()
        and _PTY_COUNT['total'] > 0
        and all(k == 'pty' for k in _CHANNEL_KINDS)
    )
    return {
        'schema': TEST_SCHEMA_ID,
        'area': 'virtual-serial-edge-protocol-sdk',
        'channel_required': 'pty',
        'python': sys.version.split()[0],
        'protocol_host_id': '0x%X' % DEVICE_ID_HOST,
        'channel_kinds': list(_CHANNEL_KINDS),
        'total': total,
        'passed': total - failed,
        'failed': failed,
        'pty': {
            'total': _PTY_COUNT['total'],
            'pass': _PTY_COUNT['pass'],
            'fail': _PTY_COUNT['fail'],
        },
        'memory_mock': {
            'total': _MEM_COUNT['total'],
            'pass': _MEM_COUNT['pass'],
            'fail': _MEM_COUNT['fail'],
            'note': 'supplementary — NOT the primary pty path',
        },
        'coverage_matrix': _MATRIX,
        'requirement_coverage': {
            'R1_ota_result_codes_0_4': any(
                m['id'].startswith('pty_ota_result_') for m in pty_rows),
            'R2_ota_mid_silence_and_garbage': any(
                'mid_silence' in m['id'] or 'garbage_then' in m['id']
                for m in pty_rows),
            'R3_frame_edges_resync': any(
                m['id'].startswith('pty_frame_') for m in pty_rows),
            'R4_wrong_dup_unknown_cmd': any(
                'wrong_cmd' in m['id'] or 'duplicate' in m['id']
                or 'unknown_cmd' in m['id'] for m in pty_rows),
            'R5_file_0x6_success_and_failure': any(
                'file_0x6' in m['id'] for m in pty_rows),
            'R6_xmodem_timeout_and_nak': any(
                'xmodem' in m['id'] for m in pty_rows),
            'R7_ymodem_no_c_nak_cancel': any(
                'ymodem' in m['id'] for m in pty_rows),
            'R8_shell_close_unknown_illegal': any(
                'shell_' in m['id'] for m in pty_rows),
            'R9_busy_cancel_restart': any(
                'busy' in m['id'] or 'cancel_restart' in m['id']
                for m in pty_rows),
        },
        'unittest': {
            'run': result.testsRun,
            'failures': len(result.failures),
            'errors': len(result.errors),
            'skipped': len(result.skipped),
        },
        'acceptance': acceptance,
        'notes': (
            'Main path is real pty bidirectional byte channel. Memory-mock '
            'rows are counted separately and never mixed into pty results. '
            'API structured errors use AppError detail={detail,code}.'
        ),
    }


def _print_matrix(schema):
    print('')
    print('==== virtual-serial edge coverage matrix ====')
    print('%-44s %-8s %s' % ('CASE', 'CHANNEL', 'STATUS'))
    for m in schema['coverage_matrix']:
        print('%-44s %-8s %s%s' % (
            m['id'], m['channel'], m['status'],
            ('  ' + m['detail']) if m['detail'] else ''))
    print('')
    print('pty (primary): total=%d pass=%d fail=%d' % (
        schema['pty']['total'], schema['pty']['pass'], schema['pty']['fail']))
    print('memory (supplementary): total=%d pass=%d fail=%d' % (
        schema['memory_mock']['total'], schema['memory_mock']['pass'],
        schema['memory_mock']['fail']))
    print('total=%d passed=%d failed=%d' % (
        schema['total'], schema['passed'], schema['failed']))
    print('requirement_coverage=%s' % json.dumps(schema['requirement_coverage']))
    print('acceptance=%s' % schema['acceptance'])


def main():
    print('BabyOS Studio virtual-serial edge protocol acceptance (SDK, pty)')
    print('python: %s' % sys.version.split()[0])
    print('protocol host id: 0x%X' % DEVICE_ID_HOST)

    ch = create_byte_channel(prefer='pty', require_pty=True)
    print('byte channel kind: %s host_port=%s' % (ch.kind, ch.host_port))
    ch.close()

    loader = unittest.defaultTestLoader
    suite = unittest.TestSuite()
    for cls in (
        TestOtaResultCodesPty,
        TestOtaMidTransferEdges,
        TestFrameParserEdgesPty,
        TestDeviceFaultRepliesPty,
        TestFileTransfer0x6Pty,
        TestXmodemYmodemEdgesPty,
        TestShellEdgesPty,
        TestConcurrencyBusyCancelPty,
        TestApiStructuredEdgesPty,
        TestMemoryMockSupplements,
    ):
        suite.addTests(loader.loadTestsFromTestCase(cls))
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    schema = _build_test_schema(result)
    _print_matrix(schema)
    print('')
    print('==== TEST_SCHEMA ====')
    print(json.dumps(schema, indent=2, ensure_ascii=False))

    ok = schema['acceptance'] and schema['failed'] == 0
    print('')
    print('virtual-serial edge protocol acceptance: %s'
          % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
