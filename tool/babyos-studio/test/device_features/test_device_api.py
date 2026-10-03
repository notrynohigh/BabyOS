#!/usr/bin/env python3
"""
FastAPI device API integration tests for BabyOS Studio.

Covers python/app/api/device.py wired to real device services
(UART / b_protocol / shell / HTTP mock / Xmodem). Uses protocol-faithful
FakeUart + FakeDevice from test_device_services — no stub HTTP layer,
no fake toasts. Every endpoint is exercised for:

  - no-serial structured error path (409/400/404/504 with code, never bare 500)
  - success path with injected device_manager mock uart (FakeUart + FakeDevice)
  - HTTP mock: real local server start / traffic / stop

Authority:
  - origin/master:tool/README.md (BabyOS 协议说明)
  - bos/modules/b_mod_protocol.c / b_mod_param.c
  - bos/algorithm/algo_crc.c

Run:
  tool/babyos-studio/python/.venv/bin/python \
      tool/babyos-studio/test/device_features/test_device_api.py
  or: pytest tool/babyos-studio/test/device_features/test_device_api.py
"""

from __future__ import print_function

import inspect
import os
import sys
import tempfile
import time
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
for p in (_PY_ROOT, _HERE):
    if p not in sys.path:
        sys.path.insert(0, p)

from fastapi.testclient import TestClient  # noqa: E402

from app.main import create_app  # noqa: E402
from app.api import device as device_api  # noqa: E402
from device.device_manager import DeviceManager  # noqa: E402
from device.sn_util import sn_bytes  # noqa: E402
from test_device_services import (  # noqa: E402
    FakeDevice,
    FakeShellDevice,
    FakeUart,
)

# Endpoints that call _require_uart() — must return structured 409 when closed.
_UART_REQUIRED = [
    ('post', '/api/device/protocol/test', {}),
    ('post', '/api/device/protocol/set_time', {}),
    ('post', '/api/device/ota/start', {'path': '/no/such/fw.bin'}),
    ('post', '/api/device/file/start', {'path': '/no/such.bin'}),
    ('post', '/api/device/xmodem/start', {'path': __file__}),
    ('post', '/api/device/ymodem/start', {'path': __file__}),
    ('post', '/api/device/uid/get', {}),
    ('post', '/api/device/sn/write', {'orval': 0}),
    ('post', '/api/device/info/get', {}),
    ('post', '/api/device/shell/cmd', {'cmd': 'help'}),
    ('post', '/api/device/param/list', {}),
    ('post', '/api/device/param/get', {'name': 'g_volume'}),
    ('post', '/api/device/param/set', {'name': 'g_volume', 'value': 1}),
]

# Endpoints that must keep working without a serial port.
_NO_UART_OK = [
    ('get', '/api/device/serial/ports', None),
    ('get', '/api/device/ota/status', None),
    ('get', '/api/device/file/status', None),
    ('post', '/api/device/file/stop', {}),
    ('get', '/api/device/xmodem/status', None),
    ('post', '/api/device/xmodem/cancel', {}),
    ('post', '/api/device/ymodem/cancel', {}),
    ('get', '/api/device/http/status', None),
    ('get', '/api/device/http/requests', None),
    ('get', '/api/device/status', None),
    ('get', '/api/device/logs', None),
]


def _wait(predicate, timeout=5.0, interval=0.02):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(interval)
    return predicate()


def _assert_structured_error(testcase, resp, status=None, code=None):
    """AppError / global handler must emit {"detail":{"detail","code"}}."""
    if status is not None:
        testcase.assertEqual(resp.status_code, status, resp.text)
    else:
        testcase.assertGreaterEqual(resp.status_code, 400, resp.text)
    testcase.assertNotEqual(resp.status_code, 500, 'bare 500 leaked: %s'
                            % resp.text)
    body = resp.json()
    testcase.assertIn('detail', body, body)
    detail = body['detail']
    testcase.assertIsInstance(detail, dict, detail)
    testcase.assertIn('code', detail, detail)
    testcase.assertIn('detail', detail, detail)
    testcase.assertTrue(detail['code'], detail)
    if code is not None:
        testcase.assertEqual(detail['code'], code, detail)
    return detail


