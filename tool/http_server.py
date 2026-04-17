"""
http_server.py — Mock HTTP/HTTPS server for BabyOS HTTP client testing.

Features:
- Starts a simple HTTP server on a configurable port
- Records all incoming requests (method, path, headers, body)
- Sends back a configurable response (default: simple JSON with request info)
- Optional HTTPS support using a self-signed certificate
- Can be enabled/disabled at runtime from the GUI
- TLS handshake diagnostic logging (msg_callback) — prints client sig_algs,
  negotiated cipher/sig_alg, and key handshake messages.
"""

import http.server
import json
import ssl
import struct
import threading
from datetime import datetime
from http.server import BaseHTTPRequestHandler
from typing import Callable, List, Optional


# M-NEW-12 fix: 上限保护 — 恶意/异常客户端的 Content-Length 不能无界读取.
# Mock 测试服务器不需要支持真实大文件传输, 1MB 已经远超 BabyOS Config-Web
# POST body 实际需要 (WiFi SSID/PASS + IP/MASK/GW 总共 < 256B).
_MAX_BODY_LEN = 1024 * 1024  # 1 MiB


# ---------------------------------------------------------------------------
# TLS 诊断辅助
# ---------------------------------------------------------------------------

# RFC 5246 / RFC 8017 / RFC 8446 signature_algorithms 编码
# (HashAlgorithm << 8) | SignatureAlgorithm
# 客户端 sig_algs 扩展里的每一项都是这个编码
_SIG_ALG_NAMES = {
    0x0201: "rsa_pkcs1_sha1",
    0x0203: "ecdsa_sha1",
    0x0401: "rsa_pkcs1_sha256",
    0x0403: "ecdsa_secp256r1_sha256",
    0x0501: "rsa_pkcs1_sha384",
    0x0503: "ecdsa_secp384r1_sha384",
    0x0601: "rsa_pkcs1_sha512",
    0x0603: "ecdsa_secp521r1_sha512",
    0x0804: "rsa_pss_rsae_sha256",
    0x0805: "rsa_pss_rsae_sha384",
    0x0806: "rsa_pss_rsae_sha512",
    0x0807: "ed25519",
    0x0808: "ed448",
    0x0809: "rsa_pss_pss_sha256",
    0x080A: "rsa_pss_pss_sha384",
    0x080B: "rsa_pss_pss_sha512",
}

# TLS 1.2 handshake message types
_HS_TYPES = {
    0: "hello_request",
    1: "client_hello",
    2: "server_hello",
    4: "newsessionticket",
    11: "certificate",
    12: "server_key_exchange",
    13: "certificate_request",
    14: "server_hello_done",
    15: "certificate_verify",
    16: "client_key_exchange",
    20: "finished",
}


def _fmt_sig_alg(code: int) -> str:
    """Format a signature_algorithms code as '0xXXXX name'."""
    name = _SIG_ALG_NAMES.get(code, "unknown")
    return f"0x{code:04X}({name})"


def _parse_client_hello_sig_algs(data: bytes) -> list:
    """
    Parse the signature_algorithms extension from a ClientHello.

    `data` is the raw bytes passed to msg_callback — for a ClientHello
    frame that INCLUDES the 4-byte handshake header (msg_type + length),
    so we skip those first 4 bytes before parsing the body.

    Returns a list of sig_alg codes (int), or [] on parse failure.
    """
    try:
        # Skip 4-byte handshake header: msg_type(1) + length(3)
        if len(data) < 4:
            return []
        data = data[4:]
        # ClientHello body layout (TLS 1.2):
        #   protocol_version (2) | random (32) | session_id_len (1) |
        #   session_id (var) | cipher_suites_len (2) | cipher_suites |
        #   compression_len (1) | compression_methods | extensions_len (2) | extensions
        if len(data) < 2 + 32 + 1:
            return []
        i = 2 + 32  # skip version + random
        sid_len = data[i]
        i += 1 + sid_len
        if i + 2 > len(data):
            return []
        cs_len = struct.unpack("!H", data[i:i+2])[0]
        i += 2 + cs_len
        if i + 1 > len(data):
            return []
        comp_len = data[i]
        i += 1 + comp_len
        if i + 2 > len(data):
            return []
        ext_len = struct.unpack("!H", data[i:i+2])[0]
        i += 2
        ext_end = i + ext_len
        sig_algs = []
        # walk extensions
        while i + 4 <= ext_end:
            ext_type, this_len = struct.unpack("!HH", data[i:i+4])
            i += 4
            if i + this_len > ext_end:
                break
            if ext_type == 0x000D and this_len >= 2:  # signature_algorithms
                sal_len = struct.unpack("!H", data[i:i+2])[0]
                j = i + 2
                while j + 2 <= i + 2 + sal_len:
                    code = struct.unpack("!H", data[j:j+2])[0]
                    sig_algs.append(code)
                    j += 2
                break
            i += this_len
        return sig_algs
    except (IndexError, struct.error):
        return []


