#!/usr/bin/env python3
"""
test_edge_https_certs — HTTPS cert edge / exception scenarios.

Authority (硬性验收):
  1. Edge/exception 主路径 = 真实虚拟串口 pty + 真实主机栈
     (TestClient → DeviceManager → UartService → pty → MockBabyOSDevice
      真实解析 BabyOS 帧)。协议层先做 CMD 0x1 双向验证。
  2. 证书候选隔离：真实移动 origin/dev PEM 文件 + env 指向非 dev/缺失路径，
     不 stub 产品代码、不改 _candidate_cert_pairs。隔离结果单独计数
     (host_resolver supplement)；API 结构化错误走 pty 主路径。
  3. 唯一可接受内容 = origin/dev tool/mock_https_{cert,key}.pem。
     运行时 openssl 自签生成禁止。
  4. 异常时 API 返回结构化错误 (detail.code + detail.detail)，禁止裸 500。
     SDK 侧明确 RuntimeError，禁止 busy-wait / 死循环。
  5. Python 3.8 兼容；不修改 bos/thirdparty；不提交 git。

Covered edge cases:
  A. env 指向非 dev 证书文件且无其它有效候选 → RuntimeError (+ API 409)
  B. 缺失证书且无其它有效候选 → RuntimeError，无 openssl 生成 (+ API 409)
  C. HTTPS mock 重复 start → 结构化 409 HTTP_MOCK_RUNNING
  D. live TLS 仍匹配 origin/dev 指纹 (content + X.509 DER)

Run:
  tool/babyos-studio/python/.venv/bin/python \
      tool/babyos-studio/test/device_features/test_edge_https_certs.py
  or pytest on the same path.

Emits TEST_SCHEMA JSON on stdout at the end (marks regression vs edge).
"""

from __future__ import print_function

