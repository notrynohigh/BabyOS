"""
http_server — Mock HTTP/HTTPS server for device HTTP client testing.

Real implementation that runs an HTTP server in a background thread,
responding to device requests with a configurable body. Optional HTTPS
uses a self-signed certificate.

Used by the BabyOS upper-PC GUI to verify the device's HTTP client
implementation.

Request log records are dicts with these keys:
    timestamp : str  "HH:MM:SS.mmm"  (local time, millisecond precision)
    source    : str  "IP:port"        (client remote endpoint)
    method    : str  GET / POST / PUT / DELETE
    path      : str  request URI path (with query string)
    headers   : dict header name -> value
    body      : bytes|None  request body (consumed from socket)

Response body supports the placeholder "${SERVER_TIME}" which is replaced
with the current local-time string at request time, e.g.:
    body='{"server_time": "${SERVER_TIME}"}'
"""

import http.server
import ssl
import socket
import socketserver
import threading
import tempfile
import shutil
import os
import sys
import time
import traceback
import datetime


def _make_self_signed_cert(certfile, keyfile):
    """Locate the bundled cert files. Certs are pre-generated at build time
    via OpenSSL and shipped alongside the exe (see build_exe.bat). We
    fall back to runtime openssl generation if bundled certs are missing
    (e.g. running from source)."""
    import sys
    import shutil
    import subprocess

    candidates = []
    if getattr(sys, 'frozen', False):
        meipass = getattr(sys, '_MEIPASS', None)
        if meipass:
            candidates.append(meipass)
    candidates.append(os.path.dirname(os.path.abspath(__file__)))

    for base in candidates:
        c = os.path.join(base, 'mock_https_cert.pem')
        k = os.path.join(base, 'mock_https_key.pem')
        if os.path.isfile(c) and os.path.isfile(k):
            try:
                shutil.copyfile(c, certfile)
                shutil.copyfile(k, keyfile)
                return True
            except Exception:
                pass

    try:
        subprocess.run(
            ['openssl', 'req', '-x509', '-newkey', 'rsa:2048',
             '-keyout', keyfile, '-out', certfile,
             '-days', '1', '-nodes',
             '-subj', '/CN=localhost'],
            check=True, capture_output=True,
        )
        return True
    except Exception:
        return False


def _now_iso_ms():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


_LOG_FILENAME = "mock_http.log"


def _resolve_log_path():
    """Return (log_dir, log_path) for the mock_http.log file.

    Priority:
      1. PyInstaller-frozen: dir of sys.executable (writable, persistent).
      2. Source mode:        dir of this source file (tool/).
      3. CWD fallback.
    Never raises; returns (None, None) if all candidates are unusable.
    """
    candidates = []
    if getattr(sys, "frozen", False):
        candidates.append(os.path.dirname(os.path.abspath(sys.executable)))
    try:
        candidates.append(os.path.dirname(os.path.abspath(__file__)))
    except Exception:
        pass
    try:
        candidates.append(os.getcwd())
    except Exception:
        pass
    for d in candidates:
        try:
            if d and os.path.isdir(d) and os.access(d, os.W_OK):
                return d, os.path.join(d, _LOG_FILENAME)
        except Exception:
            continue
    return None, None


def _server_time():
    """Time string embedded in response body. Format: ISO8601-ish."""
    return datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S")