def _parse_server_key_exchange_sig_alg(data: bytes) -> int:
    """
    Parse the sig_alg field from a TLS 1.2 ECDHE ServerKeyExchange.

    `data` includes the 4-byte handshake header (msg_type=12 + length);
    skip it before parsing the body. Body layout for ECDHE-RSA:
      curve_type (1) | named_curve (2) | point_len (1) | point |
      hash_alg (1) | sig_alg (1) | sig_len (2) | sig (var)
    where the hash_alg + sig_alg bytes are sent separately and combine
    to form the signature_algorithms code (hash<<8 | sig).

    Returns the 2-byte sig_alg code, or -1 on parse failure.
    """
    try:
        # Skip 4-byte handshake header
        if len(data) < 4:
            return -1
        body = data[4:]
        # ECDHE: curve_type=3, then named_curve(2), point_len(1), point(?)
        if len(body) < 4 or body[0] != 3:
            return -1
        point_len = body[3]
        i = 4 + point_len
        if i + 2 > len(body):
            return -1
        # Next 2 bytes are hash_alg (1) + sig_alg (1) in network order
        hash_alg = body[i]
        sig_alg = body[i + 1]
        return (hash_alg << 8) | sig_alg
    except (IndexError, struct.error):
        return -1


def _tls_msg_cb(log_fn):
    """
    Return a function with the SSLContext msg_callback signature
    (conn, direction, version, content_type, msg_type, data).

    `log_fn` is captured in the closure (set per-server-start call).
    Output goes both to the GUI log panel (if log_fn given) and to stdout.

    The callback is fired for every TLS protocol message
    (header / handshake / alert). Skips application_data.

    Prints:
      - ClientHello signature_algorithms extension contents
      - ServerHello cipher_suite
      - ServerKeyExchange sig_alg (the actual signature scheme server chose)
      - alert messages
    """
    def _msg_cb(conn, direction, version, content_type, msg_type, data):
        log = log_fn or (lambda s: None)
        def emit(s):
            log(s)
            print(s)
        # content_type: TLS record type. Python 3.11+ passes a
        # `_TLSContentType` enum. Older versions pass a plain int.
        # The enum's .value may include a HEADER bit (0x100) for record-header
        # frames; mask it off to recover the plain record type.
        if hasattr(content_type, "value"):
            ct = content_type.value
            if isinstance(ct, int) and ct > 0xFF:
                ct = ct & 0xFF
        else:
            ct = int(content_type)
        if ct == 23:  # application_data, skip
            return
        if ct == 0:
            # Header-only frame (no payload), nothing to inspect
            return
        dir_s = "→" if direction == "read" else "←"
        if ct == 22:  # handshake
            # msg_type is the int handshake type (1=ClientHello, 2=ServerHello, ...)
            if hasattr(msg_type, "value"):
                mt = int(msg_type.value)
            else:
                mt = int(msg_type)
            mt_name = _HS_TYPES.get(mt, f"hs_{mt}")
            if mt == 1:  # ClientHello (read by server)
                sig_algs = _parse_client_hello_sig_algs(data)
                if sig_algs:
                    pretty = ", ".join(_fmt_sig_alg(c) for c in sig_algs)
                    emit(f"[TLS] {dir_s} ClientHello: sig_algs offered = {pretty}")
                else:
                    emit(f"[TLS] {dir_s} ClientHello: (no sig_algs extension found)")
            elif mt == 2:  # ServerHello
                # server_hello body: version(2) + random(32) + sid_len(1) + sid + cipher(2)
                if len(data) >= 2 + 32 + 1:
                    sid_len = data[2 + 32]
                    cs_off = 2 + 32 + 1 + sid_len
                    if cs_off + 2 <= len(data):
                        cs = struct.unpack("!H", data[cs_off:cs_off + 2])[0]
                        emit(f"[TLS] {dir_s} ServerHello: cipher_suite = 0x{cs:04X}")
            elif mt == 12:  # ServerKeyExchange
                sig_alg = _parse_server_key_exchange_sig_alg(data)
                emit(f"[TLS] {dir_s} ServerKeyExchange: chosen sig_alg = "
                    f"{_fmt_sig_alg(sig_alg) if sig_alg >= 0 else 'parse_failed'}")
            elif mt == 16:  # ClientKeyExchange
                emit(f"[TLS] {dir_s} ClientKeyExchange")
            elif mt == 20:  # Finished
                emit(f"[TLS] {dir_s} Finished")
            else:
                emit(f"[TLS] {dir_s} Handshake type={mt} ({mt_name})")
        elif ct == 21:  # alert
            # alert body: level(1) + description(1)
            if len(data) >= 2:
                level, desc = data[0], data[1]
                emit(f"[TLS] {dir_s} ALERT: level={level} desc={desc} "
                    f"({_alert_desc_name(desc)})")
            else:
                emit(f"[TLS] {dir_s} ALERT ({len(data)} bytes)")
        elif ct == 20:
            pass  # change_cipher_spec, boring
        else:
            emit(f"[TLS] {dir_s} content_type={ct}")
    return _msg_cb