import contextlib
import hashlib
import json
import os
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
_REPO = os.path.abspath(os.path.join(_STUDIO, '..', '..'))
for _p in (_PY_ROOT, _HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from device.protocol_client import (  # noqa: E402
    DEVICE_ID_HOST,
    CMD_TEST,
)
from mock_babyos_device import (  # noqa: E402
    MockBabyOSDevice,
    create_byte_channel,
    open_api_over_pty,
)

# origin/dev tool/mock_https_{cert,key}.pem pinned content hashes
DEV_CERT_SHA256 = (
    '45020779c14d326cb5306b68687a2985f0b6c626f9587f1ce39493a77f9b034c'
)
DEV_KEY_SHA256 = (
    '24017d03c4f9b83779cc0d2d957528bf6333d7338369f1141b90e4d81dcfd689'
)

MOCK_UID = b'\x11\x22\x33\x44\x55\x66\x77\x88\x99\xAA\xBB\xCC'
MOCK_VERSION = 'v1.2.3'
MOCK_MODEL = 'BabyOS-MCU'
MOCK_DEV_ID = 0x0000ABCD

TEST_SCHEMA_ID = 'babyos_edge_https_certs_v1'

# Real on-disk candidate locations (product lookup order)
_REPO_TOOL_CERT = os.path.join(_REPO, 'tool', 'mock_https_cert.pem')
_REPO_TOOL_KEY = os.path.join(_REPO, 'tool', 'mock_https_key.pem')
_PKG_CERT = os.path.join(_PY_ROOT, 'device', 'certs', 'mock_https_cert.pem')
_PKG_KEY = os.path.join(_PY_ROOT, 'device', 'certs', 'mock_https_key.pem')
_REAL_CERT_PATHS = [
    _REPO_TOOL_CERT, _REPO_TOOL_KEY, _PKG_CERT, _PKG_KEY,
]

# Coverage registry — pty main path vs host_resolver supplement vs memory_mock
_MATRIX = []  # type: list
_PTY_TOTAL = 0
_PTY_PASS = 0
_PTY_FAIL = 0
_HOST_TOTAL = 0
_HOST_PASS = 0
_HOST_FAIL = 0
_MEM_TOTAL = 0
_MEM_PASS = 0
_MEM_FAIL = 0
_CERT_INFO = {}  # type: dict


def _matrix_add(case_id, path_kind, status, detail=''):
    _MATRIX.append({
        'id': case_id,
        'path': path_kind,
        'status': status,
        'detail': detail or '',
    })
    global _PTY_TOTAL, _PTY_PASS, _PTY_FAIL
    global _HOST_TOTAL, _HOST_PASS, _HOST_FAIL
    global _MEM_TOTAL, _MEM_PASS, _MEM_FAIL
    ok = (status == 'PASS')
    if path_kind == 'pty':
        _PTY_TOTAL += 1
        if ok:
            _PTY_PASS += 1
        else:
            _PTY_FAIL += 1
    elif path_kind == 'host_resolver':
        _HOST_TOTAL += 1
        if ok:
            _HOST_PASS += 1
        else:
            _HOST_FAIL += 1
    elif path_kind == 'memory_mock':
        _MEM_TOTAL += 1
        if ok:
            _MEM_PASS += 1
        else:
            _MEM_FAIL += 1
    else:
        raise AssertionError('unknown path_kind %r' % path_kind)


def _sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            h.update(chunk)
    return h.hexdigest()


def _sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def _git_show(rel):
    try:
        return subprocess.check_output(
            ['git', '-C', _REPO, 'show', 'origin/dev:%s' % rel],
            stderr=subprocess.DEVNULL)
    except Exception:
        return None


def _write_temp(data, suffix='_edge.pem'):
    fd, path = tempfile.mkstemp(suffix=suffix, prefix='babyos_edge_https_')
    try:
        os.write(fd, data)
    finally:
        os.close(fd)
    return path


def _reset_cert_cache():
    from device import http_mock as hm
    with hm._https_cert_lock:
        hm._https_cert_paths = None


def _snapshot_candidate_files():
    """List existing product cert candidate paths (for openssl-gen detection)."""
    out = []
    for p in _REAL_CERT_PATHS:
        if os.path.isfile(p):
            out.append((p, _sha256_file(p)))
    return sorted(out)


@contextlib.contextmanager
def _hide_origin_dev_certs():
    """
    Temporarily move real origin/dev PEM files aside.

    Product lookup order is <repo>/tool/mock_https_* → package certs/ → env.
    After the move, only the env candidate remains, so a non-dev env pair or
    a missing env pair is the sole outcome. Product code is not stubbed.
    """
    moved = []
    try:
        for p in _REAL_CERT_PATHS:
            if os.path.isfile(p):
                tmp = p + '.edge_hidden'
                if os.path.exists(tmp):
                    os.unlink(tmp)
                os.rename(p, tmp)
                moved.append((tmp, p))
        _reset_cert_cache()
        yield moved
    finally:
        for tmp, p in moved:
            if os.path.isfile(tmp):
                try:
                    os.rename(tmp, p)
                except OSError:
                    pass
        _reset_cert_cache()


class _OpenSSLGuard(object):
    """Record subprocess calls that would invoke openssl (no generation)."""

    def __init__(self):
        self.openssl_calls = []
        self._old = {}

    @staticmethod
    def _is_openssl(cmd):
        if cmd is None:
            return False
        if isinstance(cmd, (list, tuple)):
            return any('openssl' in str(x) for x in cmd)
        return 'openssl' in str(cmd)

    def __enter__(self):
        import subprocess as sp
        guard = self
        self._old['Popen'] = sp.Popen
        self._old['run'] = sp.run
        self._old['check_output'] = sp.check_output
        self._old['check_call'] = sp.check_call

        def _wrap(name, orig):
            def _fn(*a, **k):
                cmd = a[0] if a else k.get('args') or k.get('cmd')
                if guard._is_openssl(cmd):
                    guard.openssl_calls.append((name, cmd))
                    raise RuntimeError(
                        'test guard: openssl invocation forbidden in this case')
                return orig(*a, **k)
            return _fn

        sp.Popen = _wrap('Popen', self._old['Popen'])
        sp.run = _wrap('run', self._old['run'])
        sp.check_output = _wrap('check_output', self._old['check_output'])
        sp.check_call = _wrap('check_call', self._old['check_call'])
        return self

    def __exit__(self, exc_type, exc, tb):
        import subprocess as sp
        for name, orig in self._old.items():
            setattr(sp, name, orig)
        return False


def _verify_origin_dev_certs():
    """On-disk + git origin/dev authority check (no openssl)."""
    global _CERT_INFO
    info = {
        'ok': False,
        'repo_tool_cert': _REPO_TOOL_CERT,
        'repo_tool_key': _REPO_TOOL_KEY,
        'pkg_cert': _PKG_CERT,
        'pkg_key': _PKG_KEY,
        'resolved': None,
        'origin': 'origin/dev tool/mock_https_{cert,key}.pem',
    }
    try:
        from device import http_mock as hm
        cert, key = hm._resolve_https_cert()
        info['resolved'] = [cert, key]
        for path in _REAL_CERT_PATHS:
            assert os.path.isfile(path), path
        dev_cert = _git_show('tool/mock_https_cert.pem')
        dev_key = _git_show('tool/mock_https_key.pem')
        with open(_REPO_TOOL_CERT, 'rb') as f:
            disk_cert = f.read()
        with open(_REPO_TOOL_KEY, 'rb') as f:
            disk_key = f.read()
        with open(_PKG_CERT, 'rb') as f:
            pkg_cert = f.read()
        with open(_PKG_KEY, 'rb') as f:
            pkg_key = f.read()
        if dev_cert is not None:
            assert disk_cert == dev_cert, 'repo tool cert != origin/dev'
        if dev_key is not None:
            assert disk_key == dev_key, 'repo tool key != origin/dev'
        assert pkg_cert == disk_cert, 'package cert != repo tool cert'
        assert pkg_key == disk_key, 'package key != repo tool key'
        assert _sha256_bytes(disk_cert) == DEV_CERT_SHA256
        assert _sha256_bytes(disk_key) == DEV_KEY_SHA256
        der = ssl.PEM_cert_to_DER_cert(disk_cert.decode('ascii'))
        info['cert_sha256'] = hashlib.sha256(der).hexdigest().upper()
        info['content_sha256'] = DEV_CERT_SHA256
        info['key_sha256'] = DEV_KEY_SHA256
        info['ok'] = True
    except Exception as exc:
        info['error'] = str(exc)
        info['ok'] = False
    _CERT_INFO = info
    return info


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


def _non_dev_cert_bytes():
    return (
        b'-----BEGIN CERTIFICATE-----\n'
        b'MIIBnot-origin-dev-cert-content-edge-case\n'
        b'-----END CERTIFICATE-----\n'
    )


def _non_dev_key_bytes():
    return (
        b'-----BEGIN PRIVATE KEY-----\n'
        b'MIIBnot-origin-dev-key-content-edge-case\n'
        b'-----END PRIVATE KEY-----\n'
    )


# ---------------------------------------------------------------------------
# Shared pty + API fixture (main path)
# ---------------------------------------------------------------------------

class _EdgePtyBase(unittest.TestCase):
    """FastAPI TestClient + real pty + MockBabyOSDevice. main path."""

    case_id = 'unset'
    path_kind = 'pty'
    _marked = False

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
                'uid': MOCK_UID,
                'version': MOCK_VERSION,
                'model': MOCK_MODEL,
                'encrypt': False,
                'log_fn': self.logs.append,
            },
            require_pty=True,
        )
        self.addCleanup(self.link.close)
        self.assertEqual(self.link.kind, 'pty',
                         'edge/exception main path requires real pty')
        _matrix_add('channel_pty_setup', 'pty', 'PASS',
                    'host_port=%s' % self.link.host_port)

        from app.main import create_app
        from fastapi.testclient import TestClient
        self.app = create_app()
        self.client = TestClient(self.app)
        self.addCleanup(self._stop_https_mock)

        # Real two-way protocol over pty before any cert edge work
        r = self.client.post('/api/device/serial/open', json={
            'path': self.link.host_port,
            'baud': 115200,
            'encrypt': False,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('host_id'), DEVICE_ID_HOST,
                         'upper tool host DeviceID must be 0x1314')
        _matrix_add('pty_serial_open_host_id', 'pty', 'PASS',
                    'host_id=0x%X' % (body.get('host_id') or 0))

        r = self.client.post('/api/device/protocol/test', json={})
        self.assertEqual(r.status_code, 200, r.text)
        pbody = r.json()
        self.assertTrue(pbody.get('ok'), pbody)
        self.assertEqual(pbody.get('cmd'), CMD_TEST, pbody)
        with self.link.device._lock:
            cmds = list(self.link.device.written_cmds)
        self.assertIn(CMD_TEST, cmds,
                      'mock device must receive CMD 0x%02X over pty; cmds=%s'
                      % (CMD_TEST, cmds))
        _matrix_add('pty_protocol_test_two_way', 'pty', 'PASS',
                    'device_cmds=%s' % cmds)

        self._marked = False

    def tearDown(self):
        if not getattr(self, '_marked', False) and self.case_id \
                and self.case_id != 'unset':
            _matrix_add(self.case_id, self.path_kind, 'FAIL',
                        'assertion aborted before _mark')
        _reset_cert_cache()

    def _stop_https_mock(self):
        try:
            self.client.post('/api/device/http/stop', json={})
        except Exception:
            pass

    def _mark(self, ok, detail=''):
        self._marked = True
        status = 'PASS' if ok else 'FAIL'
        _matrix_add(self.case_id, self.path_kind, status, detail)


# ---------------------------------------------------------------------------
# EDGE A+B+C+D over pty API — cert isolation + live TLS + repeated start
# ---------------------------------------------------------------------------

class TestEdgeHttpsCertsPty(_EdgePtyBase):

    def test_edge_env_content_mismatch_no_other_candidate(self):
        """env → non-dev PEMs + no other valid candidate → RuntimeError."""
        self.case_id = 'pty_env_cert_content_mismatch'
        bad_c = _write_temp(_non_dev_cert_bytes(), '_bad_cert.pem')
        bad_k = _write_temp(_non_dev_key_bytes(), '_bad_key.pem')
        old_c = os.environ.get('BABYOS_MOCK_HTTPS_CERT')
        old_k = os.environ.get('BABYOS_MOCK_HTTPS_KEY')
        try:
            os.environ['BABYOS_MOCK_HTTPS_CERT'] = bad_c
            os.environ['BABYOS_MOCK_HTTPS_KEY'] = bad_k
            with _hide_origin_dev_certs():
                # SDK-level: resolver must raise RuntimeError (clear failure)
                from device import http_mock as hm
                with self.assertRaises(RuntimeError) as cm:
                    hm._resolve_https_cert()
                msg = str(cm.exception)
                self.assertIn('content mismatch', msg.lower(), msg)
                self.assertIn(DEV_CERT_SHA256, msg)
                self.assertIn(DEV_KEY_SHA256, msg)
                self.assertIn(bad_c, msg)

                # API-level: structured 409 HTTPS_CERT_MISMATCH, never bare 500
                r = self.client.post('/api/device/http/start', json={
                    'https': True, 'port': 0,
                    'body': '{"x":1}', 'status_code': 200,
                })
                _assert_structured_error(self, r, 409, 'HTTPS_CERT_MISMATCH')
                detail = r.json()['detail']['detail'].lower()
                self.assertTrue(
                    'cert' in detail or 'mismatch' in detail
                    or 'origin/dev' in detail or 'sha256' in detail, detail)
                st = self.client.get('/api/device/http/status')
                self.assertEqual(st.status_code, 200, st.text)
                self.assertFalse(st.json().get('running'),
                                 'https mock must not start on content mismatch')

            self._mark(True, 'env=%s hidden origin/dev pairs' % bad_c)
        finally:
            if old_c is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_CERT', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_CERT'] = old_c
            if old_k is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_KEY', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_KEY'] = old_k
            _reset_cert_cache()
            for p in (bad_c, bad_k):
                try:
                    os.unlink(p)
                except OSError:
                    pass

    def test_edge_missing_cert_no_other_candidate_no_openssl(self):
        """env → missing paths + no other valid candidate → RuntimeError."""
        self.case_id = 'pty_missing_cert_no_openssl'
        missing_c = os.path.join(
            tempfile.gettempdir(), 'babyos_edge_missing_cert.pem')
        missing_k = os.path.join(
            tempfile.gettempdir(), 'babyos_edge_missing_key.pem')
        for p in (missing_c, missing_k):
            if os.path.isfile(p):
                os.unlink(p)
        old_c = os.environ.get('BABYOS_MOCK_HTTPS_CERT')
        old_k = os.environ.get('BABYOS_MOCK_HTTPS_KEY')
        try:
            os.environ['BABYOS_MOCK_HTTPS_CERT'] = missing_c
            os.environ['BABYOS_MOCK_HTTPS_KEY'] = missing_k
            with _hide_origin_dev_certs():
                # Candidates are hidden; openssl must not recreate any.
                before = _snapshot_candidate_files()
                self.assertEqual(before, [],
                                 'candidates should be hidden, got %s' % before)
                with _OpenSSLGuard() as guard:
                    from device import http_mock as hm
                    with self.assertRaises(RuntimeError) as cm:
                        hm._resolve_https_cert()
                    msg = str(cm.exception)
                    self.assertIn('not found', msg.lower(), msg)
                    self.assertIn('missing', msg.lower(), msg)
                    self.assertIn('Runtime openssl self-signed generation '
                                  'is disabled', msg)
                    self.assertEqual(guard.openssl_calls, [],
                                     'openssl must not be invoked')

                    # SDK start(https=True) must also raise RuntimeError
                    from device.http_mock import HttpMock
                    mock = HttpMock()
                    with self.assertRaises(RuntimeError) as cm2:
                        mock.start(port=0, https=True)
                    self.assertIn('certificate', str(cm2.exception).lower())
                    self.assertFalse(mock.is_running)

                    # API-level structured 409 (not bare 500 / stack)
                    r = self.client.post('/api/device/http/start', json={
                        'https': True, 'port': 0,
                        'body': '{"x":1}', 'status_code': 200,
                    })
                    _assert_structured_error(self, r, 409,
                                             'HTTPS_CERT_MISMATCH')
                    self.assertNotIn('Traceback', r.text)
                    self.assertFalse(
                        self.client.get('/api/device/http/status')
                        .json().get('running'))

                # No product candidate PEMs may be generated during isolation
                after = _snapshot_candidate_files()
                self.assertEqual(before, after,
                                 'cert candidate files changed: %s -> %s'
                                 % (before, after))

            # After restore, original origin/dev candidates must return unchanged
            restored = _snapshot_candidate_files()
            expected = sorted([
                (_REPO_TOOL_CERT, DEV_CERT_SHA256),
                (_REPO_TOOL_KEY, DEV_KEY_SHA256),
                (_PKG_CERT, DEV_CERT_SHA256),
                (_PKG_KEY, DEV_KEY_SHA256),
            ])
            self.assertEqual(restored, expected,
                             'restored certs differ: %s' % restored)

            self._mark(True, 'missing=%s openssl_calls=0' % missing_c)
        finally:
            if old_c is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_CERT', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_CERT'] = old_c
            if old_k is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_KEY', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_KEY'] = old_k
            _reset_cert_cache()

    def test_edge_https_repeated_start_structured_409(self):
        """HTTPS mock repeated start → structured 409 HTTP_MOCK_RUNNING."""
        self.case_id = 'pty_https_repeated_start_409'
        r = self.client.post('/api/device/http/start', json={
            'https': True, 'port': 0,
            'body': '{"edge":"repeated-start"}',
            'content_type': 'application/json',
            'status_code': 200,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertTrue(body.get('https'))
        status = body.get('status') or {}
        self.assertTrue(status.get('https'))
        self.assertEqual(status.get('https_cert_sha256'), DEV_CERT_SHA256)
        self.assertEqual(status.get('https_key_sha256'), DEV_KEY_SHA256)
        base_url = status.get('base_url') or ''
        self.assertTrue(base_url.startswith('https://127.0.0.1:'), base_url)

        # Second start while running → structured 409, not bare 500 / hang
        r2 = self.client.post('/api/device/http/start', json={
            'https': True, 'port': 0,
        })
        _assert_structured_error(self, r2, 409, 'HTTP_MOCK_RUNNING')
        self.assertNotIn('Traceback', r2.text)

        # SDK-level: second HttpMock.start raises RuntimeError (no busy-wait)
        from device.http_mock import HttpMock
        mock = HttpMock()
        try:
            port = mock.start(port=0, https=True)
            self.assertTrue(port > 0)
            with self.assertRaises(RuntimeError) as cm:
                mock.start(port=0, https=True)
            self.assertIn('already running', str(cm.exception).lower())
        finally:
            mock.stop()

        # First mock still healthy after the 409
        st = self.client.get('/api/device/http/status')
        self.assertEqual(st.status_code, 200, st.text)
        self.assertTrue(st.json().get('running'))
        self.assertTrue((st.json().get('base_url') or '').startswith('https://'))

        # stop is clean and allows a fresh start
        r3 = self.client.post('/api/device/http/stop', json={})
        self.assertEqual(r3.status_code, 200, r3.text)
        self.assertTrue(r3.json().get('was_running'))
        r4 = self.client.post('/api/device/http/start', json={
            'https': True, 'port': 0,
            'body': '{"edge":"restart-after-stop"}',
        })
        self.assertEqual(r4.status_code, 200, r4.text)
        self.assertTrue(r4.json().get('https'))

        self._mark(True, 'second_start=409 HTTP_MOCK_RUNNING; restart ok')

    def test_edge_live_tls_matches_origin_dev_fingerprint(self):
        """Live TLS peer cert must still match origin/dev fingerprints."""
        self.case_id = 'pty_live_tls_origin_dev_fingerprint'
        cinfo = _verify_origin_dev_certs()
        self.assertTrue(cinfo.get('ok'), cinfo.get('error'))
        self.assertEqual(cinfo.get('content_sha256'), DEV_CERT_SHA256)
        self.assertEqual(cinfo.get('key_sha256'), DEV_KEY_SHA256)

        repo_cert = cinfo['repo_tool_cert']
        repo_key = cinfo['repo_tool_key']
        with open(repo_cert, 'rb') as f:
            disk_cert = f.read()
        with open(repo_key, 'rb') as f:
            disk_key = f.read()
        if _git_show('tool/mock_https_cert.pem') is not None:
            self.assertEqual(disk_cert,
                             _git_show('tool/mock_https_cert.pem'))
        if _git_show('tool/mock_https_key.pem') is not None:
            self.assertEqual(disk_key,
                             _git_show('tool/mock_https_key.pem'))

        r = self.client.post('/api/device/http/start', json={
            'https': True, 'port': 0,
            'body': '{"edge":"live-tls","src":"live-tls"}',
            'content_type': 'application/json',
            'status_code': 200,
        })
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        status = body.get('status') or {}
        cert_path = status.get('https_cert') or ''
        key_path = status.get('https_key') or ''
        self.assertIn(os.path.realpath(cert_path),
                      (os.path.realpath(repo_cert),
                       os.path.realpath(_PKG_CERT)),
                      'HTTPS mock must load origin/dev cert, got %s' % cert_path)
        self.assertEqual(status.get('https_cert_sha256'), DEV_CERT_SHA256)
        self.assertEqual(status.get('https_key_sha256'), DEV_KEY_SHA256)
        base_url = status.get('base_url') or ''
        self.assertTrue(base_url.startswith('https://127.0.0.1:'), base_url)
        port = int(status.get('port') or 0)
        self.assertTrue(port > 0)

        # 1) Real TLS request verified against on-disk origin/dev cert
        import httpx
        with httpx.Client(verify=repo_cert, timeout=5.0) as hc:
            resp = hc.get(base_url + '/api/device/probe')
            self.assertEqual(resp.status_code, 200, resp.text)
            self.assertEqual(resp.json().get('src'), 'live-tls', resp.text)

        # 2) Live TLS peer DER must equal origin/dev cert DER fingerprint
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        with socket.create_connection(('127.0.0.1', port),
                                      timeout=3.0) as sock:
            with ctx.wrap_socket(sock) as ssock:
                der = ssock.getpeercert(binary_form=True)
        self.assertIsNotNone(der)
        disk_der = ssl.PEM_cert_to_DER_cert(disk_cert.decode('ascii'))
        live_sha = hashlib.sha256(der).hexdigest().upper()
        disk_sha = hashlib.sha256(disk_der).hexdigest().upper()
        self.assertEqual(live_sha, disk_sha,
                         'live TLS peer cert differs from origin/dev cert')
        self.assertEqual(disk_sha, cinfo.get('cert_sha256'))

        # 3) X.509 SHA256 fingerprint helper agrees with live peer
        #    (_x509_sha256_fingerprint returns colon-separated hex)
        from device.http_mock import _x509_sha256_fingerprint
        x509_fp = _x509_sha256_fingerprint(cert_path)
        self.assertTrue(x509_fp, 'x509 fingerprint empty')
        self.assertEqual(x509_fp.replace(':', '').upper(), disk_sha)

        # 4) API proxy performs a real HTTPS handshake against the mock
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

        self._mark(True, 'peer_der=%s x509=%s' % (live_sha[:16], x509_fp[:16]))


# ---------------------------------------------------------------------------
# host_resolver supplement — direct SDK isolation (counted separately)
# ---------------------------------------------------------------------------

class TestHostResolverEdge(unittest.TestCase):
    """Direct _resolve_https_cert isolation. Not the pty main path."""

    case_id = 'unset'
    path_kind = 'host_resolver'
    _marked = False

    def setUp(self):
        self._marked = False

    def tearDown(self):
        if not getattr(self, '_marked', False) and self.case_id \
                and self.case_id != 'unset':
            _matrix_add(self.case_id, self.path_kind, 'FAIL',
                        'assertion aborted before _mark')
        _reset_cert_cache()

    def _mark(self, ok, detail=''):
        self._marked = True
        status = 'PASS' if ok else 'FAIL'
        _matrix_add(self.case_id, self.path_kind, status, detail)

    def test_host_env_incomplete_override_runtime_error(self):
        self.case_id = 'host_env_incomplete_override'
        cert = _REPO_TOOL_CERT
        old_c = os.environ.get('BABYOS_MOCK_HTTPS_CERT')
        old_k = os.environ.get('BABYOS_MOCK_HTTPS_KEY')
        try:
            os.environ['BABYOS_MOCK_HTTPS_CERT'] = cert
            os.environ.pop('BABYOS_MOCK_HTTPS_KEY', None)
            _reset_cert_cache()
            from device import http_mock as hm
            with self.assertRaises(RuntimeError) as cm:
                hm._resolve_https_cert()
            msg = str(cm.exception)
            self.assertIn('BOTH', msg)
            self.assertIn('BABYOS_MOCK_HTTPS_KEY', msg)
            self._mark(True, 'incomplete env rejected')
        finally:
            if old_c is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_CERT', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_CERT'] = old_c
            if old_k is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_KEY', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_KEY'] = old_k
            _reset_cert_cache()

    def test_host_env_same_content_still_accepted(self):
        """env override pointing at origin/dev bytes is accepted (debug only)."""
        self.case_id = 'host_env_same_content_accepted'
        old_c = os.environ.get('BABYOS_MOCK_HTTPS_CERT')
        old_k = os.environ.get('BABYOS_MOCK_HTTPS_KEY')
        try:
            os.environ['BABYOS_MOCK_HTTPS_CERT'] = _REPO_TOOL_CERT
            os.environ['BABYOS_MOCK_HTTPS_KEY'] = _REPO_TOOL_KEY
            _reset_cert_cache()
            from device import http_mock as hm
            cert, key = hm._resolve_https_cert()
            self.assertEqual(_sha256_file(cert), DEV_CERT_SHA256)
            self.assertEqual(_sha256_file(key), DEV_KEY_SHA256)
            self._mark(True, 'env=%s content=origin/dev' % cert)
        finally:
            if old_c is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_CERT', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_CERT'] = old_c
            if old_k is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_KEY', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_KEY'] = old_k
            _reset_cert_cache()

    def test_host_mismatch_only_candidate_runtime_error(self):
        """env non-dev + hidden origin/dev → RuntimeError (SDK, no API)."""
        self.case_id = 'host_mismatch_only_candidate'
        bad_c = _write_temp(_non_dev_cert_bytes(), '_host_bad_cert.pem')
        bad_k = _write_temp(_non_dev_key_bytes(), '_host_bad_key.pem')
        old_c = os.environ.get('BABYOS_MOCK_HTTPS_CERT')
        old_k = os.environ.get('BABYOS_MOCK_HTTPS_KEY')
        try:
            os.environ['BABYOS_MOCK_HTTPS_CERT'] = bad_c
            os.environ['BABYOS_MOCK_HTTPS_KEY'] = bad_k
            with _hide_origin_dev_certs():
                from device import http_mock as hm
                with self.assertRaises(RuntimeError) as cm:
                    hm._resolve_https_cert()
                self.assertIn('content mismatch', str(cm.exception).lower())
                self.assertIn('Runtime openssl self-signed generation '
                              'is disabled', str(cm.exception))
            self._mark(True, 'sdk RuntimeError content mismatch')
        finally:
            if old_c is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_CERT', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_CERT'] = old_c
            if old_k is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_KEY', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_KEY'] = old_k
            _reset_cert_cache()
            for p in (bad_c, bad_k):
                try:
                    os.unlink(p)
                except OSError:
                    pass

    def test_host_missing_only_candidate_runtime_error_no_openssl(self):
        """env missing + hidden origin/dev → RuntimeError, no openssl."""
        self.case_id = 'host_missing_only_candidate_no_openssl'
        missing_c = os.path.join(
            tempfile.gettempdir(), 'babyos_edge_host_missing_cert.pem')
        missing_k = os.path.join(
            tempfile.gettempdir(), 'babyos_edge_host_missing_key.pem')
        for p in (missing_c, missing_k):
            if os.path.isfile(p):
                os.unlink(p)
        old_c = os.environ.get('BABYOS_MOCK_HTTPS_CERT')
        old_k = os.environ.get('BABYOS_MOCK_HTTPS_KEY')
        try:
            os.environ['BABYOS_MOCK_HTTPS_CERT'] = missing_c
            os.environ['BABYOS_MOCK_HTTPS_KEY'] = missing_k
            with _hide_origin_dev_certs():
                before = _snapshot_candidate_files()
                self.assertEqual(before, [],
                                 'candidates should be hidden, got %s' % before)
                with _OpenSSLGuard() as guard:
                    from device import http_mock as hm
                    with self.assertRaises(RuntimeError) as cm:
                        hm._resolve_https_cert()
                    msg = str(cm.exception)
                    self.assertIn('not found', msg.lower())
                    self.assertIn('Runtime openssl self-signed generation '
                                  'is disabled', msg)
                    self.assertEqual(guard.openssl_calls, [])
                after = _snapshot_candidate_files()
                self.assertEqual(before, after)
            restored = _snapshot_candidate_files()
            expected = sorted([
                (_REPO_TOOL_CERT, DEV_CERT_SHA256),
                (_REPO_TOOL_KEY, DEV_KEY_SHA256),
                (_PKG_CERT, DEV_CERT_SHA256),
                (_PKG_KEY, DEV_KEY_SHA256),
            ])
            self.assertEqual(restored, expected,
                             'restored certs differ: %s' % restored)
            self._mark(True, 'sdk RuntimeError missing; openssl_calls=0')
        finally:
            if old_c is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_CERT', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_CERT'] = old_c
            if old_k is None:
                os.environ.pop('BABYOS_MOCK_HTTPS_KEY', None)
            else:
                os.environ['BABYOS_MOCK_HTTPS_KEY'] = old_k
            _reset_cert_cache()

    def test_host_live_https_mock_fingerprint(self):
        """SDK HttpMock https=True live TLS vs origin/dev (no pty)."""
        self.case_id = 'host_live_https_mock_fingerprint'
        cinfo = _verify_origin_dev_certs()
        self.assertTrue(cinfo.get('ok'), cinfo.get('error'))
        from device.http_mock import HttpMock, _x509_sha256_fingerprint
        mock = HttpMock()
        try:
            port = mock.start(port=0, https=True)
            self.assertTrue(port > 0)
            st = mock.status()
            self.assertTrue(st.get('https'))
            self.assertEqual(st.get('https_cert_sha256'), DEV_CERT_SHA256)
            self.assertEqual(st.get('https_key_sha256'), DEV_KEY_SHA256)
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            with socket.create_connection(('127.0.0.1', port),
                                          timeout=3.0) as sock:
                with ctx.wrap_socket(sock) as ssock:
                    der = ssock.getpeercert(binary_form=True)
            self.assertIsNotNone(der)
            disk_der = ssl.PEM_cert_to_DER_cert(
                open(cinfo['repo_tool_cert']).read())
            self.assertEqual(hashlib.sha256(der).hexdigest().upper(),
                             hashlib.sha256(disk_der).hexdigest().upper())
            x509_fp = _x509_sha256_fingerprint(cinfo['repo_tool_cert'])
            self.assertTrue(x509_fp)
            # _x509_sha256_fingerprint is colon-separated hex; compare normalized
            self.assertEqual(
                x509_fp.replace(':', '').upper(),
                hashlib.sha256(disk_der).hexdigest().upper())
            self._mark(True, 'sdk live tls peer matches origin/dev')
        finally:
            mock.stop()


