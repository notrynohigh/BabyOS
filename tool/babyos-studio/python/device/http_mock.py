"""
http_mock — real local HTTP/HTTPS mock server for device HTTP request testing.

Uses Python's http.server.ThreadingHTTPServer (stdlib, no stubs).
Records every request (method / path / body / headers / client) and
returns a configurable response (body, content_type, status_code).

GET /_requests returns the recorded log as JSON — convenient for tests
and for FastAPI to surface mock traffic.

HTTPS: loads ONLY the fixed BabyOS mock certificate content from
origin/dev tool/mock_https_cert.pem + tool/mock_https_key.pem.
Runtime openssl self-signed generation is forbidden. Candidate paths:
  1. repo path  <repo>/tool/mock_https_{cert,key}.pem  (preferred)
  2. package    python/device/certs/mock_https_{cert,key}.pem
  3. env BABYOS_MOCK_HTTPS_CERT / BABYOS_MOCK_HTTPS_KEY (debug only;
     content MUST still equal origin/dev tool certs — other PEMs rejected)
Whichever path is used, file SHA256 must match the pinned origin/dev
fingerprints below. Missing or non-matching files raise RuntimeError.

Python 3.8 compatible.
"""

from __future__ import print_function

import hashlib
import json
import os
import ssl
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, List, Optional, Tuple

# Process-wide resolved cert cache (paths validated once per process)
_https_cert_lock = threading.Lock()
_https_cert_paths: Optional[Tuple[str, str]] = None  # (certfile, keyfile)

# Documented stable locations (kept for error messages / status)
_REPO_TOOL_CERT = 'tool/mock_https_cert.pem'
_REPO_TOOL_KEY = 'tool/mock_https_key.pem'
_PKG_CERTS_DIR = 'certs'

# Pinned content fingerprints of origin/dev tool/mock_https_{cert,key}.pem
# (sha256 of PEM bytes). HTTPS must use THESE certs, not arbitrary paths.
DEV_TOOL_CERT_SHA256 = (
    '45020779c14d326cb5306b68687a2985f0b6c626f9587f1ce39493a77f9b034c'
)
DEV_TOOL_KEY_SHA256 = (
    '24017d03c4f9b83779cc0d2d957528bf6333d7338369f1141b90e4d81dcfd689'
)


def _sha256_file(path: str) -> str:
    h = hashlib.sha256()
    try:
        with open(path, 'rb') as f:
            for chunk in iter(lambda: f.read(65536), b''):
                h.update(chunk)
    except Exception:
        return ''
    return h.hexdigest()


def _candidate_cert_pairs() -> List[Tuple[str, str]]:
    """
    Ordered (certfile, keyfile) candidates. Does not touch the filesystem.
    Env override is last-resort debug only; content still must match origin/dev.
    """
    pairs: List[Tuple[str, str]] = []

    here = os.path.dirname(os.path.abspath(__file__))

    # 1) repo layout: walk up from this file until <root>/tool/mock_https_*.pem
    #    package lives at <repo>/tool/babyos-studio/python/device/
    p = here
    for _ in range(8):
        parent = os.path.dirname(p)
        if parent == p:
            break
        p = parent
        pairs.append((
            os.path.join(p, 'tool', 'mock_https_cert.pem'),
            os.path.join(p, 'tool', 'mock_https_key.pem'),
        ))

    # 2) package-relative certs/ (installer / packaged layout)
    pairs.append((
        os.path.join(here, _PKG_CERTS_DIR, 'mock_https_cert.pem'),
        os.path.join(here, _PKG_CERTS_DIR, 'mock_https_key.pem'),
    ))

    # 3) env override — debug only; content must still equal origin/dev tool PEMs
    env_c = (os.environ.get('BABYOS_MOCK_HTTPS_CERT') or '').strip()
    env_k = (os.environ.get('BABYOS_MOCK_HTTPS_KEY') or '').strip()
    if env_c or env_k:
        if not (env_c and env_k):
            raise RuntimeError(
                'HTTPS cert env override incomplete: set BOTH '
                'BABYOS_MOCK_HTTPS_CERT and BABYOS_MOCK_HTTPS_KEY '
                '(got cert=%r key=%r)' % (env_c or None, env_k or None))
        pairs.append((env_c, env_k))
    return pairs


