#!/usr/bin/env python3
"""
test_virtual_serial_edge_api — API-layer edge / exception scenarios over real pty.

Authority (硬性验收):
  1. 边缘/异常场景必须走虚拟串口 pty 真实双向通讯（主路径）。
  2. 补充可含内存 mock（FakeUart / in-process），但报告必须分开计数。
  3. 主机侧真实 UartService / ProtocolClient / ShellClient / Xmodem / API。
     设备侧 mock 真实解析 BabyOS 帧。
  4. Python 3.8；不改 bos/thirdparty；不提交 git；禁止 stub；禁止改测试造假。
  5. 异常时 API 返回结构化错误，禁止裸 500/裸栈；
     SDK 返回明确 None/异常，禁止死循环 busy-wait。
  6. 上位机 DeviceID 0x1314；HTTPS 证书 pin origin/dev tool/mock_https_*.pem。

Coverage matrix (API layer over pty):
  1. 串口关闭后全设备端点结构化错误（不裸 500）
  2. OTA/Xmodem/文件 missing/empty/busy/cancel 后再启动
  3. OTA 异步结果码失败时 status.job 结构完整
  4. Shell/param 非法请求
  5. HTTP mock: 重复 start 409；https=true 证书为 origin/dev；
     证书内容不匹配时 start 失败结构化
  6. HTTP proxy 非法 URL/method；下游失败 502
  7. 设备断链后 status 可读、不悬挂
  8. 未打开串口时 protocol/uid/sn/info 全部 409

Run:
  tool/babyos-studio/python/.venv/bin/python \
      tool/babyos-studio/test/device_features/test_virtual_serial_edge_api.py
  or: pytest tool/babyos-studio/test/device_features/test_virtual_serial_edge_api.py
"""

from __future__ import print_function