# ---------------------------------------------------------------------------
# Runner + TEST_SCHEMA
# ---------------------------------------------------------------------------

def _build_test_schema():
    required_edge = [
        'pty_env_cert_content_mismatch',
        'pty_missing_cert_no_openssl',
        'pty_https_repeated_start_409',
        'pty_live_tls_origin_dev_fingerprint',
    ]
    present = {m['id'] for m in _MATRIX}
    missing_required = [c for c in required_edge if c not in present]
    pty_required_ok = all(
        any(m['id'] == c and m['status'] == 'PASS' for m in _MATRIX)
        for c in required_edge)
    cert_ok = bool(_CERT_INFO.get('ok'))
    acceptance = (
        pty_required_ok
        and _PTY_FAIL == 0
        and _HOST_FAIL == 0
        and _MEM_FAIL == 0
        and cert_ok
        and not missing_required
    )
    return {
        'schema': TEST_SCHEMA_ID,
        'area': 'https_certs_edge',
        'python': sys.version.split()[0],
        'repo': _REPO,
        'protocol_host_id': '0x%X' % DEVICE_ID_HOST,
        'certs': {
            'ok': cert_ok,
            'origin': 'origin/dev tool/mock_https_{cert,key}.pem',
            'repo_tool_cert': _CERT_INFO.get('repo_tool_cert'),
            'repo_tool_key': _CERT_INFO.get('repo_tool_key'),
            'pkg_cert': _CERT_INFO.get('pkg_cert'),
            'pkg_key': _CERT_INFO.get('pkg_key'),
            'resolved': _CERT_INFO.get('resolved'),
            'content_sha256': _CERT_INFO.get('content_sha256'),
            'key_sha256': _CERT_INFO.get('key_sha256'),
            'cert_sha256': _CERT_INFO.get('cert_sha256'),
            'error': _CERT_INFO.get('error'),
        },
        'total': _PTY_TOTAL + _HOST_TOTAL + _MEM_TOTAL,
        'passed': _PTY_PASS + _HOST_PASS + _MEM_PASS,
        'failed': _PTY_FAIL + _HOST_FAIL + _MEM_FAIL,
        'edge': {
            'pty_total': _PTY_TOTAL,
            'pty_passed': _PTY_PASS,
            'pty_failed': _PTY_FAIL,
            'host_resolver_total': _HOST_TOTAL,
            'host_resolver_passed': _HOST_PASS,
            'host_resolver_failed': _HOST_FAIL,
            'memory_mock_total': _MEM_TOTAL,
            'memory_mock_passed': _MEM_PASS,
            'memory_mock_failed': _MEM_FAIL,
            'required_edge_cases': required_edge,
            'missing_required': missing_required,
        },
        'regression': {
            'note': 'Main acceptance re-run is executed separately; '
                    'this suite only emits edge counts here.',
            'files': [
                'test_virtual_serial.py',
                'test_virtual_serial_protocol.py',
                'test_api_virtual_serial.py',
            ],
        },
        'coverage_matrix': _MATRIX,
        'acceptance': acceptance,
        'notes': (
            'Edge/exception main path is real pty + FastAPI API + host '
            'UartService/ProtocolClient/Shell/Xmodem stack and device-side '
            'MockBabyOSDevice BabyOS frame parse. Cert isolation uses real '
            'filesystem moves + env override (no product stubs). '
            'host_resolver cases are SDK supplements counted separately. '
            'HTTPS certs are origin/dev tool/ files — no runtime openssl.'
        ),
    }


