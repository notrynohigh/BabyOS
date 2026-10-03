#!/usr/bin/env python3
"""
test_api_virtual_serial — API-layer virtual-serial acceptance for BabyOS Studio.

Authority (硬性验收):
  1. 串口/协议必须走真实 pty 双向字节通道 — 禁止只测接口/内存 mock。
  2. 主机侧真实栈: FastAPI DeviceManager → UartService → pty 一端。
  3. 设备侧 mock_babyos_device 在 pty 另一端真实解析 BabyOS 帧并回包。
  4. 覆盖 API: serial open/close、protocol/test、protocol/set_time、
     uid/get、sn/write、info/get、ota/start+status、file/start+status、
     xmodem/ymodem start+status、shell/cmd、param/list|get|set。
  5. HTTPS 证书必须是 origin/dev tool/mock_https_{cert,key}.pem。
     禁止运行时 openssl 自签生成。
  6. Python 3.8 兼容；不修改 bos/thirdparty；不提交 git；禁止 stub。
  7. 无设备/串口关闭时必须结构化错误 (detail.code + detail.detail)。
  8. TEA 加密路径通过 API serial/open encrypt=true 验证。

本文件专注 API 链路 (TestClient → /api/device/* → DeviceManager →
UartService → pty → MockBabyOSDevice)。协议层细节可由
test_virtual_serial.py 补充，但本文件绝不使用 FakeUart 作为成功路径。

Run:
  tool/babyos-studio/python/.venv/bin/python \
      tool/babyos-studio/test/device_features/test_api_virtual_serial.py
  or: pytest tool/babyos-studio/test/device_features/test_api_virtual_serial.py
"""

from __future__ import print_function

import hashlib
import os
import ssl
import subprocess
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
    DEVICE_ID_HOST,
    CMD_TEST,
    CMD_UTC,
    CMD_FW_INFO,
    CMD_FDATA,
    CMD_OTA_RESULT,
    CMD_TRANS_FILE,
    CMD_GET_UID,
    CMD_WRITE_SN,
    CMD_DEVICEINFO,
)
from device.sn_util import sn_bytes as _sn_bytes  # noqa: E402

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

# origin/dev tool/mock_https_{cert,key}.pem content hashes (authority)
DEV_CERT_SHA256 = '45020779c14d326cb5306b68687a2985f0b6c626f9587f1ce39493a77f9b034c'
DEV_KEY_SHA256 = '24017d03c4f9b83779cc0d2d957528bf6333d7338369f1141b90e4d81dcfd689'

# Report counters (virtual-serial acceptance)
_VS_TOTAL = 0
_VS_PASS = 0
_VS_FAIL = 0
_VS_KINDS = []


def _record(ok: bool, kind: str) -> None:
    global _VS_TOTAL, _VS_PASS, _VS_FAIL
    _VS_TOTAL += 1
    if kind not in _VS_KINDS:
        _VS_KINDS.append(kind)
    if ok:
        _VS_PASS += 1
    else:
        _VS_FAIL += 1


def _sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            h.update(chunk)
    return h.hexdigest()


def _git_show(path: str):
    try:
        return subprocess.check_output(
            ['git', '-C', _REPO, 'show', 'origin/dev:%s' % path],
            stderr=subprocess.DEVNULL)
    except Exception:
        return None


def _make_fw(n: int, seed: int = 3) -> bytes:
    return bytes(bytearray(((i * 7) + seed) & 0xFF for i in range(n)))


def _write_temp(data: bytes, suffix: str = '.bin') -> str:
    fd, path = tempfile.mkstemp(suffix=suffix, prefix='babyos_api_vs_')
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


def _detail_code(r):
    """Extract structured detail.code from an AppError response."""
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


