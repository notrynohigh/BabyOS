#!/usr/bin/env python3
"""
test_product_device_protocol — product-grade device-protocol acceptance over real pty.

Authority (硬性验收):
  1. Serial/protocol MUST use open_api_over_pty + MockBabyOSDevice
     (real byte I/O on a real pty — no FakeUart success paths).
  2. Coverage (product domain):
       - CMD 0x6 file transfer start + stop all-zero + merge_folder
         → allfile.bin → transfer chain end-to-end
       - CMD 0x9 TSL invoke
       - CMD 0x30 SETCFGNET / 0x31 GET_NETINFO (LE u32 IP parse)
       - CMD 0x40–0x44 voice switch / volume / stat / TTS
       - CMD 0x50–0x53 device HTTP init / request / deinit
  3. FastAPI TestClient paths exercised when feasible
     (open_api_over_pty already binds DeviceManager singleton).
  4. API routes must return structured errors when serial is closed
     (detail.code + detail.detail).
  5. Python 3.8 compatible; do not modify bos/thirdparty; no git commit.

Run:
  cd /home/yyds/code/BabyOS/tool/babyos-studio/python && \
    .venv/bin/python -m pytest ../test/device_features/test_product_device_protocol.py -v
"""

from __future__ import print_function

import json
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
from device.crc_util import crc32 as crc32_util  # noqa: E402
from device.file_util import merge_folder, parse_merged_file  # noqa: E402
from device.protocol_client import DEVICE_ID_HOST  # noqa: E402
from mock_babyos_device import (  # noqa: E402
    open_api_over_pty,
    CMD_TRANS_FILE,
    CMD_TSL_INVOKE,
    CMD_SETCFGNET_MODE,
    CMD_GET_NETINFO,
    CMD_SET_VOICE_SWITCH,
    CMD_SET_VOICE_VOLUME,
    CMD_GET_VOICE_VOLUME,
    CMD_GET_VOICE_STAT,
    CMD_TTS_CONTENT,
    CMD_HTTP_INIT,
    CMD_HTTP_DEINIT,
    CMD_HTTP_REQUEST,
    CMD_HTTP_RESPONSE,
)

# Mock device default netinfo (little-endian u32)
MOCK_NETINFO_SSID = 'BabyOSNet'
MOCK_NETINFO_IP = 0x0102A8C0       # 192.168.2.1
MOCK_NETINFO_GW = 0x0101A8C0       # 192.168.1.1
MOCK_NETINFO_MASK = 0x00FFFFFF     # 255.255.255.0


def _u32_le_str(v):
    b = struct.pack('<I', v & 0xFFFFFFFF)
    return '.'.join(str(x) for x in b)


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


def _make_merge_folder(n_files=3):
    """Create a temp folder with n_files distinct payload files."""
    folder = tempfile.mkdtemp(prefix='babyos_prod_merge_')
    for i in range(n_files):
        name = 'part_%02d.bin' % i
        data = bytes(bytearray(((i * 17) + 3) & 0xFF
                               for _ in range(20 + i * 5)))
        with open(os.path.join(folder, name), 'wb') as f:
            f.write(data)
    return folder


def _reset_device_state(device):
    """Clear mock-device transfer / command state between cases."""
    device.written_cmds.clear()
    device.trans_file = None
    device.received_fw_bytes = bytearray()
    device.received_chunks = []
    device.last_ota_result = None
    device.last_tsl = b''
    device.last_cfgnet = None
    device.last_tts = ''
    device.last_http_request = None
    device.http_inited = False
    device.voice_switch = 1
    device.voice_volume = 50
    device.voice_stat = bp.VOICE_STAT_IDLE