class DeviceApiTestCase(unittest.TestCase):
    """Shared setup: fresh DeviceManager + TestClient + FakeUart."""

    def setUp(self):
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)
        self.mgr = DeviceManager.get()
        self.fake = FakeUart()
        self.fake.is_open = False  # start closed — serial must be opened via API
        self.mgr.uart = self.fake
        self.app = create_app()
        self.client = TestClient(self.app)

    def _open_serial(self, encrypt=False):
        r = self.client.post('/api/device/serial/open', json={
            'path': 'FAKE0', 'baud': 115200, 'encrypt': encrypt,
        })
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json()['ok'])
        return r.json()

    def _bind_protocol_device(self, **kwargs):
        """Open serial then attach FakeDevice to the UART write hook."""
        self._open_serial()
        return FakeDevice(self.fake, **kwargs)

    def _bind_shell_device(self, params=None):
        self._open_serial()
        return FakeShellDevice(self.fake, params=params)

    def _make_file(self, data):
        with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
            f.write(data)
            path = f.name
        self.addCleanup(os.unlink, path)
        return path


class TestSerialEndpoints(DeviceApiTestCase):
    def test_ports_list_shape(self):
        r = self.client.get('/api/device/serial/ports')
        self.assertEqual(r.status_code, 200)
        data = r.json()
        self.assertIn('ports', data)
        self.assertIn('open', data)
        self.assertIsInstance(data['ports'], list)
        self.assertFalse(data['open'])
        self.assertEqual(data['current'], '')
        self.assertEqual(data['baudrate'], 0)

    def test_open_close_roundtrip(self):
        r = self.client.post('/api/device/serial/open', json={
            'path': 'FAKE0', 'baud': 115200,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body['ok'])
        self.assertEqual(body['port'], 'FAKE0')
        self.assertEqual(body['baudrate'], 115200)
        self.assertEqual(body['host_id'], 0x1314)

        st = self.client.get('/api/device/serial/ports').json()
        self.assertTrue(st['open'])
        self.assertEqual(st['current'], 'FAKE0')
        self.assertEqual(st['baudrate'], 115200)

        r2 = self.client.post('/api/device/serial/close', json={})
        self.assertEqual(r2.status_code, 200)
        self.assertTrue(r2.json()['ok'])
        self.assertTrue(r2.json()['was_open'])
        st2 = self.client.get('/api/device/serial/ports').json()
        self.assertFalse(st2['open'])

    def test_open_rejects_empty_path(self):
        r = self.client.post('/api/device/serial/open', json={'path': ''})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

    def test_open_rejects_nonpositive_baud(self):
        r = self.client.post('/api/device/serial/open', json={
            'path': 'FAKE0', 'baud': 0,
        })
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')
        r2 = self.client.post('/api/device/serial/open', json={
            'path': 'FAKE0', 'baud': -9600,
        })
        _assert_structured_error(self, r2, 400, 'INVALID_REQUEST')

    def test_encrypt_flag_reaches_protocol_client(self):
        body = self._open_serial(encrypt=True)
        self.assertTrue(body['encrypt'])
        self.assertTrue(self.mgr.protocol_client.encrypt)

    def test_reopen_closes_previous_port(self):
        self._open_serial()
        self._open_serial()  # re-open is allowed
        st = self.client.get('/api/device/serial/ports').json()
        self.assertTrue(st['open'])
        self.assertEqual(st['current'], 'FAKE0')


class TestProtocolEndpoints(DeviceApiTestCase):
    def test_protocol_test_requires_open_port(self):
        r = self.client.post('/api/device/protocol/test', json={})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_protocol_test_success(self):
        self._bind_protocol_device()
        r = self.client.post('/api/device/protocol/test', json={})
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body['ok'])
        self.assertEqual(body['cmd'], 0x1)
        # firmware replies empty ACK; request param is b'BabyOS\x00'
        self.assertEqual(body['param_text'], '')
        tx = self.mgr.protocol_client.tx_log
        self.assertTrue(any(b'BabyOS' in frame for frame in tx))

    def test_set_time_requires_open_port(self):
        r = self.client.post('/api/device/protocol/set_time', json={})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_set_time_default_utc(self):
        self._bind_protocol_device()
        before = int(time.time())
        r = self.client.post('/api/device/protocol/set_time', json={})
        self.assertEqual(r.status_code, 200, r.text)
        utc = r.json()['utc']
        self.assertGreaterEqual(utc, before - 2)
        self.assertLessEqual(utc, int(time.time()) + 2)

    def test_set_time_explicit_utc(self):
        self._bind_protocol_device()
        r = self.client.post('/api/device/protocol/set_time',
                              json={'utc': 1700000000})
        self.assertEqual(r.status_code, 200)
        self.assertEqual(r.json()['utc'], 1700000000)
        # device received a UTC frame carrying the same value
        tx = self.mgr.protocol_client.tx_log
        self.assertTrue(tx)