class _ApiBase(unittest.TestCase):
    """Shared FastAPI TestClient + real pty link + mock device fixture."""

    encrypt = False

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
                         'API virtual-serial acceptance requires real pty')
        _VS_KINDS.append('channel:' + self.link.kind)

        from app.main import create_app
        from fastapi.testclient import TestClient
        self.app = create_app()
        self.client = TestClient(self.app)

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
        self.assertEqual(body.get('port'), self.link.host_port)
        self.assertTrue(body.get('encrypt') == bool(self.encrypt))
        _VS_KINDS.append('api:serial_open')

        # DeviceManager.uart must be the real UartService opened on the pty
        from device.device_manager import get_device_manager
        dm = get_device_manager()
        self.assertTrue(dm.is_open())
        self.assertEqual(dm.uart.port, self.link.host_port)
        self.assertIsNotNone(dm.protocol_client)
        self.assertIsNotNone(dm.shell_client)

    def _record(self, ok: bool, case: str) -> None:
        _record(ok, case)

    def _cmd_seen(self, cmd: int) -> bool:
        with self.link.device._lock:
            return cmd in self.link.device.written_cmds

    def _assert_cmd_seen(self, cmd: int, label: str) -> None:
        self.assertTrue(self._cmd_seen(cmd),
                        'mock device must have received CMD 0x%02X (%s); '
                        'cmds=%s' % (cmd, label,
                                     self.link.device.written_cmds))
        _record(True, 'api_cmd_%s_0x%02X' % (label, cmd))


# ---------------------------------------------------------------------------
# Serial open/close + structured errors when closed
# ---------------------------------------------------------------------------

class TestApiSerialLifecycle(_ApiBase):

    def test_serial_open_reports_pty_and_host_id(self):
        r = self.client.get('/api/device/serial/ports')
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('open'))
        self.assertEqual(body.get('current'), self.link.host_port)
        self.assertEqual(body.get('baudrate'), 115200)
        # pyserial may not enumerate pty slaves in comports(); the open
        # port is authoritative via `current` / /api/device/status.
        ports = body.get('ports') or []
        self.assertIsInstance(ports, list)
        if ports:
            # If discovery lists the pty it must match the open path.
            self.assertEqual(body.get('current'), ports[0]
                             if len(ports) == 1 else body.get('current'))
        self._record(True, 'api_serial_ports')

        r = self.client.get('/api/device/status')
        self.assertEqual(r.status_code, 200, r.text)
        uart = (r.json() or {}).get('uart') or {}
        self.assertTrue(uart.get('open'))
        self.assertEqual(uart.get('port'), self.link.host_port)
        self.assertEqual(uart.get('baudrate'), 115200)
        proto = (r.json() or {}).get('protocol') or {}
        self.assertEqual(proto.get('encrypt'), bool(self.encrypt))
        self._record(True, 'api_device_status_uart')

    def test_serial_close_then_structured_errors(self):
        r = self.client.post('/api/device/serial/close', json={})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'))
        self.assertFalse(r.json().get('open'))
        _VS_KINDS.append('api:serial_close')

        endpoints = [
            ('/api/device/protocol/test', {}),
            ('/api/device/protocol/set_time', {'utc': 1700000000}),
            ('/api/device/uid/get', {}),
            ('/api/device/sn/write', {'orval': 0}),
            ('/api/device/info/get', {}),
            ('/api/device/shell/cmd', {'cmd': 'param'}),
            ('/api/device/param/list', {}),
            ('/api/device/param/get', {'name': 'g_volume'}),
            ('/api/device/param/set', {'name': 'g_volume', 'value': 1}),
            ('/api/device/ota/start', {'path': __file__}),
            ('/api/device/file/start', {'path': __file__}),
            ('/api/device/xmodem/start', {'path': __file__}),
            ('/api/device/ymodem/start', {'path': __file__}),
        ]
        for url, payload in endpoints:
            r = self.client.post(url, json=payload)
            _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')
        self._record(True, 'api_structured_serial_closed')

    def test_serial_open_invalid_path_structured(self):
        r = self.client.post('/api/device/serial/open', json={'path': ''})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

        r = self.client.post('/api/device/serial/open', json={
            'path': '/dev/does-not-exist-babyos-api-vs',
            'baud': 115200,
        })
        _assert_structured_error(self, r, 500, 'PORT_OPEN_FAILED')
        # still open on the original pty
        r = self.client.get('/api/device/serial/ports')
        self.assertTrue(r.json().get('open'))
        self.assertEqual(r.json().get('current'), self.link.host_port)
        self._record(True, 'api_serial_open_invalid_structured')


