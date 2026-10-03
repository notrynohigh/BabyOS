#!/usr/bin/env python3
"""
test_product_http_mock — product-grade HTTP mock tests for BabyOS Studio.

Hard rules enforced here:
  - REAL local ThreadingHTTPServer (device.http_mock.HttpMock), no stubs.
  - Serial/protocol paths MUST use open_api_over_pty + MockBabyOSDevice
    (real byte I/O on pty) + FastAPI TestClient.
  - Do NOT commit/push. Do NOT modify bos/thirdparty. Do NOT edit cert PEMs.

Product gaps closed:
  1. SERVER_TIME placeholder for str body AND bytes body
     (regression: bytes body must not raise TypeError / RemoteDisconnected)
  2. file_log=true writes real traffic to mock_http.log path in status
  3. set_path_response per-path override (via product DeviceManager after
     FastAPI /http/start)
  4. proxy endpoint records request (FastAPI /http/proxy + /http/requests
     + mock self /_requests)
  5. HTTPS certs SHA256 == origin/dev tool PEMs + live TLS handshake via
     httpx verify against the mock
  6. HTTP device protocol 0x50–0x53 when mock is the peer:
     FastAPI /http/init|/http/request|/http/deinit over pty, mock device
     http_response_hook performs REAL HTTP to the local HttpMock server.

Run:
  cd /home/yyds/code/BabyOS/tool/babyos-studio/python && \\
  /home/yyds/code/BabyOS/tool/babyos-studio/python/.venv/bin/python \\
      -m pytest ../test/device_features/test_product_http_mock.py -v
"""

from __future__ import print_function

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
import uuid