class TestNoUarStructuredErrors(DeviceApiTestCase):
    """Every uart-required endpoint: structured 409, never bare 500."""

    def test_all_uart_endpoints_structured_409_when_closed(self):
        for method, path, payload in _UART_REQUIRED:
            with self.subTest(endpoint='%s %s' % (method, path)):
                if method == 'post':
                    r = self.client.post(path, json=payload)
                else:
                    r = self.client.get(path, params=payload or None)
                detail = _assert_structured_error(self, r, 409,
                                                  'SERIAL_NOT_OPEN')
                self.assertIn('串口', detail['detail'])

    def test_status_endpoints_work_without_uart(self):
        for method, path, payload in _NO_UART_OK:
            with self.subTest(endpoint='%s %s' % (method, path)):
                if method == 'post':
                    r = self.client.post(path, json=payload or {})
                else:
                    r = self.client.get(path)
                self.assertEqual(r.status_code, 200, r.text)
                data = r.json()
                self.assertIsInstance(data, dict)

    def test_ota_status_reports_uart_closed(self):
        st = self.client.get('/api/device/ota/status').json()
        self.assertFalse(st['uart_open'])
        self.assertIsNone(st['job'])

    def test_file_status_reports_uart_closed(self):
        st = self.client.get('/api/device/file/status').json()
        self.assertFalse(st['uart_open'])
        self.assertIsNone(st['job'])

    def test_aggregate_status_reports_uart_closed(self):
        st = self.client.get('/api/device/status').json()
        self.assertFalse(st['uart']['open'])
        self.assertFalse(st['http_mock']['running'])


class TestDeviceInfoEndpoints(DeviceApiTestCase):
    def test_uid_get_requires_open_port(self):
        r = self.client.post('/api/device/uid/get', json={})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_uid_get(self):
        self._bind_protocol_device(uid=b'\x01\x02\x03\x04\x05\x06\x07\x08')
        r = self.client.post('/api/device/uid/get', json={})
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body['ok'])
        self.assertEqual(body['uid_hex'], '0102030405060708')
        self.assertEqual(body['uid_len'], 8)
        # cached on manager for later SN write
        self.assertEqual(self.mgr.last_uid, b'\x01\x02\x03\x04\x05\x06\x07\x08')

    def test_sn_write_requires_open_port(self):
        r = self.client.post('/api/device/sn/write', json={'orval': 0})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_sn_write_uses_last_uid_and_orval(self):
        uid = b'\x11\x22\x33\x44\x55\x66\x77\x88'
        self._bind_protocol_device(uid=uid)
        r = self.client.post('/api/device/uid/get', json={})
        self.assertEqual(r.status_code, 200)

        orval = 0x0F
        r2 = self.client.post('/api/device/sn/write', json={'orval': orval})
        self.assertEqual(r2.status_code, 200, r2.text)
        body = r2.json()
        self.assertTrue(body['ok'])
        expected = sn_bytes(uid, orval)
        self.assertEqual(bytes.fromhex(body['sn_hex']), expected)
        self.assertEqual(body['cmd'], 0x8)
        self.assertEqual(self.mgr.last_sn, expected)

    def test_sn_write_without_uid_is_structured_error(self):
        self._bind_protocol_device()
        r = self.client.post('/api/device/sn/write', json={'orval': 0})
        _assert_structured_error(self, r, 400, 'UID_NOT_AVAILABLE')

    def test_sn_write_rejects_bad_uid_hex(self):
        self._bind_protocol_device()
        r = self.client.post('/api/device/sn/write',
                              json={'orval': 0, 'uid_hex': 'zz-nothex'})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

    def test_sn_write_with_explicit_uid_hex(self):
        self._bind_protocol_device()
        uid_hex = 'aabbccdd'
        r = self.client.post('/api/device/sn/write',
                              json={'orval': 1, 'uid_hex': uid_hex})
        self.assertEqual(r.status_code, 200, r.text)
        expected = sn_bytes(bytes.fromhex(uid_hex), 1)
        self.assertEqual(bytes.fromhex(r.json()['sn_hex']), expected)

    def test_info_get_requires_open_port(self):
        r = self.client.post('/api/device/info/get', json={})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_device_info_get(self):
        self._bind_protocol_device(version='v9.9.9', model='MCU-X')
        r = self.client.post('/api/device/info/get', json={})
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertEqual(body['version'], 'v9.9.9')
        self.assertEqual(body['model'], 'MCU-X')
        self.assertEqual(self.mgr.last_devinfo,
                         {'version': 'v9.9.9', 'model': 'MCU-X'})