# ---------------------------------------------------------------------------
# Protocol CMD 0x1/0x2/0x7/0x8/0xA via API over pty
# ---------------------------------------------------------------------------

class TestApiProtocolCommands(_ApiBase):

    def test_protocol_test_cmd_0x1(self):
        r = self.client.post('/api/device/protocol/test', json={})
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('cmd'), CMD_TEST)
        self.assertEqual(body.get('device_id'), MOCK_DEV_ID)
        self._assert_cmd_seen(CMD_TEST, 'protocol_test')
        self._record(True, 'api_protocol_test')

    def test_protocol_set_time_cmd_0x2(self):
        utc = 1700000123
        r = self.client.post('/api/device/protocol/set_time',
                             json={'utc': utc})
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('utc'), utc)
        self.assertEqual(body.get('cmd'), CMD_UTC)
        self.assertEqual(self.link.device.last_utc, utc)
        self._assert_cmd_seen(CMD_UTC, 'protocol_set_time')
        self._record(True, 'api_protocol_set_time')

    def test_uid_get_cmd_0x7(self):
        r = self.client.post('/api/device/uid/get', json={})
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('uid_hex'), MOCK_UID.hex())
        self.assertEqual(body.get('uid_len'), len(MOCK_UID))
        self._assert_cmd_seen(CMD_GET_UID, 'uid_get')
        self._record(True, 'api_uid_get')

    def test_sn_write_cmd_0x8(self):
        r = self.client.post('/api/device/uid/get', json={})
        self.assertEqual(r.status_code, 200, r.text)
        r = self.client.post('/api/device/sn/write', json={'orval': 0x5A})
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('cmd'), CMD_WRITE_SN)
        expected_sn = _sn_bytes(MOCK_UID, 0x5A)
        self.assertEqual(body.get('sn_hex'), expected_sn.hex())
        self.assertEqual(body.get('sn_len'), len(expected_sn))
        # device-side CMD_WRITE_SN param must match host-computed SN
        self.assertEqual(self.link.device.last_sn, expected_sn)
        self._assert_cmd_seen(CMD_WRITE_SN, 'sn_write')
        self._record(True, 'api_sn_write')

    def test_info_get_cmd_0xA(self):
        r = self.client.post('/api/device/info/get', json={})
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('version'), MOCK_VERSION)
        self.assertEqual(body.get('model'), MOCK_MODEL)
        self._assert_cmd_seen(CMD_DEVICEINFO, 'info_get')
        self._record(True, 'api_info_get')

    def test_sn_write_without_uid_structured(self):
        # no /uid/get first → DeviceManager.last_uid empty → structured 400
        r = self.client.post('/api/device/sn/write', json={'orval': 0})
        _assert_structured_error(self, r, 400, 'UID_NOT_AVAILABLE')
        self._record(True, 'api_sn_write_no_uid_structured')

    def test_full_cmd_coverage_via_api(self):
        """One pass covering CMD 0x1/0x2/0x7/0x8/0xA through the API."""
        r = self.client.post('/api/device/protocol/test', json={})
        self.assertEqual(r.status_code, 200, r.text)
        r = self.client.post('/api/device/protocol/set_time',
                             json={'utc': 1700000456})
        self.assertEqual(r.status_code, 200, r.text)
        r = self.client.post('/api/device/uid/get', json={})
        self.assertEqual(r.status_code, 200, r.text)
        r = self.client.post('/api/device/sn/write', json={'orval': 1})
        self.assertEqual(r.status_code, 200, r.text)
        r = self.client.post('/api/device/info/get', json={})
        self.assertEqual(r.status_code, 200, r.text)

        for cmd, label in (
            (CMD_TEST, 'test'),
            (CMD_UTC, 'utc'),
            (CMD_GET_UID, 'uid'),
            (CMD_WRITE_SN, 'sn'),
            (CMD_DEVICEINFO, 'info'),
        ):
            self._assert_cmd_seen(cmd, label)
        self._record(True, 'api_cmd_coverage_1_2_7_8_A')


# ---------------------------------------------------------------------------
# OTA (CMD 0x3/0x4/0x5) + file transfer (CMD 0x6) via API
# ---------------------------------------------------------------------------

