"""
uart_service — pyserial wrapper for BabyOS Studio device communication.

Real serial I/O via pyserial. No stubs.

API:
  list()                 -> list[str]   available port device names
  open(port, baudrate)   -> bool
  close()                -> None
  write(data)            -> int         bytes written
  read_available()       -> bytes       all currently-buffered input
  is_open                -> bool        property

UART journal: every byte flowing through the port is journaled (direction +
timestamp + raw bytes) into a bounded ring buffer, and can be recorded to a
local file. Powers the UI "串口日志" page.

RX is drained continuously by a background monitor thread into a shared
buffer (consumed by read()/read_available()), so idle unsolicited device
output is journaled in real time even when no protocol op is reading.
"""

import os
import threading
import time
from collections import deque
from typing import Any, Dict, List, Optional

# 共享 RX 缓冲上限（丢最旧）。监控线程在无人读取时持续排空 OS 缓冲，
# 长时间闲置后协议调用方不应拿到几小时前的陈旧字节。
_RX_BUF_MAX = 256 * 1024


def _bytes_to_text(data: bytes) -> str:
    """Printable ASCII as-is; CR/LF/TAB kept as real chars (UI renders line
    breaks); everything else as '·'."""
    out = []
    for b in data:
        if 32 <= b <= 126 or b in (9, 10, 13):
            out.append(chr(b))
        else:
            out.append('·')
    return ''.join(out)


