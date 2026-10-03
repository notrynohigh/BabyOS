"""
device_manager — process-wide singleton holding BabyOS Studio device services.

Holds:
  - uart            : UartService (pyserial singleton)
  - protocol_client : ProtocolClient  (b_protocol frames, OTA / file xfer)
  - shell_client    : ShellClient     (param shell text commands)
  - http_mock       : HttpMock        (real local HTTP mock server)
  - xmodem state    : active sender + last filename/data

供 FastAPI 调用. Python 3.8 compatible. Real functionality only.
"""

from __future__ import print_function

import threading
import time
from typing import Any, Dict, List, Optional

from .http_mock import HttpMock
from .protocol_client import ProtocolClient
from .shell_client import ShellClient
from .uart_service import UartService, get_uart_service
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

    def get_logs(self, tail: int = 100) -> List[str]:
        if tail <= 0:
            return list(self.log_lines)
        return list(self.log_lines[-tail:])

    # ------------------------------------------------------------------
    # UART lifecycle
    # ------------------------------------------------------------------

    def list_ports(self) -> List[str]:
        return UartService.list()

    def open_port(self, port: str, baudrate: int = 115200,
                  encrypt: bool = False) -> bool:
        """Open UART and (re)build protocol + shell clients bound to it."""
        ok = self.uart.open(port, baudrate)
        self._log('open_port %s @%s encrypt=%s -> %s' % (
            port, baudrate, bool(encrypt), ok))
        if ok:
            self._bind_clients(encrypt=encrypt)
        return ok

    def close_port(self) -> None:
        # abort any active protocol transfer / xmodem first
        try:
            if self.protocol_client is not None:
                self.protocol_client.stop_transfer()
        except Exception:
            pass
        self.cancel_xmodem()
        self.cancel_ymodem()
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
    # HTTP mock
    # ------------------------------------------------------------------

    def start_http_mock(self, port: int = 0,
                        body: Any = b'{"ok":true}',
                        content_type: str = 'application/json',
                        status_code: int = 200,
                        https: bool = False) -> int:
        return self.http_mock.start(port=port, body=body,
                                    content_type=content_type,
                                    status_code=status_code,
                                    https=https)

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
        if self.active_xfer is not None and getattr(self.active_xfer, 'is_active', False):
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
        if self.active_xfer is not None and getattr(self.active_xfer, 'is_active', False):
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
            'last_uid_hex': self.last_uid.hex() if self.last_uid else '',
            'last_devinfo': self.last_devinfo,
            'last_param_list': list(self.last_param_list),
        }

    # ------------------------------------------------------------------
    # teardown
    # ------------------------------------------------------------------

    def close_all(self) -> None:
        """Close UART, HTTP mock and any transfer state."""
        try:
            self.stop_transfer_soft()
        except Exception:
            pass
        try:
            self.stop_http_mock()
        except Exception:
            pass
        try:
            self.close_port()
        except Exception:
            pass

    def stop_transfer_soft(self) -> None:
        if self.protocol_client is not None:
            self.protocol_client.stop_transfer()


def get_device_manager() -> DeviceManager:
    """Module-level accessor matching get_uart_service() style."""
    return DeviceManager.get()