def _alert_desc_name(desc: int) -> str:
    """RFC 5246 alert descriptions (subset)."""
    return {
        0: "close_notify",
        10: "unexpected_message",
        20: "bad_record_mac",
        22: "record_overflow",
        30: "decompression_failure",
        40: "handshake_failure",
        42: "bad_certificate",
        43: "unsupported_certificate",
        44: "certificate_revoked",
        45: "certificate_expired",
        46: "certificate_unknown",
        47: "illegal_parameter",
        48: "unknown_ca",
        49: "access_denied",
        50: "decode_error",
        51: "decrypt_error",
        60: "export_restriction",
        70: "protocol_version",
        71: "insufficient_security",
        80: "internal_error",
        90: "user_cancelled",
        100: "no_renegotiation",
        110: "unsupported_extension",
    }.get(desc, f"unknown_alert_{desc}")


class RecordedRequest:
    """A single HTTP request captured by the server."""

    def __init__(self, method: str, path: str, headers: dict, body: str,
                 timestamp: str, source: str = "client"):
        self.method = method
        self.path = path
        self.headers = headers
        self.body = body
        self.timestamp = timestamp
        self.source = source  # "client" (received) or "server_response" (sent)

    def to_dict(self) -> dict:
        return {
            "method": self.method,
            "path": self.path,
            "headers": self.headers,
            "body": self.body,
            "timestamp": self.timestamp,
            "source": self.source,
        }