class UartService:
    """Singleton-friendly serial port service wrapping pyserial.Serial."""

    def __init__(self) -> None:
        self._ser = None
        self._port = ''
        self._baudrate = 0
        self._last_error = ''
        # UART journal (ring buffer) + optional record-to-file
        self._journal = deque(maxlen=3000)
        self._journal_lock = threading.Lock()
        self._journal_seq = 0
        self._record_fh = None
        self._record_path = ''
        self._record_lock = threading.Lock()
        # 后台 RX 采集：所有读取经共享缓冲，闲置时也持续排空并入 journal
        self._rx_buf = bytearray()
        self._rx_cond = threading.Condition()
        self._io_lock = threading.Lock()
        self._rx_thread: Optional[threading.Thread] = None
        self._rx_stop = threading.Event()

    @property
    def last_error(self) -> str:
        """Human-readable reason for the last failed open()/list()."""
        return self._last_error

    # -- discovery ---------------------------------------------------------

    @staticmethod
    def list() -> List[str]:
        """
        List available serial ports (device names only).
        Returns [] if pyserial is unavailable.
        """
        try:
            from serial.tools import list_ports
        except ImportError:
            return []
        ports = [p.device for p in list_ports.comports()]
        # 部分虚拟串口驱动（如 ELTIMA）不出现在 list_ports，补扫
        # 注册表 HARDWARE\DEVICEMAP\SERIALCOMM（不存在的平台直接跳过）。
        try:
            import winreg
            key = winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE, r'HARDWARE\DEVICEMAP\SERIALCOMM')
            try:
                i = 0
                while True:
                    try:
                        _name, val, _ = winreg.EnumValue(key, i)
                    except OSError:
                        break
                    i += 1
                    if isinstance(val, str) and val not in ports:
                        ports.append(val)
            finally:
                winreg.CloseKey(key)
        except Exception:
            pass
        return ports

    @staticmethod
    def pyserial_available() -> bool:
        try:
            import serial  # noqa: F401
            return True
        except ImportError:
            return False

    # -- lifecycle ---------------------------------------------------------

    def open(self, port: str, baudrate: int = 115200,
             timeout: float = 0.05, write_timeout: float = 1.0) -> bool:
        """
        Open a serial port. Returns True on success.

        Product rule: a FAILED open must not tear down an already-working
        port. Only a successful open replaces the previous handle (after
        closing it). Reopening the same path closes it first so pyserial
        can re-acquire the device.
        """
        if not port:
            return False
        try:
            import serial
        except ImportError:
            self._last_error = 'pyserial 未安装（pip install pyserial）'
            return False
        # Same-path reopen requires closing the existing handle first.
        if self._ser is not None and self._port == port:
            self.close()
        try:
            # pyserial URL schemes (socket://, loop://, spy:// ...) go through
            # serial_for_url; plain device names (COM3, /dev/ttyUSB0, pty path)
            # go through serial.Serial. The socket:// scheme is what the test
            # harness uses for a TCP-loopback virtual serial link on Windows
            # (no pty there) — see test/device_features/mock_babyos_device.py.
            if '://' in port:
                ser = serial.serial_for_url(
                    port,
                    baudrate=baudrate,
                    bytesize=serial.EIGHTBITS,
                    parity=serial.PARITY_NONE,
                    stopbits=serial.STOPBITS_ONE,
                    timeout=timeout,
                    write_timeout=write_timeout,
                    xonxoff=False,
                    rtscts=False,
                    dsrdtr=False,
                )
            else:
                ser = serial.Serial(
                    port=port,
                    baudrate=baudrate,
                    bytesize=serial.EIGHTBITS,
                    parity=serial.PARITY_NONE,
                    stopbits=serial.STOPBITS_ONE,
                    timeout=timeout,
                    write_timeout=write_timeout,
                    xonxoff=False,
                    rtscts=False,
                    dsrdtr=False,
                )
        except Exception as exc:
            # Keep any previously open port — do not drop the live link.
            self._last_error = str(exc)
            return False
        self._last_error = ''
        # Success: install the new handle, drop the previous one if any.
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass
        self._ser = ser
        self._port = port
        self._baudrate = baudrate
        # flush any stale bytes from a previous session
        try:
            self._ser.reset_input_buffer()
            self._ser.reset_output_buffer()
        except Exception:
            pass
        with self._rx_cond:
            self._rx_buf.clear()
        self._rx_stop.clear()
        self._ensure_rx_thread()
        return True

    def close(self) -> None:
        """Close the port if open. Safe to call when already closed."""
        self._stop_rx_thread()
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass
        self._ser = None
        self._port = ''
        self._baudrate = 0
        with self._rx_cond:
            self._rx_buf.clear()
            self._rx_cond.notify_all()

    # -- RX 后台采集 ----------------------------------------------------------

    def _drain_once(self) -> None:
        """Pull pending OS-buffered bytes into the shared RX buffer.

        Single RX ingestion point — the monitor thread and callers both use it
        (serialized by _io_lock). Each non-empty burst is journaled here.
        """
        with self._io_lock:
            ser = self._ser
            if ser is None or not getattr(ser, 'is_open', False):
                return
            try:
                n = int(ser.in_waiting)
                data = bytes(ser.read(n)) if n > 0 else b''
            except Exception:
                data = b''
        if data:
            self._journal_add('RX', data)
            with self._rx_cond:
                self._rx_buf.extend(data)
                if len(self._rx_buf) > _RX_BUF_MAX:
                    del self._rx_buf[:-_RX_BUF_MAX]
                self._rx_cond.notify_all()

    def _rx_loop(self) -> None:
        """Monitor thread: keep draining so idle unsolicited RX is journaled
        in real time even when no protocol op is reading."""
        while not self._rx_stop.is_set():
            self._drain_once()
            self._rx_stop.wait(0.01)

    def _ensure_rx_thread(self) -> None:
        t = self._rx_thread
        if t is not None and t.is_alive():
            return
        self._rx_stop.clear()
        t = threading.Thread(target=self._rx_loop, name='uart-rx-monitor',
                             daemon=True)
        self._rx_thread = t
        t.start()

    def _stop_rx_thread(self) -> None:
        self._rx_stop.set()
        t = self._rx_thread
        self._rx_thread = None
        if t is not None and t.is_alive() and t is not threading.current_thread():
            t.join(timeout=1.0)

    # -- journal / record ----------------------------------------------------

    def _journal_add(self, direction: str, data: bytes) -> None:
        """Append one burst to the ring journal and (if enabled) the record file."""
        if not data:
            return
        now = time.time()
        with self._journal_lock:
            self._journal_seq += 1
            self._journal.append({
                'seq': self._journal_seq,
                'ts': now,
                'dir': direction,
                'raw': bytes(data),
            })
        self._record_write(direction, now, data)

    def journal_since(self, since: int, limit: int = 500) -> Dict[str, Any]:
        """Return entries with seq > since, oldest first.

        truncated=True means the ring buffer overran `since` (entries lost) —
        the caller should clear its view and re-render from what it gets.
        """
        with self._journal_lock:
            truncated = bool(self._journal) and since > 0 and \
                self._journal[0]['seq'] > since + 1
            entries = [e for e in self._journal if e['seq'] > since]
            if len(entries) > limit:
                entries = entries[-limit:]
                truncated = True
            out = [{
                'seq': e['seq'],
                'ts': e['ts'],
                'dir': e['dir'],
                'hex': e['raw'].hex(' '),
                'text': _bytes_to_text(e['raw']),
            } for e in entries]
            return {'entries': out, 'next': self._journal_seq,
                    'truncated': truncated}

    def journal_clear(self) -> None:
        with self._journal_lock:
            self._journal.clear()
            self._journal_seq = 0

    def start_record(self, path: str = '') -> str:
        """Append all subsequent journal lines to a file. Returns the path."""
        if not path or not str(path).strip():
            logdir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                  'logs')
            os.makedirs(logdir, exist_ok=True)
            path = os.path.join(
                logdir, time.strftime('serial_%Y%m%d_%H%M%S.log'))
        path = str(path).strip()
        d = os.path.dirname(os.path.abspath(path))
        if d and not os.path.isdir(d):
            raise IOError('记录目录不存在: %s' % d)
        self.stop_record()
        fh = open(path, 'a', encoding='utf-8')
        with self._record_lock:
            self._record_fh = fh
            self._record_path = path
        fh.write('--- record start %s ---\n'
                 % time.strftime('%Y-%m-%d %H:%M:%S'))
        fh.flush()
        return path

    def stop_record(self) -> None:
        with self._record_lock:
            fh = self._record_fh
            self._record_fh = None
            self._record_path = ''
        if fh is not None:
            try:
                fh.write('--- record stop %s ---\n'
                         % time.strftime('%Y-%m-%d %H:%M:%S'))
                fh.close()
            except Exception:
                pass

    def record_status(self) -> Dict[str, Any]:
        with self._record_lock:
            return {'enabled': self._record_fh is not None,
                    'path': self._record_path}

    def _record_write(self, direction: str, ts: float, data: bytes) -> None:
        with self._record_lock:
            fh = self._record_fh
        if fh is None:
            return
        # 转义换行/制表符，保持"一条记录一行"的格式
        text = _bytes_to_text(data) \
            .replace('\r', '\\r').replace('\n', '\\n').replace('\t', '\\t')
        line = '[%s.%03d] %s %s | %s' % (
            time.strftime('%H:%M:%S', time.localtime(ts)),
            int((ts % 1) * 1000), direction, data.hex(' '), text)
        try:
            fh.write(line + '\n')
            fh.flush()
        except Exception:
            pass

    # -- I/O ---------------------------------------------------------------

    def write(self, data: bytes) -> int:
        """
        Write raw bytes to the port.
        Returns number of bytes written, or -1 on error / not open.
        """
        if self._ser is None or not self._ser.is_open or data is None:
            return -1
        if isinstance(data, bytearray):
            data = bytes(data)
        if not isinstance(data, (bytes, bytearray)):
            return -1
        try:
            with self._io_lock:
                n = int(self._ser.write(data))
        except Exception:
            return -1
        if n > 0:
            self._journal_add('TX', data[:n])
        return n

    def read_available(self) -> bytes:
        """
        Read all bytes currently buffered (shared RX buffer fed by the
        background monitor). Returns b'' when closed or nothing available.
        """
        if self._ser is None or not self._ser.is_open:
            return b''
        self._ensure_rx_thread()
        self._drain_once()
        with self._rx_cond:
            out = bytes(self._rx_buf)
            self._rx_buf.clear()
            return out

    def read(self, size: int = 1, timeout: Optional[float] = None) -> bytes:
        """
        Blocking read of up to `size` bytes (pyserial semantics: waits until
        `size` bytes arrive or the timeout expires, returning what it has).
        Optionally override timeout (seconds) for this call.
        """
        if self._ser is None or not self._ser.is_open or size <= 0:
            return b''
        if timeout is None:
            try:
                timeout = float(self._ser.timeout) \
                    if self._ser.timeout is not None else 0.05
            except Exception:
                timeout = 0.05
        self._ensure_rx_thread()
        self._drain_once()
        deadline = time.time() + max(0.0, float(timeout))
        with self._rx_cond:
            while len(self._rx_buf) < size:
                remain = deadline - time.time()
                if remain <= 0:
                    break
                self._rx_cond.wait(remain)
            out = bytes(self._rx_buf[:size])
            del self._rx_buf[:size]
            return out
    def flush(self) -> None:
        """Flush output buffer (wait until all written bytes leave the OS buffer)."""
        if self._ser is None or not self._ser.is_open:
            return
        try:
            self._ser.flush()
        except Exception:
            pass

    def reset_buffers(self) -> None:
        """Clear input and output buffers."""
        if self._ser is None or not self._ser.is_open:
            return
        try:
            with self._io_lock:
                self._ser.reset_input_buffer()
                self._ser.reset_output_buffer()
        except Exception:
            pass
        with self._rx_cond:
            self._rx_buf.clear()

    # -- state -------------------------------------------------------------

    @property
    def is_open(self) -> bool:
        """True when a port is currently open."""
        return bool(self._ser is not None and self._ser.is_open)

    @property
    def port(self) -> str:
        return self._port

    @property
    def baudrate(self) -> int:
        return self._baudrate

    @property
    def in_waiting(self) -> int:
        """Bytes waiting in the input buffer (0 when closed)."""
        if self._ser is None or not self._ser.is_open:
            return 0
        self._drain_once()
        with self._rx_cond:
            return len(self._rx_buf)


# Module-level singleton (Studio pages share one UART)
_uart_service: Optional[UartService] = None


def get_uart_service() -> UartService:
    """Return the process-wide UartService singleton, creating it on first use."""
    global _uart_service
    if _uart_service is None:
        _uart_service = UartService()
    return _uart_service