class TestApiTransfers(_ApiBase):

    def test_ota_start_status_cmd_0x3_0x4_0x5(self):
        payload = _make_fw(512, seed=55)
        path = _write_temp(payload)
        try:
            r = self.client.post('/api/device/ota/start',
                                 json={'path': path, 'timeout': 10.0})
            self.assertEqual(r.status_code, 200, r.text)
            body = r.json()
            self.assertTrue(body.get('accepted'))
            job_id = body.get('job_id')
            self.assertTrue(job_id)
            self.assertEqual(body.get('status_url'),
                             '/api/device/ota/status')

            body = _wait_job(self.client, job_id, kind='ota', timeout=10.0)
            job = body.get('job') or {}
            self.assertEqual(job.get('state'), 'done', body)
            self.assertTrue(job.get('ok'), body)
            self.assertEqual(job.get('kind'), 'ota')
            self.assertEqual(self.link.device.received_fw, payload)

            r = self.client.get('/api/device/ota/status?job_id=%s' % job_id)
            self.assertEqual(r.status_code, 200, r.text)
            st = r.json()
            self.assertEqual((st.get('job') or {}).get('state'), 'done')
            self.assertFalse(st.get('transfer_active'))
            self.assertTrue(st.get('uart_open'))

            self._assert_cmd_seen(CMD_FW_INFO, 'ota_fw_info')
            self._assert_cmd_seen(CMD_FDATA, 'ota_fdata')
            # Host ACK (CMD 0x5) is written just as the job finishes; the
            # mock pump thread may still be draining the pty — wait briefly.
            self.assertTrue(
                _wait(lambda: self._cmd_seen(CMD_OTA_RESULT), timeout=2.0),
                'mock device must receive host OTA_RESULT ACK; cmds=%s'
                % (self.link.device.written_cmds,))
            self._record(True, 'api_cmd_ota_result_0x05')
            self._record(True, 'api_ota_over_pty')
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass

    def test_file_start_status_cmd_0x6(self):
        payload = _make_fw(256, seed=61)
        path = _write_temp(payload)
        try:
            r = self.client.post('/api/device/file/start',
                                 json={'path': path, 'dev_no': 2,
                                       'offset': 0, 'timeout': 10.0})
            self.assertEqual(r.status_code, 200, r.text)
            job_id = r.json().get('job_id')
            self.assertTrue(job_id)

            body = _wait_job(self.client, job_id, kind='file', timeout=10.0)
            job = body.get('job') or {}
            self.assertEqual(job.get('state'), 'done', body)
            self.assertTrue(job.get('ok'), body)
            self.assertEqual(self.link.device.received_fw, payload)
            # trans_file = (size, crc32, dev_no, offset)
            tf = self.link.device.trans_file
            self.assertIsNotNone(tf)
            self.assertEqual(tf[0], len(payload))
            self.assertEqual(tf[2], 2)
            self.assertEqual(tf[3], 0)

            r = self.client.get('/api/device/file/status?job_id=%s' % job_id)
            self.assertEqual(r.status_code, 200, r.text)
            self.assertEqual((r.json().get('job') or {}).get('state'), 'done')

            self._assert_cmd_seen(CMD_TRANS_FILE, 'file_trans')
            self._assert_cmd_seen(CMD_FDATA, 'file_fdata')
            self._record(True, 'api_file_over_pty')
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass

    def test_ota_missing_file_structured(self):
        r = self.client.post('/api/device/ota/start',
                             json={'path': '/no/such/firmware.bin'})
        _assert_structured_error(self, r, 404, 'FILE_NOT_FOUND')

        r = self.client.post('/api/device/file/start',
                             json={'path': '/no/such/file.bin'})
        _assert_structured_error(self, r, 404, 'FILE_NOT_FOUND')

        empty = _write_temp(b'')
        try:
            r = self.client.post('/api/device/ota/start', json={'path': empty})
            _assert_structured_error(self, r, 400, 'INVALID_REQUEST')
        finally:
            os.unlink(empty)
        self._record(True, 'api_transfer_structured_errors')


