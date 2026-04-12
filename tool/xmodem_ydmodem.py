"""
Xmodem / Ymodem file transfer protocol implementations.

Xmodem-128 (标准协议):
  Checksum 模式: SOH + blk# + ~blk# + 128B data + checksum(1B)  = 132 字节
  CRC 模式:      SOH + blk# + ~blk# + 128B data + CRC16(2B)    = 133 字节
  Receiver: NAK 触发 checksum 模式，'C' 触发 CRC-16 模式
  EOT:  所有数据块发完后发送 EOT(0x04)，接收方 ACK 确认

Ymodem-1K (标准协议):
  Block 0:  SOH + 00 + FF + filename\\0filesize\\0 + padding(128B) + CRC16(2B) = 133 字节
  数据块:    STX + blk# + ~blk# + 1024B data + CRC16(2B) = 1029 字节
  Padding:  SOH + blk# + ~blk# + zeros(128B) + CRC16(2B) = 133 字节（仅当数据 < 1024B 时）
  Receiver: 'C' 触发 CRC-16 模式（全程 CRC-16）
"""

import struct
import time
from enum import IntEnum


# ---- Protocol constants ----
SOH = 0x01   # 128-byte block
STX = 0x02   # 1024-byte block
EOT = 0x04   # End of transmission
ACK = 0x06   # Acknowledge
NAK = 0x15   # Negative acknowledge / checksum mode request
CAN = 0x18   # Cancel
CRCPKT = 0x43  # 'C' — CRC-16 mode request

CRC16_CCITT_POLY = 0x1021


def _crc16_ccitt(data: bytes) -> int:
    """Compute CRC-16 CCITT (Xmodem) over data. Matches BabyOS _bYmodemCalCheck."""
    crc = 0
    for byte in data:
        crc = (crc ^ byte) << 8   # C: crc = (crc ^ byte) << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ CRC16_CCITT_POLY) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _checksum(data: bytes) -> int:
    """Compute 1-byte checksum (sum of all bytes, modulo 256)."""
    return sum(data) & 0xFF


# ---- State machine ----
class XferState(IntEnum):
    IDLE = 0
    WAIT_START = 1     # waiting for NAK or 'C'
    SEND_BLOCK0 = 2   # Ymodem block 0 (header)
    SEND_DATA = 3     # sending data blocks
    SEND_EOT = 4      # sending EOT
    DONE = 5
    ABORTED = 6


# ---- Xmodem-128 sender ----

