"""
xmodem_ydmodem — Xmodem-128 / Ymodem-1K sender for BabyOS Studio.

Authority:
  - origin/master:tool/xmodem_ydmodem.py
  - CRC16-CCITT (poly 0x1021) with the BabyOS C form:
        crc = (crc ^ byte) << 8
    then 8 shift/xor steps. Empty data → 0.

Xmodem-128:
  NAK mode: SOH + blk + ~blk + 128B + checksum(1B)   = 132B
  CRC  mode: SOH + blk + ~blk + 128B + CRC16(2B BE)  = 133B
  Receiver 'C' requests CRC-16; NAK requests checksum.
  Partial last block padded to 128B with 0x00.
  EOT after last block; receiver ACKs.

Ymodem-1K:
  Block 0:   SOH + 00 + FF + "name\\0size\\0" + pad(128) + CRC16 = 133B
  Data:      STX + blk + ~blk + 1024B + CRC16                   = 1029B
  Tail pad:  SOH + blk + ~blk + zeros(128) + CRC16              = 133B
  Always CRC-16 (receiver starts with 'C').
"""

import struct
import time
from enum import IntEnum
from typing import Callable, Optional

# ---- Protocol constants ----
SOH = 0x01   # 128-byte block
STX = 0x02   # 1024-byte block
EOT = 0x04   # End of transmission
ACK = 0x06   # Acknowledge
NAK = 0x15   # Negative acknowledge / checksum mode request
CAN = 0x18   # Cancel
CRCPKT = 0x43  # 'C' — CRC-16 mode request

CRC16_CCITT_POLY = 0x1021

# BabyOS Ymodem CRC16 known vectors (firmware _bYmodemCalCheck, NOT standard XMODEM)
#   crc = (crc ^ byte) << 8;  then 8 x (crc<<1 [^0x1021 if msb])
#   empty        -> 0x0000
#   b'123456789' -> 0x2672   (standard CRC-16/XMODEM would be 0x31C3 — different form)
#   b'BabyOS'    -> 0x5424
CRC16_YMODEM_VECTORS = {
    b'': 0x0000,
    b'123456789': 0x2672,
    b'BabyOS': 0x5424,
}


def crc16_ccitt(data: bytes) -> int:
    """
    BabyOS Ymodem/Xmodem-transfer CRC16 — matches firmware _bYmodemCalCheck
    (bos/modules/b_mod_ymodem.c) and origin/master:tool/xmodem_ydmodem.py.

        crc = (crc ^ byte) << 8          # parentheses REQUIRED
        for _ in range(8):
            crc = (crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1

    poly 0x1021, init 0. 16-bit truncation on every step (C uint16_t).

    NOTE: this is NOT standard CRC-16/XMODEM (`crc ^= byte << 8`, check
    value 0x31C3). Firmware algo_crc.c ALGO_CRC16_XMODEM is the standard
    form; Ymodem transfer uses the (crc^byte)<<8 form above.
    """
    crc = 0
    if not data:
        return 0
    for byte in data:
        crc = ((crc ^ byte) << 8) & 0xFFFF  # uint16_t assignment
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ CRC16_CCITT_POLY) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# Alias matching firmware symbol name
crc16_ymodem = crc16_ccitt


def _checksum(data: bytes) -> int:
    """1-byte additive checksum (sum of bytes, mod 256)."""
    return sum(data) & 0xFF


class XferState(IntEnum):
    IDLE = 0
    WAIT_START = 1     # waiting for NAK or 'C'
    SEND_BLOCK0 = 2    # Ymodem block 0 (header)
    SEND_DATA = 3      # sending data blocks
    SEND_EOT = 4       # sending EOT
    DONE = 5
    ABORTED = 6


