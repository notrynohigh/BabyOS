"""
UART driver wrapper around PySerial.
Provides the same interface as the C++ uartClass.
"""

import serial
import serial.tools.list_ports
from typing import List, Optional


class UartDriver:
    def __init__(self):
        self._port: Optional[serial.Serial] = None
        self._open = False
        self._com_list: List[str] = []
        self._refresh_com()

    def _refresh_com(self):
        """Scan available COM ports (same logic as C++ constructor)."""
        self._com_list.clear()
        for info in serial.tools.list_ports.comports():
            # M-NEW-14 fix: 之前无论端口是否被其他程序占用都尝试打开 (115200,0).
            # 这会把正在跑 firmware 或 mid-flash 的 MCU 重置, 用户烧录中断.
            # 这里: 只在"list_ports 返回该端口"且"未在我们 self._port 里打开"时才 probe.
            # 已打开的端口 (self._port) 跳过 — 不需要再 probe 一次.
            if self._port is not None and info.device == self._port.port:
                # 已持有, 直接加入可用列表, 不再 open/close.
                self._com_list.append(info.device)
                continue
            try:
                with serial.Serial(info.device, baudrate=115200, timeout=0) as test:
                    self._com_list.append(info.device)
            except (serial.SerialException, OSError):
                pass

    def uartRefreshCOM(self):
        """Refresh the list of available COM ports."""
        self._refresh_com()

    @property
    def uartComAvailable(self) -> List[str]:
        """Return list of available COM port names."""
        return list(self._com_list)

    def uartOpenPort(self, port_name: str, bps: int = 115200) -> bool:
        """Open the specified COM port at given baud rate."""
        if self._open:
            return False
        try:
            self._port = serial.Serial(
                port=port_name,
                baudrate=bps,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0
            )
            self._open = True
            return True
        except (serial.SerialException, OSError):
            self._port = None
            self._open = False
            return False

    def uartClosePort(self):
        """Close the currently open COM port."""
        if self._open and self._port:
            self._port.close()
        self._port = None
        self._open = False

    def uartSendBuff(self, data: bytes):
        """Send raw bytes over UART."""
        if self._open and self._port and data:
            self._port.write(data)

    def uartReadBuff(self, buf: bytearray) -> int:
        """
        Read all available bytes from UART into buf.
        Returns number of bytes read.
        """
        if not self._open or not self._port:
            return 0
        try:
            available = self._port.in_waiting
            if available == 0:
                return 0
            raw = self._port.read(available)
            n = len(raw)
            if n > 0:
                buf[:n] = raw
            return n
        except serial.SerialException:
            return 0

    def uartGetOpenStatus(self) -> bool:
        """Return True if the port is open."""
        return self._open
