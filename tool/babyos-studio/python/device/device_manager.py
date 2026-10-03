"""
device_manager — process-wide singleton holding BabyOS Studio device services.

Holds:
  - uart            : UartService (pyserial singleton)
  - protocol_client : ProtocolClient  (b_protocol frames, OTA / file xfer)
  - shell_client    : ShellClient     (param shell text commands)
  - http_mock       : HttpMock        (real local HTTP mock server)
  - webconfig       : WebConfigTool   (Keil/OpenOCD/log host tooling)
  - xmodem state    : active sender + last filename/data
  - param polling   : timed shell `param <name>` poller
  - log-to-file     : append DeviceManager log lines to disk

供 FastAPI 调用. Python 3.8 compatible. Real functionality only.
"""

from __future__ import print_function

import os
import threading
import time
from typing import Any, Dict, List, Optional

from . import file_util
from .http_mock import HttpMock
from .protocol_client import ProtocolClient
from .shell_client import ShellClient
from .uart_service import UartService, get_uart_service
from .webconfig_tool import WebConfigTool
from .xmodem_ydmodem import XmodemSender, YmodemSender


class DeviceManager:
    """Singleton device-service registry shared by FastAPI routers / pages."""

    _instance: Optional['DeviceManager'] = None
    _instance_lock = threading.Lock()

    @classmethod
    def get(cls) -> 'DeviceManager':
        """Return the process-wide DeviceManager, creating it on first use."""
        with cls._instance_lock:
            if cls._instance is None:
                cls._instance = cls()
            return cls._instance

    @classmethod
    def reset(cls) -> None:
        """
        Tear down and drop the singleton (tests / app shutdown).
        Does NOT close the UART automatically — call close_all() first.
        """
        with cls._instance_lock:
            inst = cls._instance
            cls._instance = None
        if inst is not None:
            try:
                inst.close_all()
            except Exception:
                pass

    def __init__(self) -> None:
        self.uart: UartService = get_uart_service()
        self.protocol_client: Optional[ProtocolClient] = None
        self.shell_client: Optional[ShellClient] = None
        self.http_mock: HttpMock = HttpMock()
        self.webconfig = WebConfigTool()

        # Async job registry (OTA / file / xmodem) — lives on the instance so
        # DeviceManager.reset() in tests cannot leak jobs across suites.
        self.jobs: Dict[str, Dict[str, Any]] = {}
        self._jobs_lock = threading.Lock()

        # Xmodem / Ymodem state
        self.xmodem_sender: Optional[XmodemSender] = None
        self.ymodem_sender: Optional[YmodemSender] = None
        self.active_xfer: Any = None
        self.xmodem_data: bytes = b''
        self.xmodem_filename: str = ''
        self.ymodem_data: bytes = b''
        self.ymodem_filename: str = ''

        # last known device info (cache for UI)
        self.last_uid: bytes = b''
        self.last_sn: bytes = b''
        self.last_devinfo: Optional[Dict[str, str]] = None
        self.last_param_list: List[str] = []
        self.last_netinfo: Optional[Dict[str, Any]] = None
        self.last_http_resp: Optional[Dict[str, Any]] = None

        # param timed polling (origin/dev mainwindow _on_param_polling_*)
        self._poll_thread: Optional[threading.Thread] = None
        self._poll_stop_evt = threading.Event()
        self._poll_name = ''
        self._poll_interval_ms = 1000
        self._poll_enabled = False
        self._poll_lock = threading.Lock()
        self._poll_last_value: Optional[int] = None
        self._poll_last_at: Optional[float] = None
        self._poll_error = ''

        # UART log-to-file (origin/dev mainwindow _on_open_log_file)
        self._log_file = None
        self._log_file_path = ''
        self._log_file_lock = threading.Lock()

        # optional log sink (FastAPI can attach)
        self.log_lines: List[str] = []
        self._log_limit = 500

    # ------------------------------------------------------------------
    # logging
    # ------------------------------------------------------------------

    def _log(self, msg: str) -> None:
        line = '[%s] %s' % (time.strftime('%H:%M:%S'), msg)
        self.log_lines.append(line)
        if len(self.log_lines) > self._log_limit:
            del self.log_lines[:len(self.log_lines) - self._log_limit]
        with self._log_file_lock:
            if self._log_file is not None:
                try:
                    self._log_file.write(line + '\n')
                    self._log_file.flush()
                except Exception:
                    pass

    def get_logs(self, tail: int = 100) -> List[str]:
        if tail <= 0:
            return list(self.log_lines)
        return list(self.log_lines[-tail:])

    def start_log_to_file(self, path: str) -> str:
        """Open append-mode log file; all subsequent _log() lines go to disk."""
        if not path or not str(path).strip():
            raise ValueError('log path empty')
        path = str(path).strip()
        d = os.path.dirname(os.path.abspath(path))
        if d and not os.path.isdir(d):
            raise IOError('log directory not found: %s' % d)
        self.stop_log_to_file()
        fh = open(path, 'a', encoding='utf-8')
        with self._log_file_lock:
            self._log_file = fh
            self._log_file_path = path
        self._log('log-to-file started: %s' % path)
        return path

    def stop_log_to_file(self) -> None:
        with self._log_file_lock:
            fh = self._log_file
            self._log_file = None
            self._log_file_path = ''
        if fh is not None:
            try:
                fh.close()
            except Exception:
                pass

    def log_to_file_status(self) -> Dict[str, Any]:
        with self._log_file_lock:
            return {
                'enabled': self._log_file is not None,
                'path': self._log_file_path,
            }

    # ------------------------------------------------------------------
    # UART lifecycle
    # ------------------------------------------------------------------

    def list_ports(self) -> List[str]:
        return UartService.list()

    def open_port(self, port: str, baudrate: int = 115200,
                  encrypt: bool = False) -> bool:
        """Open UART and (re)build protocol + shell clients bound to it.

        Any in-flight transfer / param poll bound to the previous binding is
        cancelled first — otherwise a re-open leaves zombie jobs racing the
        new ProtocolClient on the same UART.
        """
        self._cancel_inflight_before_rebind()
        ok = self.uart.open(port, baudrate)
        self._log('open_port %s @%s encrypt=%s -> %s' % (
            port, baudrate, bool(encrypt), ok))
        if ok:
            self._bind_clients(encrypt=encrypt)
        return ok

    def _cancel_inflight_before_rebind(self) -> None:
        """Stop transfers + param polling before (re)binding UART clients."""
        try:
            if self.protocol_client is not None:
                self.protocol_client.stop_transfer(notify_device=False)
        except Exception:
            pass
        try:
            self.cancel_xmodem()
        except Exception:
            pass
        try:
            self.cancel_ymodem()
        except Exception:
            pass
        try:
            self.stop_param_polling()
        except Exception:
            pass

    def close_port(self) -> None:
        self._cancel_inflight_before_rebind()
        self.uart.close()
        self._log('close_port')

    def is_open(self) -> bool:
        return bool(self.uart.is_open)

    def _bind_clients(self, encrypt: bool = False) -> None:
        """(Re)create ProtocolClient / ShellClient on the current UART."""
        self.protocol_client = ProtocolClient(self.uart, encrypt=encrypt,
                                              log_fn=self._log)
        self.shell_client = ShellClient(self.uart)
        self._log('clients bound host_id=0x%X encrypt=%s' % (
            self.protocol_client.host_id, bool(encrypt)))

    def ensure_clients(self) -> ProtocolClient:
        """Return protocol client, building it if UART is open but clients missing."""
        if self.protocol_client is None:
            if not self.uart.is_open:
                raise RuntimeError('UART is not open')
            self._bind_clients()
        assert self.protocol_client is not None
        return self.protocol_client

    def ensure_shell(self) -> ShellClient:
        if self.shell_client is None:
            if not self.uart.is_open:
                raise RuntimeError('UART is not open')
            self._bind_clients()
        assert self.shell_client is not None
        return self.shell_client

    # ------------------------------------------------------------------
    # protocol commands
    # ------------------------------------------------------------------

    def test_link(self, timeout: float = 2.0) -> Optional[tuple]:
        pc = self.ensure_clients()
        return pc.test_link(timeout=timeout)

    def set_time(self, utc: int, timeout: float = 2.0) -> Optional[tuple]:
        pc = self.ensure_clients()
        return pc.set_time(utc, timeout=timeout)

    def get_uid(self, timeout: float = 2.0) -> Optional[bytes]:
        pc = self.ensure_clients()
        uid = pc.get_uid(timeout=timeout)
        if uid is not None:
            self.last_uid = uid
        return uid

    def write_sn(self, sn_bytes: Optional[bytes] = None,
                 uid: Optional[bytes] = None, orval: int = 0,
                 timeout: float = 2.0) -> Optional[tuple]:
        pc = self.ensure_clients()
        if sn_bytes is None and uid is None:
            uid = self.last_uid or None
        resp = pc.write_sn(sn_bytes=sn_bytes, uid=uid, orval=orval,
                           timeout=timeout)
        if resp is not None:
            self.last_sn = resp[2] if resp[2] else b''
        return resp

    def get_device_info(self, timeout: float = 2.0) -> Optional[tuple]:
        pc = self.ensure_clients()
        info = pc.get_device_info(timeout=timeout)
        if info is not None:
            self.last_devinfo = {'version': info[0], 'model': info[1]}
        return info

    def start_ota(self, firmware_path: str, name: Optional[str] = None,
                  timeout: float = 30.0) -> bool:
        pc = self.ensure_clients()
        return pc.start_ota(firmware_path, name=name, timeout=timeout)

    def start_file_transfer(self, path: str, dev_no: int = 0,
                            offset: int = 0, name: Optional[str] = None,
                            timeout: float = 30.0) -> bool:
        pc = self.ensure_clients()
        return pc.start_file_transfer(path, dev_no=dev_no, offset=offset,
                                      name=name, timeout=timeout)

    def stop_transfer(self, notify_device: bool = True) -> None:
        """Soft-stop protocol transfer; optionally send 0x6 zeros (dev tool)."""
        pc = self.protocol_client
        if pc is not None:
            pc.stop_transfer(notify_device=notify_device)
        self._log('stop_transfer notify=%s' % bool(notify_device))

    def merge_folder(self, folder_path: str, out_name: str = 'allfile.bin'
                     ) -> Dict[str, Any]:
        """Merge folder files into BabyOS multi-file allfile.bin (CMD 0x6 prep)."""
        out_path, count, size, crc = file_util.merge_folder(folder_path, out_name)
        self._log('folder merged %s -> %s count=%d size=%d crc=0x%08X' % (
            folder_path, out_path, count, size, crc & 0xFFFFFFFF))
        return {
            'path': out_path,
            'folder': folder_path,
            'file_count': count,
            'size': size,
            'crc32': crc & 0xFFFFFFFF,
        }

    def set_cfgnet_mode(self, cfg_type: int = 0, ssid: str = '',
                        passwd: str = '', timeout: float = 2.0,
                        wait_ack: bool = True) -> Optional[tuple]:
        pc = self.ensure_clients()
        return pc.set_cfgnet_mode(cfg_type, ssid, passwd,
                                  timeout=timeout, wait_ack=wait_ack)

    def get_netinfo(self, timeout: float = 2.0) -> Optional[dict]:
        pc = self.ensure_clients()
        info = pc.get_netinfo(timeout=timeout)
        if info is not None:
            self.last_netinfo = info
        return info

    def set_voice_switch(self, on: int, timeout: float = 2.0,
                         wait_ack: bool = True) -> Optional[tuple]:
        pc = self.ensure_clients()
        return pc.set_voice_switch(on, timeout=timeout, wait_ack=wait_ack)

    def set_voice_volume(self, volume: int, timeout: float = 2.0,
                         wait_ack: bool = True) -> Optional[tuple]:
        pc = self.ensure_clients()
        return pc.set_voice_volume(volume, timeout=timeout, wait_ack=wait_ack)

    def get_voice_volume(self, timeout: float = 2.0) -> Optional[int]:
        pc = self.ensure_clients()
        return pc.get_voice_volume(timeout=timeout)

    def get_voice_stat(self, timeout: float = 2.0) -> Optional[dict]:
        pc = self.ensure_clients()
        return pc.get_voice_stat(timeout=timeout)

    def send_tts_content(self, content: str, timeout: float = 2.0,
                         wait_ack: bool = True) -> Optional[tuple]:
        pc = self.ensure_clients()
        return pc.send_tts_content(content, timeout=timeout,
                                   wait_ack=wait_ack)

    def invoke_tsl(self, content: str, timeout: float = 2.0) -> Optional[tuple]:
        pc = self.ensure_clients()
        return pc.invoke_tsl(content, timeout=timeout)

    def http_init(self, timeout: float = 2.0) -> Optional[tuple]:
        pc = self.ensure_clients()
        return pc.http_init(timeout=timeout)

    def http_deinit(self, timeout: float = 2.0) -> Optional[tuple]:
        pc = self.ensure_clients()
        return pc.http_deinit(timeout=timeout)

    def http_request(self, method: Any, url: str, headers: str = '',
                     body: Any = b'', timeout: float = 5.0) -> Optional[dict]:
        pc = self.ensure_clients()
        resp = pc.http_request(method, url, headers, body, timeout=timeout)
        if resp is not None:
            self.last_http_resp = resp
        return resp

    def send_raw_cmd(self, cmd: int, param: bytes = b'') -> bytes:
        pc = self.ensure_clients()
        return pc.send_cmd(cmd, param)

    # ------------------------------------------------------------------
    # shell / param
    # ------------------------------------------------------------------

    def param_list(self, timeout: float = 1.0) -> List[str]:
        sh = self.ensure_shell()
        names = sh.param_list(timeout=timeout)
        self.last_param_list = names
        return names

    def param_get(self, name: str, timeout: float = 1.0) -> Optional[int]:
        sh = self.ensure_shell()
        return sh.param_get(name, timeout=timeout)

    def param_set(self, name: str, value: Any, timeout: float = 1.0,
                  verify: bool = True) -> bool:
        sh = self.ensure_shell()
        return sh.param_set(name, value, timeout=timeout, verify=verify)

    def shell_command(self, cmd: str, timeout: float = 1.0) -> str:
        sh = self.ensure_shell()
        return sh.send_command(cmd, timeout=timeout)

    # ------------------------------------------------------------------
    # param timed polling
    # ------------------------------------------------------------------

    def _transfer_busy(self) -> bool:
        """True if OTA/file transfer is active on the protocol client OR
        xmodem/ymodem sender. Both share the UART with shell/param ops."""
        pc = self.protocol_client
        if pc is not None and getattr(pc, 'transfer_active', False):
            return True
        if self.active_xfer is not None and getattr(self.active_xfer, 'is_active', False):
            return True
        return False

    def require_no_transfer(self, what: str = '操作') -> None:
        """Raise RuntimeError if a transfer is using the UART."""
        if self._transfer_busy():
            raise RuntimeError('传输进行中，无法执行%s' % what)

    def start_param_polling(self, name: str, interval_ms: int = 1000) -> bool:
        """Start shell `param <name>` polling every interval_ms (dev tool)."""
        if not name or not str(name).strip():
            raise ValueError('param name empty')
        # shell text and binary transfer frames must not share the UART
        self.require_no_transfer('参数轮询')
        interval_ms = int(interval_ms)
        if interval_ms < 100:
            interval_ms = 100
        if interval_ms > 3600000:
            interval_ms = 3600000
        if self._poll_enabled:
            self.stop_param_polling()
        self._poll_name = str(name).strip()
        self._poll_interval_ms = interval_ms
        self._poll_stop_evt.clear()
        self._poll_error = ''
        self._poll_enabled = True
        self._poll_thread = threading.Thread(
            target=self._param_poll_loop, name='param-poll', daemon=True)
        self._poll_thread.start()
        self._log('param polling start name=%s interval=%dms' % (
            self._poll_name, interval_ms))
        return True

    def stop_param_polling(self) -> None:
        was = self._poll_enabled
        self._poll_enabled = False
        self._poll_stop_evt.set()
        t = self._poll_thread
        self._poll_thread = None
        if t is not None and t.is_alive():
            t.join(timeout=2.0)
        if was:
            self._log('param polling stopped')

    def _param_poll_loop(self) -> None:
        while self._poll_enabled and not self._poll_stop_evt.is_set():
            name = self._poll_name
            try:
                if not self.uart.is_open:
                    self._poll_error = 'uart closed'
                    value = None
                else:
                    value = self.param_get(name, timeout=0.8)
                    self._poll_error = ''
            except (IOError, OSError, RuntimeError) as exc:
                value = None
                self._poll_error = str(exc)
            with self._poll_lock:
                if value is not None:
                    self._poll_last_value = value
                self._poll_last_at = time.time()
            self._poll_stop_evt.wait(self._poll_interval_ms / 1000.0)

    def param_polling_status(self) -> Dict[str, Any]:
        with self._poll_lock:
            last_value = self._poll_last_value
            last_at = self._poll_last_at
        return {
            'enabled': self._poll_enabled,
            'name': self._poll_name,
            'interval_ms': self._poll_interval_ms,
            'last_value': last_value,
            'last_at': last_at,
            'error': self._poll_error,
            'uart_open': self.is_open(),
        }

    # ------------------------------------------------------------------
    # HTTP mock
    # ------------------------------------------------------------------

    def start_http_mock(self, port: int = 0,
                        body: Any = b'{"ok":true}',
                        content_type: str = 'application/json',
                        status_code: int = 200,
                        https: bool = False,
                        file_log: bool = True) -> int:
        return self.http_mock.start(port=port, body=body,
                                    content_type=content_type,
                                    status_code=status_code,
                                    https=https,
                                    file_log=file_log)

    def stop_http_mock(self) -> None:
        self.http_mock.stop()

    # ------------------------------------------------------------------
    # Xmodem / Ymodem
    # ------------------------------------------------------------------

    def load_xmodem_file(self, path: str) -> int:
        with open(path, 'rb') as f:
            self.xmodem_data = f.read()
        self.xmodem_filename = path.replace('\\', '/').split('/')[-1]
        self._log('xmodem file loaded %s (%d bytes)' % (
            self.xmodem_filename, len(self.xmodem_data)))
        return len(self.xmodem_data)

    def load_ymodem_file(self, path: str) -> int:
        with open(path, 'rb') as f:
            self.ymodem_data = f.read()
        self.ymodem_filename = path.replace('\\', '/').split('/')[-1]
        self._log('ymodem file loaded %s (%d bytes)' % (
            self.ymodem_filename, len(self.ymodem_data)))
        return len(self.ymodem_data)

    def start_xmodem(self, data: Optional[bytes] = None,
                     log_fn: Optional[Any] = None,
                     timeout_sec: Optional[float] = None,
                     start_timeout_sec: Optional[float] = None) -> bool:
        if data is not None:
            self.xmodem_data = data
        if not self.xmodem_data:
            return False
        if self._transfer_busy():
            return False
        if not self.uart.is_open:
            return False

        def _send(buf: bytes) -> None:
            self.uart.write(buf)

        def _log(msg: str) -> None:
            self._log(msg)
            if log_fn is not None:
                try:
                    log_fn(msg)
                except Exception:
                    pass

        kwargs = {}
        if timeout_sec is not None:
            kwargs['timeout_sec'] = float(timeout_sec)
        if start_timeout_sec is not None:
            kwargs['start_timeout_sec'] = float(start_timeout_sec)
        self.xmodem_sender = XmodemSender(_send, log_fn=_log, **kwargs)
        self.xmodem_sender.start(self.xmodem_data)
        self.active_xfer = self.xmodem_sender
        self._log('xmodem started')
        return True

    def start_ymodem(self, data: Optional[bytes] = None,
                     filename: Optional[str] = None,
                     log_fn: Optional[Any] = None,
                     timeout_sec: Optional[float] = None,
                     start_timeout_sec: Optional[float] = None) -> bool:
        if data is not None:
            self.ymodem_data = data
        if filename:
            self.ymodem_filename = filename
        if not self.ymodem_data:
            return False
        if self._transfer_busy():
            return False
        if not self.uart.is_open:
            return False

        def _send(buf: bytes) -> None:
            self.uart.write(buf)

        def _log(msg: str) -> None:
            self._log(msg)
            if log_fn is not None:
                try:
                    log_fn(msg)
                except Exception:
                    pass

        kwargs = {}
        if timeout_sec is not None:
            kwargs['timeout_sec'] = float(timeout_sec)
        if start_timeout_sec is not None:
            kwargs['start_timeout_sec'] = float(start_timeout_sec)
        self.ymodem_sender = YmodemSender(_send, log_fn=_log, **kwargs)
        self.ymodem_sender.start(self.ymodem_data,
                                  filename=self.ymodem_filename or 'file.bin')
        self.active_xfer = self.ymodem_sender
        self._log('ymodem started')
        return True

    def cancel_xmodem(self) -> None:
        if self.xmodem_sender is not None:
            self.xmodem_sender.cancel()
        if self.active_xfer is self.xmodem_sender:
            self.active_xfer = None
        self.xmodem_sender = None

    def cancel_ymodem(self) -> None:
        if self.ymodem_sender is not None:
            self.ymodem_sender.cancel()
        if self.active_xfer is self.ymodem_sender:
            self.active_xfer = None
        self.ymodem_sender = None

    def pump_xmodem(self) -> bool:
        """Feed UART bytes into the active xmodem/ymodem sender."""
        sender = self.active_xfer
        if sender is None:
            return False
        try:
            data = self.uart.read_available()
        except Exception:
            data = b''
        if data:
            for b in data:
                sender.on_uart_byte(b)
        if hasattr(sender, 'on_timer_tick'):
            sender.on_timer_tick()
        if not getattr(sender, 'is_active', False):
            self.active_xfer = None
            return False
        return True

    # ------------------------------------------------------------------
    # aggregate status
    # ------------------------------------------------------------------

    def job_put(self, job_id: str, job: Dict[str, Any]) -> None:
        with self._jobs_lock:
            self.jobs[job_id] = job

    def job_get(self, job_id: str) -> Optional[Dict[str, Any]]:
        with self._jobs_lock:
            return self.jobs.get(job_id)

    def job_update(self, job_id: str, **fields: Any) -> None:
        with self._jobs_lock:
            job = self.jobs.get(job_id)
            if job is not None:
                job.update(fields)
                job["updated_at"] = time.time()

    def latest_job(self, kind: str) -> Optional[Dict[str, Any]]:
        with self._jobs_lock:
            candidates = [j for j in self.jobs.values()
                          if j.get("kind") == kind]
            if not candidates:
                return None
            candidates.sort(key=lambda j: j.get("created_at", 0), reverse=True)
            return dict(candidates[0])

    def clear_jobs(self) -> None:
        with self._jobs_lock:
            self.jobs.clear()

    def status(self) -> dict:
        proto = None
        try:
            if self.protocol_client is not None:
                proto = self.protocol_client.status()
        except Exception:
            proto = None
        shell = None
        try:
            if self.shell_client is not None:
                shell = self.shell_client.status()
        except Exception:
            shell = None
        return {
            'uart': {
                'open': self.uart.is_open,
                'port': self.uart.port,
                'baudrate': self.uart.baudrate,
                'available': self.list_ports(),
            },
            'protocol': proto,
            'shell': shell,
            'http_mock': self.http_mock.status(),
            'xmodem': {
                'active': self.active_xfer is not None
                and getattr(self.active_xfer, 'is_active', False),
                'xmodem_file': self.xmodem_filename,
                'ymodem_file': self.ymodem_filename,
            },
            'param_polling': self.param_polling_status(),
            'log_to_file': self.log_to_file_status(),
            'webconfig': self.webconfig.status(),
            'last_uid_hex': self.last_uid.hex() if self.last_uid else '',
            'last_devinfo': self.last_devinfo,
            'last_param_list': list(self.last_param_list),
            'last_netinfo': self.last_netinfo,
            'last_http_resp': self.last_http_resp,
        }

    # ------------------------------------------------------------------
    # teardown
    # ------------------------------------------------------------------

    def close_all(self) -> None:
        """Close UART, HTTP mock, polling, log file and any transfer state."""
        try:
            self.clear_jobs()
        except Exception:
            pass
        try:
            self.stop_param_polling()
        except Exception:
            pass
        try:
            self.stop_transfer_soft()
        except Exception:
            pass
        try:
            self.stop_http_mock()
        except Exception:
            pass
        try:
            self.stop_log_to_file()
        except Exception:
            pass
        try:
            self.webconfig.stop_log()
        except Exception:
            pass
        try:
            self.close_port()
        except Exception:
            pass

    def stop_transfer_soft(self) -> None:
        if self.protocol_client is not None:
            try:
                self.protocol_client.stop_transfer(notify_device=False)
            except Exception:
                self.protocol_client.stop_transfer()


def get_device_manager() -> DeviceManager:
    """Module-level accessor matching get_uart_service() style."""
    return DeviceManager.get()
