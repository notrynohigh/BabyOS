#!/usr/bin/env python3
"""
test_product_device_ops — product-grade device-ops acceptance for BabyOS Studio.

Covers real end-to-end paths over pty + FastAPI TestClient (no UI shells,
no protocol stubs):

  1. Param polling FULL cycle:
       POST /param/poll/start  → mock shell returns param values
       GET  /param/poll/status → last_value reflects device shell
       change device param     → last_value updates (repeated gets)
       POST /param/poll/stop   → no further updates after stop
  2. Log-to-file start / stop / status + real file content written.
  3. Job registry:
       DeviceManager.reset() clears jobs;
       /ota/status and /file/status report uart_open + job correctly
       after reset (job=None, uart_open=False).
  4. DeviceManager.status() aggregate shape via GET /api/device/status.

Authority:
  - python/device/device_manager.py (param polling, log-to-file, jobs, status)
  - python/app/api/device.py (FastAPI routes)
  - test/device_features/mock_babyos_device.py (shell text b_mod_param.c)
  - bos/modules/b_mod_param.c

Hard rules:
  - Serial/protocol MUST use open_api_over_pty + MockBabyOSDevice
    (real byte I/O on pty). No FakeUart on the success path.
  - Python 3.8 compatible. Do not modify bos/thirdparty.

Run:
  cd tool/babyos-studio/python && \
      .venv/bin/python -m pytest ../test/device_features/test_product_device_ops.py -v
"""

from __future__ import print_function

import os
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

from mock_babyos_device import (  # noqa: E402
    open_api_over_pty,
    DEFAULT_SHELL_PARAMS,
)

MOCK_DEV_ID = 0x0000ABCD
PARAM_NAME = 'g_param_test_val'
PARAM_DEFAULT = DEFAULT_SHELL_PARAMS[PARAM_NAME]

# Aggregate status() keys that must always be present.
STATUS_KEYS = (
    'uart', 'protocol', 'shell', 'http_mock', 'xmodem',
    'param_polling', 'log_to_file', 'webconfig',
)


def _wait(pred, timeout=5.0, interval=0.02):
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


def _detail_code(r):
    try:
        detail = r.json().get('detail')
        if isinstance(detail, dict):
            return detail.get('code')
    except Exception:
        pass
    return None


def _assert_structured_error(testcase, r, status, code):
    testcase.assertEqual(r.status_code, status,
                         'status=%s body=%s' % (r.status_code, r.text))
    detail = r.json().get('detail')
    testcase.assertIsInstance(detail, dict,
                             'AppError must return structured detail, got %r'
                             % (detail,))
    testcase.assertEqual(detail.get('code'), code, r.text)
    testcase.assertTrue(str(detail.get('detail') or '').strip(),
                        'detail.detail must be non-empty: %r' % (detail,))


def _make_fw(n: int, seed: int = 3) -> bytes:
    return bytes(bytearray(((i * 7) + seed) & 0xFF for i in range(n)))


def _write_temp(data: bytes, suffix: str = '.bin') -> str:
    fd, path = tempfile.mkstemp(suffix=suffix, prefix='babyos_prod_ops_')
    try:
        os.write(fd, data)
    finally:
        os.close(fd)
    return path


class _PtyApiBase(unittest.TestCase):
    """Shared FastAPI TestClient + real pty + MockBabyOSDevice."""

    def setUp(self):
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

        self.logs = []
        self.link = open_api_over_pty(
            encrypt=False,
            open_host_uart=False,
            bind_device_manager=False,
            device_kwargs={
                'device_id': MOCK_DEV_ID,
                'encrypt': False,
                'log_fn': self.logs.append,
            },
            require_pty=True,
        )
        self.addCleanup(self.link.close)
        self.assertEqual(self.link.kind, 'pty',
                         'product ops tests require real pty')
        self.device = self.link.device

        from app.main import create_app
        from fastapi.testclient import TestClient
        self.app = create_app()
        self.client = TestClient(self.app)

        r = self.client.post('/api/device/serial/open', json={
            'path': self.link.host_port,
            'baud': 115200,
            'encrypt': False,
        })
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'))

        from device.device_manager import get_device_manager
        self.dm = get_device_manager()
        self.assertTrue(self.dm.is_open())
        self.assertEqual(self.dm.uart.port, self.link.host_port)

    def _poll_status(self):
        r = self.client.get('/api/device/param/poll/status')
        self.assertEqual(r.status_code, 200, r.text)
        return r.json()