class XmodemSender:
    """
    Xmodem-128 sender，严格遵循标准协议：
      - NAK 模式: 128B 数据块 + 1 字节校验和
      - CRC  模式: 128B 数据块 + 2 字节 CRC16
      - 最后一个数据块后发送 SOH padding 块（128 零字节）
    """

    BLOCK_SIZE = 128

    def __init__(self, uart_send, log_fn=None, timeout_sec: float = 10.0,
                 max_retries: int = 16):
        self._uart_send = uart_send
        self._log = log_fn or (lambda *_: None)
        self._timeout = timeout_sec
        self._max_retries = max_retries

        self._state = XferState.IDLE
        self._block_num = 1          # NEXT block to send
        self._last_sent_block = 0    # LAST successfully-sent block number (for NAK resend)
        self._padding_sent = False   # True after SOH padding block is sent
        self._data = b''
        self._file_size = 0
        self._total_data_blocks = 0  # number of actual data blocks

        self._crc_mode = False
        self._retry_count = 0
        self._last_tx_time = 0.0

    # ---- Public API ----

    def start(self, data: bytes, file_size: int = None):
        if self._state not in (XferState.IDLE, XferState.DONE, XferState.ABORTED):
            self._log("[Xmodem] busy, cannot start")
            return
        self._data = data
        self._file_size = file_size if file_size is not None else len(data)
        self._total_data_blocks = (self._file_size + self.BLOCK_SIZE - 1) // self.BLOCK_SIZE
        self._block_num = 1
        self._padding_sent = False
        self._crc_mode = False
        self._retry_count = 0
        self._state = XferState.WAIT_START
        self._log(f"[Xmodem] started, size={self._file_size}, data_blocks={self._total_data_blocks}")

    def cancel(self):
        self._state = XferState.ABORTED
        self._log("[Xmodem] cancelled")

    @property
    def is_active(self) -> bool:
        return self._state not in (XferState.IDLE, XferState.DONE, XferState.ABORTED)

    # ---- Call this periodically from UART polling loop ----

    def on_uart_byte(self, byte: int):
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
                self._log(f"[Xmodem] NAK, resending block {self._last_sent_block}")
                self._resend_current()
            elif byte == CRCPKT:
                # 'C' in SEND_DATA: switch to CRC-16 and resend the current block
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
                self._send_eot()
                self._last_tx_time = time.monotonic()
            elif byte == CAN:
                self._state = XferState.ABORTED

    def on_timer_tick(self) -> bool:
        """Called periodically. Handles timeout-based retries."""
        if not self.is_active:
            return False
        if self._state == XferState.WAIT_START:
            return True
        if self._state == XferState.SEND_DATA:
            elapsed = time.monotonic() - self._last_tx_time
            if elapsed >= self._timeout:
                if self._retry_count >= self._max_retries:
                    self._log("[Xmodem] timeout, aborting")
                    self._send_cancel()
                    self._state = XferState.ABORTED
                    return False
                self._retry_count += 1
                self._log(f"[Xmodem] timeout retry {self._retry_count}/{self._max_retries}")
                self._resend_current()
        return self.is_active

    # ---- Block helpers ----

    def _data_payload(self, blk_num: int) -> bytes:
        """Return the payload for block number blk_num (1-based)."""
        start = (blk_num - 1) * self.BLOCK_SIZE
        chunk = self._data[start:start + self.BLOCK_SIZE]
        return chunk.ljust(self.BLOCK_SIZE, b'\x00')

    def _send_data_block(self):
        """Send one data block (SOH, 128B) and advance block number."""
        self._state = XferState.SEND_DATA
        self._send_frame(SOH, self._block_num, self._data_payload(self._block_num))
        self._last_sent_block = self._block_num
        self._block_num += 1
        self._last_tx_time = time.monotonic()
        self._log_progress()

    def _resend_current(self):
        """Resend the last-sent block (same block number, same payload)."""
        self._send_frame(SOH, self._last_sent_block, self._data_payload(self._last_sent_block))
        self._last_tx_time = time.monotonic()
        self._log_progress()

    def _send_padding_block(self):
        """Send SOH padding block (128 zero bytes) for Xmodem."""
        self._send_frame(SOH, self._block_num, b'\x00' * self.BLOCK_SIZE)
        self._last_sent_block = self._block_num
        self._block_num += 1
        self._last_tx_time = time.monotonic()
        self._log_progress()

    def _on_ack(self):
        """Handle ACK in SEND_DATA state."""
        # _block_num is the NEXT block to send; _last_sent_block is the last sent
        if self._last_sent_block == self._total_data_blocks and not self._padding_sent:
            # Last data block ACKed: send SOH padding only if file is not an exact multiple
            if self._file_size % self.BLOCK_SIZE != 0:
                self._send_padding_block()
                self._padding_sent = True
                self._retry_count = 0
            else:
                # Exact multiple: no padding needed, go straight to EOT
                self._padding_sent = True
                self._send_eot()
                self._state = XferState.SEND_EOT
                self._log("[Xmodem] all data sent, sending EOT")
        elif self._block_num > self._total_data_blocks:
            # All data blocks sent (+ padding if needed) -> EOT
            self._send_eot()
            self._state = XferState.SEND_EOT
            self._log("[Xmodem] all data sent, sending EOT")
        else:
            # More data blocks remaining
            self._send_data_block()
            self._retry_count = 0

    def _send_frame(self, blk_code: int, blk_num: int, payload: bytes):
        header = struct.pack('>BB', blk_num, 255 - blk_num)
        if self._crc_mode:
            trailer = struct.pack('>H', _crc16_ccitt(payload))
        else:
            trailer = bytes([_checksum(payload)])
        self._uart_send(bytes([blk_code]) + header + payload + trailer)

    def _log_progress(self):
        total = self._total_data_blocks
        if self._file_size % self.BLOCK_SIZE != 0:
            total += 1  # padding block
        sent = self._last_sent_block
        pct = min(100, sent * 100 // max(total, 1))
        self._log(f"[Xmodem] sent block {sent}/{total} ({pct}%)")

    def _send_eot(self):
        self._uart_send(bytes([EOT]))

    def _send_cancel(self):
        self._uart_send(bytes([CAN, CAN]))


# ---- Ymodem-1K sender ----

class YmodemSender:
    """
    Ymodem-1K sender，严格遵循标准协议：
      - Block 0: SOH + 00 + FF + filename\\0filesize\\0 + padding(128B) + CRC16(2B) = 133 字节
      - 数据块:   STX + blk# + ~blk# + 1024B data + CRC16(2B) = 1029 字节
      - Padding:  SOH + blk# + ~blk# + zeros(128B) + CRC16(2B) = 133 字节（仅当数据 < 1024B 时）
      - Receiver 发送 'C' 触发 CRC-16 模式（全程 CRC-16）
    """

    BLOCK_SIZE = 1024

    def __init__(self, uart_send, log_fn=None, timeout_sec: float = 10.0,
                 max_retries: int = 16):
        self._uart_send = uart_send
        self._log = log_fn or (lambda *_: None)
        self._timeout = timeout_sec
        self._max_retries = max_retries

        self._state = XferState.IDLE
        self._block_num = 0   # Block 0 = header; data blocks start at 1
        self._last_sent_block = 0  # LAST successfully-sent block number (for NAK resend)
        self._padding_sent = False  # True after SOH padding block is sent
        self._data = b''
        self._file_size = 0
        self._filename = ""
        self._total_data_blocks = 0  # number of actual data blocks

        self._crc_mode = True
        self._retry_count = 0
        self._last_tx_time = 0.0

    # ---- Public API ----

    def start(self, data: bytes, filename: str = "", file_size: int = None):
        if self._state not in (XferState.IDLE, XferState.DONE, XferState.ABORTED):
            self._log("[Ymodem] busy, cannot start")
            return
        self._filename = filename
        self._file_size = file_size if file_size is not None else len(data)
        self._data = data
        # Number of 1K data blocks needed (last block may be partial, padded to 1K)
        self._total_data_blocks = (self._file_size + self.BLOCK_SIZE - 1) // self.BLOCK_SIZE
        self._block_num = 0
        self._padding_sent = False
        self._crc_mode = True
        self._retry_count = 0
        self._state = XferState.WAIT_START
        self._log(f"[Ymodem] started, file='{filename}', size={self._file_size}, data_blocks={self._total_data_blocks}")

    def cancel(self):
        self._state = XferState.ABORTED
        self._log("[Ymodem] cancelled")

    @property
    def is_active(self) -> bool:
        return self._state not in (XferState.IDLE, XferState.DONE, XferState.ABORTED)

    # ---- Call this periodically from UART polling loop ----

    def on_uart_byte(self, byte: int):
        """Handle receiver responses."""
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
                self._log(f"[Ymodem] NAK, resending block {self._last_sent_block}")
                self._resend_current()
            elif byte == CAN:
                self._log("[Ymodem] receiver sent CAN, aborting")
                self._state = XferState.ABORTED

        elif self._state == XferState.SEND_EOT:
            if byte == ACK:
                self._log("[Ymodem] transfer complete!")
                self._state = XferState.DONE
            elif byte == NAK:
                self._send_eot()
                self._last_tx_time = time.monotonic()
            elif byte == CAN:
                self._state = XferState.ABORTED

    def on_timer_tick(self) -> bool:
        """Called periodically. Handles timeout-based retries."""
        if not self.is_active:
            return False
        if self._state == XferState.WAIT_START:
            return True
        if self._state == XferState.SEND_BLOCK0:
            elapsed = time.monotonic() - self._last_tx_time
            if elapsed >= self._timeout:
                if self._retry_count >= self._max_retries:
                    self._log("[Ymodem] timeout, aborting")
                    self._send_cancel()
                    self._state = XferState.ABORTED
                    return False
                self._retry_count += 1
                self._log(f"[Ymodem] timeout retry {self._retry_count}/{self._max_retries}")
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
                self._log(f"[Ymodem] timeout retry {self._retry_count}/{self._max_retries}")
                self._resend_current()
        return self.is_active

    # ---- Block helpers ----

    def _build_block0_payload(self) -> bytes:
        filename = self._filename or "noname"
        info = f"{filename}\x00{self._file_size}\x00"
        return info.encode('latin-1').ljust(128, b'\x00')

    def _data_payload(self, blk_num: int) -> bytes:
        """Return the payload for block number blk_num (1-based)."""
        start = (blk_num - 1) * self.BLOCK_SIZE
        chunk = self._data[start:start + self.BLOCK_SIZE]
        return chunk.ljust(self.BLOCK_SIZE, b'\x00')

    def _send_block0(self):
        """Send Ymodem block 0 (SOH, 128B payload, always CRC-16)."""
        self._state = XferState.SEND_BLOCK0
        self._send_frame(SOH, 0, self._build_block0_payload())
        self._last_sent_block = 0
        self._last_tx_time = time.monotonic()
        self._log("[Ymodem] sent block 0 (header)")

    def _send_data_block(self):
        """Send one data block (STX, 1K) and advance block number."""
        self._send_frame(STX, self._block_num, self._data_payload(self._block_num))
        self._last_sent_block = self._block_num
        self._block_num += 1
        self._last_tx_time = time.monotonic()
        self._log_progress()

    def _resend_current(self):
        """Resend the last-sent block (same block number, same payload)."""
        if self._last_sent_block == 0:
            self._send_block0()
        elif self._last_sent_block <= self._total_data_blocks:
            self._send_frame(STX, self._last_sent_block, self._data_payload(self._last_sent_block))
            self._last_tx_time = time.monotonic()
            self._log_progress()
        else:
            # _last_sent_block > total_data_blocks -> resend padding block
            self._send_padding_block()
        self._last_tx_time = time.monotonic()

    def _send_padding_block(self):
        """Send SOH padding block (128 zero bytes)."""
        self._send_frame(SOH, self._block_num, b'\x00' * 128)
        self._last_sent_block = self._block_num
        self._block_num += 1
        self._last_tx_time = time.monotonic()
        self._log_progress()

    def _on_ack(self):
        """Handle ACK in SEND_DATA state."""
        # _block_num is the NEXT block to send; _last_sent_block is the last sent
        if self._last_sent_block == self._total_data_blocks and not self._padding_sent:
            # Last data block ACKed: send SOH padding only if file is not an exact multiple
            if self._file_size % self.BLOCK_SIZE != 0:
                # Data is partial: send SOH padding block
                self._send_padding_block()
                self._padding_sent = True
                self._retry_count = 0
            else:
                # Exact multiple: no padding needed, go straight to EOT
                self._padding_sent = True
                self._send_eot()
                self._state = XferState.SEND_EOT
                self._log("[Ymodem] all data sent, sending EOT")
        elif self._block_num > self._total_data_blocks:
            # All data blocks sent (+ padding if needed) -> EOT
            self._send_eot()
            self._state = XferState.SEND_EOT
            self._log("[Ymodem] all data sent, sending EOT")
        else:
            # More data blocks remaining
            self._send_data_block()
            self._retry_count = 0

    def _send_frame(self, blk_code: int, blk_num: int, payload: bytes):
        header = struct.pack('>BB', blk_num, 255 - blk_num)
        crc = struct.pack('>H', _crc16_ccitt(payload))
        self._uart_send(bytes([blk_code]) + header + payload + crc)

    def _log_progress(self):
        total = self._total_data_blocks
        if self._file_size % self.BLOCK_SIZE != 0:
            total += 1  # padding block
        sent = self._last_sent_block
        pct = min(100, sent * 100 // max(total, 1))
        self._log(f"[Ymodem] sent block {sent}/{total} ({pct}%)")

    def _send_eot(self):
        self._uart_send(bytes([EOT]))

    def _send_cancel(self):
        self._uart_send(bytes([CAN, CAN]))