class _MockHandler(http.server.BaseHTTPRequestHandler):
    """HTTP handler that responds with the configured status/body."""
    server_version = "BabyOS-MockHttp/1.0"
    protocol_version = "HTTP/1.1"  # send Connection: close so client disconnects too

    # CRIT-MOCKHTTP-HANDLER-HANG fix: BaseHTTPRequestHandler.handle() 默认会循环
    # 处理 keep-alive 请求 (while not self.close_connection: handle_one_request()).
    # 嵌入式 client (ESP-12F AT 固件, AT+CIPSTART="TCP" 不带 Connection 头) 发完
    # 一个请求后会 hold socket 等 server 回 close, 但从不主动 close. 结果:
    # handler 处理完 do_GET 后, self.rfile.readline(65537) 永远阻塞在等下一个
    # HTTP 请求行 — 线程卡死, fd 已释放但线程仍存活. 累积到一定数量后
    # ThreadingMixIn 调度排队 (或 OS 资源耗尽), 新连接的 accept 停止处理,
    # netstat 上看到 device 端 ESTABLISHED 堆积 + accept 计数停滞. 表现为
    # "服务运行几分钟后失效".
    # 修复: handle() 入口强制 close_connection=True, 每个请求处理完即返回,
    # handler 线程退出, shutdown_request 顺势释放所有资源.
    def handle(self):
        self.close_connection = True
        # 限制 socket 超时 — 防御万一 device 在响应中途断开, server rfile
        # 不会永久阻塞. 10s 比 device 13s recv_timeout 短一点, 让 server 先
        # 主动断开, 避免 ESTABLISHED 长时间占用.
        try:
            self.request.settimeout(10.0)
        except Exception:
            pass
        try:
            self.handle_one_request()
        finally:
            # 标记 close, 让上层 while 循环退出 (虽然我们已经只跑了一次).
            self.close_connection = True

    # NOTE: 旧版本在这里定义过 shutdown_request 来处理 SSL 关闭. 但
    # BaseServer.process_request_thread 的 finally 块调的是
    #   self.server.shutdown_request(request)
    # 而不是 handler 的. handler 上的同名方法永远不会被触发. 现已把真正的
    # SSL-aware shutdown_request 移到 _MockServer (在 start() 内定义), 这里
    # 留 _MockHandler.shutdown_request = TCPServer.shutdown_request 的 alias
    # 不再有意义, 直接删掉避免误导后人.


    # Config injected by MockHttpServer
    _resp_code = 200
    _resp_type = "application/json"
    _resp_body = b"{}"
    _log_fn = staticmethod(lambda msg: None)
    _record_fn = staticmethod(lambda method, path, hdrs, body, client_address: None)
    _tb_log_fn = staticmethod(lambda tag, exc_info: None)
    _file_log_enabled = False

    def log_message(self, fmt, *args):
        try:
            self._log_fn("[MockHttp] " + (fmt % args))
        except Exception:
            pass

    def _read_request_body(self):
        try:
            length = int(self.headers.get('Content-Length', '0') or 0)
            if length > 0:
                return self.rfile.read(length)
        except Exception:
            self._tb_log_fn("read_request_body", sys.exc_info())
        return b""

    def _record_and_respond(self, method):
        hdrs = {k: v for k, v in self.headers.items()}
        body = self._read_request_body()
        try:
            # _record_request lives on MockHttpServer (no client_address), so we
            # pass our own (the handler has the real client endpoint).
            self._record_fn(method, self.path, hdrs, body, self.client_address)
        except Exception:
            self._tb_log_fn("record_fn", sys.exc_info())

        # Build response: replace ${SERVER_TIME} placeholder with current time
        body_template = type(self)._resp_body
        ctype = type(self)._resp_type
        code = type(self)._resp_code
        if isinstance(body_template, bytes):
            try:
                body = body_template.replace(b"${SERVER_TIME}", _server_time().encode("utf-8"))
            except Exception:
                self._tb_log_fn("record_and_respond.body_bytes", sys.exc_info())
                body = body_template
        else:
            try:
                body = body_template.replace("${SERVER_TIME}", _server_time()).encode("utf-8")
            except Exception:
                self._tb_log_fn("record_and_respond.body_str", sys.exc_info())
                body = body_template if isinstance(body_template, bytes) else str(body_template).encode("utf-8")

        try:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.send_header("X-Server-Time", _server_time())
            self.end_headers()
            self.wfile.write(body)
        except Exception:
            self._tb_log_fn("send_response", sys.exc_info())

    def do_GET(self):
        self._record_and_respond("GET")

    def do_POST(self):
        self._record_and_respond("POST")

    def do_PUT(self):
        self._record_and_respond("PUT")

    def do_DELETE(self):
        self._record_and_respond("DELETE")