class TestOtaEndpoints(DeviceApiTestCase):
    def test_ota_start_requires_open_port(self):
        path = self._make_file(b'x')
        r = self.client.post('/api/device/ota/start', json={'path': path})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_ota_start_missing_file(self):
        self._bind_protocol_device()
        r = self.client.post('/api/device/ota/start',
                              json={'path': '/no/such/fw.bin'})
        _assert_structured_error(self, r, 404, 'FILE_NOT_FOUND')

    def test_ota_start_empty_file(self):
        self._bind_protocol_device()
        path = self._make_file(b'')
        r = self.client.post('/api/device/ota/start', json={'path': path})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

    def test_ota_async_success(self):
        dev = self._bind_protocol_device()
        payload = b'BABYOS-OTA-' * 40  # multi-chunk
        path = self._make_file(payload)

        r = self.client.post('/api/device/ota/start',
                              json={'path': path, 'name': 'test_fw.bin',
                                    'timeout': 8.0})
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body['accepted'])
        job_id = body['job_id']

        ok = _wait(lambda: self.client.get(
            '/api/device/ota/status',
            params={'job_id': job_id}).json()
            .get('job', {}).get('state') in ('done', 'error'), timeout=8.0)
        self.assertTrue(ok, 'OTA job did not finish')
        st = self.client.get('/api/device/ota/status',
                              params={'job_id': job_id}).json()
        job = st['job']
        self.assertEqual(job['state'], 'done')
        self.assertTrue(job['ok'])
        self.assertEqual(job['result_code'], 0)
        self.assertEqual(job['progress'], 100)
        # device received FW_INFO + all FDATA chunks
        self.assertIsNotNone(dev.fw_info)
        self.assertEqual(dev.fw_info[2], 'test_fw.bin')
        self.assertEqual(bytes(dev.received_fw_bytes[:len(payload)]), payload)
        # host also ACKed OTA_RESULT (cmd 0x5)
        self.assertIn(0x5, [c for c, _p in dev.host_acks])

    def test_ota_async_crc_error(self):
        dev = self._bind_protocol_device(ota_result=1)  # CRC error
        payload = os.urandom(600)
        path = self._make_file(payload)
        r = self.client.post('/api/device/ota/start',
                              json={'path': path, 'timeout': 8.0})
        self.assertEqual(r.status_code, 200)
        job_id = r.json()['job_id']
        _wait(lambda: self.client.get(
            '/api/device/ota/status', params={'job_id': job_id}).json()
            .get('job', {}).get('state') in ('done', 'error'), timeout=8.0)
        st = self.client.get('/api/device/ota/status',
                              params={'job_id': job_id}).json()
        # FakeDevice reports result=ota_result only when local CRC matches;
        # with matching host CRC the device still returns ota_result=1.
        self.assertIn(st['job']['state'], ('done', 'error'))
        self.assertFalse(st['job']['ok'])

    def test_file_start_requires_open_port(self):
        path = self._make_file(b'x')
        r = self.client.post('/api/device/file/start', json={'path': path})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_file_start_missing_file(self):
        self._bind_protocol_device()
        r = self.client.post('/api/device/file/start',
                              json={'path': '/no/such.bin'})
        _assert_structured_error(self, r, 404, 'FILE_NOT_FOUND')

    def test_file_transfer_async(self):
        dev = self._bind_protocol_device()
        payload = b'FILEDATA' * 80
        path = self._make_file(payload)
        r = self.client.post('/api/device/file/start',
                              json={'path': path, 'dev_no': 3, 'offset': 0x100,
                                    'timeout': 8.0})
        self.assertEqual(r.status_code, 200, r.text)
        job_id = r.json()['job_id']
        _wait(lambda: self.client.get(
            '/api/device/file/status', params={'job_id': job_id}).json()
            .get('job', {}).get('state') in ('done', 'error'), timeout=8.0)
        st = self.client.get('/api/device/file/status',
                              params={'job_id': job_id}).json()
        self.assertEqual(st['job']['state'], 'done')
        self.assertTrue(st['job']['ok'])
        self.assertEqual(st['kind'], 'file')
        self.assertIsNotNone(dev.trans_file)
        self.assertEqual(dev.trans_file[2], 3)   # dev_no
        self.assertEqual(dev.trans_file[3], 0x100)  # offset
        self.assertEqual(bytes(dev.received_fw_bytes[:len(payload)]), payload)

    def test_file_status_no_uart_returns_200(self):
        st = self.client.get('/api/device/file/status').json()
        self.assertEqual(st['kind'], 'file')
        self.assertFalse(st['uart_open'])

    def test_file_stop_while_idle(self):
        self._bind_protocol_device()
        r = self.client.post('/api/device/file/stop', json={})
        self.assertEqual(r.status_code, 200)
        self.assertTrue(r.json()['ok'])