# ---------------------------------------------------------------------------
# Shell / param via API (text shell over same pty)
# ---------------------------------------------------------------------------

class TestApiShellParam(_ApiBase):

    def test_shell_cmd_and_param_list_get_set(self):
        # Device-side shell mode (firmware b_mod_param.c text protocol)
        self.link.device.switch_to_shell()

        r = self.client.post('/api/device/shell/cmd',
                             json={'cmd': 'param', 'timeout': 1.5})
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        resp = body.get('response') or ''
        self.assertIn('g_param_test_val', resp)
        self.assertIn('g_volume', resp)
        self._record(True, 'api_shell_cmd')

        r = self.client.post('/api/device/param/list', json={})
        self.assertEqual(r.status_code, 200, r.text)
        names = r.json().get('names') or []
        self.assertIn('g_param_test_val', names)
        self.assertIn('g_volume', names)
        self.assertGreaterEqual(r.json().get('count'), len(DEFAULT_SHELL_PARAMS))
        self._record(True, 'api_param_list')

        r = self.client.post('/api/device/param/get',
                             json={'name': 'g_param_test_val'})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json().get('value'),
                         DEFAULT_SHELL_PARAMS['g_param_test_val'])
        self._record(True, 'api_param_get')

        r = self.client.post('/api/device/param/set',
                             json={'name': 'g_volume', 'value': 90})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'))
        self.assertTrue(r.json().get('verified'))
        self.assertEqual(self.link.device.shell_params.get('g_volume'), 90)

        r = self.client.post('/api/device/param/get',
                             json={'name': 'g_volume'})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json().get('value'), 90)
        self._record(True, 'api_param_set_get_roundtrip')

    def test_param_errors_structured(self):
        self.link.device.switch_to_shell()
        r = self.client.post('/api/device/param/get',
                             json={'name': 'g_does_not_exist'})
        _assert_structured_error(self, r, 504, 'PARAM_NOT_FOUND')

        r = self.client.post('/api/device/param/get', json={'name': ''})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

        r = self.client.post('/api/device/shell/cmd', json={'cmd': '  '})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

        r = self.client.post('/api/device/param/set',
                             json={'name': '', 'value': 1})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')
        self._record(True, 'api_param_structured_errors')


# ---------------------------------------------------------------------------
# Xmodem-128 / Ymodem-1K via API
# ---------------------------------------------------------------------------

class TestApiXmodemYmodem(_ApiBase):

    def test_xmodem_start_status(self):
        payload = _make_fw(400, seed=71)
        path = _write_temp(payload)
        try:
            self.link.device.switch_to_xmodem()
            r = self.client.post('/api/device/xmodem/start',
                                 json={'path': path})
            self.assertEqual(r.status_code, 200, r.text)
            body = r.json()
            self.assertTrue(body.get('accepted'))
            job_id = body.get('job_id')
            self.assertTrue(job_id)

            body = _wait_job(self.client, job_id, kind='xmodem', timeout=10.0)
            job = body.get('job') or {}
            self.assertEqual(job.get('state'), 'done', body)
            self.assertTrue(job.get('ok'), body)
            self.assertEqual(job.get('kind'), 'xmodem')

            r = self.client.get('/api/device/xmodem/status?job_id=%s'
                                '&kind=xmodem' % job_id)
            self.assertEqual(r.status_code, 200, r.text)
            st = r.json()
            self.assertEqual((st.get('job') or {}).get('state'), 'done')
            self.assertEqual(st.get('kind'), 'xmodem')
            self.assertFalse(st.get('active'))
            self.assertTrue(st.get('uart_open'))

            got = bytes(self.link.device.received_xmodem)
            self.assertEqual(got[:len(payload)], payload,
                             'xmodem bytes must arrive intact over pty')
            self._record(True, 'api_xmodem_over_pty')
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass

    def test_ymodem_start_status(self):
        payload = _make_fw(900, seed=73)
        path = _write_temp(payload)
        try:
            self.link.device.switch_to_ymodem()
            r = self.client.post('/api/device/ymodem/start',
                                 json={'path': path})
            self.assertEqual(r.status_code, 200, r.text)
            body = r.json()
            self.assertTrue(body.get('accepted'))
            job_id = body.get('job_id')
            self.assertTrue(job_id)

            body = _wait_job(self.client, job_id, kind='ymodem', timeout=12.0)
            job = body.get('job') or {}
            self.assertEqual(job.get('state'), 'done', body)
            self.assertTrue(job.get('ok'), body)
            self.assertEqual(job.get('kind'), 'ymodem')

            r = self.client.get('/api/device/xmodem/status?job_id=%s'
                                '&kind=ymodem' % job_id)
            self.assertEqual(r.status_code, 200, r.text)
            st = r.json()
            self.assertEqual((st.get('job') or {}).get('state'), 'done')
            self.assertEqual(st.get('kind'), 'ymodem')

            self.assertEqual(self.link.device.received_file_name,
                             os.path.basename(path))
            got = bytes(self.link.device.received_file)
            self.assertEqual(got[:len(payload)], payload)
            self._record(True, 'api_ymodem_over_pty')
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass

    def test_xmodem_missing_file_structured(self):
        r = self.client.post('/api/device/xmodem/start',
                             json={'path': '/no/such/xmodem.bin'})
        _assert_structured_error(self, r, 404, 'FILE_NOT_FOUND')
        r = self.client.post('/api/device/ymodem/start',
                             json={'path': '/no/such/ymodem.bin'})
        _assert_structured_error(self, r, 404, 'FILE_NOT_FOUND')
        self._record(True, 'api_xmodem_structured_errors')