class TestProductProtocolOverPty(unittest.TestCase):
    """
    DeviceManager path over real pty (open_host_uart=True).

    Exercises the full product protocol surface end-to-end on real bytes:
    merge → allfile.bin → CMD 0x6 transfer → stop-zero, plus
    TSL / net / voice / device-HTTP.
    """

    @classmethod
    def setUpClass(cls):
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        cls.link = open_api_over_pty(encrypt=False, open_host_uart=True)
        cls.device = cls.link.device
        cls.dm = cls.link.dm
        assert cls.dm is not None, 'DeviceManager not bound on pty link'
        cls.dm.ensure_clients()
        time.sleep(0.15)

    @classmethod
    def tearDownClass(cls):
        try:
            cls.link.close()
        except Exception:
            pass
        try:
            from device.device_manager import DeviceManager
            DeviceManager.reset()
        except Exception:
            pass

    def setUp(self):
        _reset_device_state(self.device)

    # ------------------------------------------------------------------
    # CMD 0x6 — merge_folder → allfile.bin → transfer chain → stop zeros
    # ------------------------------------------------------------------

    def test_merge_folder_to_allfile_transfer_chain(self):
        """Temp folder (2+ files) → merge → allfile.bin → CMD 0x6 transfer."""
        folder = _make_merge_folder(3)
        out_name = 'allfile.bin'

        # 1) merge via DeviceManager (same path as API /file/merge_folder)
        info = self.dm.merge_folder(folder, out_name)
        self.assertTrue(info.get('path'))
        self.assertEqual(info.get('file_count'), 3)
        self.assertEqual(info.get('size'),
                         os.path.getsize(info['path']))
        out_path = info['path']
        self.assertTrue(os.path.isfile(out_path))
        self.assertEqual(os.path.basename(out_path), out_name)

        # 2) allfile.bin records parse back to the original files
        ents = parse_merged_file(out_path)
        self.assertEqual(len(ents), 3)
        names = [e['name'] for e in ents]
        self.assertEqual(names, sorted(names), 'files must be sorted by name')
        for e in ents:
            src = os.path.join(folder, e['name'])
            with open(src, 'rb') as f:
                self.assertEqual(e['data'], f.read(),
                                 'merged payload mismatch for %s' % e['name'])

        with open(out_path, 'rb') as f:
            raw = f.read()
        self.assertEqual(info['crc32'], crc32_util(raw) & 0xFFFFFFFF)

        # 3) CMD 0x6 transfer of the merged allfile.bin
        pc = self.dm.protocol_client
        self.assertIsNotNone(pc)
        self.device.written_cmds.clear()
        self.device.trans_file = None
        self.device.received_fw_bytes = bytearray()
        self.device.last_ota_result = None

        ok = self.dm.start_file_transfer(out_path, dev_no=0, offset=0,
                                         timeout=10.0)
        self.assertTrue(ok, 'allfile.bin transfer must succeed over pty')
        self.assertIn(CMD_TRANS_FILE, self.device.written_cmds)
        self.assertIsNotNone(self.device.trans_file)
        size, crc, dev_no, offset = self.device.trans_file
        self.assertEqual(size, len(raw))
        self.assertEqual(crc, info['crc32'] & 0xFFFFFFFF)
        self.assertEqual(dev_no, 0)
        self.assertEqual(offset, 0)
        self.assertEqual(bytes(self.device.received_fw_bytes[:size]), raw)
        self.assertEqual(self.device.last_ota_result, bp.OTA_RESULT_OK)
        self.assertEqual(pc.transfer_result, bp.OTA_RESULT_OK)

        # 4) stop with all-zero CMD 0x6 (notify_device=True)
        self.device.written_cmds.clear()
        self.dm.stop_transfer(notify_device=True)
        deadline = time.time() + 2.0
        while time.time() < deadline:
            if CMD_TRANS_FILE in self.device.written_cmds:
                break
            time.sleep(0.01)
        self.assertIn(CMD_TRANS_FILE, self.device.written_cmds,
                      'stop_transfer(notify_device=True) must send CMD 0x6')
        self.assertEqual(self.device.trans_file, (0, 0, 0, 0),
                         'device must see all-zero size/crc/dev_no/offset')
        self.assertEqual(bp.build_trans_file_stop_param(), b'\x00' * 16)
        # completed transfer result stays OK (stop-after-done is not a failure)
        self.assertEqual(pc.transfer_result, bp.OTA_RESULT_OK)

    def test_0x6_single_file_transfer_with_dev_no_offset(self):
        """Single-file 0x6 transfer still honors dev_no/offset on the wire."""
        data = b'BABY-OS-FILE-CONTENT-0123456789'
        path = os.path.join(tempfile.mkdtemp(), 'single.bin')
        with open(path, 'wb') as f:
            f.write(data)

        self.device.trans_file = None
        self.device.received_fw_bytes = bytearray()
        self.device.last_ota_result = None

        ok = self.dm.start_file_transfer(path, dev_no=3, offset=0x1000,
                                         timeout=5.0)
        self.assertTrue(ok)
        size, crc, dev_no, offset = self.device.trans_file
        self.assertEqual(size, len(data))
        self.assertEqual(crc, crc32_util(data) & 0xFFFFFFFF)
        self.assertEqual(dev_no, 3)
        self.assertEqual(offset, 0x1000)
        self.assertEqual(bytes(self.device.received_fw_bytes[:size]), data)
        self.assertEqual(self.device.last_ota_result, bp.OTA_RESULT_OK)

        # stop zeros with notify_device=False must NOT rewrite result
        self.dm.stop_transfer(notify_device=False)
        self.assertEqual(self.device.last_ota_result, bp.OTA_RESULT_OK)

    # ------------------------------------------------------------------
    # CMD 0x9 TSL invoke
    # ------------------------------------------------------------------

    def test_0x9_tsl_invoke(self):
        content = '{"id":"1","method":"power","params":{"on":true}}'
        self.device.written_cmds.clear()
        self.device.last_tsl = b''
        resp = self.dm.invoke_tsl(content, timeout=2.0)
        self.assertIsNotNone(resp)
        device_id, cmd, param = resp
        self.assertEqual(cmd, CMD_TSL_INVOKE)
        self.assertIn(CMD_TSL_INVOKE, self.device.written_cmds)
        self.assertEqual(self.device.last_tsl, content.encode('utf-8'))
        # device replies empty ACK for TSL
        self.assertEqual(param, b'')

    # ------------------------------------------------------------------
    # CMD 0x30 SETCFGNET / 0x31 GET_NETINFO (LE u32 IP parse)
    # ------------------------------------------------------------------

    def test_0x30_cfgnet_mode_and_0x31_netinfo_le_u32(self):
        self.device.written_cmds.clear()
        resp = self.dm.set_cfgnet_mode(0, 'HomeAP', 'pw123456', timeout=2.0)
        self.assertIsNotNone(resp)
        device_id, cmd, param = resp
        self.assertEqual(cmd, CMD_SETCFGNET_MODE)
        self.assertIn(CMD_SETCFGNET_MODE, self.device.written_cmds)
        self.assertEqual(self.device.last_cfgnet, (0, 'HomeAP', 'pw123456'))

        # BLE mode (cfg_type=1) — empty ssid allowed as BLE toggle
        resp = self.dm.set_cfgnet_mode(1, '', '', timeout=2.0)
        self.assertIsNotNone(resp)
        self.assertEqual(self.device.last_cfgnet, (1, '', ''))

        info = self.dm.get_netinfo(timeout=2.0)
        self.assertIsNotNone(info)
        self.assertIn(CMD_GET_NETINFO, self.device.written_cmds)
        # LE u32 → dotted IP (mock defaults)
        self.assertEqual(info['ssid'], MOCK_NETINFO_SSID)
        self.assertEqual(info['ip'], _u32_le_str(MOCK_NETINFO_IP))
        self.assertEqual(info['ip'], '192.168.2.1')
        self.assertEqual(info['gw'], _u32_le_str(MOCK_NETINFO_GW))
        self.assertEqual(info['gw'], '192.168.1.1')
        self.assertEqual(info['mask'], _u32_le_str(MOCK_NETINFO_MASK))
        self.assertEqual(info['mask'], '255.255.255.0')
        self.assertEqual(info['ip_u32'], MOCK_NETINFO_IP)
        self.assertEqual(info['gw_u32'], MOCK_NETINFO_GW)
        self.assertEqual(info['mask_u32'], MOCK_NETINFO_MASK)

    # ------------------------------------------------------------------
    # CMD 0x40–0x44 voice
    # ------------------------------------------------------------------

    def test_voice_0x40_0x41_0x42_0x43_0x44(self):
        self.device.written_cmds.clear()
        self.device.voice_volume = 30
        self.device.voice_stat = bp.VOICE_STAT_PLAY
        self.device.last_tts = ''

        r = self.dm.set_voice_switch(0, timeout=2.0)
        self.assertIsNotNone(r)
        self.assertEqual(r[1], CMD_SET_VOICE_SWITCH)
        self.assertEqual(self.device.voice_switch, 0)

        r = self.dm.set_voice_switch(1, timeout=2.0)
        self.assertIsNotNone(r)
        self.assertEqual(self.device.voice_switch, 1)

        r = self.dm.set_voice_volume(77, timeout=2.0)
        self.assertIsNotNone(r)
        self.assertEqual(r[1], CMD_SET_VOICE_VOLUME)
        self.assertEqual(self.device.voice_volume, 77)

        vol = self.dm.get_voice_volume(timeout=2.0)
        self.assertEqual(vol, 77)

        st = self.dm.get_voice_stat(timeout=2.0)
        self.assertIsNotNone(st)
        self.assertEqual(st['code'], bp.VOICE_STAT_PLAY)
        self.assertEqual(st['state'], bp.VOICE_STAT_PLAY)
        self.assertEqual(st['name'], 'playing')

        r = self.dm.send_tts_content('Hello BabyOS', timeout=2.0)
        self.assertIsNotNone(r)
        self.assertEqual(r[1], CMD_TTS_CONTENT)
        self.assertEqual(self.device.last_tts, 'Hello BabyOS')

        for c in (CMD_SET_VOICE_SWITCH, CMD_SET_VOICE_VOLUME,
                  CMD_GET_VOICE_VOLUME, CMD_GET_VOICE_STAT, CMD_TTS_CONTENT):
            self.assertIn(c, self.device.written_cmds)

    def test_voice_stat_idle_mapping(self):
        self.device.voice_stat = bp.VOICE_STAT_IDLE
        st = self.dm.get_voice_stat(timeout=2.0)
        self.assertIsNotNone(st)
        self.assertEqual(st['name'], 'idle')

        self.device.voice_stat = bp.VOICE_STAT_LISTEN
        st = self.dm.get_voice_stat(timeout=2.0)
        self.assertEqual(st['name'], 'listening')

    # ------------------------------------------------------------------
    # CMD 0x50–0x53 device HTTP
    # ------------------------------------------------------------------

    def test_http_0x50_0x53_init_request_deinit(self):
        self.device.http_inited = False
        self.device.last_http_request = None
        self.device.written_cmds.clear()

        r = self.dm.http_init(timeout=2.0)
        self.assertIsNotNone(r)
        self.assertEqual(r[1], CMD_HTTP_INIT)
        self.assertTrue(self.device.http_inited)

        url = 'http://192.168.1.100:8080/status'
        resp = self.dm.http_request(
            'GET', url,
            headers='User-Agent: BabyOS', body=b'', timeout=3.0)
        self.assertIsNotNone(resp)
        self.assertEqual(resp['cmd'], CMD_HTTP_RESPONSE)
        self.assertEqual(resp['status'], 200)
        self.assertIn('192.168.1.100:8080/status', resp['body'])
        self.assertIn('mock', resp['body'].lower())
        self.assertIsNotNone(self.device.last_http_request)
        self.assertEqual(self.device.last_http_request['method'], 'GET')
        self.assertEqual(self.device.last_http_request['url'], url)

        # POST with body
        post_url = 'http://192.168.1.100:8080/api/data'
        resp = self.dm.http_request(
            'POST', post_url,
            headers='Content-Type: application/json',
            body=b'{"k":"v"}', timeout=3.0)
        self.assertIsNotNone(resp)
        self.assertEqual(resp['status'], 200)
        self.assertEqual(self.device.last_http_request['method'], 'POST')
        self.assertEqual(self.device.last_http_request['url'], post_url)
        self.assertEqual(self.device.last_http_request['body'], '{"k":"v"}')

        r = self.dm.http_deinit(timeout=2.0)
        self.assertIsNotNone(r)
        self.assertEqual(r[1], CMD_HTTP_DEINIT)
        self.assertFalse(self.device.http_inited)

        for c in (CMD_HTTP_INIT, CMD_HTTP_REQUEST, CMD_HTTP_DEINIT):
            self.assertIn(c, self.device.written_cmds)