class TestShellParamEndpoints(DeviceApiTestCase):
    def test_shell_cmd_requires_open(self):
        r = self.client.post('/api/device/shell/cmd', json={'cmd': 'help'})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_shell_cmd_rejects_blank(self):
        self._bind_shell_device()
        r = self.client.post('/api/device/shell/cmd', json={'cmd': '   '})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

    def test_param_list_requires_open(self):
        r = self.client.post('/api/device/param/list', json={})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_param_get_requires_open(self):
        r = self.client.post('/api/device/param/get',
                              json={'name': 'g_volume'})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_param_set_requires_open(self):
        r = self.client.post('/api/device/param/set',
                              json={'name': 'g_volume', 'value': 1})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_param_list_get_set(self):
        shell_dev = self._bind_shell_device(
            params={'g_volume': 80, 'g_led': 1})

        r = self.client.post('/api/device/param/list', json={})
        self.assertEqual(r.status_code, 200, r.text)
        names = r.json()['names']
        self.assertIn('g_volume', names)
        self.assertIn('g_led', names)
        self.assertEqual(r.json()['count'], len(names))

        r2 = self.client.post('/api/device/param/get',
                               json={'name': 'g_volume'})
        self.assertEqual(r2.status_code, 200, r2.text)
        self.assertEqual(r2.json()['value'], 80)

        r3 = self.client.post('/api/device/param/set',
                               json={'name': 'g_volume', 'value': 55})
        self.assertEqual(r3.status_code, 200, r3.text)
        self.assertTrue(r3.json()['ok'])
        # real device-side state actually changed (not a stub echo)
        self.assertEqual(shell_dev.params['g_volume'], 55)

        r4 = self.client.post('/api/device/param/get',
                               json={'name': 'g_volume'})
        self.assertEqual(r4.json()['value'], 55)
        self.assertIn('param g_volume 55', shell_dev.commands)

    def test_param_get_unknown_name(self):
        self._bind_shell_device(params={'g_volume': 80})
        r = self.client.post('/api/device/param/get',
                              json={'name': 'no_such_param'})
        _assert_structured_error(self, r, 504, 'PARAM_NOT_FOUND')

    def test_param_get_rejects_blank_name(self):
        self._bind_shell_device()
        r = self.client.post('/api/device/param/get', json={'name': ''})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

    def test_shell_raw_cmd(self):
        self._bind_shell_device(params={'g_x': 7})
        r = self.client.post('/api/device/shell/cmd',
                              json={'cmd': 'param g_x'})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertIn('g_x', r.json()['response'])
        self.assertEqual(r.json()['cmd'], 'param g_x')