class _BabyOSHttpHandler(BaseHTTPRequestHandler):
    """Custom HTTP request handler that records each request and replies."""

    # Class-level reference to the parent MockServer instance
    # Set on each new instance via set_server()
    server_ref = None  # type: Optional[MockHttpServer]

    def log_message(self, format, *args):  # suppress stderr access log
        pass

    def setup(self):
        """
        Called by BaseHTTPRequestHandler for every new connection.

        For HTTPS, the server's socket is already an SSLSocket that has
        performed the TLS handshake. We print a one-line summary of the
        negotiated version + cipher so the diagnostic log makes sense.
        """
        # Trigger the base class's accept logic (per BaseHTTPRequestHandler)
        BaseHTTPRequestHandler.setup(self)
        # If the underlying socket is an SSLSocket, dump handshake summary.
        conn = getattr(self, "connection", None)
        if conn is not None and isinstance(conn, ssl.SSLSocket):
            log = self.server_ref._log if self.server_ref is not None else (lambda s: None)
            def emit(s):
                log(s)
                print(s)
            try:
                ver = conn.version()
                cipher = conn.cipher()
                if cipher is not None:
                    name = cipher[0]
                    proto = cipher[1]
                    bits = cipher[2]
                    emit(f"[TLS] === handshake complete === "
                         f"version={ver} cipher={name} proto={proto} bits={bits}")
                else:
                    emit(f"[TLS] === handshake complete === "
                         f"version={ver} cipher=<none>")
            except Exception as e:
                emit(f"[TLS] post-handshake diag error: {e}")

    def _record(self, method: str, body_len: int):
        headers = {k: v for k, v in self.headers.items()}
        body_bytes = self.rfile.read(body_len) if body_len > 0 else b''
        body_str = body_bytes.decode('utf-8', errors='replace')
        ts = datetime.now().strftime('%H:%M:%S.%f')[:-3]
        rec = RecordedRequest(method, self.path, headers, body_str, ts)
        if self.server_ref is not None:
            self.server_ref._add_request(rec)

    def do_GET(self):
        self._record('GET', 0)
        self._send_default_response()

    def do_POST(self):
        length = int(self.headers.get('Content-Length', 0))
        # M-NEW-12 fix: 超长 body 直接 413 拒绝, 不读不写.
        if length < 0 or length > _MAX_BODY_LEN:
            self.send_response(413)
            self.send_header('Content-Type', 'text/plain')
            self.send_header('Content-Length', '0')
            self.end_headers()
            self._record('POST', length)
            return
        self._record('POST', length)
        self._send_default_response()

    def do_PUT(self):
        length = int(self.headers.get('Content-Length', 0))
        if length < 0 or length > _MAX_BODY_LEN:
            self.send_response(413)
            self.send_header('Content-Type', 'text/plain')
            self.send_header('Content-Length', '0')
            self.end_headers()
            self._record('PUT', length)
            return
        self._record('PUT', length)
        self._send_default_response()

    def do_DELETE(self):
        self._record('DELETE', 0)
        self._send_default_response()

    def _send_default_response(self):
        # Default response: JSON with request echo + server status
        if self.server_ref is None:
            body = b'{"status":"ok"}'
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        cfg = self.server_ref.get_response_config()
        status_code = cfg.get('status_code', 200)
        content_type = cfg.get('content_type', 'application/json')
        body_str = cfg.get('body', json.dumps({
            "status": "ok",
            "server": "BabyOS-MockHttpServer",
            "time": datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
        }))
        body = body_str.encode('utf-8')
        self.send_response(status_code)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

        # Record the response
        ts = datetime.now().strftime('%H:%M:%S.%f')[:-3]
        rec = RecordedRequest(
            'SERVER_RESPONSE', self.path,
            {'Content-Type': content_type, 'Status': str(status_code)},
            body_str, ts, source='server_response'
        )
        self.server_ref._add_request(rec)