# ---------------------------------------------------------------------------
# 1. Param polling FULL cycle over pty + FastAPI
# ---------------------------------------------------------------------------

class TestParamPollingFullCycle(_PtyApiBase):

    def test_full_cycle_start_get_update_stop(self):
        # Device mock must be in shell mode for b_mod_param text protocol.
        self.device.switch_to_shell(params={
            PARAM_NAME: PARAM_DEFAULT,
            'g_param_test_val2': -999,
        })

        # Idle status before start
        st0 = self._poll_status()
        self.assertFalse(st0.get('enabled'))

        # START
        r = self.client.post('/api/device/param/poll/start', json={
            'name': PARAM_NAME,
            'interval_ms': 200,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        st = body.get('status') or {}
        self.assertTrue(st.get('enabled'))
        self.assertEqual(st.get('name'), PARAM_NAME)
        self.assertGreaterEqual(int(st.get('interval_ms') or 0), 100)
        self.assertTrue(st.get('uart_open'))

        # Repeated gets → last_value appears (real shell over pty)
        ok = _wait(lambda: self._poll_status().get('last_value') == PARAM_DEFAULT,
                   timeout=5.0)
        self.assertTrue(ok,
                        'polling must read %s=%s via pty shell, got %r'
                        % (PARAM_NAME, PARAM_DEFAULT,
                           self._poll_status().get('last_value')))
        st1 = self._poll_status()
        self.assertEqual(st1['last_value'], PARAM_DEFAULT)
        self.assertIsNotNone(st1.get('last_at'))
        self.assertEqual(st1.get('error'), '')
        self.assertEqual(st1.get('name'), PARAM_NAME)
        self.assertTrue(st1.get('enabled'))

        # Mock device must have received real shell text commands
        shell_cmds = [line for line, _resp in self.device.shell_log]
        self.assertTrue(any(c.startswith('param %s' % PARAM_NAME)
                            for c in shell_cmds),
                        'mock shell must see "param %s", got %r'
                        % (PARAM_NAME, shell_cmds[-8:]))

        # Change device-side param → poller must pick up the new value
        new_val = 4242
        with self.device._lock:
            self.device.shell_params[PARAM_NAME] = new_val
        ok = _wait(lambda: self._poll_status().get('last_value') == new_val,
                   timeout=5.0)
        self.assertTrue(ok,
                        'last_value must update to %d after device change, got %r'
                        % (new_val, self._poll_status().get('last_value')))

        # STOP
        r = self.client.post('/api/device/param/poll/stop')
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertTrue(body.get('was_enabled'))
        st2 = body.get('status') or {}
        self.assertFalse(st2.get('enabled'))
        frozen = st2.get('last_value')
        self.assertEqual(frozen, new_val)
        frozen_at = st2.get('last_at')
        self.assertIsNotNone(frozen_at)

        # After stop: no further updates even if device param changes
        with self.device._lock:
            self.device.shell_params[PARAM_NAME] = 99999
        time.sleep(0.6)  # > 3 poll intervals
        st3 = self._poll_status()
        self.assertFalse(st3.get('enabled'))
        self.assertEqual(st3.get('last_value'), frozen,
                         'last_value must freeze after stop')
        self.assertEqual(st3.get('last_at'), frozen_at,
                         'last_at must freeze after stop')

        # Stop is idempotent when already idle
        r = self.client.post('/api/device/param/poll/stop')
        self.assertEqual(r.status_code, 200, r.text)
        self.assertFalse(r.json().get('was_enabled'))

    def test_poll_start_structured_errors(self):
        # blank name → 400
        r = self.client.post('/api/device/param/poll/start', json={
            'name': '   ', 'interval_ms': 200,
        })
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

        # interval below ge=100 → pydantic 422 (not AppError)
        r = self.client.post('/api/device/param/poll/start', json={
            'name': PARAM_NAME, 'interval_ms': 10,
        })
        self.assertGreaterEqual(r.status_code, 400, r.text)

    def test_poll_start_requires_uart(self):
        r = self.client.post('/api/device/serial/close', json={})
        self.assertEqual(r.status_code, 200, r.text)
        r = self.client.post('/api/device/param/poll/start', json={
            'name': PARAM_NAME, 'interval_ms': 200,
        })
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_poll_restart_switches_param(self):
        """Starting again while enabled must stop the previous poller."""
        self.device.switch_to_shell(params={
            PARAM_NAME: PARAM_DEFAULT,
            'g_other': 1,
        })
        r = self.client.post('/api/device/param/poll/start', json={
            'name': PARAM_NAME, 'interval_ms': 200,
        })
        self.assertEqual(r.status_code, 200, r.text)
        ok = _wait(lambda: self._poll_status().get('last_value') == PARAM_DEFAULT,
                   timeout=5.0)
        self.assertTrue(ok)

        # Restart on a different param
        r = self.client.post('/api/device/param/poll/start', json={
            'name': 'g_other', 'interval_ms': 200,
        })
        self.assertEqual(r.status_code, 200, r.text)
        st = r.json().get('status') or {}
        self.assertEqual(st.get('name'), 'g_other')
        self.assertTrue(st.get('enabled'))
        ok = _wait(lambda: self._poll_status().get('last_value') == 1,
                   timeout=5.0)
        self.assertTrue(ok, 'restart must poll the new param name')

        r = self.client.post('/api/device/param/poll/stop')
        self.assertEqual(r.status_code, 200, r.text)


# ---------------------------------------------------------------------------
# 2. Log-to-file start / stop / status + content written
# ---------------------------------------------------------------------------

class TestLogFileProductOps(_PtyApiBase):

    def test_log_start_stop_status_and_content(self):
        td = tempfile.mkdtemp(prefix='babyos_prod_log_')
        path = os.path.join(td, 'device_ops.log')

        # status before start
        st0 = self.client.get('/api/device/log/status')
        self.assertEqual(st0.status_code, 200, st0.text)
        self.assertFalse(st0.json().get('enabled'))

        # START
        r = self.client.post('/api/device/log/start', json={'path': path})
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('path'), path)

        st = self.client.get('/api/device/log/status').json()
        self.assertTrue(st.get('enabled'))
        self.assertEqual(st.get('path'), path)

        # Generate real DeviceManager log lines via protocol over pty
        r = self.client.post('/api/device/protocol/test', json={})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'))

        r = self.client.post('/api/device/uid/get', json={})
        self.assertEqual(r.status_code, 200, r.text)

        # File must exist and contain the start marker + protocol activity
        self.assertTrue(os.path.isfile(path), 'log file must be written')
        with open(path, 'r', encoding='utf-8') as f:
            text = f.read()
        self.assertIn('log-to-file started:', text)
        self.assertIn(path, text)
        # at least one of the real activity markers
        self.assertTrue(
            any(k in text for k in (
                'open_port', 'clients bound', 'cmd:', 'test_link',
                'protocol', 'uid', 'UID', 'get_uid')),
            'log file must capture real DeviceManager activity, got:\n%s'
            % text[-500:])

        # marker written while logging is active must land on disk
        self.dm._log('PROD_OPS_LOG_MARKER_4242')
        ok = _wait(lambda: 'PROD_OPS_LOG_MARKER_4242' in
                   open(path, 'r', encoding='utf-8').read(),
                   timeout=2.0)
        self.assertTrue(ok, 'active _log line must flush to disk')

        # STOP
        r = self.client.post('/api/device/log/stop')
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertTrue((body.get('was') or {}).get('enabled'))
        self.assertFalse((body.get('status') or {}).get('enabled'))

        st = self.client.get('/api/device/log/status').json()
        self.assertFalse(st.get('enabled'))
        self.assertEqual(st.get('path'), '')

        # After stop, new _log lines must NOT append
        size_before = os.path.getsize(path)
        self.dm._log('PROD_OPS_AFTER_STOP_MARKER')
        time.sleep(0.15)
        with open(path, 'r', encoding='utf-8') as f:
            after = f.read()
        self.assertNotIn('PROD_OPS_AFTER_STOP_MARKER', after)
        self.assertEqual(os.path.getsize(path), size_before,
                         'log file size must not grow after stop')

    def test_log_start_structured_errors(self):
        # empty path
        r = self.client.post('/api/device/log/start', json={'path': ''})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

        # missing directory
        r = self.client.post('/api/device/log/start', json={
            'path': '/no/such/dir/babyos.log',
        })
        _assert_structured_error(self, r, 400, 'LOG_START_FAILED')

    def test_log_restart_switches_file(self):
        td = tempfile.mkdtemp(prefix='babyos_prod_log2_')
        p1 = os.path.join(td, 'first.log')
        p2 = os.path.join(td, 'second.log')

        r = self.client.post('/api/device/log/start', json={'path': p1})
        self.assertEqual(r.status_code, 200, r.text)
        self.dm._log('FIRST_FILE_MARKER')
        r = self.client.post('/api/device/log/start', json={'path': p2})
        self.assertEqual(r.status_code, 200, r.text)
        self.dm._log('SECOND_FILE_MARKER')
        time.sleep(0.1)

        with open(p2, 'r', encoding='utf-8') as f:
            t2 = f.read()
        self.assertIn('SECOND_FILE_MARKER', t2)
        self.assertIn('log-to-file started:', t2)

        r = self.client.post('/api/device/log/stop')
        self.assertEqual(r.status_code, 200, r.text)


# ---------------------------------------------------------------------------
# 3. Job registry: reset clears jobs; ota/file status after reset
# ---------------------------------------------------------------------------

class TestJobRegistryReset(_PtyApiBase):

    def _wait_job(self, kind, job_id, timeout=15.0):
        url = {
            'ota': '/api/device/ota/status?job_id=%s',
            'file': '/api/device/file/status?job_id=%s',
        }[kind] % job_id
        deadline = time.time() + timeout
        body = {}
        while time.time() < deadline:
            r = self.client.get(url)
            if r.status_code == 200:
                body = r.json()
                job = body.get('job') or {}
                if job.get('state') in ('done', 'error', 'cancelled'):
                    return body
            time.sleep(0.03)
        return body

    def test_ota_job_then_reset_clears_registry(self):
        payload = _make_fw(400, seed=9)
        path = _write_temp(payload, suffix='.bin')
        self.addCleanup(os.unlink, path)

        # START OTA over real pty (mock device handles 0x2/0x4/0x5)
        r = self.client.post('/api/device/ota/start', json={
            'path': path, 'name': 'prod_ops_fw.bin', 'timeout': 10.0,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('accepted'))
        job_id = body.get('job_id')
        self.assertTrue(job_id)
        self.assertIn('/api/device/ota/status', body.get('status_url') or '')

        # Job visible while running / after done
        st = self._wait_job('ota', job_id, timeout=15.0)
        job = st.get('job') or {}
        self.assertEqual(job.get('state'), 'done',
                         'OTA over pty must complete, got %r' % job)
        self.assertTrue(job.get('ok'))
        self.assertEqual(job.get('progress'), 100)
        self.assertTrue(st.get('uart_open'),
                        'ota/status must report uart_open=True while open')
        self.assertEqual(st.get('kind'), 'ota')
        self.assertIsNotNone(st.get('transfer_result'))

        # Device actually received the firmware bytes
        self.assertIsNotNone(self.device.fw_info)
        self.assertEqual(bytes(self.device.received_fw_bytes[:len(payload)]),
                         payload)

        # DeviceManager singleton holds the job
        self.assertIsNotNone(self.dm.job_get(job_id))
        self.assertEqual(len(self.dm.jobs), 1)

        # Snapshot pre-reset API shape
        st_before = self.client.get('/api/device/ota/status').json()
        self.assertTrue(st_before.get('uart_open'))
        self.assertIsNotNone(st_before.get('job'))

        # RESET — clears singleton + jobs + closes uart
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

        from device.device_manager import get_device_manager
        dm2 = get_device_manager()
        self.assertIsNot(dm2, self.dm,
                         'reset must create a new DeviceManager instance')
        self.assertEqual(dm2.jobs, {},
                         'DeviceManager.reset() must clear the job registry')
        self.assertFalse(dm2.is_open())

        # ota/status after reset: uart_open=False, job=None
        st_after = self.client.get('/api/device/ota/status').json()
        self.assertFalse(st_after.get('uart_open'),
                         'after reset ota/status must report uart_open=False')
        self.assertIsNone(st_after.get('job'),
                          'after reset ota/status job must be None')
        self.assertEqual(st_after.get('kind'), 'ota')
        self.assertFalse(st_after.get('transfer_active'))

        # file/status after reset also clean
        st_file = self.client.get('/api/device/file/status').json()
        self.assertFalse(st_file.get('uart_open'))
        self.assertIsNone(st_file.get('job'))
        self.assertEqual(st_file.get('kind'), 'file')

        # Looking up the old job_id after reset → None
        self.assertIsNone(dm2.job_get(job_id))

        # Direct latest_job also empty
        self.assertIsNone(dm2.latest_job('ota'))
        self.assertIsNone(dm2.latest_job('file'))

    def test_file_job_then_reset_clears_registry(self):
        data = b'PROD-OPS-FILE-CHAIN-' * 20
        path = _write_temp(data, suffix='.bin')
        self.addCleanup(os.unlink, path)

        r = self.client.post('/api/device/file/start', json={
            'path': path, 'dev_no': 0, 'offset': 0, 'timeout': 10.0,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('accepted'))
        job_id = body.get('job_id')
        self.assertTrue(job_id)

        st = self._wait_job('file', job_id, timeout=15.0)
        job = st.get('job') or {}
        self.assertEqual(job.get('state'), 'done',
                         'file transfer over pty must complete, got %r' % job)
        self.assertTrue(job.get('ok'))
        self.assertTrue(st.get('uart_open'))

        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

        from device.device_manager import get_device_manager
        dm2 = get_device_manager()
        self.assertEqual(dm2.jobs, {})

        st_after = self.client.get('/api/device/file/status').json()
        self.assertFalse(st_after.get('uart_open'))
        self.assertIsNone(st_after.get('job'))

    def test_clear_jobs_isolated_from_status_endpoints(self):
        """job_put/clear_jobs work without uart; status still reports closed."""
        from device.device_manager import DeviceManager
        dm = self.dm
        dm.job_put('manual-1', {
            'job_id': 'manual-1', 'kind': 'ota', 'state': 'starting',
            'created_at': time.time(),
        })
        self.assertIsNotNone(dm.job_get('manual-1'))
        dm.clear_jobs()
        self.assertEqual(dm.jobs, {})
        self.assertIsNone(dm.job_get('manual-1'))

        # status endpoints remain functional (uart still open in this fixture)
        st = self.client.get('/api/device/ota/status').json()
        self.assertTrue(st.get('uart_open'))
        self.assertIsNone(st.get('job'))


# ---------------------------------------------------------------------------
# 4. DeviceManager.status() aggregate shape
# ---------------------------------------------------------------------------

class TestStatusAggregateShape(_PtyApiBase):

    def test_status_shape_over_pty_open(self):
        r = self.client.get('/api/device/status')
        self.assertEqual(r.status_code, 200, r.text)
        st = r.json()
        self.assertIsInstance(st, dict)

        for key in STATUS_KEYS:
            self.assertIn(key, st, 'status() missing key %r' % key)

        # uart
        uart = st['uart']
        self.assertIsInstance(uart, dict)
        self.assertTrue(uart.get('open'))
        self.assertEqual(uart.get('port'), self.link.host_port)
        self.assertEqual(uart.get('baudrate'), 115200)
        self.assertIsInstance(uart.get('available'), list)

        # protocol (bound after serial open)
        proto = st['protocol']
        self.assertIsInstance(proto, dict)
        self.assertIn('host_id', proto)
        self.assertIn('encrypt', proto)
        self.assertIn('transfer_active', proto)
        self.assertIn('transfer_result', proto)
        self.assertIn('tx_frames', proto)
        self.assertIn('rx_frames', proto)
        self.assertFalse(proto.get('transfer_active'))

        # shell
        shell = st['shell']
        self.assertIsInstance(shell, dict)
        self.assertIn('commands_sent', shell)
        self.assertIn('line_ending', shell)

        # http_mock
        hm = st['http_mock']
        self.assertIsInstance(hm, dict)
        self.assertIn('running', hm)
        self.assertFalse(hm.get('running'))

        # xmodem
        xm = st['xmodem']
        self.assertIsInstance(xm, dict)
        self.assertIn('active', xm)
        self.assertIn('xmodem_file', xm)
        self.assertIn('ymodem_file', xm)
        self.assertFalse(xm.get('active'))

        # param_polling
        pp = st['param_polling']
        self.assertIsInstance(pp, dict)
        for k in ('enabled', 'name', 'interval_ms', 'last_value',
                  'last_at', 'error', 'uart_open'):
            self.assertIn(k, pp, 'param_polling missing %r' % k)
        self.assertFalse(pp.get('enabled'))
        self.assertTrue(pp.get('uart_open'))

        # log_to_file
        lf = st['log_to_file']
        self.assertIsInstance(lf, dict)
        self.assertIn('enabled', lf)
        self.assertIn('path', lf)
        self.assertFalse(lf.get('enabled'))

        # webconfig
        wc = st['webconfig']
        self.assertIsInstance(wc, dict)
        self.assertTrue(wc, 'webconfig.status() must return a non-empty dict')

        # caches
        self.assertIn('last_uid_hex', st)
        self.assertIn('last_devinfo', st)
        self.assertIn('last_param_list', st)
        self.assertIn('last_netinfo', st)
        self.assertIn('last_http_resp', st)

    def test_status_reflects_param_polling_and_log_to_file(self):
        """status() must surface live param-poll + log-to-file state."""
        self.device.switch_to_shell(params={PARAM_NAME: PARAM_DEFAULT})

        r = self.client.post('/api/device/param/poll/start', json={
            'name': PARAM_NAME, 'interval_ms': 200,
        })
        self.assertEqual(r.status_code, 200, r.text)

        td = tempfile.mkdtemp(prefix='babyos_prod_status_log_')
        path = os.path.join(td, 'status_ops.log')
        r = self.client.post('/api/device/log/start', json={'path': path})
        self.assertEqual(r.status_code, 200, r.text)

        ok = _wait(lambda: self.client.get('/api/device/status').json()
                   .get('param_polling', {}).get('last_value') == PARAM_DEFAULT,
                   timeout=5.0)
        self.assertTrue(ok)

        st = self.client.get('/api/device/status').json()
        self.assertTrue(st['param_polling']['enabled'])
        self.assertEqual(st['param_polling']['name'], PARAM_NAME)
        self.assertEqual(st['param_polling']['last_value'], PARAM_DEFAULT)
        self.assertTrue(st['log_to_file']['enabled'])
        self.assertEqual(st['log_to_file']['path'], path)

        # stop both; status must follow
        r = self.client.post('/api/device/param/poll/stop')
        self.assertEqual(r.status_code, 200, r.text)
        r = self.client.post('/api/device/log/stop')
        self.assertEqual(r.status_code, 200, r.text)

        st = self.client.get('/api/device/status').json()
        self.assertFalse(st['param_polling']['enabled'])
        self.assertFalse(st['log_to_file']['enabled'])
        self.assertEqual(st['log_to_file']['path'], '')

    def test_status_after_serial_close(self):
        r = self.client.post('/api/device/serial/close', json={})
        self.assertEqual(r.status_code, 200, r.text)

        st = self.client.get('/api/device/status').json()
        for key in STATUS_KEYS:
            self.assertIn(key, st)
        self.assertFalse(st['uart']['open'])
        self.assertEqual(st['uart']['port'], '')
        # protocol/shell clients may still be non-None but uart is closed
        self.assertFalse(st['param_polling']['uart_open'])
        self.assertFalse(st['log_to_file']['enabled'])

    def test_status_after_reset_shape_still_valid(self):
        """After DeviceManager.reset(), aggregate status still returns full shape."""
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

        st = self.client.get('/api/device/status').json()
        for key in STATUS_KEYS:
            self.assertIn(key, st, 'post-reset status missing %r' % key)
        self.assertFalse(st['uart']['open'])
        self.assertFalse(st['param_polling']['enabled'])
        self.assertFalse(st['log_to_file']['enabled'])
        self.assertEqual(st['uart']['available'],
                         self.client.get('/api/device/status').json()
                         ['uart']['available'])


# ---------------------------------------------------------------------------
# Combined product path: param poll + log + status in one session
# ---------------------------------------------------------------------------

class TestCombinedProductSession(_PtyApiBase):

    def test_poll_log_status_lifecycle(self):
        """One session: protocol logs → shell poll+log → status → stop all."""
        # Phase 1 — binary protocol mode (default): produce real protocol logs.
        r = self.client.post('/api/device/protocol/test', json={})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'))

        td = tempfile.mkdtemp(prefix='babyos_prod_combo_')
        log_path = os.path.join(td, 'combo.log')
        r = self.client.post('/api/device/log/start', json={'path': log_path})
        self.assertEqual(r.status_code, 200, r.text)

        # Phase 2 — switch mock to shell mode (b_mod_param) and start polling.
        # NOTE: the device UART is single-mode — binary protocol and shell
        # text cannot run concurrently on the same pty. This matches firmware
        # (nr_micro_shell / b_mod_protocol share one UART mode).
        self.device.switch_to_shell(params={
            PARAM_NAME: PARAM_DEFAULT,
            'g_volume': 50,
        })

        r = self.client.post('/api/device/param/poll/start', json={
            'name': PARAM_NAME, 'interval_ms': 200,
        })
        self.assertEqual(r.status_code, 200, r.text)

        ok = _wait(lambda: self.client.get('/api/device/param/poll/status')
                   .json().get('last_value') == PARAM_DEFAULT, timeout=5.0)
        self.assertTrue(ok)

        st = self.client.get('/api/device/status').json()
        self.assertTrue(st['param_polling']['enabled'])
        self.assertTrue(st['log_to_file']['enabled'])
        self.assertTrue(st['uart']['open'])

        # Log file must capture real session activity (start marker + poll start)
        with open(log_path, 'r', encoding='utf-8') as f:
            text = f.read()
        self.assertIn('log-to-file started:', text)
        self.assertIn(log_path, text)
        self.assertTrue(
            any(k in text for k in (
                'param polling start', 'clients bound', 'open_port',
                'log-to-file started')),
            'log must capture real DeviceManager activity, got:\n%s'
            % text[-500:])

        # Active _log while poll is running still flushes to disk
        self.dm._log('COMBO_POLL_ACTIVE_MARKER')
        ok = _wait(lambda: 'COMBO_POLL_ACTIVE_MARKER' in
                   open(log_path, 'r', encoding='utf-8').read(),
                   timeout=2.0)
        self.assertTrue(ok)

        r = self.client.post('/api/device/param/poll/stop')
        self.assertEqual(r.status_code, 200, r.text)
        r = self.client.post('/api/device/log/stop')
        self.assertEqual(r.status_code, 200, r.text)

        st = self.client.get('/api/device/status').json()
        self.assertFalse(st['param_polling']['enabled'])
        self.assertFalse(st['log_to_file']['enabled'])

        # shell param set still works after poll stop (same pty, shell mode)
        r = self.client.post('/api/device/param/set', json={
            'name': 'g_volume', 'value': 88, 'verify': True,
        })
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'))
        self.assertEqual(self.device.shell_params.get('g_volume'), 88)


def main():
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    print('tests=%d failures=%d errors=%d' % (
        result.testsRun, len(result.failures), len(result.errors)))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