class TestProductProtocolApiOverPty(unittest.TestCase):
    """
    FastAPI TestClient → DeviceManager → UartService → pty → MockBabyOSDevice.

    Real byte I/O; API routes for the product protocol surface.
    """

    def setUp(self):
        from device.device_manager import DeviceManager, get_device_manager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

        self.logs = []
        self.link = open_api_over_pty(
            encrypt=False,
            open_host_uart=False,
            bind_device_manager=False,
            device_kwargs={
                'encrypt': False,
                'log_fn': self.logs.append,
            },
            require_pty=True,
        )
        self.addCleanup(self.link.close)
        self.assertEqual(self.link.kind, 'pty',
                         'product protocol tests require real pty')
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
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('host_id'), DEVICE_ID_HOST)

        self.dm = get_device_manager()
        self.assertTrue(self.dm.is_open())
        _reset_device_state(self.device)

    def _wait_cmd(self, cmd, timeout=2.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self.device._lock:
                if cmd in self.device.written_cmds:
                    return True
            time.sleep(0.01)
        return False

    # ------------------------------------------------------------------
    # merge → allfile.bin → CMD 0x6 transfer → stop zeros (API path)
    # ------------------------------------------------------------------

    def test_api_merge_folder_to_allfile_transfer_chain(self):
        folder = _make_merge_folder(2)
        r = self.client.post('/api/device/file/merge_folder', json={
            'folder_path': folder,
            'out_name': 'allfile.bin',
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('file_count'), 2)
        out_path = body.get('path')
        self.assertTrue(out_path and os.path.isfile(out_path))
        self.assertEqual(os.path.basename(out_path), 'allfile.bin')

        ents = parse_merged_file(out_path)
        self.assertEqual(len(ents), 2)
        with open(out_path, 'rb') as f:
            raw = f.read()
        self.assertEqual(body.get('crc32'), crc32_util(raw) & 0xFFFFFFFF)

        # transfer the merged product via API /file/start
        _reset_device_state(self.device)
        r = self.client.post('/api/device/file/start', json={
            'path': out_path,
            'dev_no': 0,
            'offset': 0,
            'timeout': 10.0,
        })
        self.assertEqual(r.status_code, 200, r.text)
        # file/start returns job_id at the top level (not nested under job)
        job_id = r.json().get('job_id')
        self.assertTrue(job_id, 'file/start must return job_id: %s' % r.text)

        deadline = time.time() + 15.0
        state = None
        while time.time() < deadline:
            r = self.client.get('/api/device/file/status?job_id=%s' % job_id)
            if r.status_code == 200:
                st = r.json()
                job = st.get('job') or {}
                state = job.get('state') or st.get('xfer_state')
                if state in ('done', 'error', 'cancelled'):
                    break
            time.sleep(0.02)
        self.assertEqual(state, 'done',
                         'file job must reach done, got %s body=%s'
                         % (state, r.text if r is not None else ''))

        self.assertIn(CMD_TRANS_FILE, self.device.written_cmds)
        self.assertIsNotNone(self.device.trans_file)
        size, crc, dev_no, offset = self.device.trans_file
        self.assertEqual(size, len(raw))
        self.assertEqual(crc, body['crc32'] & 0xFFFFFFFF)
        self.assertEqual(bytes(self.device.received_fw_bytes[:size]), raw)
        self.assertEqual(self.device.last_ota_result, bp.OTA_RESULT_OK)

        # stop with notify_device=True → all-zero CMD 0x6
        self.device.written_cmds.clear()
        r = self.client.post('/api/device/file/stop', json={
            'notify_device': True,
        })
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('notified_device'))
        self.assertTrue(self._wait_cmd(CMD_TRANS_FILE))
        self.assertEqual(self.device.trans_file, (0, 0, 0, 0))

        # stop with notify_device=False must NOT send zeros
        self.device.written_cmds.clear()
        r = self.client.post('/api/device/file/stop', json={
            'notify_device': False,
        })
        self.assertEqual(r.status_code, 200, r.text)
        time.sleep(0.2)
        with self.device._lock:
            self.assertNotIn(CMD_TRANS_FILE, self.device.written_cmds,
                             'notify_device=False must not send CMD 0x6')

    def test_api_merge_folder_structured_errors(self):
        r = self.client.post('/api/device/file/merge_folder', json={
            'folder_path': '/nonexistent/babyos/folder',
        })
        _assert_structured_error(self, r, 404, 'FOLDER_NOT_FOUND')

        empty = tempfile.mkdtemp(prefix='babyos_empty_')
        r = self.client.post('/api/device/file/merge_folder', json={
            'folder_path': empty,
        })
        _assert_structured_error(self, r, 400, 'MERGE_FAILED')

    # ------------------------------------------------------------------
    # CMD 0x9 TSL via API
    # ------------------------------------------------------------------

    def test_api_tsl_invoke(self):
        content = '{"id":"9","method":"report","params":{"temp":36.5}}'
        r = self.client.post('/api/device/tsl/invoke', json={
            'content': content,
            'timeout': 2.0,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('cmd'), CMD_TSL_INVOKE)
        self.assertEqual(self.device.last_tsl, content.encode('utf-8'))

        # empty content → structured 400
        r = self.client.post('/api/device/tsl/invoke', json={'content': '  '})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

    # ------------------------------------------------------------------
    # CMD 0x30 / 0x31 via API
    # ------------------------------------------------------------------

    def test_api_net_cfgnet_and_netinfo(self):
        r = self.client.post('/api/device/net/set_cfgnet', json={
            'cfg_type': 0,
            'ssid': 'OfficeAP',
            'passwd': 'secret99',
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('cfg_type'), 0)
        self.assertEqual(body.get('ssid'), 'OfficeAP')
        self.assertEqual(self.device.last_cfgnet, (0, 'OfficeAP', 'secret99'))

        # invalid cfg_type → structured 400
        r = self.client.post('/api/device/net/set_cfgnet', json={
            'cfg_type': 7, 'ssid': 'x',
        })
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

        # AP mode with empty ssid → structured 400
        r = self.client.post('/api/device/net/set_cfgnet', json={
            'cfg_type': 0, 'ssid': '',
        })
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

        r = self.client.post('/api/device/net/get_info')
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('ssid'), MOCK_NETINFO_SSID)
        self.assertEqual(body.get('ip'), '192.168.2.1')
        self.assertEqual(body.get('gw'), '192.168.1.1')
        self.assertEqual(body.get('mask'), '255.255.255.0')

    # ------------------------------------------------------------------
    # CMD 0x40–0x44 voice via API
    # ------------------------------------------------------------------

    def test_api_voice_full_cycle(self):
        self.device.voice_volume = 40
        self.device.voice_stat = bp.VOICE_STAT_LISTEN
        self.device.last_tts = ''

        r = self.client.post('/api/device/voice/set_switch', json={'on': 0})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json().get('on'), 0)
        self.assertEqual(self.device.voice_switch, 0)

        r = self.client.post('/api/device/voice/set_switch', json={'on': 1})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(self.device.voice_switch, 1)

        r = self.client.post('/api/device/voice/set_volume', json={'volume': 88})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json().get('volume'), 88)
        self.assertEqual(self.device.voice_volume, 88)

        r = self.client.post('/api/device/voice/get_volume')
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json().get('volume'), 88)

        r = self.client.post('/api/device/voice/get_stat')
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('state'), bp.VOICE_STAT_LISTEN)
        self.assertEqual(body.get('name'), 'listening')

        content = '设备已连接'
        r = self.client.post('/api/device/voice/tts', json={'content': content})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json().get('content'), content)
        self.assertEqual(self.device.last_tts, content)

        r = self.client.post('/api/device/voice/tts', json={'content': '   '})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')

    # ------------------------------------------------------------------
    # CMD 0x50–0x53 device HTTP via API
    # ------------------------------------------------------------------

    def test_api_device_http_init_request_deinit(self):
        r = self.client.post('/api/device/http/init')
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('cmd'), CMD_HTTP_INIT)
        self.assertTrue(self.device.http_inited)

        url = 'http://10.0.0.5:9000/health'
        r = self.client.post('/api/device/http/request', json={
            'method': 'GET',
            'url': url,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('cmd'), CMD_HTTP_RESPONSE)
        self.assertEqual(body.get('status'), 200)
        self.assertIn('10.0.0.5:9000/health', body.get('body') or '')
        self.assertEqual(self.device.last_http_request['url'], url)
        self.assertEqual(self.device.last_http_request['method'], 'GET')

        # POST with body + headers
        post_url = 'http://10.0.0.5:9000/v1/items'
        r = self.client.post('/api/device/http/request', json={
            'method': 'POST',
            'url': post_url,
            'headers': {'Content-Type': 'application/json'},
            'body': '{"n":1}',
        })
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(self.device.last_http_request['method'], 'POST')
        self.assertEqual(self.device.last_http_request['url'], post_url)
        self.assertEqual(self.device.last_http_request['body'], '{"n":1}')

        r = self.client.post('/api/device/http/deinit')
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json().get('cmd'), CMD_HTTP_DEINIT)
        self.assertFalse(self.device.http_inited)

        # structured validation errors
        r = self.client.post('/api/device/http/request', json={'url': ''})
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')
        r = self.client.post('/api/device/http/request', json={
            'url': 'http://x/', 'method': 'TRACE',
        })
        _assert_structured_error(self, r, 400, 'INVALID_REQUEST')


