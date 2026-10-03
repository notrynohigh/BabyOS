#!/usr/bin/env python3
"""
test_virtual_serial — real virtual-serial (pty) acceptance for BabyOS Studio.

Authority (硬性验收):
  1. 串口/协议必须走真实 pty 双向字节通道 — 禁止只测接口/内存 mock。
  2. 主机侧真实栈: UartService / ProtocolClient / ShellClient /
     Xmodem / FastAPI DeviceManager → pty 一端。
  3. 设备侧 mock_babyos_device 在 pty 另一端真实解析 BabyOS 帧并回包。
  4. 覆盖 serial open/close、CMD 0x1/0x2/0x3/0x4/0x5/0x6/0x7/0x8/0xA、
     Xmodem-128、Ymodem-1K、Shell param list/get/set、TEA 加密路径、
     API → DeviceManager → 真实 UART 链路。
  5. HTTPS 证书必须是 origin/dev tool/mock_https_{cert,key}.pem。
  6. Python 3.8 兼容；不修改 bos/thirdparty；不提交 git；禁止 stub。
  7. FakeUart/FakeDevice 仅作补充；本文件全部用例走 virtual-serial。

Run:
  tool/babyos-studio/python/.venv/bin/python \
      tool/babyos-studio/test/device_features/test_virtual_serial.py
  or: pytest tool/babyos-studio/test/device_features/test_virtual_serial.py
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
    CMD_TEST, CMD_UTC, CMD_FW_INFO, CMD_FDATA, CMD_OTA_RESULT,
    CMD_TRANS_FILE, CMD_GET_UID, CMD_WRITE_SN, CMD_DEVICEINFO,
    OTA_RESULT_OK, OTA_RESULT_CRC_ERROR, OTA_RESULT_LEN_INVALID,
    DEVICE_ID_HOST,
)
from device.shell_client import ShellClient  # noqa: E402
from device.xmodem_ydmodem import XmodemSender, YmodemSender, XferState  # noqa: E402
from device.uart_service import UartService  # noqa: E402

from mock_babyos_device import (  # noqa: E402
    MockBabyOSDevice,
    XmodemReceiver,
    YmodemReceiver,
    create_byte_channel,
    open_api_over_pty,
    DEFAULT_SHELL_PARAMS,
)

MOCK_UID = b'\x11\x22\x33\x44\x55\x66\x77\x88\x99\xAA\xBB\xCC'
MOCK_VERSION = 'v1.2.3'
MOCK_MODEL = 'BabyOS-MCU'
MOCK_DEV_ID = 0x0000ABCD

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


def _make_fw(n: int, seed: int = 3) -> bytes:
    return bytes(bytearray(((i * 7) + seed) & 0xFF for i in range(n)))


def _write_temp(data: bytes, suffix: str = '.bin') -> str:
    fd, path = tempfile.mkstemp(suffix=suffix, prefix='babyos_vs_')
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


def _wait_job(client, job_id: str, timeout: float = 8.0) -> dict:
    deadline = time.time() + timeout
    body = {}
    while time.time() < deadline:
        r = client.get('/api/device/ota/status?job_id=%s' % job_id)
        if r.status_code != 200:
            r = client.get('/api/device/xmodem/status?job_id=%s&kind=xmodem'
                           % job_id)
            if r.status_code != 200:
                r = client.get('/api/device/xmodem/status?job_id=%s&kind=ymodem'
                               % job_id)
        if r.status_code == 200:
            body = r.json()
            job = body.get('job') or {}
            state = job.get('state') or body.get('xfer_state')
            if state in ('done', 'error', 'cancelled'):
                return body
        time.sleep(0.02)
    return body


class _VSBase(unittest.TestCase):
    """Shared pty fixture. Every test uses real virtual-serial bytes."""

    prefer = 'pty'
    require_pty = True

    def setUp(self):
        self.logs = []
        self.chan = create_byte_channel(prefer=self.prefer,
                                        open_host_uart=True,
                                        require_pty=self.require_pty)
        if self.require_pty:
            self.assertEqual(self.chan.kind, 'pty',
                             'virtual-serial acceptance requires pty')
        _VS_KINDS.append('channel:' + self.chan.kind)
        kwargs = dict(device_id=MOCK_DEV_ID, uid=MOCK_UID,
                      version=MOCK_VERSION, model=MOCK_MODEL,
                      log_fn=self.logs.append)
        kwargs.update(self.device_kwargs())
        self.device = MockBabyOSDevice(self.chan.device_endpoint(), **kwargs)
        self.device.start()
        self.pc = ProtocolClient(self.chan.host_uart, log_fn=self.logs.append)

    def device_kwargs(self):
        return {}

    def tearDown(self):
        try:
            self.device.stop()
        except Exception:
            pass
        try:
            self.chan.close()
        except Exception:
            pass

    def _record(self, ok: bool, case: str) -> None:
        _record(ok, case)


# ---------------------------------------------------------------------------
# Serial open/close + protocol CMD 0x1..0xA over pty
# ---------------------------------------------------------------------------

class TestVirtualSerialProtocol(_VSBase):
    """Host ProtocolClient ↔ mock device over real pty bytes."""

    def test_link_cmd_0x1(self):
        resp = self.pc.test_link(timeout=2.0)
        self.assertIsNotNone(resp, self.logs[-8:])
        self.assertEqual(resp[1], CMD_TEST)
        self.assertEqual(resp[2], b'')
        self.assertIn(CMD_TEST, self.device.written_cmds)
        self._record(True, 'protocol_cmd_0x1')

    def test_set_time_cmd_0x2(self):
        utc = 1700000000
        resp = self.pc.set_time(utc, timeout=2.0)
        self.assertIsNotNone(resp)
        self.assertEqual(resp[1], CMD_UTC)
        self.assertEqual(self.device.last_utc, utc)
        self._record(True, 'protocol_cmd_0x2')

    def test_get_uid_cmd_0x7(self):
        uid = self.pc.get_uid(timeout=2.0)
        self.assertEqual(uid, MOCK_UID)
        self._record(True, 'protocol_cmd_0x7')

    def test_write_sn_cmd_0x8(self):
        sn_body = bytes(bytearray(range(16)))
        param = bytes([len(sn_body)]) + sn_body
        resp = self.pc.write_sn(sn_bytes=param, timeout=2.0)
        self.assertIsNotNone(resp)
        self.assertEqual(self.device.last_sn, param)
        self._record(True, 'protocol_cmd_0x8')

    def test_device_info_cmd_0xA(self):
        info = self.pc.get_device_info(timeout=2.0)
        self.assertEqual(info, (MOCK_VERSION, MOCK_MODEL))
        self._record(True, 'protocol_cmd_0xA')

    def test_ota_cmd_0x3_0x4_0x5(self):
        payload = _make_fw(600, seed=33)
        path = _write_temp(payload)
        try:
            ok = self.pc.start_ota(path, timeout=6.0)
            self.assertTrue(ok, self.logs[-12:])
            self.assertEqual(self.device.received_fw[:len(payload)], payload)
            self.assertEqual(self.device.last_ota_result, OTA_RESULT_OK)
            self.assertIn(CMD_FW_INFO, self.device.written_cmds)
            self.assertIn(CMD_FDATA, self.device.written_cmds)
            # host ACK (0x5) may still be in flight when start_ota returns
            self.assertTrue(
                _wait(lambda: CMD_OTA_RESULT in self.device.written_cmds
                      or any(a[0] == CMD_OTA_RESULT
                             for a in self.device.host_acks),
                      timeout=2.0),
                'device never saw host OTA_RESULT ACK; cmds=%s'
                % self.device.written_cmds)
        finally:
            os.unlink(path)
        self._record(True, 'protocol_cmd_0x3_0x4_0x5')

    def test_file_xfer_cmd_0x6(self):
        payload = _make_fw(300, seed=9)
        path = _write_temp(payload)
        try:
            ok = self.pc.start_file_transfer(path, dev_no=1, offset=0x10,
                                             timeout=6.0)
            self.assertTrue(ok, self.logs[-12:])
            self.assertEqual(self.device.received_fw[:len(payload)], payload)
            self.assertEqual(self.device.trans_file[0], len(payload))
            self.assertEqual(self.device.trans_file[2], 1)
        finally:
            os.unlink(path)
        self._record(True, 'protocol_cmd_0x6')

    def test_ota_crc_error_path(self):
        self.device.force_crc_error = True
        payload = _make_fw(200, seed=1)
        path = _write_temp(payload)
        try:
            ok = self.pc.start_ota(path, timeout=6.0)
            self.assertFalse(ok)
            self.assertEqual(self.device.last_ota_result, OTA_RESULT_CRC_ERROR)
        finally:
            os.unlink(path)
        self._record(True, 'protocol_ota_crc_error')

    def test_ota_zero_len_invalid(self):
        # empty file rejected by host before wire; force via raw cmd
        param = bp.build_fw_info_param(0, 0, 'empty.bin')
        resp = self.pc.request_response(CMD_FW_INFO, param,
                                        expect_cmd=CMD_FW_INFO, timeout=2.0)
        self.assertIsNotNone(resp)
        # device then sends OTA_RESULT LEN_INVALID
        deadline = time.time() + 2.0
        got = None
        while time.time() < deadline and got is None:
            for _dev, cmd, p in self.pc.rx_log:
                if cmd == CMD_OTA_RESULT and p:
                    got = p[0]
            if got is None:
                raw = self.chan.host_uart.read_available()
                if raw:
                    self.pc.feed_bytes(raw)
                else:
                    time.sleep(0.01)
        self.assertEqual(got, OTA_RESULT_LEN_INVALID)
        self._record(True, 'protocol_ota_len_invalid')


# ---------------------------------------------------------------------------
# TEA encrypt path over pty (host encrypt=True → device decrypts)
# ---------------------------------------------------------------------------

class TestVirtualSerialTeaEncrypt(_VSBase):
    def device_kwargs(self):
        return {'encrypt': True}

    def test_test_link_encrypted(self):
        self.assertTrue(self.device.encrypt)
        # host also encrypts TX
        self.pc.encrypt = True
        resp = self.pc.test_link(timeout=2.0)
        self.assertIsNotNone(resp, self.logs[-10:])
        self.assertEqual(resp[1], CMD_TEST)
        # device really decrypted (handled CMD_TEST)
        self.assertIn(CMD_TEST, self.device.written_cmds)
        self._record(True, 'tea_test_link')

    def test_get_uid_encrypted(self):
        self.pc.encrypt = True
        uid = self.pc.get_uid(timeout=2.0)
        self.assertEqual(uid, MOCK_UID)
        self._record(True, 'tea_get_uid')

    def test_ota_encrypted(self):
        self.pc.encrypt = True
        payload = _make_fw(512, seed=21)
        path = _write_temp(payload)
        try:
            ok = self.pc.start_ota(path, timeout=6.0)
            self.assertTrue(ok, self.logs[-12:])
            self.assertEqual(self.device.received_fw[:len(payload)], payload)
        finally:
            os.unlink(path)
        self._record(True, 'tea_ota')

    def test_host_tx_is_ciphertext(self):
        self.pc.encrypt = True
        frame = self.pc.pack_cmd(CMD_TEST, bp.build_test_param())
        # HEAD byte is TEA-mangled — not 0xFE after encryption of whole frame
        self.assertNotEqual(frame[0], bp.PROTOCOL_HEAD,
                            'encrypt=True must TEA-encrypt whole TX frame')
        # decrypt restores a valid frame
        plain = bp.tea_decrypt(frame)
        parsed = bp.parse_frame(plain)
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed[1], CMD_TEST)
        self._record(True, 'tea_host_tx_ciphertext')


# ---------------------------------------------------------------------------
# Shell text protocol over pty (b_mod_param.c)
# ---------------------------------------------------------------------------

class TestVirtualSerialShell(_VSBase):
    def test_param_list_get_set(self):
        self.device.switch_to_shell()
        sh = ShellClient(self.chan.host_uart)
        names = sh.param_list(timeout=1.5)
        self.assertIn('g_param_test_val', names)
        self.assertIn('g_volume', names)
        self.assertIn('g_param_test_val', [n for n in names])

        val = sh.param_get('g_param_test_val', timeout=1.5)
        self.assertEqual(val, DEFAULT_SHELL_PARAMS['g_param_test_val'])

        ok = sh.param_set('g_volume', 80, timeout=1.5, verify=True)
        self.assertTrue(ok, self.logs[-8:])
        self.assertEqual(self.device.shell_params['g_volume'], 80)

        val2 = sh.param_get('g_volume', timeout=1.5)
        self.assertEqual(val2, 80)

        # firmware list format ": name"
        self.assertTrue(any(cmd == 'param' for cmd, _ in self.device.shell_log))
        self._record(True, 'shell_param_list_get_set')

    def test_param_set_new_key(self):
        self.device.switch_to_shell(params={'g_new_key': 1})
        sh = ShellClient(self.chan.host_uart)
        ok = sh.param_set('g_new_key', 42, timeout=1.5, verify=True)
        self.assertTrue(ok)
        self.assertEqual(self.device.shell_params['g_new_key'], 42)
        self._record(True, 'shell_param_set_new')


# ---------------------------------------------------------------------------
# Xmodem-128 over pty
# ---------------------------------------------------------------------------

class TestVirtualSerialXmodem(_VSBase):
    def _run_xmodem(self, payload: bytes) -> None:
        self.device.switch_to_xmodem()
        uart = self.chan.host_uart

        def _send(buf: bytes) -> None:
            n = uart.write(buf)
            if n is None or n < 0:
                raise IOError('host uart write failed for xmodem')

        sender = XmodemSender(_send, log_fn=self.logs.append)
        sender.start(payload)
        deadline = time.time() + 6.0
        while time.time() < deadline and sender.is_active:
            raw = uart.read_available()
            if raw:
                for b in raw:
                    sender.on_uart_byte(b)
            sender.on_timer_tick()
            time.sleep(0.002)
        raw = uart.read_available()
        if raw:
            for b in raw:
                sender.on_uart_byte(b)
        self.assertEqual(sender.state, XferState.DONE,
                         'xmodem sender state=%s logs=%s'
                         % (sender.state, self.logs[-8:]))
        got = bytes(self.device.received_xmodem)
        self.assertGreaterEqual(len(got), len(payload))
        self.assertEqual(got[:len(payload)], payload)

    def test_xmodem_small_file(self):
        payload = _make_fw(200, seed=5)
        self._run_xmodem(payload)
        self._record(True, 'xmodem_128_small')

    def test_xmodem_multi_block(self):
        payload = _make_fw(1500, seed=7)
        self._run_xmodem(payload)
        self._record(True, 'xmodem_128_multi')

    def test_xmodem_receiver_unit_crc(self):
        # unit-level: bad CRC → NAK (still real byte path into receiver)
        out = bytearray()

        def w(b):
            out.extend(b)

        rx = XmodemReceiver(w, log_fn=self.logs.append)
        payload = b'A' * 128
        bad = bytes([1, 1, 1]) + payload + struct.pack('>H', 0x1234)
        rx.feed(bad)
        self.assertEqual(rx.nak_count, 1)
        self._record(True, 'xmodem_receiver_nak')


# ---------------------------------------------------------------------------
# Ymodem-1K over pty (filename + length)
# ---------------------------------------------------------------------------

class TestVirtualSerialYmodem(_VSBase):
    def _run_ymodem(self, payload: bytes, filename: str) -> None:
        self.device.switch_to_ymodem()
        uart = self.chan.host_uart

        def _send(buf: bytes) -> None:
            n = uart.write(buf)
            if n is None or n < 0:
                raise IOError('host uart write failed for ymodem')

        sender = YmodemSender(_send, log_fn=self.logs.append)
        sender.start(payload, filename=filename)
        deadline = time.time() + 8.0
        while time.time() < deadline and sender.is_active:
            raw = uart.read_available()
            if raw:
                for b in raw:
                    sender.on_uart_byte(b)
            sender.on_timer_tick()
            time.sleep(0.002)
        raw = uart.read_available()
        if raw:
            for b in raw:
                sender.on_uart_byte(b)
        self.assertEqual(sender.state, XferState.DONE,
                         'ymodem sender state=%s logs=%s'
                         % (sender.state, self.logs[-10:]))
        self.assertEqual(self.device.ymodem.filename, filename)
        self.assertEqual(self.device.ymodem.file_size, len(payload))
        got = bytes(self.device.received_file)
        self.assertEqual(got[:len(payload)], payload)
        self.assertEqual(self.device.received_file_name, filename)

    def test_ymodem_with_name_and_size(self):
        payload = _make_fw(1300, seed=11)
        self._run_ymodem(payload, 'fw_update.bin')
        self._record(True, 'ymodem_1k_name_size')

    def test_ymodem_exact_1k_multiple(self):
        payload = _make_fw(2048, seed=13)
        self._run_ymodem(payload, 'exact.bin')
        self._record(True, 'ymodem_1k_exact')

    def test_ymodem_receiver_block0_parse(self):
        name, size = YmodemReceiver._parse_block0(
            b'app.bin\x001234\x00'.ljust(128, b'\x00'))
        self.assertEqual(name, 'app.bin')
        self.assertEqual(size, 1234)
        self._record(True, 'ymodem_block0_parse')


# ---------------------------------------------------------------------------
# DeviceManager production path over pty
# ---------------------------------------------------------------------------

class TestVirtualSerialDeviceManager(unittest.TestCase):
    """Studio DeviceManager.open_port + business APIs on real pty."""

    def _record(self, ok: bool, case: str) -> None:
        _record(ok, case)

    def test_device_manager_full_path(self):
        from device.device_manager import DeviceManager

        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)
        link = open_api_over_pty(encrypt=False, open_host_uart=False,
                                 bind_device_manager=False)
        self.addCleanup(link.close)
        self.assertEqual(link.kind, 'pty')

        dm = DeviceManager.get()
        ok = dm.open_port(link.host_port, 115200, encrypt=False)
        self.assertTrue(ok, 'DeviceManager.open_port failed on %s'
                        % link.host_port)
        try:
            self.assertTrue(dm.is_open())
            resp = dm.test_link(timeout=2.0)
            self.assertIsNotNone(resp)
            uid = dm.get_uid(timeout=2.0)
            self.assertEqual(uid, MOCK_UID)
            info = dm.get_device_info(timeout=2.0)
            self.assertEqual(info, (MOCK_VERSION, MOCK_MODEL))

            payload = _make_fw(512, seed=33)
            path = _write_temp(payload)
            try:
                ok_ota = dm.start_ota(path, timeout=8.0)
                self.assertTrue(ok_ota)
                self.assertEqual(link.device.received_fw, payload)
            finally:
                os.unlink(path)

            # Wait until the device has consumed the host OTA_RESULT ACK,
            # then switch to shell so late protocol bytes cannot glue onto
            # the first "param" line.
            self.assertTrue(
                _wait(lambda: any(a[0] == CMD_OTA_RESULT
                                  for a in link.device.host_acks),
                      timeout=2.0),
                'device never recorded host OTA ACK; snapshot=%s'
                % link.device.snapshot())
            time.sleep(0.05)
            try:
                dm.uart.read_available()
            except Exception:
                pass
            link.device.switch_to_shell()
            names = dm.param_list(timeout=2.0)
            self.assertIn('g_param_test_val', names,
                          'shell list empty; device_mode=%s snapshot=%s'
                          % (link.device.mode, link.device.snapshot()))
            ok_set = dm.param_set('g_volume', 66, timeout=2.0, verify=True)
            self.assertTrue(ok_set)
            self._record(True, 'device_manager_over_pty')
        finally:
            dm.close_port()

    def test_device_manager_encrypt_path(self):
        from device.device_manager import DeviceManager

        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)
        link = open_api_over_pty(encrypt=True, open_host_uart=False,
                                 device_kwargs={'encrypt': True},
                                 bind_device_manager=False)
        self.addCleanup(link.close)
        dm = DeviceManager.get()
        ok = dm.open_port(link.host_port, 115200, encrypt=True)
        self.assertTrue(ok)
        try:
            self.assertTrue(dm.protocol_client.encrypt)
            resp = dm.test_link(timeout=2.0)
            self.assertIsNotNone(resp, 'encrypted test_link over DM failed')
            uid = dm.get_uid(timeout=2.0)
            self.assertEqual(uid, MOCK_UID)
            payload = _make_fw(400, seed=41)
            path = _write_temp(payload)
            try:
                ok_ota = dm.start_ota(path, timeout=8.0)
                self.assertTrue(ok_ota)
                self.assertEqual(link.device.received_fw, payload)
            finally:
                os.unlink(path)
            self._record(True, 'device_manager_encrypt_over_pty')
        finally:
            dm.close_port()


# ---------------------------------------------------------------------------
# FastAPI API → DeviceManager → UART → pty → mock device
# ---------------------------------------------------------------------------

class TestVirtualSerialFastApi(unittest.TestCase):
    """Full API-over-pty: TestClient → /api/device/* → real UART → mock."""

    def setUp(self):
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)
        self.link = open_api_over_pty(encrypt=False, open_host_uart=False,
                                      bind_device_manager=False)
        self.addCleanup(self.link.close)
        self.assertEqual(self.link.kind, 'pty')

        from app.main import create_app
        from fastapi.testclient import TestClient
        self.app = create_app()
        self.client = TestClient(self.app)

        r = self.client.post('/api/device/serial/open', json={
            'path': self.link.host_port, 'baud': 115200, 'encrypt': False,
        })
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json()['ok'])
        self._record(True, 'api_serial_open_pty')

    def _record(self, ok: bool, case: str) -> None:
        _record(ok, case)

    def test_api_protocol_cmds(self):
        r = self.client.post('/api/device/protocol/test', json={})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json()['ok'])
        self._record(True, 'api_protocol_test')

        r = self.client.post('/api/device/protocol/set_time',
                             json={'utc': 1700000123})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(self.link.device.last_utc, 1700000123)
        self._record(True, 'api_protocol_set_time')

        r = self.client.post('/api/device/uid/get', json={})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json()['uid_hex'], MOCK_UID.hex())
        self._record(True, 'api_protocol_uid')

        r = self.client.post('/api/device/info/get', json={})
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertEqual(body.get('version'), MOCK_VERSION)
        self._record(True, 'api_protocol_info')

    def test_api_ota_over_pty(self):
        payload = _make_fw(512, seed=55)
        path = _write_temp(payload)
        try:
            r = self.client.post('/api/device/ota/start',
                                 json={'path': path, 'timeout': 8.0})
            self.assertEqual(r.status_code, 200, r.text)
            job_id = r.json().get('job_id')
            self.assertTrue(job_id)
            body = _wait_job(self.client, job_id, timeout=8.0)
            job = body.get('job') or {}
            self.assertEqual(job.get('state'), 'done', body)
            self.assertTrue(job.get('ok'), body)
            self.assertEqual(self.link.device.received_fw, payload)
        finally:
            os.unlink(path)
        self._record(True, 'api_ota_over_pty')

    def test_api_file_over_pty(self):
        payload = _make_fw(256, seed=61)
        path = _write_temp(payload)
        try:
            r = self.client.post('/api/device/file/start',
                                 json={'path': path, 'dev_no': 2,
                                       'offset': 0, 'timeout': 8.0})
            self.assertEqual(r.status_code, 200, r.text)
            job_id = r.json().get('job_id')
            body = _wait_job(self.client, job_id, timeout=8.0)
            job = body.get('job') or {}
            self.assertEqual(job.get('state'), 'done', body)
            self.assertEqual(self.link.device.received_fw, payload)
        finally:
            os.unlink(path)
        self._record(True, 'api_file_over_pty')

    def test_api_shell_over_pty(self):
        self.link.device.switch_to_shell()
        r = self.client.post('/api/device/param/list', json={})
        self.assertEqual(r.status_code, 200, r.text)
        names = r.json().get('names') or r.json().get('params') or []
        if not names:
            # tolerate alternate key
            names = r.json().get('list') or []
        self.assertIn('g_param_test_val', names)
        self._record(True, 'api_shell_param_list')

        r = self.client.post('/api/device/param/get',
                             json={'name': 'g_param_test_val'})
        self.assertEqual(r.status_code, 200, r.text)
        self._record(True, 'api_shell_param_get')

        r = self.client.post('/api/device/param/set',
                             json={'name': 'g_volume', 'value': 90})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(self.link.device.shell_params['g_volume'], 90)
        self._record(True, 'api_shell_param_set')

    def test_api_xmodem_over_pty(self):
        payload = _make_fw(400, seed=71)
        path = _write_temp(payload)
        try:
            self.link.device.switch_to_xmodem()
            r = self.client.post('/api/device/xmodem/start',
                                 json={'path': path})
            self.assertEqual(r.status_code, 200, r.text)
            job_id = r.json().get('job_id')
            # xmodem status endpoint
            deadline = time.time() + 8.0
            body = {}
            while time.time() < deadline:
                r = self.client.get('/api/device/xmodem/status?job_id=%s'
                                    % job_id)
                if r.status_code == 200:
                    body = r.json()
                    job = body.get('job') or {}
                    if job.get('state') in ('done', 'error', 'cancelled'):
                        break
                time.sleep(0.02)
            job = body.get('job') or {}
            self.assertEqual(job.get('state'), 'done', body)
            got = bytes(self.link.device.received_xmodem)
            self.assertEqual(got[:len(payload)], payload)
        finally:
            os.unlink(path)
        self._record(True, 'api_xmodem_over_pty')

    def test_api_ymodem_over_pty(self):
        payload = _make_fw(900, seed=73)
        path = _write_temp(payload)
        try:
            self.link.device.switch_to_ymodem()
            r = self.client.post('/api/device/ymodem/start',
                                 json={'path': path})
            self.assertEqual(r.status_code, 200, r.text)
            job_id = r.json().get('job_id')
            deadline = time.time() + 10.0
            body = {}
            while time.time() < deadline:
                r = self.client.get('/api/device/xmodem/status?job_id=%s'
                                    '&kind=ymodem' % job_id)
                if r.status_code == 200:
                    body = r.json()
                    job = body.get('job') or {}
                    if job.get('state') in ('done', 'error', 'cancelled'):
                        break
                time.sleep(0.02)
            job = body.get('job') or {}
            self.assertEqual(job.get('state'), 'done', body)
            self.assertEqual(self.link.device.received_file_name,
                             os.path.basename(path))
            got = bytes(self.link.device.received_file)
            self.assertEqual(got[:len(payload)], payload)
        finally:
            os.unlink(path)
        self._record(True, 'api_ymodem_over_pty')

    def test_api_serial_close(self):
        r = self.client.post('/api/device/serial/close', json={})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json()['ok'])
        self._record(True, 'api_serial_close')


# ---------------------------------------------------------------------------
# create_byte_channel policy
# ---------------------------------------------------------------------------

class TestCreateByteChannelPolicy(unittest.TestCase):
    def test_prefers_pty(self):
        ch = create_byte_channel(prefer='pty', require_pty=True)
        try:
            self.assertEqual(ch.kind, 'pty')
            self.assertTrue(ch.host_port and ch.host_port != 'socketpair')
        finally:
            ch.close()
        _record(True, 'channel_prefers_pty')

    def test_require_pty_marks_kind(self):
        ch = create_byte_channel(prefer='auto', require_pty=True)
        try:
            self.assertEqual(ch.kind, 'pty')
        finally:
            ch.close()
        _record(True, 'channel_require_pty_kind')


# ---------------------------------------------------------------------------
# HTTPS certs = origin/dev tool/mock_https_{cert,key}.pem
# ---------------------------------------------------------------------------

class TestHttpsCertsFromDev(unittest.TestCase):
    """证书权威: git show origin/dev:tool/mock_https_{cert,key}.pem"""

    CERT_SHA = '45020779c14d326cb5306b68687a2985f0b6c626f9587f1ce39493a77f9b034c'
    KEY_SHA = '24017d03c4f9b83779cc0d2d957528bf6333d7338369f1141b90e4d81dcfd689'

    def _sha(self, path: str) -> str:
        import hashlib
        with open(path, 'rb') as f:
            return hashlib.sha256(f.read()).hexdigest()

    def test_repo_tool_certs_match_origin_dev(self):
        repo = os.path.abspath(os.path.join(_STUDIO, '..', '..'))
        cert = os.path.join(repo, 'tool', 'mock_https_cert.pem')
        key = os.path.join(repo, 'tool', 'mock_https_key.pem')
        self.assertTrue(os.path.isfile(cert), cert)
        self.assertTrue(os.path.isfile(key), key)
        self.assertEqual(self._sha(cert), self.CERT_SHA)
        self.assertEqual(self._sha(key), self.KEY_SHA)
        _record(True, 'https_cert_repo_tool')

    def test_package_certs_copy_matches(self):
        pkg = os.path.join(_PY_ROOT, 'device', 'certs')
        cert = os.path.join(pkg, 'mock_https_cert.pem')
        key = os.path.join(pkg, 'mock_https_key.pem')
        self.assertTrue(os.path.isfile(cert), cert)
        self.assertTrue(os.path.isfile(key), key)
        self.assertEqual(self._sha(cert), self.CERT_SHA)
        self.assertEqual(self._sha(key), self.KEY_SHA)
        _record(True, 'https_cert_package_copy')

    def test_http_mock_resolves_fixed_certs(self):
        from device.http_mock import _resolve_https_cert
        cert, key = _resolve_https_cert()
        self.assertTrue(os.path.isfile(cert))
        self.assertTrue(os.path.isfile(key))
        self.assertEqual(self._sha(cert), self.CERT_SHA)
        self.assertEqual(self._sha(key), self.KEY_SHA)
        _record(True, 'https_cert_http_mock_resolve')

    def test_https_live_tls_client_cn_san(self):
        """https=true + real ssl/urllib/httpx client.

        Must load origin/dev tool certs (no openssl generation) and present
        subject CN=localhost with SAN containing DNS:localhost and IP:127.0.0.1.
        """
        import hashlib
        import ssl
        import urllib.request

        from device import http_mock as hm
        from device.http_mock import HttpMock

        cert, key = hm._resolve_https_cert()
        self.assertEqual(self._sha(cert), self.CERT_SHA)
        self.assertEqual(self._sha(key), self.KEY_SHA)

        # Decode the on-disk cert identity (CN + SAN)
        try:
            decoded = ssl._ssl._test_decode_cert(cert)
        except Exception as exc:  # pragma: no cover - helper missing
            self.fail('cannot decode cert %s: %s' % (cert, exc))
        cn = None
        for rdn in decoded.get('subject') or ():
            for name, value in rdn:
                if name == 'commonName':
                    cn = value
        self.assertEqual(cn, 'localhost',
                         'cert subject must be CN=localhost, got %r'
                         % (decoded.get('subject'),))
        san = decoded.get('subjectAltName') or ()
        san_dns = [v for t, v in san if t == 'DNS']
        san_ip = [v for t, v in san if t == 'IP Address']
        self.assertIn('localhost', san_dns,
                      'SAN must contain DNS:localhost, got %r' % (san,))
        self.assertIn('127.0.0.1', san_ip,
                      'SAN must contain IP:127.0.0.1, got %r' % (san,))

        mock = HttpMock()
        port = mock.start(port=0, body=b'{"https":1}', https=True)
        self.addCleanup(mock.stop)
        self.assertTrue(mock.https)
        st = mock.status()
        self.assertEqual(st['https_cert'], cert)
        self.assertEqual(st['https_cert_sha256'], self.CERT_SHA)
        # X.509 fingerprint of on-disk cert (DER) must be reported by mock
        der = ssl.PEM_cert_to_DER_cert(open(cert).read())
        x509_fp = ':'.join('%02X' % b for b in hashlib.sha256(der).digest())
        self.assertEqual(st['https_cert_x509_sha256'], x509_fp)

        # 1) urllib + ssl: trust the fixed cert as CA, verify IP SAN via 127.0.0.1
        ctx_ip = ssl.create_default_context(cafile=cert)
        ctx_ip.check_hostname = True
        url_ip = 'https://127.0.0.1:%d/api/ping' % port
        with urllib.request.urlopen(url_ip, context=ctx_ip, timeout=3.0) as resp:
            self.assertEqual(resp.status, 200)
            self.assertEqual(resp.read(), b'{"https":1}')
        self.assertIsNotNone(mock.wait_for_request(path_only='/api/ping',
                                                   timeout=1.0))

        # 2) urllib + ssl: hostname=localhost must also validate against SAN
        ctx_host = ssl.create_default_context(cafile=cert)
        ctx_host.check_hostname = True
        url_host = 'https://localhost:%d/api/ping2' % port
        try:
            with urllib.request.urlopen(url_host, context=ctx_host,
                                        timeout=3.0) as resp:
                self.assertEqual(resp.status, 200)
                self.assertEqual(resp.read(), b'{"https":1}')
            self.assertIsNotNone(mock.wait_for_request(path_only='/api/ping2',
                                                       timeout=1.0))
        except Exception as exc:
            # Some hosts resolve localhost oddly; fall back to raw ssl socket
            # with server_hostname=localhost against 127.0.0.1.
            import socket
            raw = ssl.create_default_context(cafile=cert)
            raw.check_hostname = True
            with socket.create_connection(('127.0.0.1', port),
                                          timeout=3.0) as sock:
                with raw.wrap_socket(sock, server_hostname='localhost') as ssock:
                    peer = ssock.getpeercert()
            cn2 = None
            for rdn in peer.get('subject') or ():
                for name, value in rdn:
                    if name == 'commonName':
                        cn2 = value
            self.assertEqual(cn2, 'localhost')
            san2 = peer.get('subjectAltName') or ()
            self.assertIn(('DNS', 'localhost'), san2)
            self.assertIn(('IP Address', '127.0.0.1'), san2)
            self.assertTrue(exc)  # fallback path used

        # 3) httpx client against the same mock (if installed)
        try:
            import httpx
        except ImportError:
            httpx = None
        if httpx is not None:
            with httpx.Client(verify=cert, timeout=3.0) as client:
                r = client.get(url_ip)
                self.assertEqual(r.status_code, 200)
                self.assertEqual(r.content, b'{"https":1}')
            _record(True, 'https_live_tls_httpx')
        _record(True, 'https_live_tls_cn_san')

    def test_https_live_peer_der_matches_origin_dev(self):
        """Live TLS peer cert DER must match origin/dev tool cert file."""
        import hashlib
        import socket
        import ssl

        from device import http_mock as hm
        from device.http_mock import HttpMock

        cert, key = hm._resolve_https_cert()
        mock = HttpMock()
        port = mock.start(port=0, body=b'{}', https=True)
        self.addCleanup(mock.stop)

        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        with socket.create_connection(('127.0.0.1', port),
                                      timeout=3.0) as sock:
            with ctx.wrap_socket(sock) as ssock:
                der = ssock.getpeercert(binary_form=True)
        self.assertIsNotNone(der)
        live_sha = hashlib.sha256(der).hexdigest().upper()
        disk_der = ssl.PEM_cert_to_DER_cert(open(cert).read())
        disk_sha = hashlib.sha256(disk_der).hexdigest().upper()
        self.assertEqual(live_sha, disk_sha,
                         'live TLS peer cert differs from origin/dev cert')
        _record(True, 'https_live_peer_der_matches_dev')


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

class _CountingResult(unittest.TextTestResult):
    def addSuccess(self, test):
        super(_CountingResult, self).addSuccess(test)

    def addFailure(self, test, err):
        super(_CountingResult, self).addFailure(test, err)

    def addError(self, test, err):
        super(_CountingResult, self).addError(test, err)

    def addSkip(self, test, reason):
        super(_CountingResult, self).addSkip(test, reason)


def main() -> int:
    print('BabyOS Studio virtual-serial acceptance (pty)')
    print('python: %s' % sys.version.split()[0])
    print('protocol host id: 0x%X' % DEVICE_ID_HOST)

    ch = create_byte_channel(prefer='pty', require_pty=True)
    print('byte channel kind: %s host_port=%s' % (ch.kind, ch.host_port))
    ch.close()

    loader = unittest.defaultTestLoader
    suite = loader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2, resultclass=_CountingResult)
    result = runner.run(suite)

    print('')
    print('==== virtual-serial acceptance report ====')
    print('channel kinds observed: %s' % (sorted(set(_VS_KINDS)) or ['(none)']))
    print('virtual-serial cases: %d' % _VS_TOTAL)
    print('virtual-serial passed: %d' % _VS_PASS)
    print('virtual-serial failed: %d' % _VS_FAIL)
    print('unittest: run=%d failures=%d errors=%d skipped=%d'
          % (result.testsRun, len(result.failures), len(result.errors),
             len(result.skipped)))
    # Acceptance: require pty and no failures
    kinds = set(_VS_KINDS)
    pty_ok = any(k.startswith('channel:pty') for k in kinds)
    print('pty required: %s' % ('OK' if pty_ok else 'MISSING'))
    ok = result.wasSuccessful() and _VS_FAIL == 0 and pty_ok and _VS_TOTAL > 0
    print('virtual-serial acceptance: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