import hashlib
import json
import os
import sys
import tempfile
import time
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_REPO = os.path.abspath(os.path.join(_STUDIO, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
for _p in (_PY_ROOT, _HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

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
    DEVICE_ID_HOST,
)
from device.sn_util import sn_bytes as _sn_bytes  # noqa: E402

from mock_babyos_device import (  # noqa: E402
    open_api_over_pty,
    DEFAULT_SHELL_PARAMS,
)

MOCK_UID = b'\x11\x22\x33\x44\x55\x66\x77\x88\x99\xAA\xBB\xCC'
MOCK_VERSION = 'v1.2.3'
MOCK_MODEL = 'BabyOS-MCU'
MOCK_DEV_ID = 0x0000ABCD

# origin/dev tool/mock_https_{cert,key}.pem content hashes (authority)
DEV_CERT_SHA256 = '45020779c14d326cb5306b68687a2985f0b6c626f9587f1ce39493a77f9b034c'
DEV_KEY_SHA256 = '24017d03c4f9b83779cc0d2d957528bf6333d7338369f1141b90e4d81dcfd689'

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
_ERRORS = []  # (case, detail) for failed assertions


def _rec_pty(ok: bool, case_id: str, detail: str = '') -> None:
    global _PTY_TOTAL, _PTY_PASS, _PTY_FAIL
    _PTY_TOTAL += 1
    if case_id not in _PTY_CASES:
        _PTY_CASES.append(case_id)
    if ok:
        _PTY_PASS += 1
    else:
        _PTY_FAIL += 1
        _ERRORS.append((case_id, detail))


def _rec_mem(ok: bool, case_id: str, detail: str = '') -> None:
    global _MEM_TOTAL, _MEM_PASS, _MEM_FAIL
    _MEM_TOTAL += 1
    if case_id not in _MEM_CASES:
        _MEM_CASES.append(case_id)
    if ok:
        _MEM_PASS += 1
    else:
        _MEM_FAIL += 1
        _ERRORS.append((case_id, detail))


def _make_fw(n: int, seed: int = 3) -> bytes:
    return bytes(bytearray(((i * 7) + seed) & 0xFF for i in range(n)))


def _write_temp(data: bytes, suffix: str = '.bin') -> str:
    fd, path = tempfile.mkstemp(suffix=suffix, prefix='babyos_edge_api_')
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
              timeout: float = 10.0) -> dict:
    """Poll the matching status endpoint until the job reaches a terminal state."""
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


def _detail_obj(r):
    """Return structured detail dict from an AppError response, else None."""
    try:
        payload = r.json()
        detail = payload.get('detail')
        if isinstance(detail, dict):
            return detail
    except Exception:
        pass
    return None


def _detail_code(r) -> str:
    d = _detail_obj(r)
    if d is None:
        return ''
    return str(d.get('code') or '')


def _assert_structured(testcase, r, status, code):
    """Assert structured AppError: HTTP status + detail.code + non-empty detail."""
    testcase.assertEqual(r.status_code, status,
                         'status=%s body=%s' % (r.status_code, r.text))
    detail = _detail_obj(r)
    testcase.assertIsInstance(
        detail, dict,
        'AppError must return structured detail, got %r (body=%s)'
        % (detail, r.text[:400]))
    testcase.assertEqual(detail.get('code'), code, r.text)
    testcase.assertTrue(
        str(detail.get('detail') or '').strip(),
        'detail.detail must be non-empty: %r' % (detail,))
    # no bare stack / traceback leak
    testcase.assertNotIn('Traceback (most recent call last)', r.text)
    testcase.assertNotIn('File "/home/', r.text)


class _ApiEdgeBase(unittest.TestCase):
    """Shared FastAPI TestClient + real pty link + mock device fixture."""

    encrypt = False
    open_serial = True

    def setUp(self):
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

        self.logs = []
        self.link = open_api_over_pty(
            encrypt=self.encrypt,
            open_host_uart=False,
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
                         'API edge suite requires real pty, got %s'
                         % self.link.kind)
        if self.link.kind not in _CHANNEL_KINDS:
            _CHANNEL_KINDS.append(self.link.kind)

        from app.main import create_app
        from fastapi.testclient import TestClient
        self.app = create_app()
        self.client = TestClient(self.app)

        if self.open_serial:
            r = self.client.post('/api/device/serial/open', json={
                'path': self.link.host_port,
                'baud': 115200,
                'encrypt': self.encrypt,
            })
            self.assertEqual(r.status_code, 200, r.text)
            body = r.json()
            self.assertTrue(body.get('ok'))
            self.assertEqual(body.get('host_id'), DEVICE_ID_HOST,
                             'upper tool host DeviceID must be 0x1314')

    def _record(self, ok: bool, case_id: str, detail: str = '') -> None:
        _rec_pty(ok, case_id, detail)

    def _cmd_seen(self, cmd: int) -> bool:
        with self.link.device._lock:
            return cmd in self.link.device.written_cmds

    def _restore_device_respond(self) -> None:
        """
        Clear fault knobs so the still-running mock pump answers again.

        Do NOT call device.stop() here — stop() closes the pty endpoint fd,
        and a subsequent start() only restarts the pump on a dead endpoint
        (every later transfer then times out as OTA_RESULT_TIMEOUT).
        """
        self.link.device.clear_faults()
        self.assertTrue(self.link.device.endpoint is not None)


# ---------------------------------------------------------------------------
# 1. / 8. Serial closed → structured errors; never-opened → 409
# ---------------------------------------------------------------------------

class TestSerialClosedStructuredErrors(_ApiEdgeBase):
    """串口关闭后全设备端点结构化错误（不裸 500）。"""

    ENDPOINTS = [
        ('/api/device/protocol/test', {}),
        ('/api/device/protocol/set_time', {'utc': 1700000000}),
        ('/api/device/uid/get', {}),
        ('/api/device/sn/write', {'orval': 0}),
        ('/api/device/info/get', {}),
        ('/api/device/shell/cmd', {'cmd': 'param'}),
        ('/api/device/param/list', {}),
        ('/api/device/param/get', {'name': 'g_volume'}),
        ('/api/device/param/set', {'name': 'g_volume', 'value': 1}),
        ('/api/device/ota/start', {'path': __file__, 'timeout': 1.0}),
        ('/api/device/file/start', {'path': __file__, 'timeout': 1.0}),
        ('/api/device/xmodem/start', {'path': __file__}),
        ('/api/device/ymodem/start', {'path': __file__}),
    ]

    def test_serial_close_then_all_device_endpoints_structured(self):
        case = 'pty_api_serial_close_all_endpoints'
        r = self.client.post('/api/device/serial/close', json={})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'))
        self.assertFalse(r.json().get('open'))

        failures = []
        for url, payload in self.ENDPOINTS:
            try:
                rr = self.client.post(url, json=payload)
            except Exception as exc:
                failures.append('%s raised %s' % (url, exc))
                continue
            try:
                _assert_structured(self, rr, 409, 'SERIAL_NOT_OPEN')
            except AssertionError as exc:
                failures.append('%s: %s' % (url, exc))

        # status endpoints remain readable after close (no hang)
        for url in ('/api/device/status', '/api/device/ota/status',
                    '/api/device/file/status', '/api/device/xmodem/status',
                    '/api/device/http/status', '/api/device/serial/ports'):
            try:
                rr = self.client.get(url)
                if rr.status_code != 200:
                    failures.append('%s status=%s body=%s'
                                    % (url, rr.status_code, rr.text[:200]))
            except Exception as exc:
                failures.append('%s raised %s' % (url, exc))

        self._record(not failures, case, '; '.join(failures[:4]))

    def test_never_opened_serial_protocol_uid_sn_info_409(self):
        """未打开串口时 protocol/uid/sn/info 全部 409。"""
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        from app.main import create_app
        from fastapi.testclient import TestClient
        client = TestClient(create_app())

        cases = [
            ('/api/device/protocol/test', {}, 'protocol_test'),
            ('/api/device/uid/get', {}, 'uid_get'),
            ('/api/device/sn/write', {'orval': 0}, 'sn_write'),
            ('/api/device/info/get', {}, 'info_get'),
            ('/api/device/protocol/set_time', {'utc': 1}, 'set_time'),
        ]
        failures = []
        for url, payload, label in cases:
            try:
                rr = client.post(url, json=payload)
                _assert_structured(self, rr, 409, 'SERIAL_NOT_OPEN')
            except AssertionError as exc:
                failures.append('%s: %s' % (label, exc))
            except Exception as exc:
                failures.append('%s raised %s' % (label, exc))
        self._record(not failures, 'pty_api_never_opened_409',
                     '; '.join(failures[:4]))


# ---------------------------------------------------------------------------
# 2. OTA / Xmodem / file: missing / empty / busy / cancel → restart
# ---------------------------------------------------------------------------

class TestTransferEdgeLifecycle(_ApiEdgeBase):

    def test_ota_file_xmodem_missing_empty_structured(self):
        case = 'pty_api_transfer_missing_empty'
        empty = _write_temp(b'')
        missing = '/no/such/babyos_edge_fw_%d.bin' % int(time.time() * 1000)
        failures = []
        try:
            for url, path, want_status, want_code in (
                ('/api/device/ota/start', missing, 404, 'FILE_NOT_FOUND'),
                ('/api/device/file/start', missing, 404, 'FILE_NOT_FOUND'),
                ('/api/device/xmodem/start', missing, 404, 'FILE_NOT_FOUND'),
                ('/api/device/ymodem/start', missing, 404, 'FILE_NOT_FOUND'),
                ('/api/device/ota/start', empty, 400, 'INVALID_REQUEST'),
                ('/api/device/file/start', empty, 400, 'INVALID_REQUEST'),
                ('/api/device/xmodem/start', empty, 400, 'INVALID_REQUEST'),
                ('/api/device/ymodem/start', empty, 400, 'INVALID_REQUEST'),
            ):
                payload = {'path': path}
                if 'timeout' in url:
                    payload['timeout'] = 1.0
                rr = self.client.post(url, json=payload)
                try:
                    _assert_structured(self, rr, want_status, want_code)
                except AssertionError as exc:
                    failures.append('%s %s: %s' % (url, path, exc))
        finally:
            try:
                os.unlink(empty)
            except OSError:
                pass
        self._record(not failures, case, '; '.join(failures[:4]))

    def test_ota_busy_cancel_then_restart(self):
        """OTA busy → 409 TRANSFER_BUSY；cancel 后可再启动。"""
        case = 'pty_api_ota_busy_cancel_restart'
        path = _write_temp(_make_fw(1500, seed=41))
        failures = []
        try:
            # silent device keeps first OTA running
            self.link.device.drop_after_n_frames = 0
            self.link.device._frames_handled_limit = 0
            self.link.device.stop_responding_after(CMD_FW_INFO, 0)

            r1 = self.client.post('/api/device/ota/start',
                                  json={'path': path, 'timeout': 8.0})
            self.assertEqual(r1.status_code, 200, r1.text)
            job1 = r1.json().get('job_id')
            self.assertTrue(job1)

            _wait(lambda: (self.client.get(
                '/api/device/ota/status?job_id=%s' % job1).json()
                .get('transfer_active')), timeout=3.0)

            r2 = self.client.post('/api/device/ota/start',
                                  json={'path': path, 'timeout': 8.0})
            try:
                _assert_structured(self, r2, 409, 'TRANSFER_BUSY')
            except AssertionError as exc:
                failures.append('busy: %s' % exc)

            # file transfer also rejected while OTA active (shared busy flag)
            r2b = self.client.post('/api/device/file/start',
                                   json={'path': path, 'timeout': 8.0})
            try:
                _assert_structured(self, r2b, 409, 'TRANSFER_BUSY')
            except AssertionError as exc:
                failures.append('file busy: %s' % exc)

            # cancel / stop → structured cleanup
            rstop = self.client.post('/api/device/file/stop', json={})
            self.assertEqual(rstop.status_code, 200, rstop.text)
            body = _wait_job(self.client, job1, 'ota', timeout=6.0)
            job = body.get('job') or {}
            if job.get('state') not in ('cancelled', 'error'):
                failures.append('cancel state=%s body=%s'
                                % (job.get('state'), body))
            if body.get('transfer_active', True):
                failures.append('transfer_active still True after cancel')

            # device can respond again → new OTA accepted and completes
            self._restore_device_respond()
            r3 = self.client.post('/api/device/ota/start',
                                  json={'path': path, 'timeout': 10.0})
            self.assertEqual(r3.status_code, 200, r3.text)
            body3 = _wait_job(self.client, r3.json()['job_id'], 'ota',
                              timeout=12.0)
            job3 = body3.get('job') or {}
            if job3.get('state') != 'done':
                failures.append('restart state=%s body=%s'
                                % (job3.get('state'), body3))
            if not job3.get('ok'):
                failures.append('restart ok=False body=%s' % body3)
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass
        self._record(not failures, case, '; '.join(failures[:4]))

    def test_xmodem_ymodem_busy_cancel_then_restart(self):
        """Xmodem/Ymodem busy → 409；cancel 后可再启动。"""
        case = 'pty_api_xmodem_busy_cancel_restart'
        path = _write_temp(_make_fw(300, seed=44))
        failures = []
        try:
            # device silent in xmodem (never emits 'C')
            self.link.device.switch_to_protocol()
            self.link.device.xm_fault = {'send_start_byte': False}

            r1 = self.client.post('/api/device/xmodem/start',
                                  json={'path': path})
            self.assertEqual(r1.status_code, 200, r1.text)
            job1 = r1.json().get('job_id')
            self.assertTrue(job1)
            self.assertTrue(
                _wait(lambda: (self.client.get(
                    '/api/device/xmodem/status?job_id=%s&kind=xmodem'
                    % job1).json().get('active')), timeout=3.0))

            r2 = self.client.post('/api/device/xmodem/start',
                                  json={'path': path})
            try:
                _assert_structured(self, r2, 409, 'TRANSFER_BUSY')
            except AssertionError as exc:
                failures.append('xm busy: %s' % exc)

            rc = self.client.post('/api/device/xmodem/cancel', json={})
            self.assertEqual(rc.status_code, 200, rc.text)
            body = _wait_job(self.client, job1, 'xmodem', timeout=6.0)
            job = body.get('job') or {}
            if job.get('state') not in ('cancelled', 'error'):
                failures.append('xm cancel state=%s body=%s'
                                % (job.get('state'), body))
            if body.get('active', True):
                failures.append('xm active still True after cancel')

            # after cancel, new start is accepted (even if still silent)
            self.link.device.clear_faults()
            self.link.device.switch_to_xmodem()
            r3 = self.client.post('/api/device/xmodem/start',
                                  json={'path': path})
            self.assertEqual(r3.status_code, 200, r3.text)
            job3 = r3.json().get('job_id')
            rc2 = self.client.post('/api/device/xmodem/cancel', json={})
            self.assertEqual(rc2.status_code, 200, rc2.text)
            _wait_job(self.client, job3, 'xmodem', timeout=6.0)

            # ymodem busy/cancel
            self.link.device.ym_fault = {'send_start_byte': False}
            self.link.device.switch_to_protocol()
            r4 = self.client.post('/api/device/ymodem/start',
                                  json={'path': path})
            self.assertEqual(r4.status_code, 200, r4.text)
            job4 = r4.json().get('job_id')
            r5 = self.client.post('/api/device/ymodem/start',
                                  json={'path': path})
            try:
                _assert_structured(self, r5, 409, 'TRANSFER_BUSY')
            except AssertionError as exc:
                failures.append('ym busy: %s' % exc)
            rc3 = self.client.post('/api/device/ymodem/cancel', json={})
            self.assertEqual(rc3.status_code, 200, rc3.text)
            body4 = _wait_job(self.client, job4, 'ymodem', timeout=6.0)
            job4b = body4.get('job') or {}
            if job4b.get('state') not in ('cancelled', 'error'):
                failures.append('ym cancel state=%s body=%s'
                                % (job4b.get('state'), body4))
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass
        self._record(not failures, case, '; '.join(failures[:4]))


# ---------------------------------------------------------------------------
# 3. OTA async result-code failure → status.job structure complete
# ---------------------------------------------------------------------------

class TestOtaAsyncResultCodeJobStructure(_ApiEdgeBase):

    REQUIRED_JOB_KEYS = (
        'job_id', 'kind', 'state', 'progress', 'ok', 'result_code',
        'error', 'created_at', 'updated_at', 'finished_at',
        'path', 'name', 'timeout',
    )

    def _check_job_structure(self, body, expect_code, label):
        failures = []
        job = body.get('job')
        if not isinstance(job, dict) or not job:
            return ['%s: job missing/not dict: %r' % (label, body)]
        for key in self.REQUIRED_JOB_KEYS:
            if key not in job:
                failures.append('%s: job missing key %s' % (label, key))
        if job.get('kind') != 'ota':
            failures.append('%s: kind=%r' % (label, job.get('kind')))
        if job.get('state') != 'error':
            failures.append('%s: state=%r body=%s'
                            % (label, job.get('state'), body))
        if job.get('ok') is not False:
            failures.append('%s: ok=%r (expected False)' % (label, job.get('ok')))
        if job.get('result_code') != expect_code:
            failures.append('%s: result_code=%r expected %r'
                            % (label, job.get('result_code'), expect_code))
        err = str(job.get('error') or '').strip()
        if not err:
            failures.append('%s: error empty (job=%r)' % (label, job))
        if job.get('finished_at') is None:
            failures.append('%s: finished_at missing' % label)
        if body.get('transfer_active', True):
            failures.append('%s: transfer_active still True' % label)
        if body.get('uart_open') is not True:
            failures.append('%s: uart_open=%r' % (label, body.get('uart_open')))
        return failures

    def test_ota_failure_result_codes_job_structure(self):
        """设备异步 OTA_RESULT 失败码 → status.job 结构完整、不悬挂。"""
        case = 'pty_api_ota_result_code_job'
        path = _write_temp(_make_fw(400, seed=45))
        failures = []
        codes = (
            (OTA_RESULT_CRC_ERROR, 'CRC_ERROR'),
            (OTA_RESULT_NAME_MISMATCH, 'NAME_MISMATCH'),
            (OTA_RESULT_LEN_INVALID, 'LEN_INVALID'),
            (OTA_RESULT_TIMEOUT, 'TIMEOUT'),
        )
        try:
            # Device pump stays alive across OTA jobs — only flip the result
            # override. Do NOT stop/restart the mock: stop() closes the pty
            # endpoint and every later transfer would time out as result=4.
            for code, label in codes:
                self.link.device.ota_result_override = code
                r = self.client.post('/api/device/ota/start',
                                     json={'path': path, 'timeout': 8.0})
                self.assertEqual(r.status_code, 200, r.text)
                job_id = r.json().get('job_id')
                self.assertTrue(job_id, label)
                body = _wait_job(self.client, job_id, 'ota', timeout=10.0)
                failures.extend(self._check_job_structure(body, code, label))
                # status endpoint stays readable after failure
                rr = self.client.get('/api/device/ota/status?job_id=%s'
                                     % job_id)
                if rr.status_code != 200:
                    failures.append('%s: status after fail=%s' % (label, rr.status_code))
            self.link.device.ota_result_override = None
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass
            self.link.device.ota_result_override = None
        self._record(not failures, case, '; '.join(failures[:6]))


# ---------------------------------------------------------------------------
# 4. Shell / param illegal requests
# ---------------------------------------------------------------------------

class TestShellParamIllegalRequests(_ApiEdgeBase):

    def test_shell_param_illegal_structured(self):
        case = 'pty_api_shell_param_illegal'
        failures = []
        # device in shell mode so param paths exercise real shell client
        self.link.device.switch_to_shell()

        illegal = [
            ('/api/device/shell/cmd', {'cmd': ''}, 400, 'INVALID_REQUEST'),
            ('/api/device/shell/cmd', {'cmd': '   '}, 400, 'INVALID_REQUEST'),
            ('/api/device/param/get', {'name': ''}, 400, 'INVALID_REQUEST'),
            ('/api/device/param/get', {'name': '   '}, 400, 'INVALID_REQUEST'),
            ('/api/device/param/set', {'name': '', 'value': 1}, 400,
             'INVALID_REQUEST'),
            ('/api/device/param/set', {'name': '  ', 'value': 1}, 400,
             'INVALID_REQUEST'),
            ('/api/device/param/get', {'name': 'g_does_not_exist_xyz'},
             504, 'PARAM_NOT_FOUND'),
            ('/api/device/sn/write', {'uid_hex': 'zz-not-hex'},
             400, 'INVALID_REQUEST'),
            ('/api/device/sn/write', {'orval': 0}, 400, 'UID_NOT_AVAILABLE'),
            ('/api/device/serial/open', {'path': ''}, 400, 'INVALID_REQUEST'),
            ('/api/device/serial/open', {'path': '/no/such/port-xyz',
                                         'baud': 115200},
             500, 'PORT_OPEN_FAILED'),
        ]
        for url, payload, status, code in illegal:
            try:
                rr = self.client.post(url, json=payload)
                _assert_structured(self, rr, status, code)
            except AssertionError as exc:
                failures.append('%s %r: %s' % (url, payload, exc))
            except Exception as exc:
                failures.append('%s raised %s' % (url, exc))

        # param set unknown name → structured PARAM_SET_FAILED (not bare 500)
        rr = self.client.post('/api/device/param/set',
                              json={'name': 'g_missing_param_xyz',
                                    'value': 5})
        try:
            _assert_structured(self, rr, 500, 'PARAM_SET_FAILED')
        except AssertionError as exc:
            failures.append('param_set missing: %s' % exc)

        # still able to talk to device after illegal requests (shell mode)
        rr = self.client.post('/api/device/param/get',
                              json={'name': 'g_volume'})
        if rr.status_code != 200:
            failures.append('param_get after illegal: status=%s body=%s'
                            % (rr.status_code, rr.text[:200]))
        self._record(not failures, case, '; '.join(failures[:5]))


# ---------------------------------------------------------------------------
# 5. HTTP mock: duplicate start 409 / https origin/dev / cert mismatch
# ---------------------------------------------------------------------------

class TestHttpMockEdge(_ApiEdgeBase):
    """HTTP mock 边缘：重复 start、https 证书 pin、证书内容不匹配。"""

    def test_http_mock_duplicate_start_409(self):
        case = 'pty_api_http_mock_duplicate_start'
        failures = []
        r = self.client.post('/api/device/http/start', json={
            'port': 0, 'body': '{"ok":true}', 'status_code': 200,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        base_url = body.get('base_url') or ''
        self.assertTrue(base_url.startswith('http://'), base_url)

        r2 = self.client.post('/api/device/http/start', json={
            'port': 0, 'body': '{"ok":true}',
        })
        try:
            _assert_structured(self, r2, 409, 'HTTP_MOCK_RUNNING')
        except AssertionError as exc:
            failures.append('duplicate start: %s' % exc)

        # status endpoint still shows the running mock
        rr = self.client.get('/api/device/http/status')
        self.assertEqual(rr.status_code, 200, rr.text)
        self.assertTrue(rr.json().get('running'))

        # stop then start again is allowed
        rs = self.client.post('/api/device/http/stop', json={})
        self.assertEqual(rs.status_code, 200, rs.text)
        r3 = self.client.post('/api/device/http/start', json={
            'port': 0, 'body': '{"ok":true}',
        })
        self.assertEqual(r3.status_code, 200, r3.text)
        self.client.post('/api/device/http/stop', json={})
        self._record(not failures, case, '; '.join(failures[:3]))

    def test_http_mock_https_uses_origin_dev_certs(self):
        """https=true 证书必须是 origin/dev tool/mock_https_*.pem。"""
        case = 'pty_api_http_mock_https_origin_dev'
        failures = []
        repo_cert = os.path.join(_REPO, 'tool', 'mock_https_cert.pem')
        repo_key = os.path.join(_REPO, 'tool', 'mock_https_key.pem')
        pkg_cert = os.path.join(_PY_ROOT, 'device', 'certs',
                                'mock_https_cert.pem')
        pkg_key = os.path.join(_PY_ROOT, 'device', 'certs',
                               'mock_https_key.pem')

        def _sha(path):
            h = hashlib.sha256()
            with open(path, 'rb') as f:
                for chunk in iter(lambda: f.read(65536), b''):
                    h.update(chunk)
            return h.hexdigest()

        for path, want in ((repo_cert, DEV_CERT_SHA256),
                           (repo_key, DEV_KEY_SHA256),
                           (pkg_cert, DEV_CERT_SHA256),
                           (pkg_key, DEV_KEY_SHA256)):
            try:
                got = _sha(path)
                if got != want:
                    failures.append('%s sha=%s want=%s' % (path, got, want))
            except Exception as exc:
                failures.append('%s unreadable: %s' % (path, exc))

        r = self.client.post('/api/device/http/start', json={
            'https': True, 'port': 0,
            'body': '{"https":true,"src":"edge-api"}', 'status_code': 200,
        })
        try:
            self.assertEqual(r.status_code, 200, r.text)
            body = r.json()
            self.assertTrue(body.get('https'))
            status = body.get('status') or {}
            self.assertEqual(status.get('https_cert_sha256'), DEV_CERT_SHA256)
            self.assertEqual(status.get('https_key_sha256'), DEV_KEY_SHA256)
            cert_path = os.path.realpath(status.get('https_cert') or '')
            key_path = os.path.realpath(status.get('https_key') or '')
            allowed_certs = {
                os.path.realpath(repo_cert),
                os.path.realpath(pkg_cert),
            }
            allowed_keys = {
                os.path.realpath(repo_key),
                os.path.realpath(pkg_key),
            }
            if cert_path not in allowed_certs:
                failures.append('https cert path not origin/dev: %s' % cert_path)
            if key_path not in allowed_keys:
                failures.append('https key path not origin/dev: %s' % key_path)
            base_url = body.get('base_url') or ''
            if not base_url.startswith('https://'):
                failures.append('base_url not https: %s' % base_url)
        except AssertionError as exc:
            failures.append('https start: %s' % exc)

        # /http/status reports pinned cert content
        rr = self.client.get('/api/device/http/status')
        if rr.status_code == 200:
            st = rr.json()
            if not st.get('https'):
                failures.append('status.https not True')
            if st.get('https_cert_sha256') != DEV_CERT_SHA256:
                failures.append('status cert sha=%r' % st.get('https_cert_sha256'))
        else:
            failures.append('http/status=%s' % rr.status_code)

        self.client.post('/api/device/http/stop', json={})
        self._record(not failures, case, '; '.join(failures[:4]))

    def test_http_mock_https_cert_mismatch_structured(self):
        """证书内容不匹配 → start 失败结构化 HTTPS_CERT_MISMATCH。"""
        case = 'pty_api_http_mock_cert_mismatch'
        import device.http_mock as hm
        failures = []
        tmp_c = _write_temp(
            b'-----BEGIN CERTIFICATE-----\nNOT-ORIGIN-DEV\n'
            b'-----END CERTIFICATE-----\n', suffix='_bad.pem')
        tmp_k = _write_temp(
            b'-----BEGIN PRIVATE KEY-----\nNOT-ORIGIN-DEV\n'
            b'-----END PRIVATE KEY-----\n', suffix='_bad.key')
        old_pairs = hm._candidate_cert_pairs
        old_paths = hm._https_cert_paths
        try:
            hm._candidate_cert_pairs = lambda: [(tmp_c, tmp_k)]
            hm._https_cert_paths = None
            r = self.client.post('/api/device/http/start', json={
                'https': True, 'port': 0, 'body': '{"x":1}',
            })
            try:
                _assert_structured(self, r, 409, 'HTTPS_CERT_MISMATCH')
                msg = str(_detail_obj(r).get('detail') or '')
                low = msg.lower()
                if not any(k in low for k in
                           ('cert', 'mismatch', 'origin/dev', 'sha256')):
                    failures.append('msg missing cert keywords: %s' % msg)
            except AssertionError as exc:
                failures.append('cert mismatch: %s' % exc)
            # mock must NOT have started
            st = self.client.get('/api/device/http/status').json()
            if st.get('running'):
                failures.append('mock running despite cert mismatch')
        finally:
            hm._candidate_cert_pairs = old_pairs
            hm._https_cert_paths = old_paths
            for p in (tmp_c, tmp_k):
                try:
                    os.unlink(p)
                except OSError:
                    pass
        self._record(not failures, case, '; '.join(failures[:3]))


# ---------------------------------------------------------------------------
# 6. HTTP proxy: invalid URL / method → 400; downstream fail → 502
# ---------------------------------------------------------------------------

class TestHttpProxyEdge(_ApiEdgeBase):

    def test_http_proxy_invalid_url_method_structured(self):
        case = 'pty_api_http_proxy_invalid'
        failures = []
        illegal = [
            ({'url': ''}, 400, 'INVALID_REQUEST'),
            ({'url': 'not-a-url'}, 400, 'INVALID_REQUEST'),
            ({'url': 'ftp://example.invalid/x'}, 400, 'INVALID_REQUEST'),
            ({'url': 'http://'}, 400, 'INVALID_REQUEST'),
            ({'url': 'http://127.0.0.1:1/', 'method': 'FOO'},
             400, 'INVALID_REQUEST'),
            ({'url': 'http://127.0.0.1:1/', 'method': 'TRACE'},
             400, 'INVALID_REQUEST'),
        ]
        for payload, status, code in illegal:
            try:
                rr = self.client.post('/api/device/http/proxy', json=payload)
                _assert_structured(self, rr, status, code)
            except AssertionError as exc:
                failures.append('%r: %s' % (payload, exc))
            except Exception as exc:
                failures.append('%r raised %s' % (payload, exc))
        self._record(not failures, case, '; '.join(failures[:4]))

    def test_http_proxy_downstream_fail_502(self):
        """下游不可达 → 502 HTTP_PROXY_FAILED（结构化，不裸 500）。"""
        case = 'pty_api_http_proxy_downstream_502'
        failures = []
        # start mock, grab base_url, stop mock → proxy must fail structured
        r = self.client.post('/api/device/http/start', json={
            'port': 0, 'body': '{"ok":true}',
        })
        self.assertEqual(r.status_code, 200, r.text)
        base_url = (r.json().get('base_url') or '').rstrip('/')
        self.assertTrue(base_url, r.text)
        self.client.post('/api/device/http/stop', json={})

        try:
            rr = self.client.post('/api/device/http/proxy', json={
                'url': base_url + '/api/device/proxy-ping',
                'method': 'GET',
                'timeout': 2.0,
            })
            _assert_structured(self, rr, 502, 'HTTP_PROXY_FAILED')
        except AssertionError as exc:
            failures.append('stopped mock proxy: %s' % exc)
        except Exception as exc:
            failures.append('stopped mock proxy raised %s' % exc)

        # also: definitely-dead local port
        try:
            rr2 = self.client.post('/api/device/http/proxy', json={
                'url': 'http://127.0.0.1:9/',
                'method': 'GET',
                'timeout': 1.0,
            })
            _assert_structured(self, rr2, 502, 'HTTP_PROXY_FAILED')
        except AssertionError as exc:
            failures.append('dead port proxy: %s' % exc)
        except Exception as exc:
            failures.append('dead port proxy raised %s' % exc)

        # happy path still works after failures
        r2 = self.client.post('/api/device/http/start', json={
            'port': 0, 'body': '{"ok":true}',
        })
        self.assertEqual(r2.status_code, 200, r2.text)
        base2 = (r2.json().get('base_url') or '').rstrip('/')
        rr3 = self.client.post('/api/device/http/proxy', json={
            'url': base2 + '/ping', 'method': 'GET', 'timeout': 3.0,
        })
        if rr3.status_code != 200:
            failures.append('happy proxy status=%s body=%s'
                            % (rr3.status_code, rr3.text[:200]))
        self.client.post('/api/device/http/stop', json={})
        self._record(not failures, case, '; '.join(failures[:4]))


# ---------------------------------------------------------------------------
# 7. Device disconnect → status readable, no hang
# ---------------------------------------------------------------------------

class TestDeviceDisconnectStatusReadable(_ApiEdgeBase):

    def test_device_break_link_status_readable_no_hang(self):
        """设备断链后 status/ota/status 可读；协议命令结构化超时。"""
        case = 'pty_api_device_disconnect_status'
        path = _write_temp(_make_fw(1200, seed=43))
        failures = []
        try:
            # start OTA then break the device side mid-transfer
            r = self.client.post('/api/device/ota/start',
                                 json={'path': path, 'timeout': 3.0})
            self.assertEqual(r.status_code, 200, r.text)
            job_id = r.json().get('job_id')
            self.assertTrue(job_id)

            _wait(lambda: self.link.device.frames_handled >= 1, timeout=3.0)
            self.link.device.break_link_and_close_endpoint()

            body = _wait_job(self.client, job_id, 'ota', timeout=8.0)
            job = body.get('job') or {}
            if job.get('state') not in ('error', 'cancelled', 'done'):
                failures.append('job state after break=%s body=%s'
                                % (job.get('state'), body))

            # status endpoints must stay readable (bounded time, no hang)
            t0 = time.time()
            for url in ('/api/device/status',
                        '/api/device/ota/status?job_id=%s' % job_id,
                        '/api/device/ota/status',
                        '/api/device/xmodem/status',
                        '/api/device/http/status'):
                try:
                    rr = self.client.get(url)
                    if rr.status_code != 200:
                        failures.append('%s status=%s body=%s'
                                        % (url, rr.status_code,
                                           rr.text[:200]))
                except Exception as exc:
                    failures.append('%s raised %s' % (url, exc))
            elapsed = time.time() - t0
            if elapsed > 5.0:
                failures.append('status poll took %.2fs (hang?)' % elapsed)

            # protocol commands: structured error, never bare 500 / hang
            for url, payload in (
                ('/api/device/protocol/test', {}),
                ('/api/device/uid/get', {}),
                ('/api/device/info/get', {}),
            ):
                try:
                    t1 = time.time()
                    rr = self.client.post(url, json=payload)
                    dt = time.time() - t1
                    if rr.status_code == 200:
                        # device may still ACK before link fully dies — ok
                        pass
                    else:
                        detail = _detail_obj(rr)
                        if not isinstance(detail, dict) or not detail.get('code'):
                            failures.append(
                                '%s unstructured status=%s body=%s'
                                % (url, rr.status_code, rr.text[:200]))
                        if rr.status_code == 500 and detail and \
                                detail.get('code') == 'UNHANDLED':
                            failures.append('%s bare UNHANDLED 500' % url)
                    if dt > 6.0:
                        failures.append('%s took %.2fs (hang?)' % (url, dt))
                except Exception as exc:
                    failures.append('%s raised %s' % (url, exc))
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass
        self._record(not failures, case, '; '.join(failures[:5]))


# ---------------------------------------------------------------------------
# Memory-mock supplements (NOT the main path) — counted separately
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

    def test_protocol_client_timeout_returns_none_no_busy_wait(self):
        """SDK: silent peer → test_link returns None in bounded time."""
        case = 'mem_protocol_timeout_none'
        uart = _MemUart()
        pc = ProtocolClient(uart)
        t0 = time.time()
        resp = pc.test_link(timeout=0.3)
        dt = time.time() - t0
        ok = (resp is None and dt < 2.0)
        _rec_mem(ok, case,
                 'resp=%r dt=%.2f' % (resp, dt))
        self.assertTrue(ok)
        self.assertFalse(pc.transfer_active)

    def test_protocol_client_oversized_length_no_crash(self):
        case = 'mem_protocol_oversized_length'
        import device.b_protocol as bp
        uart = _MemUart()
        pc = ProtocolClient(uart)
        bad = bytearray(b'\xFE') + (0x1314).to_bytes(4, 'little') \
            + (0xFFFF).to_bytes(2, 'little') + b'\x01\x00\x00'
        frames = pc.feed_bytes(bytes(bad))
        good = bp.pack_frame(0xABCD, CMD_TEST, b'')
        frames2 = pc.feed_bytes(good)
        ok = (frames == [] and len(frames2) == 1 and frames2[0][1] == CMD_TEST)
        _rec_mem(ok, case, 'frames=%r frames2=%r' % (frames, frames2[:1]))
        self.assertTrue(ok)

    def test_stop_transfer_clears_state_structured_result(self):
        case = 'mem_stop_transfer_cleanup'
        uart = _MemUart()
        pc = ProtocolClient(uart)
        pc._xfer_busy = True
        pc._xfer_done = False
        pc._xfer_result = None
        pc.stop_transfer()
        st = pc.status()
        ok = (not pc.transfer_active
              and pc.transfer_result == OTA_RESULT_TIMEOUT
              and st.get('transfer_active') is False
              and st.get('transfer_result') == OTA_RESULT_TIMEOUT)
        _rec_mem(ok, case, 'st=%r' % st)
        self.assertTrue(ok)


# ---------------------------------------------------------------------------
# Runner / report
# ---------------------------------------------------------------------------

TEST_SCHEMA_ID = 'babyos_virtual_serial_edge_api_v1'


def _build_test_schema(result):
    failed = _PTY_FAIL + _MEM_FAIL
    total = _PTY_TOTAL + _MEM_TOTAL
    acceptance = (
        _PTY_FAIL == 0 and _PTY_TOTAL > 0
        and _MEM_FAIL == 0
        and result.wasSuccessful()
        and 'pty' in _CHANNEL_KINDS
    )
    return {
        'schema': TEST_SCHEMA_ID,
        'area': 'virtual-serial-edge-api',
        'channel_required': 'pty',
        'python': sys.version.split()[0],
        'protocol_host_id': '0x%X' % DEVICE_ID_HOST,
        'total': total,
        'passed': total - failed,
        'failed': failed,
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
        'channel_kinds': list(_CHANNEL_KINDS),
        'coverage': [
            'serial_closed_all_endpoints_structured',
            'never_opened_protocol_uid_sn_info_409',
            'transfer_missing_empty_busy_cancel_restart',
            'ota_async_result_code_job_structure',
            'shell_param_illegal_structured',
            'http_mock_duplicate_start_409',
            'http_mock_https_origin_dev_certs',
            'http_mock_cert_mismatch_structured',
            'http_proxy_invalid_url_method_400',
            'http_proxy_downstream_502',
            'device_disconnect_status_readable',
            'memory_mock_supplements_separate_count',
        ],
        'unittest': {
            'run': result.testsRun,
            'failures': len(result.failures),
            'errors': len(result.errors),
            'skipped': len(result.skipped),
        },
        'errors': [
            {'case': c, 'detail': d} for c, d in _ERRORS[:20]
        ],
        'acceptance': acceptance,
        'notes': (
            'Main acceptance path is kind=pty only. Memory-mock cases are '
            'classified separately and never counted as pty results. '
            'HTTPS certs are origin/dev tool/ files — no runtime openssl. '
            'API errors must be structured AppError (detail.code + detail).'
        ),
    }


def _print_schema(schema):
    print('')
    print('==== edge/api coverage (pty primary) ====')
    print('pty total/pass/fail: %d/%d/%d' % (
        schema['pty']['total'], schema['pty']['pass'], schema['pty']['fail']))
    print('  cases: %s' % (schema['pty']['cases'],))
    print('memory_mock total/pass/fail: %d/%d/%d (separate count)' % (
        schema['memory_mock']['total'], schema['memory_mock']['pass'],
        schema['memory_mock']['fail']))
    print('  cases: %s' % (schema['memory_mock']['cases'],))
    print('channel_kinds=%s python=%s host_id=%s' % (
        schema['channel_kinds'], schema['python'], schema['protocol_host_id']))
    print('total=%d passed=%d failed=%d acceptance=%s' % (
        schema['total'], schema['passed'], schema['failed'],
        schema['acceptance']))
    if schema['errors']:
        print('errors:')
        for e in schema['errors']:
            print('  - %s: %s' % (e['case'], e['detail']))


def main():
    print('BabyOS Studio virtual-serial edge/API acceptance (pty)')
    print('python: %s' % sys.version.split()[0])
    print('protocol host id: 0x%X' % DEVICE_ID_HOST)
    print('repo: %s' % _REPO)

    loader = unittest.defaultTestLoader
    suite = unittest.TestSuite()
    for cls in (
        TestSerialClosedStructuredErrors,
        TestTransferEdgeLifecycle,
        TestOtaAsyncResultCodeJobStructure,
        TestShellParamIllegalRequests,
        TestHttpMockEdge,
        TestHttpProxyEdge,
        TestDeviceDisconnectStatusReadable,
        TestMemoryMockSupplements,
    ):
        suite.addTests(loader.loadTestsFromTestCase(cls))
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    schema = _build_test_schema(result)
    _print_schema(schema)
    print('')
    print('==== TEST_SCHEMA ====')
    print(json.dumps(schema, indent=2, ensure_ascii=False))

    ok = schema['acceptance'] and schema['failed'] == 0
    print('')
    print('edge/api virtual-serial acceptance: %s'
          % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