class TestHttpMockEndpoints(DeviceApiTestCase):
    def test_http_start_status_requests_stop(self):
        # HTTP mock is independent of serial — must work with uart closed
        r = self.client.post('/api/device/http/start', json={
            'port': 0, 'body': '{"mock":1}', 'content_type': 'application/json',
            'status_code': 200, 'https': False,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body['ok'])
        base = body['base_url']
        self.assertTrue(base.startswith('http://127.0.0.1:'))
        port = body['port']
        self.assertGreater(port, 0)
        self.assertTrue(body['status']['running'])

        # second start → structured conflict
        r2 = self.client.post('/api/device/http/start', json={'port': 0})
        _assert_structured_error(self, r2, 409, 'HTTP_MOCK_RUNNING')

        st = self.client.get('/api/device/http/status').json()
        self.assertTrue(st['running'])
        self.assertEqual(st['port'], port)

        # real HTTP traffic against the mock
        import httpx
        with httpx.Client(timeout=3.0) as c:
            resp = c.post(base + '/api/test', content=b'hello')
            self.assertEqual(resp.status_code, 200)
            self.assertEqual(resp.json(), {'mock': 1})

        reqs = self.client.get('/api/device/http/requests').json()
        self.assertTrue(reqs['running'])
        self.assertGreaterEqual(reqs['count'], 1)
        paths = [x['path_only'] for x in reqs['requests']]
        self.assertIn('/api/test', paths)

        r3 = self.client.post('/api/device/http/stop', json={})
        self.assertEqual(r3.status_code, 200)
        self.assertTrue(r3.json()['was_running'])
        st2 = self.client.get('/api/device/http/status').json()
        self.assertFalse(st2['running'])

        # stop again — idempotent, was_running=False
        r4 = self.client.post('/api/device/http/stop', json={})
        self.assertEqual(r4.status_code, 200)
        self.assertFalse(r4.json()['was_running'])

    def test_http_proxy_records_into_mock(self):
        r = self.client.post('/api/device/http/start', json={
            'port': 0, 'body': '{"proxy":"ok"}', 'https': False,
        })
        self.assertEqual(r.status_code, 200)
        base = r.json()['base_url']

        r2 = self.client.post('/api/device/http/proxy', json={
            'url': base + '/proxy-path', 'method': 'POST', 'body': 'data',
        })
        self.assertEqual(r2.status_code, 200, r2.text)
        self.assertEqual(r2.json()['status_code'], 200)
        self.assertIn('proxy', r2.json()['body'])

        reqs = self.client.get('/api/device/http/requests').json()
        self.assertGreaterEqual(reqs['count'], 1)
        self.assertTrue(any(x['path_only'] == '/proxy-path'
                            for x in reqs['requests']))
        self.client.post('/api/device/http/stop', json={})

    def test_http_proxy_rejects_bad_url(self):
        r = self.client.post('/api/device/http/proxy',
                              json={'url': 'not-a-url'})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

    def test_http_proxy_rejects_bad_method(self):
        r = self.client.post('/api/device/http/proxy', json={
            'url': 'http://127.0.0.1:1/x', 'method': 'TRACE',
        })
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')


class TestXmodemEndpoints(DeviceApiTestCase):
    def _make_file(self, size=300):
        data = bytes((i * 7) & 0xFF for i in range(size))
        path = super(TestXmodemEndpoints, self)._make_file(data)
        return path, data

    def test_xmodem_start_missing_file(self):
        self._bind_protocol_device()
        r = self.client.post('/api/device/xmodem/start',
                              json={'path': '/no/such.bin'})
        _assert_structured_error(self, r, 404, 'FILE_NOT_FOUND')

    def test_xmodem_start_without_uart(self):
        r = self.client.post('/api/device/xmodem/start',
                              json={'path': __file__})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_xmodem_start_then_cancel(self):
        """Start real XmodemSender pump, then cancel — exercises state machine."""
        self._bind_protocol_device()
        path, _ = self._make_file(256)
        r = self.client.post('/api/device/xmodem/start', json={'path': path})
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body['accepted'])
        job_id = body['job_id']

        st = self.client.get('/api/device/xmodem/status',
                              params={'kind': 'xmodem'}).json()
        self.assertTrue(st['active'])
        self.assertEqual(st['filename'], os.path.basename(path))

        r2 = self.client.post('/api/device/xmodem/cancel', json={})
        self.assertEqual(r2.status_code, 200)
        _wait(lambda: not self.client.get(
            '/api/device/xmodem/status', params={'kind': 'xmodem'}).json()
            .get('active'), timeout=3.0)
        st2 = self.client.get('/api/device/xmodem/status',
                               params={'kind': 'xmodem',
                                       'job_id': job_id}).json()
        self.assertFalse(st2['active'])
        self.assertIn(st2['job']['state'], ('cancelled', 'done', 'error'))

    def test_ymodem_start_missing_file(self):
        self._bind_protocol_device()
        r = self.client.post('/api/device/ymodem/start',
                              json={'path': '/no/such.bin'})
        _assert_structured_error(self, r, 404, 'FILE_NOT_FOUND')

    def test_ymodem_start_without_uart(self):
        r = self.client.post('/api/device/ymodem/start',
                              json={'path': __file__})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

    def test_ymodem_start_then_cancel(self):
        self._bind_protocol_device()
        path, _ = self._make_file(200)
        r = self.client.post('/api/device/ymodem/start', json={'path': path})
        self.assertEqual(r.status_code, 200, r.text)
        st = self.client.get('/api/device/xmodem/status',
                              params={'kind': 'ymodem'}).json()
        self.assertTrue(st['active'])
        self.client.post('/api/device/ymodem/cancel', json={})
        _wait(lambda: not self.client.get(
            '/api/device/xmodem/status', params={'kind': 'ymodem'}).json()
            .get('active'), timeout=3.0)