class MockHttpServer:
    """A real, working Mock HTTP/HTTPS server running on a background thread."""

    def __init__(self, log_fn=None, enable_file_log=True):
        self._log = log_fn or (lambda msg: None)
        self._thread = None
        self._httpd = None
        self._request_log = []
        self._lock = threading.Lock()
        self._resp_code = 200
        self._resp_type = "application/json"
        self._resp_body = b"{}"
        self._certfile = None
        self._keyfile = None
        # ---- diagnostic file log state ----
        self._enable_file_log = bool(enable_file_log)
        self._log_file = None
        self._log_file_lock = threading.Lock()
        self._log_path = None
        self._stats_counter = 0
        self._snapshot_stop = threading.Event()
        self._snapshot_thread = None
        self._ssl_ctx = None
        # CRIT-MOCKHTTP-RACE fix: stop() 是异步的 — 它把 self._thread / self._httpd
        # 立即清空让 is_running() 返回 False, 但真正的 httpd.shutdown() 在 daemon
        # 线程里跑 (要 0.5~2 秒, 等 in-flight request + SSL handshake). 在这个
        # "假停止" 窗口里用户调 start(), 会创建第二个 ThreadingHTTPServer 并 listen
        # 同一端口. Windows SO_REUSEADDR 允许两个 socket 同时 listen 同一端口, OS
        # 会把新 client 连接交给"先 listen"的那个 (即旧 server). 旧 server 在
        # _shutdown 里 server_close() 时把已 accept 的 client socket 强行 close →
        # 客户端 ConnectionResetError. 实测: 50 次 HTTPS rapid restart, 49/50 失败.
        # 修复: 用 _stop_event 标记 stop 是否跑完. start() 开头若 event 未 set,
        # 同步等最多 3 秒 (httpd.join timeout=2.0 + 缓冲). GUI toggle 间隔通常 >0
        # 但 < 0.5s, 3 秒足够.
        self._stop_event = threading.Event()
        self._stop_event.set()

    def is_running(self):
        return self._thread is not None and self._thread.is_alive()

    def set_response_config(self, code, content_type, body):
        self._resp_code = int(code)
        self._resp_type = str(content_type)
        if isinstance(body, str):
            self._resp_body = body.encode("utf-8")
        else:
            self._resp_body = bytes(body)

    # ---- diagnostic file log -----------------------------------------

    _SNAPSHOT_REQUEST_INTERVAL = 100
    _SNAPSHOT_TIME_INTERVAL_SEC = 60.0

    def set_file_log_enabled(self, enabled):
        """Runtime toggle from the GUI checkbox. Safe to call while running."""
        enabled = bool(enabled)
        with self._log_file_lock:
            self._enable_file_log = enabled
        if not enabled:
            self._close_log_file()
        # If enabling, lazy-open happens on the next _write_log call.

    def close_log_file(self):
        """Public close (called from MainWindow.destroy)."""
        self._close_log_file()

    def _open_log_file(self):
        """Lazy-open the log file. Idempotent. Called under _log_file_lock.
        Does NOT touch the snapshot thread — that is started/stopped by the
        server start/stop so its lifetime matches the server, not the log file.
        """
        if self._log_file is not None:
            return
        if not self._enable_file_log:
            return
        log_dir, log_path = _resolve_log_path()
        del log_dir  # not used directly; path resolution already validated it
        if log_path is None:
            try:
                self._log("[MockHttp] file log disabled: no writable directory")
            except Exception:
                pass
            return
        try:
            # 'a' mode so prior runs are kept; line-buffered for tail visibility.
            self._log_file = open(log_path, 'a', encoding='utf-8', buffering=1)
            self._log_path = log_path
            try:
                self._log(f"[MockHttp] diagnostic log -> {log_path}")
            except Exception:
                pass
            try:
                self._log_file.write(
                    f"[{_now_iso_ms()}] [LIFECYCLE] file opened (path={log_path})\n"
                )
                self._log_file.flush()
            except Exception:
                pass
        except (OSError, IOError) as e:
            self._log_file = None
            try:
                self._log(f"[MockHttp] file log open failed: {e}")
            except Exception:
                pass

    def _close_log_file(self):
        """Close file. Does NOT stop snapshot thread (that is owned by server
        start/stop, not by file toggling). The snapshot thread will simply
        no-op on its next write if file_log is disabled.
        """
        with self._log_file_lock:
            if self._log_file is not None:
                try:
                    self._log_file.write(
                        f"[{_now_iso_ms()}] [LIFECYCLE] file closed\n"
                    )
                    self._log_file.flush()
                except Exception:
                    pass
                try:
                    self._log_file.close()
                except Exception:
                    pass
                self._log_file = None

    def _write_log(self, line):
        """Append one line to the log file. Never raises."""
        if not self._enable_file_log:
            return
        with self._log_file_lock:
            if self._log_file is None:
                self._open_log_file()
                if self._log_file is None:
                    return
            try:
                ts = _now_iso_ms()
                self._log_file.write(f"[{ts}] {line}\n")
                self._log_file.flush()
            except (OSError, IOError, ValueError) as e:
                # file disappeared or disk full — close, surface once, retry next time
                try:
                    self._log_file.close()
                except Exception:
                    pass
                self._log_file = None
                try:
                    self._log(f"[MockHttp] file log write failed: {e}")
                except Exception:
                    pass

    def _traceback_log(self, tag, exc_info):
        """Format and persist an exception traceback. Never raises."""
        if not self._enable_file_log or exc_info is None:
            return
        try:
            tb_text = "".join(traceback.format_exception(*exc_info))
        except Exception:
            tb_text = f"<traceback format failed for {tag}>"
        ts = _now_iso_ms()
        indented = "\n".join(
            f"[{ts}] [TB][{tag}] {ln}"
            for ln in tb_text.rstrip("\n").splitlines()
        )
        with self._log_file_lock:
            if self._log_file is None:
                self._open_log_file()
            if self._log_file is not None:
                try:
                    self._log_file.write(indented + "\n")
                    self._log_file.flush()
                except Exception:
                    pass

    def _maybe_snapshot_session_stats(self):
        """Called from _record_request. Cheap when nothing to do."""
        self._stats_counter += 1
        if self._stats_counter < self._SNAPSHOT_REQUEST_INTERVAL:
            return
        self._stats_counter = 0
        self._do_snapshot("request-threshold")

    def _do_snapshot(self, reason):
        ctx = self._ssl_ctx
        if ctx is None:
            return  # HTTP mode has no ctx
        try:
            stats = ctx.session_stats()
            body = " ".join(f"{k}={v}" for k, v in sorted(stats.items()))
            self._write_log(f"[STATS][{reason}] {body}")
            # Explicit handshake-failure line. `accept - accept_good` is the
            # number of TCP accepts that never completed TLS handshake; they
            # never reach the handler so they don't show up in [REQ] lines.
            raw_accept = stats.get("accept", 0)
            accept_good = stats.get("accept_good", 0)
            hs_failed = raw_accept - accept_good
            if hs_failed > 0:
                self._write_log(f"[TLS][{reason}] handshake_failed={hs_failed}")
        except (AttributeError, ssl.SSLError, OSError) as e:
            try:
                self._write_log(f"[STATS][{reason}] snapshot failed: {e}")
            except Exception:
                pass

    def _snapshot_loop(self):
        """Periodic snapshot thread. Exits when _snapshot_stop is set."""
        while not self._snapshot_stop.is_set():
            if self._snapshot_stop.wait(timeout=self._SNAPSHOT_TIME_INTERVAL_SEC):
                return
            # _do_snapshot already emits [STATS] and [TLS] lines
            self._do_snapshot("time-tick")

    # ------------------------------------------------------------------

    def start(self, port=8080, use_https=False):
        if self.is_running():
            return True
        # CRIT-MOCKHTTP-RACE fix: 等上次 stop 完全结束. is_running() 在 stop 立刻
        # 返回 False (因为 self._thread 被清空), 但 _shutdown 线程还在跑. 没有这
        # 个 wait, 第二次 start 会和正在停止的旧 server 同时 listen 同一端口, OS
        # 会把 client 连接交给先 listen 的 (旧 server), 然后旧 server 的
        # server_close() 会把已 accept 的 client socket 关闭, 客户端看到
        # ConnectionResetError. 3 秒上限 = httpd.join timeout=2.0 + 1 秒缓冲.
        if not self._stop_event.wait(timeout=3.0):
            self._log("[MockHttp] previous stop did not finish in 3s, starting anyway")
        # CRIT-MOCKHTTP-TMPDIR fix: 上次 stop 还没跑完 _shutdown 线程时, 用户立刻
        # 又 start 一次 — 新 start 会把 self._certfile 覆盖成新目录, 旧目录的
        # 路径被丢失, stop 时只能清新的, 旧的永远残留. 这里在 start 开头主动清
        # 一下残留 (如果有). 是 idempotent 操作, 无残留就 no-op.
        if self._certfile is not None:
            leftover = os.path.dirname(self._certfile)
            self._certfile = None
            self._keyfile  = None
            self._cleanup_tmpdir(leftover)

        _MockHandler._resp_code = self._resp_code
        _MockHandler._resp_type = self._resp_type
        _MockHandler._resp_body = self._resp_body
        _MockHandler._log_fn = self._log
        _MockHandler._record_fn = self._record_request
        _MockHandler._tb_log_fn = self._traceback_log
        _MockHandler._file_log_enabled = self._enable_file_log

        try:
            # CRIT-MOCKHTTP-SSL-CLOSE-SERVER fix: 必须把 shutdown_request 加在
            # *server* 类上, 不是 handler 类上. BaseServer.process_request_thread
            # 的 finally 块调的是 self.server.shutdown_request(request), 而
            # self.server 是 ThreadingHTTPServer 实例, 走的 MRO 是
            # ThreadingHTTPServer -> HTTPServer -> TCPServer -> BaseServer.
            # 原先写在 _MockHandler.shutdown_request 的 SSL 释放逻辑, 实际
            # 永远不会被调用 (handler 上的同名方法被忽略), 所以 HTTPS 模式
            # 每个连接都泄漏 fd, netstat 上累积 ESTABLISHED / CLOSE_WAIT.
            class _MockServer(http.server.ThreadingHTTPServer):
                allow_reuse_address = True
                daemon_threads = True

                def shutdown_request(self, request):
                    if isinstance(request, ssl.SSLSocket):
                        # server 端: client 已经走了, 不必发 close_notify. 直接
                        # 强释放底层 socket fd. SSLSocket 内部 _sock 指向底层.
                        def _ssl_close():
                            try:
                                request.close()
                            except Exception:
                                pass
                        t = threading.Thread(target=_ssl_close, daemon=True, name="ssl-close")
                        t.start()
                        t.join(timeout=1.5)
                        try:
                            raw = getattr(request, "_sock", None)
                            if raw is None:
                                raw = getattr(request, "__self__", None)
                            if raw is not None:
                                try:
                                    raw.settimeout(0.5)
                                except Exception:
                                    pass
                                try:
                                    raw.shutdown(socket.SHUT_RDWR)
                                except Exception:
                                    pass
                                try:
                                    raw.close()
                                except Exception:
                                    pass
                        except Exception:
                            try:
                                _MockHandler._tb_log_fn(
                                    "shutdown_request.raw_force_close", sys.exc_info())
                            except Exception:
                                pass
                        return
                    # HTTP (plain) 路径: 走标准 shutdown+close
                    try:
                        request.shutdown(socket.SHUT_WR)
                    except OSError:
                        try:
                            _MockHandler._tb_log_fn(
                                "shutdown_request.shutdown_shut_wr", sys.exc_info())
                        except Exception:
                            pass
                    try:
                        request.close()
                    except Exception:
                        try:
                            _MockHandler._tb_log_fn(
                                "shutdown_request.close", sys.exc_info())
                        except Exception:
                            pass

            httpd = _MockServer(("0.0.0.0", port), _MockHandler)
        except OSError as e:
            self._log(f"[MockHttp] bind failed port={port}: {e}")
            self._write_log(f"[LIFECYCLE] bind failed port={port}: {e}")
            return False

        if use_https:
            try:
                tmpdir = tempfile.mkdtemp(prefix="mockhttps_")
                certfile = os.path.join(tmpdir, "cert.pem")
                keyfile = os.path.join(tmpdir, "key.pem")
                if _make_self_signed_cert(certfile, keyfile):
                    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                    ctx.load_cert_chain(certfile, keyfile)
                    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
                    self._certfile = certfile
                    self._keyfile = keyfile
                    self._write_log(f"[LIFECYCLE] cert file loaded from {certfile}")
                else:
                    self._log("[MockHttp] openssl not found, HTTPS unavailable")
                    self._write_log("[LIFECYCLE] openssl not found, HTTPS unavailable")
                    httpd.server_close()
                    return False
            except Exception as e:
                self._log(f"[MockHttp] HTTPS setup failed: {e}")
                self._write_log(f"[LIFECYCLE] HTTPS setup failed: {e}")
                httpd.server_close()
                # 清理: start 失败时 cert/key 临时目录已生成, 必须清掉. 否则
                # 用户不调 stop 就会泄漏 (start 失败路径不走 stop, _certfile
                # 也不会被任何逻辑清掉). 这里用 self._cleanup_tmpdir 静态方法.
                if certfile:
                    self._cleanup_tmpdir(os.path.dirname(certfile))
                self._certfile = None
                self._keyfile  = None
                return False

        self._httpd = httpd
        if use_https:
            self._ssl_ctx = httpd.socket.context
        else:
            self._ssl_ctx = None
        self._thread = threading.Thread(
            target=httpd.serve_forever,
            kwargs={"poll_interval": 0.2},
            daemon=True,
            name="MockHttpServer",
        )
        self._thread.start()
        # Start (or restart) the snapshot thread. Lifetime matches server so
        # toggling the diagnostic-log checkbox doesn't leak or duplicate it.
        self._snapshot_stop.clear()
        if self._snapshot_thread is None or not self._snapshot_thread.is_alive():
            self._snapshot_thread = threading.Thread(
                target=self._snapshot_loop,
                name="MockHttp-Stats",
                daemon=True,
            )
            self._snapshot_thread.start()
        self._log(f"[MockHttp] started on port {port} (https={use_https})")
        self._write_log(f"[LIFECYCLE] start port={port} https={use_https}")
        return True

    def stop(self):
        # shutdown() blocks until in-flight requests finish (HTTPS handshake
        # can take seconds). GUI must not block, so push work to a daemon thread.
        httpd = self._httpd
        thread = self._thread
        certdir = self._certfile
        # 关键: stop 时要把 cert/key 的临时目录记下来再清, 不能用 self._certfile
        # 因为 self._certfile 在 stop 后可能没清, 影响下一次 start 的状态判断.
        # 把"待清理目录"放到局部变量, 让 _shutdown 线程负责 shutil.rmtree.
        if certdir is not None:
            # 拿到 certfile 路径的所在目录 (mkdtemp 创建的目录)
            certdir = os.path.dirname(certdir)
            self._certfile = None
            self._keyfile  = None
        if httpd is None and thread is None:
            # 没在运行但有遗留的临时目录? 顺手清理.
            if certdir:
                self._cleanup_tmpdir(certdir)
            return
        self._httpd = None
        self._thread = None
        # CRIT-MOCKHTTP-RACE fix: clear() 让任何 wait 中的 start() 阻塞,
        # 直到 _shutdown 线程结束前 set(). 没有这个标志, start() 会立刻
        # 创建新 server, 导致两个 httpd 同时 listen 同一端口 (见 start
        # 里的 CRIT-MOCKHTTP-RACE 注释). 必须**在** _shutdown 启动**前**
        # clear, 否则出现 stop() 已返回但 event 还未 clear 的窗口, start()
        # 看到 event 仍 set 就直接继续, race window 仍存在.
        self._stop_event.clear()

        def _shutdown():
            try:
                self._write_log("[LIFECYCLE] stop requested")
                if httpd is not None:
                    httpd.shutdown()
                    try:
                        httpd.server_close()
                    except Exception:
                        pass
                self._write_log("[LIFECYCLE] httpd.shutdown complete")
            except Exception:
                pass
            if thread is not None and thread.is_alive() and thread is not threading.current_thread():
                try:
                    thread.join(timeout=2.0)
                except Exception:
                    pass
            # Stop the snapshot thread too — it lives with the server.
            self._snapshot_stop.set()
            st = self._snapshot_thread
            if st is not None and st.is_alive() and st is not threading.current_thread():
                try:
                    st.join(timeout=1.0)
                except Exception:
                    pass
            self._snapshot_thread = None
            # 清理 HTTPS 临时目录. 之前 start 用 tempfile.mkdtemp 创建, stop 完全
            # 不清理 → 每次重启都残留一个 mockhttps_xxxx 目录, 长时间累积
            # 占满 %TEMP%. 必须在 server 完全停止后清 (避免 server 还在用证书).
            if certdir:
                self._cleanup_tmpdir(certdir)
            self._log("[MockHttp] stopped")
            # Final session_stats snapshot before clearing _ssl_ctx.
            try:
                self._do_snapshot("on-stop")
            except Exception:
                pass
            self._ssl_ctx = None
            # CRIT-MOCKHTTP-RACE fix: 标记本次 stop 完成. 必须放在 _shutdown
            # 末尾 (而不是 self._thread = None 那里), 因为用户调用 stop() 后
            # 立刻调 start(), start 会等这个 event. 如果清早了, start 在旧
            # server 真关闭前就拿到 event, race 仍存在.
            self._stop_event.set()

        threading.Thread(target=_shutdown, daemon=True, name="MockHttp-Stop").start()

    @staticmethod
    def _cleanup_tmpdir(path):
        try:
            if path and os.path.isdir(path):
                shutil.rmtree(path, ignore_errors=True)
        except Exception:
            pass

    def _record_request(self, method, path, hdrs, body, client_address=None):
        ts = _now_iso_ms()
        try:
            if client_address is not None:
                src = f"{client_address[0]}:{client_address[1]}"
            else:
                src = "?"
        except Exception:
            src = "?"
        with self._lock:
            self._request_log.append({
                "timestamp": ts,
                "source":    src,
                "method":    method,
                "path":      path,
                "headers":   dict(hdrs),
                "body":      body,
            })
            if len(self._request_log) > 500:
                self._request_log = self._request_log[-500:]
        # ---- diagnostic file log: one line per request, plus stats tick ----
        try:
            body_len = len(body) if body else 0
        except Exception:
            body_len = -1
        try:
            self._write_log(f"[REQ] {src} {method} {path} body={body_len}B")
        except Exception:
            pass
        try:
            self._maybe_snapshot_session_stats()
        except Exception:
            pass

    def get_request_log(self):
        with self._lock:
            return list(self._request_log)

    def clear_request_log(self):
        with self._lock:
            self._request_log.clear()