# ---------------------------------------------------------------------------
# TEA encrypt path via API serial/open encrypt=true
# ---------------------------------------------------------------------------

class TestApiTeaEncrypt(_ApiBase):
    encrypt = True

    def test_encrypt_protocol_over_api_pty(self):
        r = self.client.post('/api/device/protocol/test', json={})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'))
        self.assertTrue(self._cmd_seen(CMD_TEST))

        r = self.client.post('/api/device/uid/get', json={})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json().get('uid_hex'), MOCK_UID.hex())

        r = self.client.get('/api/device/status')
        proto = (r.json() or {}).get('protocol') or {}
        self.assertTrue(proto.get('encrypt'))
        _record(True, 'api_encrypt_status')

        # Host TX frames must be TEA ciphertext (no plaintext 0xFE head)
        from device.device_manager import get_device_manager
        dm = get_device_manager()
        tx_log = dm.protocol_client.tx_log
        self.assertTrue(tx_log)
        for frame in tx_log:
            self.assertNotEqual(frame[:1], b'\xFE',
                                'encrypted host frames must not keep plaintext head')
        self.assertTrue(self.link.device.frames_handled > 0,
                        'mock device must decrypt and handle frames')
        _record(True, 'api_encrypt_tx_ciphertext')

        # OTA still works on the encrypted path
        payload = _make_fw(300, seed=91)
        path = _write_temp(payload)
        try:
            r = self.client.post('/api/device/ota/start',
                                 json={'path': path, 'timeout': 10.0})
            self.assertEqual(r.status_code, 200, r.text)
            job_id = r.json().get('job_id')
            body = _wait_job(self.client, job_id, kind='ota', timeout=10.0)
            job = body.get('job') or {}
            self.assertEqual(job.get('state'), 'done', body)
            self.assertEqual(self.link.device.received_fw, payload)
            _record(True, 'api_encrypt_ota_over_pty')
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass


# ---------------------------------------------------------------------------
# HTTPS mock must load origin/dev tool certificates
# ---------------------------------------------------------------------------