class XmodemSender:
    """
    Xmodem-128 sender.
      - NAK mode: 128B data + 1-byte checksum
      - CRC  mode: 128B data + 2-byte CRC16 (big-endian)
      - Partial tail padded with a SOH 128-zero block when size % 128 != 0
    """

    BLOCK_SIZE = 128

    def __init__(self, uart_send: Callable[[bytes], None], log_fn: Optional[Callable] = None,
                 timeout_sec: float = 10.0, max_retries: int = 16,
                 start_timeout_sec: Optional[float] = None) -> None:
        self._uart_send = uart_send
        self._log = log_fn or (lambda *_: None)
        self._timeout = timeout_sec
        self._max_retries = max_retries
        # WAIT_START (receiver never sends C/NAK) must not hang forever.
        # Cap start wait separately so a silent peer aborts in bounded time.
        self._start_timeout = (start_timeout_sec if start_timeout_sec is not None
                               else min(3.0, timeout_sec))
        self._start_retries = 0
        self._max_start_retries = 3

        self._state = XferState.IDLE
        self._block_num = 1
        self._last_sent_block = 0
        self._padding_sent = False
        self._data = b''
        self._file_size = 0
        self._total_data_blocks = 0

        self._crc_mode = False
        self._retry_count = 0
        self._last_tx_time = 0.0

    def start(self, data: bytes, file_size: Optional[int] = None) -> None:
        if self._state not in (XferState.IDLE, XferState.DONE, XferState.ABORTED):
            self._log("[Xmodem] busy, cannot start")
            return
        self._data = data or b''
        self._file_size = file_size if file_size is not None else len(self._data)
        self._total_data_blocks = (self._file_size + self.BLOCK_SIZE - 1) // self.BLOCK_SIZE
        self._block_num = 1
        self._padding_sent = False
        self._crc_mode = False
        self._retry_count = 0
        self._start_retries = 0
        self._state = XferState.WAIT_START
        self._last_tx_time = time.monotonic()
        self._log("[Xmodem] started, size=%d, data_blocks=%d" % (self._file_size, self._total_data_blocks))

    def cancel(self) -> None:
        self._state = XferState.ABORTED
        self._log("[Xmodem] cancelled")

    @property
    def is_active(self) -> bool:
        return self._state not in (XferState.IDLE, XferState.DONE, XferState.ABORTED)

    @property
    def state(self) -> XferState:
        return self._state

    @property
    def crc_mode(self) -> bool:
        return self._crc_mode

    def on_uart_byte(self, byte: int) -> None:
        """Feed one received byte into the state machine."""
        if self._state == XferState.WAIT_START:
            if byte == NAK:
                self._crc_mode = False
                self._log("[Xmodem] checksum mode")
                self._send_data_block()
            elif byte == CRCPKT:
                self._crc_mode = True
                self._log("[Xmodem] CRC-16 mode")
                self._send_data_block()
            elif byte == CAN:
                self._log("[Xmodem] receiver sent CAN, aborting")
                self._state = XferState.ABORTED

        elif self._state == XferState.SEND_DATA:
            if byte == ACK:
                self._on_ack()
            elif byte == NAK:
                self._log("[Xmodem] NAK, resending block %d" % self._last_sent_block)
                self._resend_current()
            elif byte == CRCPKT:
                if not self._crc_mode:
                    self._crc_mode = True
                    self._log("[Xmodem] 'C' received, switching to CRC-16 mode")
                self._resend_current()
            elif byte == CAN:
                self._log("[Xmodem] receiver sent CAN, aborting")
                self._state = XferState.ABORTED

        elif self._state == XferState.SEND_EOT:
            if byte == ACK:
                self._log("[Xmodem] transfer complete!")
                self._state = XferState.DONE
            elif byte == NAK:
                # Repeated EOT NAK must count toward retries — otherwise a
                # receiver that always NAKs EOT leaves the sender in SEND_EOT
                # forever (busy-wait / hung job).
                if self._retry_count >= self._max_retries:
                    self._log("[Xmodem] EOT rejected too many times, aborting")
                    self._send_cancel()
                    self._state = XferState.ABORTED
                    return
                self._retry_count += 1
                self._log("[Xmodem] EOT NAK, retry %d/%d"
                          % (self._retry_count, self._max_retries))
                self._send_eot()
                self._last_tx_time = time.monotonic()
            elif byte == CAN:
                self._state = XferState.ABORTED

    def on_timer_tick(self) -> bool:
        """Called periodically. Handles timeout-based retries."""
        if not self.is_active:
            return False
        if self._state == XferState.WAIT_START:
            elapsed = time.monotonic() - self._last_tx_time
            if elapsed >= self._start_timeout:
                if self._start_retries >= self._max_start_retries:
                    self._log("[Xmodem] start timeout (receiver silent), aborting")
                    self._send_cancel()
                    self._state = XferState.ABORTED
                    return False
                self._start_retries += 1
                self._log("[Xmodem] start wait retry %d/%d"
                          % (self._start_retries, self._max_start_retries))
                self._last_tx_time = time.monotonic()
            return self.is_active
        if self._state == XferState.SEND_DATA:
            elapsed = time.monotonic() - self._last_tx_time
            if elapsed >= self._timeout:
                if self._retry_count >= self._max_retries:
                    self._log("[Xmodem] timeout, aborting")
                    self._send_cancel()
                    self._state = XferState.ABORTED
                    return False
                self._retry_count += 1
                self._log("[Xmodem] timeout retry %d/%d" % (self._retry_count, self._max_retries))
                self._resend_current()
        elif self._state == XferState.SEND_EOT:
            elapsed = time.monotonic() - self._last_tx_time
            if elapsed >= self._timeout:
                if self._retry_count >= self._max_retries:
                    self._log("[Xmodem] EOT timeout, aborting")
                    self._send_cancel()
                    self._state = XferState.ABORTED
                    return False
                self._retry_count += 1
                self._send_eot()
                self._last_tx_time = time.monotonic()
        return self.is_active

    def _data_payload(self, blk_num: int) -> bytes:
        start = (blk_num - 1) * self.BLOCK_SIZE
        chunk = self._data[start:start + self.BLOCK_SIZE]
        return chunk.ljust(self.BLOCK_SIZE, b'\x00')

    def _send_data_block(self) -> None:
        self._state = XferState.SEND_DATA
        self._send_frame(SOH, self._block_num, self._data_payload(self._block_num))
        self._last_sent_block = self._block_num
        self._block_num += 1
        self._last_tx_time = time.monotonic()
        self._log_progress()

    def _resend_current(self) -> None:
        self._send_frame(SOH, self._last_sent_block, self._data_payload(self._last_sent_block))
        self._last_tx_time = time.monotonic()
        self._log_progress()

    def _send_padding_block(self) -> None:
        self._send_frame(SOH, self._block_num, b'\x00' * self.BLOCK_SIZE)
        self._last_sent_block = self._block_num
        self._block_num += 1
        self._last_tx_time = time.monotonic()
        self._log_progress()

    def _on_ack(self) -> None:
        if self._last_sent_block == self._total_data_blocks and not self._padding_sent:
            if self._file_size % self.BLOCK_SIZE != 0:
                self._send_padding_block()
                self._padding_sent = True
                self._retry_count = 0
            else:
                self._padding_sent = True
                self._send_eot()
                self._state = XferState.SEND_EOT
                self._log("[Xmodem] all data sent, sending EOT")
        elif self._block_num > self._total_data_blocks:
            self._send_eot()
            self._state = XferState.SEND_EOT
            self._log("[Xmodem] all data sent, sending EOT")
        else:
            self._send_data_block()
            self._retry_count = 0

    def _send_frame(self, blk_code: int, blk_num: int, payload: bytes) -> None:
        header = struct.pack('>BB', blk_num & 0xFF, (255 - blk_num) & 0xFF)
        if self._crc_mode:
            trailer = struct.pack('>H', crc16_ccitt(payload))
        else:
            trailer = bytes([_checksum(payload)])
        self._uart_send(bytes([blk_code]) + header + payload + trailer)

    def _log_progress(self) -> None:
        total = self._total_data_blocks
        if self._file_size % self.BLOCK_SIZE != 0:
            total += 1
        sent = self._last_sent_block
        pct = min(100, sent * 100 // max(total, 1))
        self._log("[Xmodem] sent block %d/%d (%d%%)" % (sent, total, pct))

    def _send_eot(self) -> None:
        self._uart_send(bytes([EOT]))

    def _send_cancel(self) -> None:
        self._uart_send(bytes([CAN, CAN]))


class YmodemSender:
    """
    Ymodem-1K sender.
      - Block 0 header always CRC-16
      - Data blocks STX + 1024B + CRC16
      - SOH 128-zero padding when file size is not a multiple of 1024
    """

    BLOCK_SIZE = 1024

    def __init__(self, uart_send: Callable[[bytes], None], log_fn: Optional[Callable] = None,
                 timeout_sec: float = 10.0, max_retries: int = 16,
                 start_timeout_sec: Optional[float] = None) -> None:
        self._uart_send = uart_send
        self._log = log_fn or (lambda *_: None)
        self._timeout = timeout_sec
        self._max_retries = max_retries
        self._start_timeout = (start_timeout_sec if start_timeout_sec is not None
                               else min(3.0, timeout_sec))
        self._start_retries = 0
        self._max_start_retries = 3

        self._state = XferState.IDLE
        self._block_num = 0
        self._last_sent_block = 0
        self._padding_sent = False
        self._data = b''
        self._file_size = 0
        self._filename = ""
        self._total_data_blocks = 0

        self._crc_mode = True
        self._retry_count = 0
        self._last_tx_time = 0.0

    def start(self, data: bytes, filename: str = "", file_size: Optional[int] = None) -> None:
        if self._state not in (XferState.IDLE, XferState.DONE, XferState.ABORTED):
            self._log("[Ymodem] busy, cannot start")
            return
        self._filename = filename
        self._file_size = file_size if file_size is not None else len(data or b'')
        self._data = data or b''
        self._total_data_blocks = (self._file_size + self.BLOCK_SIZE - 1) // self.BLOCK_SIZE
        self._block_num = 0
        self._padding_sent = False
        self._crc_mode = True
        self._retry_count = 0
        self._start_retries = 0
        self._state = XferState.WAIT_START
        self._last_tx_time = time.monotonic()
        self._log("[Ymodem] started, file='%s', size=%d, data_blocks=%d" % (
            filename, self._file_size, self._total_data_blocks))

    def cancel(self) -> None:
        self._state = XferState.ABORTED
        self._log("[Ymodem] cancelled")

    @property
    def is_active(self) -> bool:
        return self._state not in (XferState.IDLE, XferState.DONE, XferState.ABORTED)

    @property
    def state(self) -> XferState:
        return self._state

    def on_uart_byte(self, byte: int) -> None:
        if self._state == XferState.WAIT_START:
            if byte == NAK:
                self._log("[Ymodem] NAK received, using CRC-16 mode")
                self._send_block0()
            elif byte == CRCPKT:
                self._log("[Ymodem] CRC-16 mode")
                self._send_block0()
            elif byte == CAN:
                self._log("[Ymodem] receiver sent CAN, aborting")
                self._state = XferState.ABORTED

        elif self._state == XferState.SEND_BLOCK0:
            if byte == ACK:
                self._block_num = 1
                self._retry_count = 0
                self._state = XferState.SEND_DATA
                self._log("[Ymodem] block 0 ACKed, sending data blocks (1K)")
                self._send_data_block()
            elif byte == NAK:
                self._log("[Ymodem] NAK on block 0, resending")
                self._send_block0()
                self._last_tx_time = time.monotonic()
            elif byte == CAN:
                self._log("[Ymodem] receiver sent CAN, aborting")
                self._state = XferState.ABORTED

        elif self._state == XferState.SEND_DATA:
            if byte == ACK:
                self._on_ack()
            elif byte == NAK:
                self._log("[Ymodem] NAK, resending block %d" % self._last_sent_block)
                self._resend_current()
            elif byte == CAN:
                self._log("[Ymodem] receiver sent CAN, aborting")
                self._state = XferState.ABORTED

        elif self._state == XferState.SEND_EOT:
            if byte == ACK:
                self._log("[Ymodem] transfer complete!")
                self._state = XferState.DONE
            elif byte == NAK:
                # Same as Xmodem: EOT NAK must consume retries or a receiver
                # that always rejects EOT hangs the sender in SEND_EOT.
                if self._retry_count >= self._max_retries:
                    self._log("[Ymodem] EOT rejected too many times, aborting")
                    self._send_cancel()
                    self._state = XferState.ABORTED
                    return
                self._retry_count += 1
                self._log("[Ymodem] EOT NAK, retry %d/%d"
                          % (self._retry_count, self._max_retries))
                self._send_eot()
                self._last_tx_time = time.monotonic()
            elif byte == CAN:
                self._state = XferState.ABORTED

    def on_timer_tick(self) -> bool:
        if not self.is_active:
            return False
        if self._state == XferState.WAIT_START:
            elapsed = time.monotonic() - self._last_tx_time
            if elapsed >= self._start_timeout:
                if self._start_retries >= self._max_start_retries:
                    self._log("[Ymodem] start timeout (receiver silent), aborting")
                    self._send_cancel()
                    self._state = XferState.ABORTED
                    return False
                self._start_retries += 1
                self._log("[Ymodem] start wait retry %d/%d"
                          % (self._start_retries, self._max_start_retries))
                self._last_tx_time = time.monotonic()
            return self.is_active
        if self._state == XferState.SEND_BLOCK0:
            elapsed = time.monotonic() - self._last_tx_time
            if elapsed >= self._timeout:
                if self._retry_count >= self._max_retries:
                    self._log("[Ymodem] block0 timeout, aborting")
                    self._send_cancel()
                    self._state = XferState.ABORTED
                    return False
                self._retry_count += 1
                self._log("[Ymodem] timeout retry %d/%d" % (self._retry_count, self._max_retries))
                self._send_block0()
                self._last_tx_time = time.monotonic()
        if self._state == XferState.SEND_DATA:
            elapsed = time.monotonic() - self._last_tx_time
            if elapsed >= self._timeout:
                if self._retry_count >= self._max_retries:
                    self._log("[Ymodem] timeout, aborting")
                    self._send_cancel()
                    self._state = XferState.ABORTED
                    return False
                self._retry_count += 1
                self._log("[Ymodem] timeout retry %d/%d" % (self._retry_count, self._max_retries))
                self._resend_current()
        elif self._state == XferState.SEND_EOT:
            elapsed = time.monotonic() - self._last_tx_time
            if elapsed >= self._timeout:
                if self._retry_count >= self._max_retries:
                    self._log("[Ymodem] EOT timeout, aborting")
                    self._send_cancel()
                    self._state = XferState.ABORTED
                    return False
                self._retry_count += 1
                self._send_eot()
                self._last_tx_time = time.monotonic()
        return self.is_active

    def _build_block0_payload(self) -> bytes:
        filename = self._filename or "noname"
        info = "%s\x00%d\x00" % (filename, self._file_size)
        return info.encode('latin-1').ljust(128, b'\x00')

    def _data_payload(self, blk_num: int) -> bytes:
        start = (blk_num - 1) * self.BLOCK_SIZE
        chunk = self._data[start:start + self.BLOCK_SIZE]
        return chunk.ljust(self.BLOCK_SIZE, b'\x00')

    def _send_block0(self) -> None:
        self._state = XferState.SEND_BLOCK0
        self._send_frame(SOH, 0, self._build_block0_payload())
        self._last_sent_block = 0
        self._last_tx_time = time.monotonic()
        self._log("[Ymodem] sent block 0 (header)")

    def _send_data_block(self) -> None:
        self._send_frame(STX, self._block_num, self._data_payload(self._block_num))
        self._last_sent_block = self._block_num
        self._block_num += 1
        self._last_tx_time = time.monotonic()
        self._log_progress()

    def _resend_current(self) -> None:
        if self._last_sent_block == 0:
            self._send_block0()
        elif self._last_sent_block <= self._total_data_blocks:
            self._send_frame(STX, self._last_sent_block, self._data_payload(self._last_sent_block))
            self._last_tx_time = time.monotonic()
            self._log_progress()
        else:
            self._send_padding_block()
        self._last_tx_time = time.monotonic()

    def _send_padding_block(self) -> None:
        self._send_frame(SOH, self._block_num, b'\x00' * 128)
        self._last_sent_block = self._block_num
        self._block_num += 1
        self._last_tx_time = time.monotonic()
        self._log_progress()

    def _on_ack(self) -> None:
        if self._last_sent_block == self._total_data_blocks and not self._padding_sent:
            if self._file_size % self.BLOCK_SIZE != 0:
                self._send_padding_block()
                self._padding_sent = True
                self._retry_count = 0
            else:
                self._padding_sent = True
                self._send_eot()
                self._state = XferState.SEND_EOT
                self._log("[Ymodem] all data sent, sending EOT")
        elif self._block_num > self._total_data_blocks:
            self._send_eot()
            self._state = XferState.SEND_EOT
            self._log("[Ymodem] all data sent, sending EOT")
        else:
            self._send_data_block()
            self._retry_count = 0

    def _send_frame(self, blk_code: int, blk_num: int, payload: bytes) -> None:
        header = struct.pack('>BB', blk_num & 0xFF, (255 - blk_num) & 0xFF)
        crc = struct.pack('>H', crc16_ccitt(payload))
        self._uart_send(bytes([blk_code]) + header + payload + crc)

    def _log_progress(self) -> None:
        total = self._total_data_blocks
        if self._file_size % self.BLOCK_SIZE != 0:
            total += 1
        sent = self._last_sent_block
        pct = min(100, sent * 100 // max(total, 1))
        self._log("[Ymodem] sent block %d/%d (%d%%)" % (sent, total, pct))

    def _send_eot(self) -> None:
        self._uart_send(bytes([EOT]))

    def _send_cancel(self) -> None:
        self._uart_send(bytes([CAN, CAN]))


# Alias used by some call sites
YmodemCrc16 = crc16_ccitt