def _resolve_https_cert() -> Tuple[str, str]:
    """
    Resolve BabyOS mock HTTPS cert files that match origin/dev tool/ content.

    Search order: repo tool/mock_https_* → package certs/ → env override.
    Content SHA256 of both PEMs must equal DEV_TOOL_*_SHA256.
    Returns (certfile, keyfile). Raises RuntimeError if none match.
    Never generates certificates via openssl.
    """
    global _https_cert_paths
    with _https_cert_lock:
        if _https_cert_paths is not None:
            return _https_cert_paths
        tried: List[str] = []
        for cert, key in _candidate_cert_pairs():
            if not (os.path.isfile(cert) and os.path.isfile(key)):
                tried.append('%s + %s (missing)' % (cert, key))
                continue
            cert_sha = _sha256_file(cert)
            key_sha = _sha256_file(key)
            if cert_sha == DEV_TOOL_CERT_SHA256 and key_sha == DEV_TOOL_KEY_SHA256:
                _https_cert_paths = (cert, key)
                return _https_cert_paths
            tried.append(
                '%s + %s (content mismatch cert=%s key=%s)'
                % (cert, key, cert_sha or 'unreadable', key_sha or 'unreadable'))
        raise RuntimeError(
            'HTTPS mock certificate not found or content mismatch. Required: '
            'origin/dev tool/mock_https_cert.pem + tool/mock_https_key.pem '
            '(cert sha256=%s, key sha256=%s). Lookup order: '
            '<repo>/tool/mock_https_* → python/device/certs/mock_https_* → '
            'env BABYOS_MOCK_HTTPS_CERT/KEY (content must still match). '
            'Runtime openssl self-signed generation is disabled. '
            'Tried: %s' % (
                DEV_TOOL_CERT_SHA256, DEV_TOOL_KEY_SHA256,
                '; '.join(tried) if tried else '(no candidates)'))


def _x509_sha256_fingerprint(certfile: str) -> str:
    """X.509 SHA256 fingerprint (colon-separated hex) via ssl module."""
    try:
        der = ssl.PEM_cert_to_DER_cert(open(certfile, 'rb').read().decode('ascii'))
        return ':'.join('%02X' % b for b in hashlib.sha256(der).digest())
    except Exception:
        return ''


def _to_bytes(body: Any) -> bytes:
    if body is None:
        return b''
    if isinstance(body, bytes):
        return body
    if isinstance(body, bytearray):
        return bytes(body)
    if isinstance(body, str):
        return body.encode('utf-8')
    return str(body).encode('utf-8')


def _headers_to_dict(handler: BaseHTTPRequestHandler) -> Dict[str, str]:
    out: Dict[str, str] = {}
    try:
        for k, v in handler.headers.items():
            out[k] = v
    except Exception:
        pass
    return out


class _MockHandler(BaseHTTPRequestHandler):
    """Request handler bound to a parent HttpMock instance via server.mock."""

    # silence default stderr logging
    def log_message(self, fmt: str, *args: Any) -> None:  # noqa: A003
        return

    @property
    def mock(self) -> 'HttpMock':
        return self.server.mock  # type: ignore[attr-defined]

    def _read_body(self) -> bytes:
        length = 0
        try:
            length = int(self.headers.get('Content-Length') or 0)
        except (TypeError, ValueError):
            length = 0
        if length <= 0:
            return b''
        try:
            return self.rfile.read(length)
        except Exception:
            return b''

    def _record_and_respond(self) -> None:
        mock = self.mock
        body = self._read_body()
        path = self.path or '/'
        # strip query for the record path field but keep full path too
        entry = {
            'ts': time.time(),
            'method': self.command,
            'path': path,
            'path_only': path.split('?', 1)[0],
            'query': path.split('?', 1)[1] if '?' in path else '',
            'body': body.decode('utf-8', errors='replace'),
            'body_hex': body.hex() if body else '',
            'body_len': len(body),
            'headers': _headers_to_dict(self),
            'client': '%s:%s' % (self.client_address[0], self.client_address[1])
            if self.client_address else '',
        }
        mock._record(entry)

        # /_requests is served by the mock itself
        if entry['path_only'] == '/_requests':
            payload = json.dumps(mock.get_requests_log(),
                                 ensure_ascii=False, indent=2).encode('utf-8')
            self._send_bytes(200, payload, 'application/json; charset=utf-8')
            return

        entry['_handler'] = self
        mock.serve(entry)

    def _send_bytes(self, status: int, payload: bytes,
                    content_type: str) -> None:
        try:
            self.send_response(status)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(payload)))
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            if payload:
                self.wfile.write(payload)
        except Exception:
            pass

    # HTTP verbs → same behaviour
    def do_GET(self) -> None:  # noqa: N802
        self._record_and_respond()

    def do_POST(self) -> None:  # noqa: N802
        self._record_and_respond()

    def do_PUT(self) -> None:  # noqa: N802
        self._record_and_respond()

    def do_PATCH(self) -> None:  # noqa: N802
        self._record_and_respond()

    def do_DELETE(self) -> None:  # noqa: N802
        self._record_and_respond()

    def do_HEAD(self) -> None:  # noqa: N802
        self._record_and_respond()