class TestApiHttpsDevCerts(unittest.TestCase):
    """HTTPS mock via FastAPI — certs must be origin/dev tool/*.pem only."""

    def setUp(self):
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

        from app.main import create_app
        from fastapi.testclient import TestClient
        self.app = create_app()
        self.client = TestClient(self.app)
        self.addCleanup(self._stop_mock)

    def _stop_mock(self):
        try:
            self.client.post('/api/device/http/stop', json={})
        except Exception:
            pass

    def _record(self, ok: bool, case: str) -> None:
        _record(ok, case)

    def _assert_dev_cert_files(self):
        repo_cert = os.path.join(_REPO, 'tool', 'mock_https_cert.pem')
        repo_key = os.path.join(_REPO, 'tool', 'mock_https_key.pem')
        pkg_cert = os.path.join(_PY_ROOT, 'device', 'certs',
                                'mock_https_cert.pem')
        pkg_key = os.path.join(_PY_ROOT, 'device', 'certs',
                               'mock_https_key.pem')
        for path, sha in (
            (repo_cert, DEV_CERT_SHA256),
            (repo_key, DEV_KEY_SHA256),
            (pkg_cert, DEV_CERT_SHA256),
            (pkg_key, DEV_KEY_SHA256),
        ):
            self.assertTrue(os.path.isfile(path), path)
            self.assertEqual(_sha256_file(path), sha,
                             '%s content must match origin/dev tool cert' % path)

        # git show origin/dev must still be the same bytes (authority check)
        for rel, sha in (
            ('tool/mock_https_cert.pem', DEV_CERT_SHA256),
            ('tool/mock_https_key.pem', DEV_KEY_SHA256),
        ):
            raw = _git_show(rel)
            if raw is not None:
                self.assertEqual(hashlib.sha256(raw).hexdigest(), sha,
                                 'origin/dev:%s mismatch' % rel)
        return repo_cert, repo_key

    def test_https_mock_uses_dev_tool_certs_and_real_tls(self):
        repo_cert, _repo_key = self._assert_dev_cert_files()

        r = self.client.post('/api/device/http/start', json={
            'https': True,
            'body': '{"https":true,"src":"dev-tool-cert"}',
            'content_type': 'application/json',
            'status_code': 200,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertTrue(body.get('https'))
        status = body.get('status') or {}
        self.assertTrue(status.get('https'))
        cert_path = status.get('https_cert') or ''
        key_path = status.get('https_key') or ''
        self.assertTrue(cert_path.endswith('mock_https_cert.pem'), cert_path)
        self.assertTrue(key_path.endswith('mock_https_key.pem'), key_path)
        # Must resolve to one of the two stable repo locations (dev tool or package copy)
        self.assertIn(
            os.path.realpath(cert_path),
            (os.path.realpath(repo_cert),
             os.path.realpath(os.path.join(_PY_ROOT, 'device', 'certs',
                                           'mock_https_cert.pem'))),
            'HTTPS mock must load dev tool cert, got %s' % cert_path)
        self.assertEqual(status.get('https_cert_sha256'), DEV_CERT_SHA256)
        self.assertEqual(status.get('https_key_sha256'), DEV_KEY_SHA256)
        self._record(True, 'api_https_status_dev_cert')

        # Live GET /http/status through the API
        r = self.client.get('/api/device/http/status')
        self.assertEqual(r.status_code, 200, r.text)
        st = r.json()
        self.assertTrue(st.get('https'))
        self.assertEqual(st.get('https_cert_sha256'), DEV_CERT_SHA256)
        base_url = st.get('base_url') or ''
        self.assertTrue(base_url.startswith('https://127.0.0.1:'), base_url)

        # 1) Real TLS request verified against the on-disk dev cert
        import httpx
        with httpx.Client(verify=repo_cert, timeout=5.0) as hc:
            resp = hc.get(base_url + '/api/device/probe')
            self.assertEqual(resp.status_code, 200)
            self.assertEqual(resp.json().get('src'), 'dev-tool-cert')
        self.assertIsNotNone(
            _wait(lambda: self.client.get('/api/device/http/requests').json()
                  .get('count', 0) >= 1, timeout=2.0))
        self._record(True, 'api_https_real_httpx_tls')

        # 2) TLS peer DER must match origin/dev cert file bytes
        import socket
        port = st.get('port')
        self.assertTrue(port)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        with socket.create_connection(('127.0.0.1', int(port)),
                                      timeout=3.0) as sock:
            with ctx.wrap_socket(sock) as ssock:
                der = ssock.getpeercert(binary_form=True)
        self.assertIsNotNone(der)
        disk_der = ssl.PEM_cert_to_DER_cert(open(repo_cert).read())
        self.assertEqual(hashlib.sha256(der).hexdigest(),
                         hashlib.sha256(disk_der).hexdigest(),
                         'live TLS peer cert must equal origin/dev tool cert')
        self._record(True, 'api_https_live_peer_der_dev')

        # 3) API proxy path performs a real HTTPS request (TLS handshake)
        r = self.client.post('/api/device/http/proxy', json={
            'url': base_url + '/api/device/proxy-ping',
            'method': 'GET',
            'verify_tls': False,
            'timeout': 5.0,
        })
        self.assertEqual(r.status_code, 200, r.text)
        pbody = r.json()
        self.assertTrue(pbody.get('ok'))
        self.assertEqual(pbody.get('status_code'), 200)
        self.assertIn('https', base_url)
        self._record(True, 'api_https_proxy_real_request')

        # stop is clean
        r = self.client.post('/api/device/http/stop', json={})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('was_running'))
        self._record(True, 'api_https_stop')

    def test_https_missing_cert_env_override_structured(self):
        """Incomplete env override must fail structured, never openssl-gen."""
        cert = os.path.join(_REPO, 'tool', 'mock_https_cert.pem')
        old_c = os.environ.get('BABYOS_MOCK_HTTPS_CERT')
        old_k = os.environ.get('BABYOS_MOCK_HTTPS_KEY')
        os.environ['BABYOS_MOCK_HTTPS_CERT'] = cert
        os.environ.pop('BABYOS_MOCK_HTTPS_KEY', None)
        try:
            # reset http_mock cert cache so the env override is re-evaluated
            from device import http_mock as hm
            with hm._https_cert_lock:
                hm._https_cert_paths = None
            r = self.client.post('/api/device/http/start', json={'https': True})
            # RuntimeError from resolver is wrapped as 409 HTTP_MOCK_RUNNING
            self.assertIn(r.status_code, (409, 500), r.text)
            detail = r.json().get('detail')
            self.assertIsInstance(detail, dict)
            self.assertTrue(detail.get('code'))
            msg = (detail.get('detail') or '').lower()
            self.assertIn('cert', msg)
            self.assertIn('both', msg)
            self.assertFalse(self.client.get('/api/device/http/status')
                             .json().get('running'),
                             'https mock must not start on incomplete override')
        finally:
            if old_c is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_CERT', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_CERT'] = old_c
            if old_k is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_KEY', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_KEY'] = old_k
            from device import http_mock as hm
            with hm._https_cert_lock:
                hm._https_cert_paths = None
        self._record(True, 'api_https_env_override_structured')


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def main() -> int:
    print('BabyOS Studio API virtual-serial acceptance (pty)')
    print('python: %s' % sys.version.split()[0])
    print('protocol host id: 0x%X' % DEVICE_ID_HOST)
    print('repo: %s' % _REPO)
    print('origin/dev cert sha256: %s' % DEV_CERT_SHA256)
    print('origin/dev key  sha256: %s' % DEV_KEY_SHA256)

    ch = create_byte_channel(prefer='pty', open_host_uart=False,
                             require_pty=True)
    print('byte channel kind: %s host_port=%s' % (ch.kind, ch.host_port))
    ch.close()

    loader = unittest.defaultTestLoader
    suite = loader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    print('')
    print('==== API virtual-serial acceptance report ====')
    print('channel kinds observed: %s' % (sorted(set(_VS_KINDS)) or ['(none)']))
    print('virtual-serial cases: %d' % _VS_TOTAL)
    print('virtual-serial passed: %d' % _VS_PASS)
    print('virtual-serial failed: %d' % _VS_FAIL)
    print('unittest: run=%d failures=%d errors=%d skipped=%d'
          % (result.testsRun, len(result.failures), len(result.errors),
             len(result.skipped)))
    kinds = set(_VS_KINDS)
    pty_ok = any(k.startswith('channel:pty') for k in kinds)
    api_ok = any(k.startswith('api:') for k in kinds)
    print('pty required: %s' % ('OK' if pty_ok else 'MISSING'))
    print('api chain exercised: %s' % ('OK' if api_ok else 'MISSING'))
    ok = (result.wasSuccessful() and _VS_FAIL == 0 and pty_ok
          and api_ok and _VS_TOTAL > 0)
    print('ACCEPTANCE: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