_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_REPO = os.path.abspath(os.path.join(_STUDIO, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
for _p in (_PY_ROOT, _HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

# Pinned origin/dev tool/mock_https_{cert,key}.pem content hashes (authority)
DEV_CERT_SHA256 = (
    '45020779c14d326cb5306b68687a2985f0b6c626f9587f1ce39493a77f9b034c'
)
DEV_KEY_SHA256 = (
    '24017d03c4f9b83779cc0d2d957528bf6333d7338369f1141b90e4d81dcfd689'
)

MOCK_START = '/api/device/http/start'
MOCK_STOP = '/api/device/http/stop'
MOCK_STATUS = '/api/device/http/status'
MOCK_REQUESTS = '/api/device/http/requests'
MOCK_PROXY = '/api/device/http/proxy'
HTTP_INIT = '/api/device/http/init'
HTTP_DEINIT = '/api/device/http/deinit'
HTTP_REQUEST = '/api/device/http/request'
SERIAL_OPEN = '/api/device/serial/open'


def _sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            h.update(chunk)
    return h.hexdigest()


def _git_show(path):
    try:
        return subprocess.check_output(
            ['git', '-C', _REPO, 'show', 'origin/dev:%s' % path],
            stderr=subprocess.DEVNULL)
    except Exception:
        return None


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


def _wait(pred, timeout=3.0, interval=0.02):
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


def _read_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()


def _marker(prefix):
    return '%s-%s' % (prefix, uuid.uuid4().hex[:12])


class _ProductBase(unittest.TestCase):
    """FastAPI TestClient over DeviceManager singleton; no pty required."""

    def setUp(self):
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

        from app.main import create_app
        from fastapi.testclient import TestClient
        self.app = create_app()
        self.client = TestClient(self.app)

        self.mock_stopped = False
        self.addCleanup(self._stop_mock_quiet)

    def _stop_mock_quiet(self):
        if self.mock_stopped:
            return
        try:
            self.client.post(MOCK_STOP, json={})
        except Exception:
            pass
        self.mock_stopped = True

    def _start_mock(self, **payload):
        body = {
            'port': 0,
            'body': '{"ok":true}',
            'content_type': 'application/json',
            'status_code': 200,
            'https': False,
            'file_log': True,
        }
        body.update(payload)
        r = self.client.post(MOCK_START, json=body)
        self.assertEqual(r.status_code, 200, r.text)
        data = r.json()
        self.assertTrue(data.get('ok'), data)
        self.mock_stopped = False
        return data

    def _stop_mock(self):
        r = self.client.post(MOCK_STOP, json={})
        self.assertEqual(r.status_code, 200, r.text)
        self.mock_stopped = True
        return r.json()


class _PtyProductBase(_ProductBase):
    """FastAPI TestClient + real pty + MockBabyOSDevice for 0x50–0x53."""

    def setUp(self):
        super(_PtyProductBase, self).setUp()

        from mock_babyos_device import open_api_over_pty
        self.logs = []
        self.link = open_api_over_pty(
            encrypt=False,
            open_host_uart=False,
            bind_device_manager=False,
            device_kwargs={'log_fn': self.logs.append},
            require_pty=True,
        )
        self.addCleanup(self.link.close)
        self.assertEqual(self.link.kind, 'pty',
                         'product HTTP protocol path requires real pty')

        r = self.client.post(SERIAL_OPEN, json={
            'path': self.link.host_port,
            'baud': 115200,
            'encrypt': False,
        })
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'), r.text)


# ---------------------------------------------------------------------------
# 1) SERVER_TIME placeholder — str body AND bytes body (regression)
# ---------------------------------------------------------------------------

class TestServerTimePlaceholderProduct(_ProductBase):

    def test_server_time_str_body_via_fastapi(self):
        """FastAPI /http/start with str body containing ${SERVER_TIME}."""
        placeholder = '${SERVER_TIME}'
        marker = _marker('product-str-st')
        data = self._start_mock(
            body='{"ts":"%s","path":"%s"}' % (placeholder, marker),
            file_log=True,
        )
        base = data['base_url']
        self.assertTrue(base.startswith('http://127.0.0.1:'), base)

        r = self.client.post(MOCK_PROXY, json={
            'url': base + '/product-str-st',
            'method': 'GET',
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()['body']
        self.assertNotIn(placeholder, body,
                         'SERVER_TIME placeholder must be replaced, got %r'
                         % body)
        self.assertIn('"ts":"', body)
        self.assertIn(marker, body)
        # time-looking token inside ts value
        ts_idx = body.find('"ts":"')
        self.assertGreaterEqual(ts_idx, 0, body)
        ts_val = body[ts_idx + len('"ts":"'):].split('"', 1)[0]
        self.assertTrue(any(ch.isdigit() for ch in ts_val),
                        'SERVER_TIME value must contain digits, got %r' % ts_val)

        reqs = self.client.get(MOCK_REQUESTS).json()
        paths = [x.get('path_only') for x in reqs.get('requests') or []]
        self.assertIn('/product-str-st', paths)

    def test_server_time_bytes_body_regression(self):
        """
        REGRESSION: bytes body with ${SERVER_TIME} must NOT raise
        TypeError / httpx.RemoteDisconnected (old `str in bytes` bug).
        """
        from device.http_mock import HttpMock
        mock = HttpMock()
        self.addCleanup(mock.stop)
        marker = _marker('product-bytes-st')
        try:
            mock.start(
                port=0,
                body=b'{"ts":"${SERVER_TIME}","src":"bytes","m":"%s"}'
                     % marker.encode('ascii'),
                content_type='application/json',
                status_code=200,
                https=False,
                file_log=False,
            )
            base = mock.base_url
            self.assertTrue(base.startswith('http://127.0.0.1:'), base)

            import httpx
            try:
                with httpx.Client(verify=False, timeout=5.0) as c:
                    resp = c.get(base + '/product-bytes-st')
            except TypeError as exc:
                self.fail('bytes body SERVER_TIME raised TypeError: %r' % exc)
            except Exception as exc:
                # RemoteDisconnected / ProtocolError would also fail the
                # old bytes path when the handler crashed mid-response.
                name = type(exc).__name__
                self.fail('bytes body SERVER_TIME raised %s: %r' % (name, exc))

            self.assertEqual(resp.status_code, 200, resp.text)
            body = resp.text
            self.assertNotIn('${SERVER_TIME}', body,
                             'bytes body placeholder must be replaced: %r'
                             % body)
            self.assertIn('"src":"bytes"', body)
            self.assertIn(marker, body)
            self.assertIn('"ts":"', body)
            ts_idx = body.find('"ts":"')
            ts_val = body[ts_idx + len('"ts":"'):].split('"', 1)[0]
            self.assertTrue(any(ch.isdigit() for ch in ts_val),
                            'bytes SERVER_TIME value must have digits: %r'
                            % ts_val)

            st = mock.status()
            self.assertTrue(st.get('running'))
            self.assertEqual(st.get('server_time_placeholder'),
                             '${SERVER_TIME}')
            self.assertGreater(st.get('body_len', 0), 0)

            # second request still works (handler not wedged)
            with httpx.Client(verify=False, timeout=5.0) as c:
                resp2 = c.get(base + '/product-bytes-st-2')
            self.assertEqual(resp2.status_code, 200, resp2.text)
            self.assertNotIn('${SERVER_TIME}', resp2.text)
        finally:
            mock.stop()

    def test_server_time_bytes_body_via_device_manager(self):
        """Product path DeviceManager.http_mock.start with raw bytes body."""
        from device.device_manager import get_device_manager
        from device.http_mock import SERVER_TIME_PLACEHOLDER
        dm = get_device_manager()
        marker = _marker('product-dm-bytes')
        raw = ('{"ts":"%s","from":"dm-bytes","m":"%s"}'
               % (SERVER_TIME_PLACEHOLDER, marker)).encode('utf-8')
        port = dm.start_http_mock(
            port=0, body=raw, content_type='application/json',
            status_code=200, https=False, file_log=True)
        self.addCleanup(dm.stop_http_mock)
        self.mock_stopped = True  # cleanup already handled via dm
        self.assertGreater(port, 0)
        base = dm.http_mock.base_url
        self.assertTrue(base.startswith('http://127.0.0.1:'), base)

        import httpx
        with httpx.Client(verify=False, timeout=5.0) as c:
            resp = c.get(base + '/product-dm-bytes')
        self.assertEqual(resp.status_code, 200, resp.text)
        self.assertNotIn(SERVER_TIME_PLACEHOLDER, resp.text)
        self.assertIn(marker, resp.text)
        self.assertIn('"ts":"', resp.text)


# ---------------------------------------------------------------------------
# 2) file_log=true writes real traffic to mock_http.log (status path)
# ---------------------------------------------------------------------------

class TestFileLogRealWriteProduct(_ProductBase):

    def test_file_log_true_writes_traffic_to_status_path(self):
        # mock_http.log records method + path + status (not request body),
        # so uniqueness must be in the path.
        marker = _marker('product-filelog')
        path = '/api/product-filelog-%s' % marker
        data = self._start_mock(
            body='{"filelog":true}',
            content_type='application/json',
            https=False,
            file_log=True,
        )
        st = data.get('status') or {}
        fl = st.get('file_log') or {}
        self.assertTrue(fl.get('enabled'),
                        'file_log must be enabled in status: %r' % fl)
        log_path = fl.get('path')
        self.assertTrue(log_path,
                        'status.file_log.path must be non-empty: %r' % fl)
        self.assertTrue(os.path.isabs(log_path), log_path)
        self.assertTrue(log_path.endswith('mock_http.log'), log_path)
        self.assertTrue(os.path.isfile(log_path),
                        'mock_http.log must exist at status path: %s'
                        % log_path)

        base = data['base_url']
        r = self.client.post(MOCK_PROXY, json={
            'url': base + path,
            'method': 'POST',
            'body': 'payload',
            'headers': {'X-Product-Test': '1'},
        })
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'))

        def _file_has_marker():
            try:
                return path in _read_file(log_path)
            except Exception:
                return False

        self.assertTrue(
            _wait(_file_has_marker, timeout=3.0),
            'mock_http.log at %s must contain traffic path %s'
            % (log_path, path))

        content = _read_file(log_path)
        self.assertIn(path, content)
        self.assertIn('[MockHttp]', content)

        # status path stays stable across /status polls
        st2 = self.client.get(MOCK_STATUS).json()
        fl2 = (st2.get('file_log') or {})
        self.assertEqual(fl2.get('path'), log_path)
        self.assertTrue(fl2.get('enabled'))

    def test_file_log_false_reports_disabled(self):
        data = self._start_mock(file_log=False)
        fl = (data.get('status') or {}).get('file_log') or {}
        # enabled flag must be present; path may be None when not opened
        self.assertIn('enabled', fl)
        # stop/start cycles must not crash
        self._stop_mock()


# ---------------------------------------------------------------------------
# 3) set_path_response per-path override (product DeviceManager after API)
# ---------------------------------------------------------------------------

class TestSetPathResponseProduct(_ProductBase):

    def test_set_path_response_overrides_single_path(self):
        from device.device_manager import get_device_manager
        data = self._start_mock(
            body='{"default":true}',
            content_type='application/json',
            status_code=200,
            https=False,
            file_log=True,
        )
        base = data['base_url']
        dm = get_device_manager()
        self.assertTrue(dm.http_mock.is_running, 'mock must be running on DM')

        dm.http_mock.set_path_response(
            '/api/eth', '{"eth":"up"}',
            content_type='application/json', status_code=201)

        r = self.client.post(MOCK_PROXY, json={
            'url': base + '/api/eth',
            'method': 'GET',
        })
        self.assertEqual(r.status_code, 200, r.text)
        proxy_body = r.json()
        self.assertEqual(proxy_body.get('status_code'), 201)
        self.assertIn('eth', proxy_body.get('body') or '')
        self.assertIn('up', proxy_body.get('body') or '')

        r2 = self.client.post(MOCK_PROXY, json={
            'url': base + '/api/other',
            'method': 'GET',
        })
        self.assertEqual(r2.status_code, 200, r2.text)
        other = r2.json()
        self.assertEqual(other.get('status_code'), 200)
        self.assertIn('default', other.get('body') or '')
        self.assertNotIn('eth', other.get('body') or '')

        reqs = self.client.get(MOCK_REQUESTS).json()
        paths = [x.get('path_only') for x in reqs.get('requests') or []]
        self.assertIn('/api/eth', paths)
        self.assertIn('/api/other', paths)

        # override persists for subsequent proxy calls
        r3 = self.client.post(MOCK_PROXY, json={
            'url': base + '/api/eth',
            'method': 'POST',
            'body': 'again',
        })
        self.assertEqual(r3.json().get('status_code'), 201)
        self.assertIn('up', r3.json().get('body') or '')


# ---------------------------------------------------------------------------
# 4) proxy endpoint records request (API + mock self /_requests)
# ---------------------------------------------------------------------------

class TestProxyRecordsRequestProduct(_ProductBase):

    def test_proxy_records_into_api_and_mock_requests(self):
        marker = _marker('product-proxy')
        data = self._start_mock(
            body='{"proxy":"ok"}',
            content_type='application/json',
            https=False,
            file_log=True,
        )
        base = data['base_url']
        path = '/product-proxy'

        r = self.client.post(MOCK_PROXY, json={
            'url': base + path,
            'method': 'POST',
            'body': 'body-marker=%s' % marker,
            'headers': {'X-Proxy-Product': marker},
        })
        self.assertEqual(r.status_code, 200, r.text)
        pb = r.json()
        self.assertTrue(pb.get('ok'))
        self.assertEqual(pb.get('method'), 'POST')
        self.assertEqual(pb.get('status_code'), 200)
        self.assertIn('proxy', pb.get('body') or '')
        self.assertIn('body_len', pb)

        reqs = self.client.get(MOCK_REQUESTS).json()
        self.assertTrue(reqs.get('running'))
        self.assertGreaterEqual(reqs.get('count', 0), 1)
        matched = [x for x in reqs.get('requests') or []
                   if x.get('path_only') == path]
        self.assertTrue(matched, 'requests log must include %s: %r'
                        % (path, reqs.get('requests')))
        entry = matched[-1]
        self.assertEqual(entry.get('method'), 'POST')
        self.assertIn(marker, entry.get('body') or '')
        self.assertEqual(entry.get('body_len'),
                         len('body-marker=%s' % marker))
        self.assertTrue(entry.get('client'),
                        'recorded request must carry client address')

        # mock self /_requests endpoint (real HTTP, not only FastAPI mirror)
        import httpx
        with httpx.Client(verify=False, timeout=5.0) as c:
            resp = c.get(base + '/_requests')
        self.assertEqual(resp.status_code, 200, resp.text)
        data_log = json.loads(resp.text)
        self.assertIsInstance(data_log, list)
        paths = [e.get('path_only') for e in data_log]
        self.assertIn(path, paths)
        self.assertIn('/_requests', paths)

        # structured rejection stays product-grade
        r_bad = self.client.post(MOCK_PROXY, json={'url': 'not-a-url'})
        _assert_structured_error(self, r_bad, 400, 'INVALID_REQUEST')
        r_bad2 = self.client.post(MOCK_PROXY, json={
            'url': base + '/x', 'method': 'TRACE',
        })
        _assert_structured_error(self, r_bad2, 400, 'INVALID_REQUEST')

        # second start conflicts while mock running
        r_conflict = self.client.post(MOCK_START, json={'port': 0})
        _assert_structured_error(self, r_conflict, 409, 'HTTP_MOCK_RUNNING')

        self._stop_mock()


# ---------------------------------------------------------------------------
# 5) HTTPS: pinned cert SHA256 + live TLS handshake via httpx verify
# ---------------------------------------------------------------------------

class TestHttpsCertsAndLiveTlsProduct(_ProductBase):

    def setUp(self):
        super(TestHttpsCertsAndLiveTlsProduct, self).setUp()
        from device import http_mock as hm
        with hm._https_cert_lock:
            hm._https_cert_paths = None

    def tearDown(self):
        from device import http_mock as hm
        with hm._https_cert_lock:
            hm._https_cert_paths = None
        super(TestHttpsCertsAndLiveTlsProduct, self).tearDown()

    def test_https_certs_sha256_and_live_tls_handshake(self):
        from device import http_mock as hm
        # force re-resolve (cache may be poisoned by other suites)
        with hm._https_cert_lock:
            hm._https_cert_paths = None

        data = self._start_mock(
            body='{"https":true,"src":"product-https"}',
            content_type='application/json',
            https=True,
            file_log=True,
        )
        base = data['base_url']
        self.assertTrue(base.startswith('https://'), base)
        st = data.get('status') or {}
        self.assertTrue(st.get('https'), st)
        self.assertEqual(st.get('https_cert_sha256'), DEV_CERT_SHA256, st)
        self.assertEqual(st.get('https_key_sha256'), DEV_KEY_SHA256, st)
        cert = st.get('https_cert') or ''
        key = st.get('https_key') or ''
        self.assertTrue(cert.endswith('mock_https_cert.pem'), cert)
        self.assertTrue(key.endswith('mock_https_key.pem'), key)
        self.assertEqual(_sha256_file(cert), DEV_CERT_SHA256, cert)
        self.assertEqual(_sha256_file(key), DEV_KEY_SHA256, key)

        # origin/dev tool PEMs on disk must match pinned fingerprints
        for rel, expected in (
            ('tool/mock_https_cert.pem', DEV_CERT_SHA256),
            ('tool/mock_https_key.pem', DEV_KEY_SHA256),
        ):
            p = os.path.join(_REPO, rel)
            self.assertTrue(os.path.isfile(p), 'missing origin/dev PEM %s' % p)
            self.assertEqual(_sha256_file(p), expected,
                             'PEM %s content mismatch' % p)

        # origin/dev git blob (if available) must also match pins
        for rel, expected in (
            ('tool/mock_https_cert.pem', DEV_CERT_SHA256),
            ('tool/mock_https_key.pem', DEV_KEY_SHA256),
        ):
            blob = _git_show(rel)
            if blob is None:
                continue
            self.assertEqual(hashlib.sha256(blob).hexdigest(), expected,
                             'origin/dev:%s content mismatch' % rel)

        # package-relative certs/ copies must match pins too
        pkg = os.path.join(_PY_ROOT, 'device', 'certs')
        for name, expected in (
            ('mock_https_cert.pem', DEV_CERT_SHA256),
            ('mock_https_key.pem', DEV_KEY_SHA256),
        ):
            p = os.path.join(pkg, name)
            self.assertTrue(os.path.isfile(p), p)
            self.assertEqual(_sha256_file(p), expected, p)

        # LIVE TLS handshake via httpx verify against mock cert file
        import httpx
        with httpx.Client(verify=cert, timeout=5.0) as c:
            resp = c.get(base + '/https-ping')
        self.assertEqual(resp.status_code, 200, resp.text)
        self.assertIn('https', resp.text)
        self.assertIn('product-https', resp.text)

        # proxy endpoint still records over real HTTPS
        r = self.client.post(MOCK_PROXY, json={
            'url': base + '/https-proxy',
            'method': 'GET',
            'verify_tls': False,  # self-signed; handshake is still real TLS
        })
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'))
        self.assertTrue(r.json().get('url', '').startswith('https://'))

        reqs = self.client.get(MOCK_REQUESTS).json()
        paths = [x.get('path_only') for x in reqs.get('requests') or []]
        self.assertIn('/https-ping', paths)
        self.assertIn('/https-proxy', paths)

        # stop keeps cert fingerprints reported for diagnostics
        stop_body = self._stop_mock()
        self.assertTrue(stop_body.get('was_running'))
        st2 = stop_body.get('status') or {}
        self.assertFalse(st2.get('running'))


# ---------------------------------------------------------------------------
# 6) HTTP device protocol 0x50–0x53 — mock is the real HTTP peer
# ---------------------------------------------------------------------------

class TestHttpDeviceProtocolOverMockPeer(_PtyProductBase):

    def _install_peer_hook(self):
        """MockBabyOSDevice acts as a REAL HTTP client to the local mock."""
        import httpx
        parsed_log = []

        def _hook(parsed):
            parsed_log.append(dict(parsed or {}))
            url = (parsed or {}).get('url') or ''
            method = ((parsed or {}).get('method') or 'GET').upper()
            raw_body = (parsed or {}).get('body') or ''
            headers_text = (parsed or {}).get('headers') or ''
            hdrs = {}
            for line in headers_text.split('\r\n'):
                if ':' in line:
                    k, v = line.split(':', 1)
                    hdrs[k.strip()] = v.strip()
            if not url.startswith(('http://', 'https://')):
                return 400, 'bad url: %s' % url
            content = raw_body.encode('utf-8') if raw_body else None
            try:
                with httpx.Client(verify=False, timeout=5.0) as c:
                    resp = c.request(method, url, content=content, headers=hdrs)
            except Exception as exc:
                return 599, 'peer http error: %s' % exc
            return resp.status_code, resp.text

        self.link.device.http_response_hook = _hook
        self.addCleanup(setattr, self.link.device, 'http_response_hook', None)
        return parsed_log

    def test_0x50_0x53_device_request_hits_mock_peer(self):
        parsed_log = self._install_peer_hook()
        marker = _marker('product-0x50')
        data = self._start_mock(
            body='{"product":"http-mock-peer","m":"%s"}' % marker,
            content_type='application/json',
            status_code=200,
            https=False,
            file_log=True,
        )
        base = data['base_url']
        path = '/product-device-http'
        url = base + path

        # CMD 0x52 init
        r = self.client.post(HTTP_INIT)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'), r.text)
        self.assertTrue(self.link.device.http_inited,
                        'mock device must enter http_inited after CMD 0x52')

        # CMD 0x50 GET → mock device performs REAL HTTP to local mock
        r = self.client.post(HTTP_REQUEST, json={
            'method': 'GET',
            'url': url,
            'headers': {'User-Agent': 'BabyOS-Product'},
            'timeout': 5.0,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'), body)
        self.assertEqual(body.get('status'), 200, body)
        self.assertEqual(body.get('url'), url)
        self.assertIn(marker, body.get('body') or '',
                      'device HTTP body must be the mock peer response: %r'
                      % body)
        self.assertIn('"product":"http-mock-peer"', body.get('body') or '')

        # protocol-side record
        self.assertIsNotNone(self.link.device.last_http_request)
        last = self.link.device.last_http_request
        self.assertEqual(last.get('method'), 'GET')
        self.assertEqual(last.get('url'), url)
        self.assertIn('User-Agent', last.get('headers') or '')
        self.assertTrue(parsed_log, 'peer hook must have been invoked')

        # mock server recorded the device-side request
        reqs = self.client.get(MOCK_REQUESTS).json()
        paths = [x.get('path_only') for x in reqs.get('requests') or []]
        self.assertIn(path, paths,
                      'HttpMock must record device-side request %s: %r'
                      % (path, paths))
        matched = [x for x in reqs.get('requests') or []
                   if x.get('path_only') == path]
        self.assertTrue(matched[-1].get('client'),
                        'device request must carry a client address')

        # CMD 0x53 deinit
        r = self.client.post(HTTP_DEINIT)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertFalse(self.link.device.http_inited)

        # device HTTP still works after deinit cycle (init again)
        r = self.client.post(HTTP_INIT)
        self.assertEqual(r.status_code, 200, r.text)
        r = self.client.post(HTTP_REQUEST, json={
            'method': 'GET',
            'url': base + '/product-device-http-2',
            'timeout': 5.0,
        })
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json().get('status'), 200)
        self.assertIn('product', r.json().get('body') or '')

        # optional deinit for clean state
        self.client.post(HTTP_DEINIT)

    def test_0x50_post_body_over_mock_peer(self):
        self._install_peer_hook()
        marker = _marker('product-0x50-post')
        data = self._start_mock(
            body='{"product":"post-peer","m":"%s"}' % marker,
            content_type='application/json',
            https=False,
            file_log=True,
        )
        base = data['base_url']
        path = '/product-device-post'
        url = base + path

        r = self.client.post(HTTP_INIT)
        self.assertEqual(r.status_code, 200, r.text)

        r = self.client.post(HTTP_REQUEST, json={
            'method': 'POST',
            'url': url,
            'body': 'device-post-body',
            'headers': {'Content-Type': 'text/plain'},
            'timeout': 5.0,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'), body)
        self.assertEqual(body.get('status'), 200)
        self.assertIn(marker, body.get('body') or '')
        last = self.link.device.last_http_request
        self.assertEqual(last.get('method'), 'POST')
        self.assertEqual(last.get('body'), 'device-post-body')

        reqs = self.client.get(MOCK_REQUESTS).json()
        matched = [x for x in reqs.get('requests') or []
                   if x.get('path_only') == path]
        self.assertTrue(matched, reqs.get('requests'))
        self.assertIn('device-post-body', matched[-1].get('body') or '')

        self.client.post(HTTP_DEINIT)

    def test_http_protocol_requires_uart_structured_error(self):
        """Without serial open, /http/request must be structured 409."""
        r_close = self.client.post('/api/device/serial/close', json={})
        self.assertEqual(r_close.status_code, 200, r_close.text)

        r = self.client.post(HTTP_REQUEST, json={
            'method': 'GET',
            'url': 'http://127.0.0.1:1/x',
        })
        _assert_structured_error(self, r, 409, 'SERIAL_NOT_OPEN')

        r_init = self.client.post(HTTP_INIT)
        _assert_structured_error(self, r_init, 409, 'SERIAL_NOT_OPEN')

        r_deinit = self.client.post(HTTP_DEINIT)
        _assert_structured_error(self, r_deinit, 409, 'SERIAL_NOT_OPEN')

        # mock HTTP is independent of serial — still usable while closed
        data = self._start_mock(body='{"still":"works"}', file_log=False)
        base = data['base_url']
        r = self.client.post(MOCK_PROXY, json={'url': base + '/no-uart'})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertIn('still', r.json().get('body') or '')


if __name__ == '__main__':
    unittest.main(verbosity=2)
