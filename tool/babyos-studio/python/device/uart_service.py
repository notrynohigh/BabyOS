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
"""

from typing import List, Optional


class UartService:
    """Singleton-friendly serial port service wrapping pyserial.Serial."""

    def __init__(self) -> None:
        self._ser = None
        self._port = ''
        self._baudrate = 0

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
        return [p.device for p in list_ports.comports()]

    # -- lifecycle ---------------------------------------------------------

    def open(self, port: str, baudrate: int = 115200,
             timeout: float = 0.05, write_timeout: float = 1.0) -> bool:
        """
        Open a serial port. Returns True on success.
        Re-opens after a failed previous open attempt.
        """
        if not port:
            return False
        self.close()
        try:
            import serial
        except ImportError:
            return False
        try:
            self._ser = serial.Serial(
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
        except Exception:
            self._ser = None
            return False
        self._port = port
        self._baudrate = baudrate
        # flush any stale bytes from a previous session
        try:
            self._ser.reset_input_buffer()
            self._ser.reset_output_buffer()
        except Exception:
            pass
        return True

    def close(self) -> None:
        """Close the port if open. Safe to call when already closed."""
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass
        self._ser = None
        self._port = ''
        self._baudrate = 0

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
            return int(self._ser.write(data))
        except Exception:
            return -1

    def read_available(self) -> bytes:
        """
        Read all bytes currently in the input buffer.
        Returns b'' when closed or nothing available.
        """
        if self._ser is None or not self._ser.is_open:
            return b''
        try:
            n = int(self._ser.in_waiting)
        except Exception:
            return b''
        if n <= 0:
            return b''
        try:
            return bytes(self._ser.read(n))
        except Exception:
            return b''

    def read(self, size: int = 1, timeout: Optional[float] = None) -> bytes:
        """
        Blocking read of up to `size` bytes.
        Optionally override timeout (seconds) for this call.
        """
        if self._ser is None or not self._ser.is_open or size <= 0:
            return b''
        old_timeout = None
        try:
            if timeout is not None:
                old_timeout = self._ser.timeout
                self._ser.timeout = timeout
            return bytes(self._ser.read(size))
        except Exception:
            return b''
        finally:
            if old_timeout is not None:
                try:
                    self._ser.timeout = old_timeout
                except Exception:
                    pass

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
            self._ser.reset_input_buffer()
            self._ser.reset_output_buffer()
        except Exception:
            pass

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
        try:
            return int(self._ser.in_waiting)
        except Exception:
            return 0


# Module-level singleton (Studio pages share one UART)
_uart_service: Optional[UartService] = None


def get_uart_service() -> UartService:
    """Return the process-wide UartService singleton, creating it on first use."""
    global _uart_service
    if _uart_service is None:
        _uart_service = UartService()
    return _uart_service