class MockHttpServer:
    """
    Threaded HTTP/HTTPS mock server for BabyOS HTTP client testing.
    """

    def __init__(self, log_fn: Optional[Callable[[str], None]] = None):
        self._log = log_fn or (lambda s: None)
        self._server = None  # type: Optional[http.server.ThreadingHTTPServer]
        self._thread = None
        self._lock = threading.Lock()
        self._requests: List[RecordedRequest] = []
        self._response_cfg = {
            'status_code': 200,
            'content_type': 'application/json',
            'body': '',
        }
        self._is_running = False
        self._is_https = False
        self._port = 8080

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------
    def start(self, port: int = 8080, use_https: bool = False) -> bool:
        """
        Start the server on the given port.
        Returns True on success, False if already running or start failed.
        """
        with self._lock:
            if self._is_running:
                self._log("[MockHttp] already running")
                return False

            try:
                # Use ThreadingHTTPServer for concurrent client support
                self._server = http.server.ThreadingHTTPServer(
                    ('0.0.0.0', port), _BabyOSHttpHandler
                )
                _BabyOSHttpHandler.server_ref = self

                if use_https:
                    # Generate or use a self-signed cert
                    cert_path, key_path = self._ensure_self_signed_cert()
                    # 强制使用 TLS 1.2（BabyOS 客户端只支持 1.2）
                    # Python 3.11 默认会尝试 TLS 1.3，必须显式禁用
                    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                    ctx.maximum_version = ssl.TLSVersion.TLSv1_2
                    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
                    # 限制 cipher suite，让 BabyOS（默认只有 AES-128）能匹配
                    ctx.set_ciphers('ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256')
                    # 启用 TLS 握手诊断 — 在每个 TLS 消息回调时打印 sig_algs、
                    # 协商 cipher、ServerKeyExchange sig_alg、alert 等信息。
                    # 仅供诊断，定位 BabyOS mbedtls 与 OpenSSL 3.x 的 PSS 兼容问题。
                    # functools.partial 闭包 self._log (GUI log panel) 让诊断能传到 GUI.
                    # Python 3.8: 必须用私有 _msg_callback setter (官方 public 在 3.10+).
                    # Python 3.10+: 优先用公开 msg_callback.
                    # msg_callback 接收的位置参数为
                    # (conn, direction, version, content_type, msg_type, data).
                    # 用工厂函数 _tls_msg_cb(log_fn) 把 self._log 闭包进回调, 而不是
                    # functools.partial — 因为 ssl.SSLContext._msg_callback 的 setter
                    # 把回调包了一层 inner() 再调用, partial 会导致参数被填到错误位置.
                    cb = _tls_msg_cb(self._log)
                    if hasattr(ctx, "_msg_callback"):
                        # 私有属性 — 3.8 / 3.9 唯一可用方式
                        ctx._msg_callback = cb
                    else:
                        ctx.msg_callback = cb
                    ctx.load_cert_chain(cert_path, key_path)
                    self._server.socket = ctx.wrap_socket(
                        self._server.socket, server_side=True
                    )
                    self._is_https = True
                else:
                    self._is_https = False

                self._port = port
                self._thread = threading.Thread(
                    target=self._server.serve_forever,
                    daemon=True
                )
                self._thread.start()
                self._is_running = True

                scheme = "https" if use_https else "http"
                self._log(f"[MockHttp] server started on {scheme}://0.0.0.0:{port}")
                return True
            except Exception as e:
                self._log(f"[MockHttp] start failed: {e}")
                self._server = None
                return False

    def stop(self) -> bool:
        """Stop the server. Returns True on success."""
        with self._lock:
            if not self._is_running:
                return False
            try:
                if self._server is not None:
                    self._server.shutdown()
                    self._server.server_close()
                self._is_running = False
                self._server = None
                self._thread = None
                _BabyOSHttpHandler.server_ref = None
                self._log("[MockHttp] server stopped")
                return True
            except Exception as e:
                self._log(f"[MockHttp] stop failed: {e}")
                return False

    def is_running(self) -> bool:
        return self._is_running

    def get_port(self) -> int:
        return self._port

    def is_https(self) -> bool:
        return self._is_https

    def set_response_config(self, status_code: int, content_type: str, body: str) -> None:
        """Configure the response that will be sent to incoming requests."""
        self._response_cfg = {
            'status_code': status_code,
            'content_type': content_type,
            'body': body,
        }

    def get_response_config(self) -> dict:
        return dict(self._response_cfg)

    def get_request_log(self) -> List[RecordedRequest]:
        """Return a copy of all recorded requests."""
        with self._lock:
            return list(self._requests)

    def clear_request_log(self) -> None:
        with self._lock:
            self._requests.clear()

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------
    def _add_request(self, rec: RecordedRequest) -> None:
        with self._lock:
            self._requests.append(rec)
        self._log(f"[MockHttp] {rec.method} {rec.path} ({len(rec.body)}B)")

    def _ensure_self_signed_cert(self) -> tuple:
        """
        Use the fixed self-signed cert/key pair in tool/certs/.
        The cert is committed to the repo and matches the one embedded
        in _config/b_mbedtls_config.h (MBEDTLS_SSL_DEFAULT_CERT).
        Returns (cert_path, key_path).
        """
        import os

        cert_dir = os.path.join(os.path.dirname(__file__), 'certs')
        cert_path = os.path.join(cert_dir, 'server.pem')
        key_path = os.path.join(cert_dir, 'server.key')

        if not os.path.exists(cert_path) or not os.path.exists(key_path):
            raise FileNotFoundError(
                f"Fixed cert not found: {cert_path}\n"
                "The cert files are committed to the repo and should exist.\n"
                "If missing, restore them from git or check the certs/ directory."
            )

        return cert_path, key_path