class TestProductProtocolClosedSerial(unittest.TestCase):
    """
    Product protocol API routes must return structured errors when the
    serial port is closed (409 SERIAL_NOT_OPEN + detail.detail).
    """

    def setUp(self):
        from device.device_manager import DeviceManager, get_device_manager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

        self.link = open_api_over_pty(
            encrypt=False,
            open_host_uart=False,
            bind_device_manager=False,
            require_pty=True,
        )
        self.addCleanup(self.link.close)
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
        self.dm = get_device_manager()
        self.assertTrue(self.dm.is_open())

        # close the port — subsequent UART-required routes must error
        r = self.client.post('/api/device/serial/close', json={})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertFalse(r.json().get('open'))

    def _assert_closed(self, method, path, json_body=None):
        if method == 'GET':
            r = self.client.get(path)
        else:
            r = self.client.post(path, json=json_body or {})
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')
        return r

    def test_closed_serial_structured_errors_net_voice_tsl_http(self):
        self._assert_closed('POST', '/api/device/net/set_cfgnet',
                            {'cfg_type': 0, 'ssid': 'x'})
        self._assert_closed('POST', '/api/device/net/get_info')
        self._assert_closed('POST', '/api/device/voice/set_switch', {'on': 1})
        self._assert_closed('POST', '/api/device/voice/set_volume',
                            {'volume': 50})
        self._assert_closed('POST', '/api/device/voice/get_volume')
        self._assert_closed('POST', '/api/device/voice/get_stat')
        self._assert_closed('POST', '/api/device/voice/tts',
                            {'content': 'hi'})
        self._assert_closed('POST', '/api/device/tsl/invoke',
                            {'content': '{"m":1}'})
        self._assert_closed('POST', '/api/device/http/init')
        self._assert_closed('POST', '/api/device/http/deinit')
        self._assert_closed('POST', '/api/device/http/request',
                            {'method': 'GET', 'url': 'http://x/'})

    def test_closed_serial_structured_errors_file_start(self):
        data = b'x' * 32
        path = os.path.join(tempfile.mkdtemp(), 'closed.bin')
        with open(path, 'wb') as f:
            f.write(data)
        self._assert_closed('POST', '/api/device/file/start',
                            {'path': path})

    def test_closed_serial_structured_errors_protocol(self):
        self._assert_closed('POST', '/api/device/protocol/test')
        self._assert_closed('POST', '/api/device/protocol/set_time',
                            {'utc': 1700000000})

    def test_file_stop_and_merge_still_work_when_closed(self):
        """file/stop + merge_folder do not require UART — must stay available."""
        folder = _make_merge_folder(2)
        r = self.client.post('/api/device/file/merge_folder', json={
            'folder_path': folder,
        })
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'))

        r = self.client.post('/api/device/file/stop',
                             json={'notify_device': True})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'))


def main():
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    payload = {
        'schema': 'babyos_product_device_protocol_v1',
        'ok': result.wasSuccessful(),
        'tests': result.testsRun,
        'failures': len(result.failures),
        'errors': len(result.errors),
    }
    print(json.dumps(payload))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