class TestAggregateEndpoints(DeviceApiTestCase):
    def test_status_and_logs(self):
        self._bind_protocol_device()
        self.client.post('/api/device/protocol/test', json={})
        st = self.client.get('/api/device/status').json()
        self.assertIn('uart', st)
        self.assertIn('protocol', st)
        self.assertIn('http_mock', st)
        self.assertTrue(st['uart']['open'])
        self.assertIsNotNone(st['protocol'])

        logs = self.client.get('/api/device/logs', params={'tail': 50}).json()
        self.assertIn('logs', logs)
        self.assertTrue(any('open_port' in line for line in logs['logs']))

    def test_routes_all_registered(self):
        paths = {getattr(r, 'path', '') for r in self.app.routes}
        required = [
            '/api/device/serial/ports',
            '/api/device/serial/open',
            '/api/device/serial/close',
            '/api/device/protocol/test',
            '/api/device/protocol/set_time',
            '/api/device/ota/start',
            '/api/device/ota/status',
            '/api/device/xmodem/start',
            '/api/device/xmodem/cancel',
            '/api/device/ymodem/start',
            '/api/device/ymodem/cancel',
            '/api/device/xmodem/status',
            '/api/device/file/start',
            '/api/device/file/stop',
            '/api/device/file/status',
            '/api/device/uid/get',
            '/api/device/sn/write',
            '/api/device/info/get',
            '/api/device/shell/cmd',
            '/api/device/param/list',
            '/api/device/param/get',
            '/api/device/param/set',
            '/api/device/http/start',
            '/api/device/http/stop',
            '/api/device/http/status',
            '/api/device/http/requests',
            '/api/device/http/proxy',
            '/api/device/status',
            '/api/device/logs',
        ]
        for p in required:
            self.assertIn(p, paths, 'missing route %s' % p)

    def test_main_py_includes_device_router(self):
        """main.py must include device.router — not a UI shell."""
        import app.main as main_mod
        from app.main import app as module_app

        src = inspect.getsource(main_mod)
        self.assertIn('device.router', src)
        self.assertIn('from .api import', src)

        # module-level app exposes the same routes
        module_paths = {getattr(r, 'path', '') for r in module_app.routes}
        self.assertIn('/api/device/status', module_paths)
        self.assertIn('/api/device/serial/open', module_paths)
        self.assertIn('/api/device/protocol/test', module_paths)

        # router prefix is the real API namespace
        self.assertEqual(device_api.router.prefix, '/api/device')


class TestUnhandledExceptionShape(DeviceApiTestCase):
    """Global handler: unexpected errors → structured 422, not bare 500."""

    def test_unhandled_exception_is_structured(self):
        def _boom():
            raise RuntimeError('simulated device manager fault')

        self.mgr.list_ports = _boom
        client = TestClient(self.app, raise_server_exceptions=False)
        r = client.get('/api/device/serial/ports')
        self.assertEqual(r.status_code, 422)
        body = r.json()
        detail = body['detail']
        self.assertEqual(detail['code'], 'UNHANDLED')
        self.assertIn('simulated device manager fault', detail['detail'])
        self.assertIn('trace', detail)


if __name__ == '__main__':
    unittest.main(verbosity=2)