class HttpMock:
    """
    Real ThreadingHTTPServer-backed HTTP mock.

    Usage:
        m = HttpMock()
        m.start(port=18081, body=b'{"ok":true}',
                content_type='application/json', status_code=200)
        # ... device requests http://127.0.0.1:18081/...
        log = m.requests
        m.stop()
    """

    def __init__(self) -> None:
        self._server: Optional[ThreadingHTTPServer] = None
        self._thread: Optional[threading.Thread] = None
        self._port = 0
        self._body = b'{}'
        self._content_type = 'application/json'
        self._status_code = 200
        self._requests: List[Dict[str, Any]] = []
        self._lock = threading.Lock()
        self._started = False
        self._https = False
        self._https_cert: Optional[str] = None
        self._https_key: Optional[str] = None
        # optional per-path overrides: {path_only: (body, content_type, status)}
        self._path_rules: Dict[str, Tuple[bytes, str, int]] = {}

    # ------------------------------------------------------------------
    # lifecycle
    # ------------------------------------------------------------------

    @property
    def is_running(self) -> bool:
        return self._started and self._server is not None

    @property
    def port(self) -> int:
        return self._port

    @property
    def https(self) -> bool:
        return self._https

    @property
    def base_url(self) -> str:
        if not self._port:
            return ''
        scheme = 'https' if self._https else 'http'
        return '%s://127.0.0.1:%d' % (scheme, self._port)

    def start(self, port: int = 0, body: Any = b'{"ok":true}',
              content_type: str = 'application/json',
              status_code: int = 200, https: bool = False) -> int:
        """
        Start the mock HTTP(S) server.

        port=0 picks an ephemeral port (read back via .port).
        body/content_type/status_code apply to every non-/_requests path.
        https=True wraps the listener with the fixed BabyOS mock TLS cert
        (origin/dev tool/mock_https_*; env override or package certs/).
        Raises RuntimeError if cert files are missing (no openssl fallback).
        Returns the bound port. Raises RuntimeError if already running.
        """
        if self.is_running:
            raise RuntimeError('HttpMock already running on port %d' % self._port)

        self._body = _to_bytes(body)
        self._content_type = content_type or 'application/octet-stream'
        try:
            self._status_code = int(status_code)
        except (TypeError, ValueError):
            self._status_code = 200
        self._requests = []
        self._path_rules = {}
        self._https = bool(https)
        self._https_cert = None
        self._https_key = None

        server = ThreadingHTTPServer(('127.0.0.1', int(port)), _MockHandler)
        server.mock = self  # type: ignore[attr-defined]
        # allow fast restart on Linux
        try:
            server.allow_reuse_address = True
        except Exception:
            pass
        if self._https:
            cert, key = _resolve_https_cert()
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.load_cert_chain(certfile=cert, keyfile=key)
            server.socket = ctx.wrap_socket(server.socket, server_side=True)
            self._https_cert = cert
            self._https_key = key
        self._server = server
        self._port = int(server.server_address[1])

        t = threading.Thread(target=server.serve_forever,
                             kwargs={'poll_interval': 0.05},
                             name='HttpMock-%d' % self._port,
                             daemon=True)
        t.start()
        self._thread = t
        self._started = True
        return self._port

    def stop(self) -> None:
        """Stop the server and join the thread. Safe to call twice."""
        server = self._server
        self._started = False
        self._server = None
        if server is not None:
            try:
                server.shutdown()
            except Exception:
                pass
            try:
                server.server_close()
            except Exception:
                pass
        t = self._thread
        self._thread = None
        if t is not None and t.is_alive():
            t.join(timeout=2.0)
        self._port = 0
        self._https = False
        self._https_cert = None
        self._https_key = None

    # ------------------------------------------------------------------
    # request log
    # ------------------------------------------------------------------

    def _record(self, entry: Dict[str, Any]) -> None:
        with self._lock:
            self._requests.append(entry)

    @property
    def requests(self) -> List[Dict[str, Any]]:
        """Copy of recorded requests (method/path/body/headers/...)."""
        with self._lock:
            return [dict(r) for r in self._requests]

    @property
    def request_count(self) -> int:
        with self._lock:
            return len(self._requests)

    def clear_requests(self) -> None:
        with self._lock:
            self._requests = []

    def get_requests_log(self) -> List[Dict[str, Any]]:
        """JSON-serialisable log for GET /_requests (drops non-serialisable keys)."""
        out = []
        for r in self.requests:
            clean = {k: v for k, v in r.items()
                     if k in ('ts', 'method', 'path', 'path_only', 'query',
                              'body', 'body_hex', 'body_len', 'headers', 'client')}
            out.append(clean)
        return out

    def wait_for_request(self, path_only: Optional[str] = None,
                         method: Optional[str] = None,
                         timeout: float = 2.0) -> Optional[Dict[str, Any]]:
        """Block until a matching request appears (for tests)."""
        deadline = time.time() + max(0.0, timeout)
        while time.time() < deadline:
            for entry in self.requests:
                if path_only is not None and entry.get('path_only') != path_only:
                    continue
                if method is not None and entry.get('method') != method:
                    continue
                return entry
            time.sleep(0.02)
        return None

    # ------------------------------------------------------------------
    # response configuration
    # ------------------------------------------------------------------

    def set_response(self, body: Any = b'{"ok":true}',
                     content_type: str = 'application/json',
                     status_code: int = 200) -> None:
        """Change the default response for all paths."""
        self._body = _to_bytes(body)
        self._content_type = content_type or 'application/octet-stream'
        try:
            self._status_code = int(status_code)
        except (TypeError, ValueError):
            self._status_code = 200

    def set_path_response(self, path_only: str, body: Any,
                          content_type: str = 'application/json',
                          status_code: int = 200) -> None:
        """Override response for a single path (e.g. '/api/status')."""
        if not path_only:
            return
        self._path_rules[path_only] = (
            _to_bytes(body),
            content_type or 'application/octet-stream',
            int(status_code),
        )

    def serve(self, entry: Dict[str, Any]) -> None:
        """Send the configured response for a recorded request entry."""
        path_only = entry.get('path_only') or '/'
        rule = self._path_rules.get(path_only)
        if rule is not None:
            body, ctype, status = rule
        else:
            body, ctype, status = self._body, self._content_type, self._status_code
        # find the live handler via the server — serve() is called from handler
        # so we use the handler stored on the entry by _record_and_respond
        handler = entry.pop('_handler', None)
        if handler is None:
            return
        handler._send_bytes(status, body, ctype)

    # ------------------------------------------------------------------
    # status
    # ------------------------------------------------------------------

    def status(self) -> dict:
        out = {
            'running': self.is_running,
            'port': self._port,
            'base_url': self.base_url,
            'https': self._https,
            'status_code': self._status_code,
            'content_type': self._content_type,
            'body_len': len(self._body),
            'request_count': self.request_count,
        }
        if self._https and self._https_cert:
            out['https_cert'] = self._https_cert
            out['https_key'] = self._https_key
            out['https_cert_sha256'] = _sha256_file(self._https_cert)
            out['https_key_sha256'] = _sha256_file(self._https_key)
            out['https_cert_x509_sha256'] = _x509_sha256_fingerprint(
                self._https_cert)
        return out