def _print_schema(schema):
    print('')
    print('==== edge https certs coverage matrix ====')
    print('%-44s %-14s %s' % ('CASE', 'PATH', 'STATUS'))
    for m in schema['coverage_matrix']:
        print('%-44s %-14s %s%s' % (
            m['id'], m['path'], m['status'],
            ('  ' + m['detail']) if m['detail'] else ''))
    e = schema['edge']
    print('')
    print('EDGE pty total/passed/failed: %d/%d/%d'
          % (e['pty_total'], e['pty_passed'], e['pty_failed']))
    print('EDGE host_resolver total/passed/failed: %d/%d/%d'
          % (e['host_resolver_total'], e['host_resolver_passed'],
             e['host_resolver_failed']))
    print('EDGE memory_mock total/passed/failed: %d/%d/%d'
          % (e['memory_mock_total'], e['memory_mock_passed'],
             e['memory_mock_failed']))
    print('regression files (re-run separately): %s'
          % ', '.join(schema['regression']['files']))
    print('total=%d passed=%d failed=%d'
          % (schema['total'], schema['passed'], schema['failed']))
    print('certs ok=%s content_sha256=%s'
          % (schema['certs']['ok'], schema['certs'].get('content_sha256')))
    print('acceptance=%s' % schema['acceptance'])
    print('')
    print('==== TEST_SCHEMA ====')
    print(json.dumps(schema, indent=2, ensure_ascii=False))


def main():
    print('BabyOS Studio edge HTTPS cert tests')
    print('python: %s' % sys.version.split()[0])
    print('protocol host id: 0x%X' % DEVICE_ID_HOST)
    print('repo: %s' % _REPO)

    ch = create_byte_channel(prefer='pty', open_host_uart=False,
                             require_pty=True)
    print('byte channel kind: %s host_port=%s' % (ch.kind, ch.host_port))
    ch.close()

    cinfo = _verify_origin_dev_certs()
    print('https certs from origin/dev: %s' % cinfo.get('ok'))
    if not cinfo.get('ok'):
        print('  cert error: %s' % cinfo.get('error'))

    loader = unittest.defaultTestLoader
    suite = loader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    _verify_origin_dev_certs()
    schema = _build_test_schema()
    _print_schema(schema)

    ok = schema['acceptance'] and schema['failed'] == 0
    print('')
    print('edge https certs acceptance: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
