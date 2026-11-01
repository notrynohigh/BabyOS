#!/usr/bin/env python3
"""
test_devtool_gaps — BabyOS Studio host-tool gap acceptance vs origin/dev tool.

Covers features required by origin/dev tool/README + mainwindow + firmware:
  - file transfer CMD 0x6 + all-zero stop
  - folder merge allfile.bin (0xAA01/0xAA02)
  - network 0x30/0x31, voice 0x40–0x44, TSL 0x09
  - HTTP device cmds 0x50–0x53
  - HTTP mock SERVER_TIME + file log
  - param polling status / stop when idle
  - webconfig tool structured errors
  - log-to-file start/stop

Run:
  cd tool/babyos-studio/python && pytest \
      ../test/device_features/test_devtool_gaps.py -v
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
from device.file_util import merge_folder, parse_merged_file  # noqa: E402
from device.webconfig_tool import WebConfigTool  # noqa: E402
from device.protocol_client import ProtocolClient, DEVICE_ID_HOST  # noqa: E402
from device.crc_util import crc32 as crc32_util  # noqa: E402
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
    CMD_HTTP_REQUEST,
    CMD_HTTP_RESPONSE,
    CMD_HTTP_INIT,
    CMD_HTTP_DEINIT,
)


class TestProtocolBuilders(unittest.TestCase):
    def test_trans_file_stop_param_is_16_zero_bytes(self):
        param = bp.build_trans_file_stop_param()
        self.assertEqual(len(param), 16)
        self.assertEqual(param, b'\x00' * 16)
        parsed = bp.parse_trans_file_param(param)
        self.assertEqual(parsed, (0, 0, 0, 0))

    def test_cfgnet_param_layout(self):
        param = bp.build_set_cfgnet_param(1, 'MySSID', 'secret')
        self.assertEqual(len(param), 1 + 32 + 64)
        self.assertEqual(param[0], 1)
        self.assertEqual(param[1:1+8], b'MySSID\x00\x00')
        ptype, ssid, passwd = bp.parse_set_cfgnet_param(param)
        self.assertEqual((ptype, ssid, passwd), (1, 'MySSID', 'secret'))

    def test_http_request_param_roundtrip(self):
        param = bp.build_http_request_param('POST', 'http://192.168.1.100:8080/t',
                                            'Content-Type: application/json',
                                            b'{"a":1}')
        parsed = bp.parse_http_request_param(param)
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed['method'], 'POST')
        self.assertEqual(parsed['url'], 'http://192.168.1.100:8080/t')
        self.assertIn('Content-Type: application/json', parsed['headers'])
        self.assertEqual(parsed['body'], '{"a":1}')

    def test_http_response_param(self):
        raw = bp.build_http_response_param(201, 'hello')
        status, body = bp.parse_http_response_param(raw)
        self.assertEqual((status, body), (201, 'hello'))

    def test_netinfo_le_ip(self):
        # ssid(32) + ip/gw/mask as LE u32
        ssid = b'BabyOSNet'.ljust(32, b'\x00')
        ip = struct.pack('<I', 0x0102A8C0)   # 192.168.2.1
        gw = struct.pack('<I', 0x0101A8C0)   # 192.168.1.1
        mask = struct.pack('<I', 0x00FFFFFF)  # 255.255.255.0
        d = bp.netinfo_response_dict(ssid + ip + gw + mask)
        self.assertIsNotNone(d)
        self.assertEqual(d['ssid'], 'BabyOSNet')
        self.assertEqual(d['ip'], '192.168.2.1')
        self.assertEqual(d['gw'], '192.168.1.1')
        self.assertEqual(d['mask'], '255.255.255.0')

    def test_voice_volume_clamped(self):
        self.assertEqual(bp.build_voice_volume_param(150)[0], 100)
        self.assertEqual(bp.build_voice_volume_param(-5)[0], 0)

    def test_folder_merge_format(self):
        td = tempfile.mkdtemp()
        open(os.path.join(td, 'b.bin'), 'wb').write(b'BB')
        open(os.path.join(td, 'a.bin'), 'wb').write(b'AAA')
        out, n, size, crc = merge_folder(td)
        self.assertTrue(os.path.isfile(out))
        self.assertEqual(n, 2)
        ents = parse_merged_file(out)
        names = [e['name'] for e in ents]
        self.assertEqual(names, ['a.bin', 'b.bin'])
        self.assertEqual(ents[0]['data'], b'AAA')
        self.assertEqual(ents[1]['data'], b'BB')
        with open(out, 'rb') as f:
            raw = f.read()
        self.assertEqual(crc, crc32_util(raw) & 0xFFFFFFFF)


class TestDeviceProtocolGaps(unittest.TestCase):
    """Real pty + MockBabyOSDevice for protocol extensions."""

    @classmethod
    def setUpClass(cls):
        cls.link = open_api_over_pty(encrypt=False, open_host_uart=True)
        cls.device = cls.link.device
        cls.dm = cls.link.dm
        assert cls.dm is not None, 'DeviceManager not bound on pty link'
        # ensure protocol clients bound
        cls.dm.ensure_clients()
        time.sleep(0.2)

    @classmethod
    def tearDownClass(cls):
        try:
            cls.link.close()
        except Exception:
            pass

    def test_0x6_file_transfer_start_stop_zeros(self):
        path = os.path.join(tempfile.mkdtemp(), 'demo.bin')
        data = b'BABY-OS-FILE-CONTENT-0123456789'
        with open(path, 'wb') as f:
            f.write(data)
        from device.crc_util import crc32
        pc = self.dm.protocol_client
        self.assertIsNotNone(pc)
        self.device.written_cmds.clear()
        self.device.trans_file = None
        self.device.received_fw_bytes = bytearray()
        self.device.last_ota_result = None

        ok = self.dm.start_file_transfer(path, dev_no=0, offset=0,
                                         timeout=3.0)
        # transfer pump runs inside start_file_transfer; success expected
        self.assertTrue(ok)
        self.assertIn(CMD_TRANS_FILE, self.device.written_cmds)
        self.assertIsNotNone(self.device.trans_file)
        size, crc, dev_no, offset = self.device.trans_file
        self.assertEqual(size, len(data))
        self.assertEqual(crc, crc32(data) & 0xFFFFFFFF)
        self.assertEqual(bytes(self.device.received_fw_bytes[:size]), data)
        self.assertEqual(self.device.last_ota_result, bp.OTA_RESULT_OK)

        # stop with zeros (notify_device=True)
        self.device.written_cmds.clear()
        self.dm.stop_transfer(notify_device=True)
        time.sleep(0.3)
        # device must see CMD 0x6 all-zero (dev tool stop semantics)
        self.assertIn(CMD_TRANS_FILE, self.device.written_cmds)
        self.assertEqual(self.device.trans_file, (0, 0, 0, 0))
        self.assertEqual(bp.build_trans_file_stop_param(), b'\x00' * 16)
        # completed transfer result must remain OK (stop-after-done is not a failure)
        self.assertEqual(pc.transfer_result, bp.OTA_RESULT_OK)

    def test_0x9_tsl_invoke(self):
        self.device.written_cmds.clear()
        self.device.last_tsl = b''
        resp = self.dm.invoke_tsl('{"id":"1","method":"power"}', timeout=2.0)
        self.assertIsNotNone(resp)
        self.assertIn(CMD_TSL_INVOKE, self.device.written_cmds)
        self.assertEqual(self.device.last_tsl,
                         b'{"id":"1","method":"power"}')

    def test_0x30_cfgnet_and_0x31_netinfo(self):
        self.device.written_cmds.clear()
        resp = self.dm.set_cfgnet_mode(0, 'HomeAP', 'pw123456', timeout=2.0)
        self.assertIsNotNone(resp)
        self.assertIn(CMD_SETCFGNET_MODE, self.device.written_cmds)
        self.assertIsNotNone(self.device.last_cfgnet)
        self.assertEqual(self.device.last_cfgnet, (0, 'HomeAP', 'pw123456'))

        info = self.dm.get_netinfo(timeout=2.0)
        self.assertIsNotNone(info)
        self.assertIn(CMD_GET_NETINFO, self.device.written_cmds)
        self.assertEqual(info['ssid'], self.device.netinfo_ssid)
        self.assertEqual(info['ip'], '192.168.2.1')
        self.assertEqual(info['gw'], '192.168.1.1')
        self.assertEqual(info['mask'], '255.255.255.0')

    def test_voice_0x40_0x41_0x42_0x43_0x44(self):
        self.device.written_cmds.clear()
        self.device.voice_volume = 30
        self.device.voice_stat = bp.VOICE_STAT_PLAY
        self.device.last_tts = ''

        r = self.dm.set_voice_switch(0, timeout=2.0)
        self.assertIsNotNone(r)
        self.assertEqual(self.device.voice_switch, 0)

        r = self.dm.set_voice_volume(77, timeout=2.0)
        self.assertIsNotNone(r)
        self.assertEqual(self.device.voice_volume, 77)

        vol = self.dm.get_voice_volume(timeout=2.0)
        self.assertEqual(vol, 77)

        st = self.dm.get_voice_stat(timeout=2.0)
        self.assertIsNotNone(st)
        self.assertEqual(st['state'], bp.VOICE_STAT_PLAY)
        self.assertEqual(st['name'], 'playing')

        r = self.dm.send_tts_content('Hello BabyOS', timeout=2.0)
        self.assertIsNotNone(r)
        self.assertEqual(self.device.last_tts, 'Hello BabyOS')
        for c in (CMD_SET_VOICE_SWITCH, CMD_SET_VOICE_VOLUME,
                  CMD_GET_VOICE_VOLUME, CMD_GET_VOICE_STAT, CMD_TTS_CONTENT):
            self.assertIn(c, self.device.written_cmds)

    def test_http_0x50_0x53(self):
        self.device.http_inited = False
        self.device.last_http_request = None
        r = self.dm.http_init(timeout=2.0)
        self.assertIsNotNone(r)
        self.assertTrue(self.device.http_inited)
        self.assertIn(CMD_HTTP_INIT, self.device.written_cmds if hasattr(
            self.device, 'written_cmds') else [])

        resp = self.dm.http_request(
            'GET', 'http://192.168.1.100:8080/status',
            headers='User-Agent: BabyOS', body=b'', timeout=3.0)
        self.assertIsNotNone(resp)
        self.assertEqual(resp['cmd'], CMD_HTTP_RESPONSE)
        self.assertEqual(resp['status'], 200)
        self.assertIn('192.168.1.100:8080/status', resp['body'])
        self.assertIsNotNone(self.device.last_http_request)
        self.assertEqual(self.device.last_http_request['method'], 'GET')
        self.assertEqual(self.device.last_http_request['url'],
                         'http://192.168.1.100:8080/status')

        r = self.dm.http_deinit(timeout=2.0)
        self.assertIsNotNone(r)
        self.assertFalse(self.device.http_inited)

    def test_param_polling_stop_when_idle(self):
        st = self.dm.param_polling_status()
        self.assertIn('enabled', st)
        # stop is safe when not running
        self.dm.stop_param_polling()
        st2 = self.dm.param_polling_status()
        self.assertFalse(st2['enabled'])

    def test_log_to_file(self):
        path = os.path.join(tempfile.mkdtemp(), 'dm.log')
        self.dm.start_log_to_file(path)
        st = self.dm.log_to_file_status()
        self.assertTrue(st['enabled'])
        self.dm._log('devtool-gap-marker')
        self.dm.stop_log_to_file()
        self.assertFalse(self.dm.log_to_file_status()['enabled'])
        with open(path, 'r', encoding='utf-8') as f:
            text = f.read()
        self.assertIn('devtool-gap-marker', text)


class TestHttpMockServerTime(unittest.TestCase):
    def test_server_time_placeholder_and_file_log(self):
        import httpx
        from device.http_mock import HttpMock
        mock = HttpMock()
        try:
            port = mock.start(port=0, body='{"ts":"${SERVER_TIME}"}',
                              content_type='application/json',
                              status_code=200, https=False, file_log=True)
            base = mock.base_url
            with httpx.Client(verify=False, timeout=5.0) as client:
                resp = client.get(base + '/t')
            self.assertEqual(resp.status_code, 200)
            body = resp.text
            self.assertNotIn('${SERVER_TIME}', body)
            self.assertIn('"ts":"', body)
            st = mock.status()
            self.assertTrue(st.get('file_log', {}).get('enabled'))
            # traffic should have been written to mock_http.log path
            fl = st.get('file_log', {})
            if fl.get('path'):
                self.assertTrue(os.path.isfile(fl['path']))
        finally:
            mock.stop()


class TestWebConfigTool(unittest.TestCase):
    def test_status_and_structured_build_error(self):
        wd = tempfile.mkdtemp()
        cfg = os.path.join(wd, 'webconfig_tool.ini')
        wc = WebConfigTool(config_path=cfg)
        # missing keil/project → structured error, not exception
        wc.cfg['keil_uv4'] = os.path.join(wd, 'UV4.exe')
        wc.cfg['project_dir'] = os.path.join(wd, 'proj')
        wc.cfg['project_file_rel'] = 'proj.uvprojx'
        wc.cfg['openocd_dir'] = os.path.join(wd, 'openocd')
        st = wc.status()
        self.assertFalse(st['keil_uv4_exists'])
        self.assertFalse(st['project_exists'])
        self.assertFalse(st['can_build'])
        self.assertFalse(st['can_flash'])
        res = wc.build(timeout_sec=5)
        self.assertFalse(res.get('ok'))
        self.assertIn('error', res)
        res2 = wc.flash(timeout_sec=5)
        self.assertFalse(res2.get('ok'))
        self.assertIn('error', res2)
        # save_config roundtrip
        wc.save_config({'target_name': 'BabyOSL4'})
        wc2 = WebConfigTool(config_path=cfg)
        self.assertEqual(wc2.cfg.get('target_name'), 'BabyOSL4')


class TestApiRoutes(unittest.TestCase):
    def test_new_routes_registered(self):
        from app.api.device import router
        paths = {getattr(r, 'path', '') for r in router.routes}
        expect = {
            '/api/device/file/merge_folder',
            '/api/device/file/stop',
            '/api/device/net/set_cfgnet',
            '/api/device/net/get_info',
            '/api/device/voice/set_switch',
            '/api/device/voice/set_volume',
            '/api/device/voice/get_volume',
            '/api/device/voice/get_stat',
            '/api/device/voice/tts',
            '/api/device/tsl/invoke',
            '/api/device/http/init',
            '/api/device/http/deinit',
            '/api/device/http/request',
            '/api/device/param/poll/start',
            '/api/device/param/poll/stop',
            '/api/device/param/poll/status',
            '/api/device/log/start',
            '/api/device/log/stop',
            '/api/device/log/status',
            '/api/device/webconfig/status',
            '/api/device/webconfig/build',
            '/api/device/webconfig/flash',
            '/api/device/webconfig/log/start',
            '/api/device/webconfig/log/stop',
            '/api/device/webconfig/log/status',
        }
        missing = expect - paths
        self.assertFalse(missing, 'missing routes: %s' % sorted(missing))


def main():
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    payload = {
        'schema': 'babyos_devtool_gaps_v1',
        'ok': result.wasSuccessful(),
        'tests': result.testsRun,
        'failures': len(result.failures),
        'errors': len(result.errors),
    }
    print(json.dumps(payload))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
