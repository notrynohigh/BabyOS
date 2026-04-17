"""
MainWindow — Tk-based GUI for BabyOS_Protocol upper-computer.

Pure standard library (tkinter / tkinter.ttk). No PyQt5, no extra deps.
All protocol commands, UART I/O, and UI logic are implemented here.

The class is named ``MainWindow`` and exposes the same constructor
signature as the previous PyQt5 version (``MainWindow()``) so ``main.py``
can import it unchanged.  All UI state and methods are kept as instance
attributes; widget accessors used by the application code are unchanged.
"""

import os
import re
import struct
import threading
import tkinter as tk
from datetime import datetime
from tkinter import ttk, filedialog, messagebox
from tkinter.scrolledtext import ScrolledText

from uart_driver import UartDriver
from b_protocol import (
    bProtocolRegist, bProtocolParse, bProtocolPack,
    bProtocolEncrypt, bProtocolDecrypt,
    DEVICE_ID_HOST, INVALID_ID,
)
from algo_crc import crc_calculate, ALGO_CRC32
from algo_md5 import md5_hex_16
from b_mod_utc import bStruct2UTC
from xmodem_ydmodem import XmodemSender, YmodemSender
from http_server import MockHttpServer


# ---------------------------------------------------------------------------
# Command definitions (must match b_protocol.h / mainwindow.cpp)
# ---------------------------------------------------------------------------
CMD_TEST = 0x01
CMD_SET_TIME = 0x02
CMD_FW_INFO = 0x03
CMD_UPGRADE_DATA = 0x04
CMD_UPGRADE_RESULT = 0x05
CMD_TRANS_FILE = 0x06
CMD_GET_UID = 0x07
CMD_SET_SN = 0x08
CMD_GET_DEVICE_INFO = 0x0A

# HTTP调试命令
CMD_HTTP_REQUEST = 0x50         # 触发设备发送HTTP请求
CMD_HTTP_RESPONSE = 0x51        # 设备回复HTTP响应
CMD_HTTP_INIT = 0x52            # 初始化HTTP客户端
CMD_HTTP_DEINIT = 0x53          # 反初始化HTTP客户端


# ---------------------------------------------------------------------------
# Protocol dispatch (called by bProtocolParse for each received frame)
# ---------------------------------------------------------------------------

def hex_to_string(data: bytes) -> str:
    """Hex dump: each byte as two uppercase hex chars separated by spaces."""
    return ' '.join(f'{b:02X}' for b in data)


def parse_device_info_response(param: bytes) -> tuple:
    """Parse CMD_GET_DEVICE_INFO response: version(16B) + model(16B)."""
    if len(param) < 32:
        return '', ''
    version = param[:16].rstrip(b'\x00').decode('utf-8', errors='replace')
    model = param[16:32].rstrip(b'\x00').decode('utf-8', errors='replace')
    return version, model


class _TkLogText(ScrolledText):
    """
    Drop-in stand-in for the QTextEdit the previous PyQt5 code used for
    log display. Supports the few methods we actually call:
      - append(text)
      - clear()
      - toPlainText()
    Reads are thread-safe via an internal lock; writes use Tk's
    ``after`` to marshal onto the UI thread.
    """

    def __init__(self, master, height=8, **kwargs):
        # Pop ``height`` so it doesn't collide with the explicit one
        # we forward to the ScrolledText base class.
        kwargs.setdefault('state', 'disabled')
        super().__init__(master, height=height, **kwargs)
        self._lock = threading.Lock()

    def _append_ui(self, text: str):
        self.configure(state='normal')
        self.insert('end', text + '\n')
        self.see('end')
        self.configure(state='disabled')

    def append(self, text: str):
        with self._lock:
            payload = text
        # schedule on the UI thread
        self.after(0, lambda p=payload: self._append_ui(p))

    def clear(self):
        self.after(0, lambda: (
            self.configure(state='normal'),
            self.delete('1.0', 'end'),
            self.configure(state='disabled'),
        ))

    def toPlainText(self) -> str:
        return self.get('1.0', 'end-1c')


class _ReadOnlyLogText(_TkLogText):
    """Same as _TkLogText but visually muted; used for HTTP request log."""
    pass


class MainWindow(tk.Tk):
    """
    Top-level application window.

    Inherits from ``tk.Tk`` rather than ``QMainWindow`` so ``main.py`` can
    instantiate it via ``MainWindow()`` then call ``mainloop()``.
    """

    POLL_INTERVAL_MS = 100          # UART polling tick (was QTimer 100ms)
    HTTP_LOG_REFRESH_MS = 500       # HTTP server log refresh (was QTimer 500ms)

    def __init__(self):
        super().__init__()
        self.title("BabyOS_Protocol - 上位机")
        self.geometry("900x720")
        self.minsize(800, 650)

        # UART
        self._uart = UartDriver()

        # Protocol instance
        self._protocol_n = bProtocolRegist(DEVICE_ID_HOST, self._dispatch)
        self._protocol_id = DEVICE_ID_HOST

        # Bin file data for OTA / file transfer
        self._bin_data = b''
        self._bin_len = 0
        self._bin_crc = 0

        # Device UID (received from device)
        self._mcu_uid = bytearray(64)
        self._uid_len = 0

        # Xmodem / Ymodem senders
        self._xmodem: XmodemSender = None
        self._ymodem: YmodemSender = None
        self._active_xfer = None
        self._xmodem_data = b''
        self._xmodem_filename = ''
        self._ymodem_data = b''
        self._ymodem_filename = ''

        # UART log-to-file
        self._log_file = None
        self._log_lock = threading.Lock()

        # Param list (populated from shell response)
        self._param_names = []

        # Param polling state
        self._param_polling_enabled = False
        self._param_polling_interval_ms = 1000
        self._param_polling_name = ''

        # Mock HTTP server (for HTTP client testing)
        self._mock_http = MockHttpServer(log_fn=self._append_log)

        # Track prior request-log count for the periodic refresh tick
        self._http_log_seen_count = 0

        # M-NEW-11 fix: 加密 toggle race 防护. _encrypt_trace 记录上次 _uart_data_in
        # 处理时使用的加密状态; _encrypt_parse_inflight 标记"是否正在解析一帧".
        # 状态翻转且在解析中 → 丢弃当前 buf, 避免半加密 + 半明文混淆送 dispatch.
        self._encrypt_trace = None
        self._encrypt_parse_inflight = False

        # UI
        self._setup_ui()

        # Wire polling ticks. Tk uses ``after`` instead of QTimer.
        self._poll_uart()

    # ------------------------------------------------------------------
    # Protocol dispatch (mirrors C++ Dispatch())
    # ------------------------------------------------------------------
    def _dispatch(self, dev_id: int, cmd: int, param: bytes, param_len: int) -> int:
        self._show_log(dev_id, cmd, param)
        if cmd == CMD_UPGRADE_DATA:
            if param_len < 2 or not param:
                return -1
            num = struct.unpack('<H', param[:2])[0]
            self._trans_data(num)
        elif cmd == CMD_UPGRADE_RESULT:
            if param_len < 1 or not param:
                return -1
            self._ack_result(param[0])
        elif cmd == CMD_GET_UID:
            if param_len < 1:
                return -1
            self._uid_len = param[0]
            self._mcu_uid[:self._uid_len] = param[1:1 + self._uid_len]
        elif cmd == CMD_HTTP_RESPONSE:
            if param_len < 2:
                return -1
            status_code = struct.unpack('<H', param[:2])[0]
            body = param[2:].decode('utf-8', errors='replace')
            preview = body[:500] + ('...' if len(body) > 500 else '')
            self._append_log(f"[HTTP] <- status={status_code} body={preview}")
        return 0

    # ------------------------------------------------------------------
    # UART polling
    # ------------------------------------------------------------------
    def _poll_uart(self):
        """Re-arming timer; called every POLL_INTERVAL_MS."""
        try:
            self._on_timer()
        finally:
            self.after(self.POLL_INTERVAL_MS, self._poll_uart)

    def _on_timer(self):
        buf = bytearray(10240)
        n = self._uart.uartReadBuff(buf)
        if n <= 0:
            return
        data = buf[:n]

        # Feed bytes to active Xmodem/Ymodem sender
        if self._active_xfer is not None:
            for b in buf[:n]:
                self._active_xfer.on_uart_byte(b)
            self._active_xfer.on_timer_tick()
            if not self._active_xfer.is_active:
                self._active_xfer = None
            return

        # For param shell commands, data comes back as plain text
        try:
            text = bytes(data).decode('utf-8', errors='replace')
            if any(c in text for c in [':', '\r', '\n']):
                self._append_log(text)
                self._handle_param_response(text)
                return
        except Exception:
            pass

        # M-NEW-11 fix: 加密 toggle 在 UART 数据解析中切换会产生 race —
        #   - 上半帧是按 "未加密" 解析的, 但后半帧会被按 "加密" 解密并扔给 parse.
        #   - bProtocolParse 是 stateless (每次从头解析 raw_buf), 没有 partial
        #     buffer, 但错误地把混合帧交给上层 dispatch 会触发误派发.
        # 解决: 记录"上次 _encrypt_checked 的值 + 上次 _uart_data_in 是否正在解析
        # 中". toggle 翻转时, 在下次 UART 读之前清掉 _active_xfer 和 UART 输入
        # 缓冲 (用 bProtocolReset (如存在) 或 bProtocolRegist 重注册).
        # 这里采用最简方案: 检测翻转 → 跳过当前 buf 一次, 提示用户.
        cur_enc = self._encrypt_checked()
        if (self._encrypt_trace is not None and cur_enc != self._encrypt_trace
                and self._encrypt_parse_inflight):
            # 上次按旧状态正在解析一帧, 现在状态变了 — 丢弃本 buf.
            self._append_log('[encrypt] toggle flipped mid-parse, dropping current buf. '
                             'Next buf will be decoded with the new state.')
            self._encrypt_trace = cur_enc
            self._encrypt_parse_inflight = False
            return
        self._encrypt_trace = cur_enc
        self._encrypt_parse_inflight = True

        if cur_enc:
            bProtocolDecrypt(data)
        ret = bProtocolParse(self._protocol_n, bytes(data))
        self._encrypt_parse_inflight = False
        if ret < 0:
            try:
                text = bytes(data).decode('utf-8', errors='replace')
                self._append_log(text)
            except Exception:
                self._append_log(hex_to_string(bytes(data)))

    # ------------------------------------------------------------------
    # Protocol helpers
    # ------------------------------------------------------------------
    def _pack_and_send(self, device_id: int, cmd: int, param: bytes = b''):
        buf = bytearray(1024)
        length = bProtocolPack(self._protocol_n, device_id, cmd, param, buf)
        if length <= 0:
            return
        data = bytes(buf[:length])
        if self._encrypt_checked():
            data = bytes(bProtocolEncrypt(bytearray(data)))
        self._uart.uartSendBuff(data)
        self._append_log(f"s-> cmd:{cmd:02X} data:{hex_to_string(data[:length])}")

    def _trans_data(self, num: int):
        index = num * 512
        if index >= self._bin_len:
            return
        chunk = self._bin_data[index:index + 512]
        if len(chunk) < 512:
            chunk = chunk + b'\x00' * (512 - len(chunk))
        param = struct.pack('<H', num) + chunk
        self._pack_and_send(INVALID_ID, CMD_UPGRADE_DATA, param)
        pct = min(100, index * 100 // self._bin_len)
        self._progress_bar['value'] = pct

    def _ack_result(self, result: int):
        self._progress_bar['value'] = 100
        self._append_log(f"Upgrade result: {result}")
        names = {0: "success", 1: "crc_error", 2: "name_mismatch", 3: "len_invalid", 4: "timeout"}
        self._append_log(names.get(result, f"unknown({result})"))

    def _ack_fw_info(self):
        if not self._bin_len or not self._fw_name.get():
            return -1
        name_bytes = self._fw_name.get().encode('utf-8')
        param = struct.pack('<II', self._bin_len, self._bin_crc) + name_bytes[:64].ljust(64, b'\x00')
        self._pack_and_send(INVALID_ID, CMD_FW_INFO, param)
        return 0

    def _show_log(self, dev_id: int, cmd: int, param: bytes):
        self._append_log(f"r-> id:{dev_id} cmd:{cmd:02X} param:{hex_to_string(param)}")

    def _append_log(self, text: str):
        self._rec_text.append(text)
        if self._log_file is not None:
            with self._log_lock:
                ts = datetime.now().strftime('%H:%M:%S.%f')[:-3]
                self._log_file.write(f'[{ts}] {text}\n')
                self._log_file.flush()

    # ------------------------------------------------------------------
    # UI actions
    # ------------------------------------------------------------------
    def _encrypt_checked(self) -> bool:
        return bool(self._encrypt_var.get())

    def _on_com_clicked(self):
        if self._uart.uartGetOpenStatus():
            self._uart.uartClosePort()
            self._com_combo.configure(state='readonly')
            self._com_btn.configure(text="打开串口")
        else:
            bps = 9600 if self._bps_var.get() else 115200
            port = self._com_combo.get()
            if not port:
                return
            if self._uart.uartOpenPort(port, bps):
                self._com_combo.configure(state='disabled')
                self._com_btn.configure(text="关闭串口")

    def _on_refresh_com(self):
        self._uart.uartRefreshCOM()
        self._com_combo['values'] = tuple(self._uart.uartComAvailable)
        if self._uart.uartComAvailable:
            self._com_combo.set(self._uart.uartComAvailable[0])

    def _on_clear(self):
        self._rec_text.clear()

    def _on_open_log_file(self):
        with self._log_lock:
            if self._log_file is not None:
                self._on_close_log_file()
                return
        path = filedialog.asksaveasfilename(
            title="保存串口日志",
            defaultextension=".log",
            filetypes=[("Log Files", "*.log"), ("Text Files", "*.txt"), ("All Files", "*.*")],
        )
        if not path:
            return
        with self._log_lock:
            self._log_file = open(path, 'w', encoding='utf-8')
        self._log_file_label.configure(text=path)
        self._log_file_btn.configure(text="停止记录")

    def _on_close_log_file(self):
        with self._log_lock:
            if self._log_file is not None:
                self._log_file.close()
                self._log_file = None
        self._log_file_label.configure(text="")
        self._log_file_btn.configure(text="保存到文件")

    def _on_test(self):
        self._pack_and_send(INVALID_ID, CMD_TEST, b'BabyOS')

    def _on_set_time(self):
        now = datetime.now()
        dt = dict(
            year=now.year, month=now.month, day=now.day,
            hour=now.hour, minute=now.minute, second=now.second
        )
        utc = bStruct2UTC(dt)
        self._pack_and_send(INVALID_ID, CMD_SET_TIME, struct.pack('<I', utc))

    def _on_open_file(self):
        path = filedialog.askopenfilename(title="选择固件文件", filetypes=[("Bin Files", "*.bin")])
        if not path:
            return
        self._file_label.configure(text=path)
        with open(path, 'rb') as f:
            self._bin_data = f.read()
        self._bin_len = len(self._bin_data)
        self._bin_crc = crc_calculate(ALGO_CRC32, self._bin_data)
        self._append_log(f"file loaded: len={self._bin_len} crc32={self._bin_crc:08X}")

    def _load_bin(self, path: str):
        with open(path, 'rb') as f:
            self._bin_data = f.read()
        self._bin_len = len(self._bin_data)
        self._bin_crc = crc_calculate(ALGO_CRC32, self._bin_data)
        self._append_log(f"file loaded: len={self._bin_len} crc32={self._bin_crc:08X}")

    def _load_folder(self, folder_path: str) -> bool:
        if not os.path.isdir(folder_path):
            return False
        out_path = os.path.join(folder_path, 'allfile.bin')
        try:
            with open(out_path, 'wb') as out_f:
                for fname in sorted(os.listdir(folder_path)):
                    fpath = os.path.join(folder_path, fname)
                    if not os.path.isfile(fpath):
                        continue
                    name_bytes = fname.encode('utf-8')
                    out_f.write(struct.pack('>HI', 0xAA01, len(name_bytes)))
                    out_f.write(name_bytes)
                    with open(fpath, 'rb') as in_f:
                        content = in_f.read()
                    out_f.write(struct.pack('>HI', 0xAA02, len(content)))
                    out_f.write(content)
            self._append_log(f"folder merged -> {out_path}")
            self._load_bin(out_path)
            return True
        except Exception as e:
            self._append_log(f"folder merge error: {e}")
            return False

    def _on_trans_file_open(self):
        path = filedialog.askopenfilename(title="选择文件", filetypes=[("所有文件", "*.*")])
        if not path:
            return
        self._trans_file_label.configure(text=path)
        self._load_bin(path)

    def _on_trans_folder_open(self):
        folder = filedialog.askdirectory(title="选择文件夹（多文件自动拼接）")
        if not folder:
            return
        self._trans_file_label.configure(text=folder + "/allfile.bin")
        self._load_folder(folder)

    def _on_upgrade(self):
        if not self._bin_len:
            messagebox.showwarning("提示", "请先选择固件文件")
            return
        if not self._fw_name.get():
            messagebox.showwarning("提示", "请输入固件名称")
            return
        name_bytes = self._fw_name.get().encode('utf-8')
        param = struct.pack('<II', self._bin_len, self._bin_crc) + name_bytes[:64].ljust(64, b'\x00')
        self._pack_and_send(INVALID_ID, CMD_FW_INFO, param)

    def _on_trans_file(self):
        if not self._bin_len:
            messagebox.showwarning("提示", "请先选择文件")
            return
        try:
            dev_no = int(self._dev_no.get() or '0')
            offset = int(self._file_offset.get() or '0')
        except ValueError:
            messagebox.showwarning("提示", "设备号/偏移地址需为整数")
            return
        param = struct.pack('<IIII', self._bin_len, self._bin_crc, dev_no, offset)
        self._pack_and_send(INVALID_ID, CMD_TRANS_FILE, param)

    def _on_stop_trans_file(self):
        param = struct.pack('<IIII', 0, 0, 0, 0)
        self._pack_and_send(INVALID_ID, CMD_TRANS_FILE, param)

    def _on_get_uid(self):
        self._pack_and_send(INVALID_ID, CMD_GET_UID)

    def _on_set_sn(self):
        try:
            orval = int(self._orval_edit.get() or '0')
        except ValueError:
            messagebox.showwarning("提示", "Orval 需为整数")
            return
        if self._uid_len == 0:
            messagebox.showwarning("提示", "请先获取UID")
            return
        uid_bytes = bytes(self._mcu_uid[:self._uid_len])
        md5_val = md5_hex_16(uid_bytes)
        sn_table = bytes([len(md5_val)]) + bytes(b | orval for b in md5_val)
        self._append_log("SN: " + ' '.join(f'{b:02X}' for b in sn_table))
        self._pack_and_send(INVALID_ID, CMD_SET_SN, sn_table)

    def _on_get_device_info(self):
        self._pack_and_send(INVALID_ID, CMD_GET_DEVICE_INFO)

    # ------------------------------------------------------------------
    # Xmodem / Ymodem actions
    # ------------------------------------------------------------------
    def _on_xmodem_open_file(self):
        path = filedialog.askopenfilename(title="选择文件 (Xmodem)", filetypes=[("所有文件", "*.*")])
        if not path:
            return
        self._xmodem_file_label.configure(text=path)
        with open(path, 'rb') as f:
            self._xmodem_data = f.read()
        self._append_log(f"[Xmodem] file loaded: {len(self._xmodem_data)} bytes")

    def _on_xmodem_send(self):
        if not self._xmodem_data:
            messagebox.showwarning("提示", "请先选择文件")
            return
        if self._active_xfer is not None:
            messagebox.showwarning("提示", "当前有传输正在进行")
            return
        self._xmodem = XmodemSender(
            uart_send=lambda d: self._uart.uartSendBuff(d),
            log_fn=self._append_log,
            timeout_sec=10.0,
            max_retries=16,
        )
        self._xmodem.start(self._xmodem_data)
        self._active_xfer = self._xmodem
        self._xmodem_progress['value'] = 0
        self._xmodem.status = "传输中..."

    def _on_xmodem_cancel(self):
        if self._active_xfer is not None:
            self._active_xfer.cancel()
            self._active_xfer = None
            self._xmodem = None
            self._xmodem_progress['value'] = 0

    def _on_ymodem_open_file(self):
        path = filedialog.askopenfilename(title="选择文件 (Ymodem)", filetypes=[("所有文件", "*.*")])
        if not path:
            return
        self._ymodem_file_label.configure(text=path)
        filename = path.split('/')[-1].split('\\')[-1]
        with open(path, 'rb') as f:
            self._ymodem_data = f.read()
        self._ymodem_filename = filename
        self._append_log(f"[Ymodem] file loaded: '{filename}' {len(self._ymodem_data)} bytes")

    def _on_ymodem_send(self):
        if not self._ymodem_data:
            messagebox.showwarning("提示", "请先选择文件")
            return
        if self._active_xfer is not None:
            messagebox.showwarning("提示", "当前有传输正在进行")
            return
        self._ymodem = YmodemSender(
            uart_send=lambda d: self._uart.uartSendBuff(d),
            log_fn=self._append_log,
            timeout_sec=10.0,
            max_retries=16,
        )
        self._ymodem.start(self._ymodem_data, filename=self._ymodem_filename)
        self._active_xfer = self._ymodem
        self._ymodem_progress['value'] = 0

    def _on_ymodem_cancel(self):
        if self._active_xfer is not None:
            self._active_xfer.cancel()
            self._active_xfer = None
            self._ymodem = None
            self._ymodem_progress['value'] = 0

    # ------------------------------------------------------------------
    # Param actions
    # ------------------------------------------------------------------
    def _send_shell_cmd(self, cmd: str):
        if not self._uart.uartGetOpenStatus():
            messagebox.showwarning("提示", "请先打开串口")
            return
        data = cmd.encode('utf-8') + b'\r\n'
        self._uart.uartSendBuff(data)
        self._append_log(f"[shell] >> {cmd}")
        if hasattr(self, '_param_output'):
            self._param_output.insert('end', f">> {cmd}\n")
            self._param_output.see('end')

    def _on_param_list(self):
        self._send_shell_cmd("param")

    def _on_param_get(self):
        name = self._param_name.get().strip()
        if not name:
            messagebox.showwarning("提示", "请输入参数名称")
            return
        self._send_shell_cmd(f"param {name}")

    def _on_param_set(self):
        name = self._param_name.get().strip()
        value = self._param_value.get().strip()
        if not name:
            messagebox.showwarning("提示", "请输入参数名称")
            return
        if not value:
            messagebox.showwarning("提示", "请输入参数值")
            return
        self._send_shell_cmd(f"param {name} {value}")

    def _on_param_send_raw(self):
        cmd = self._param_raw_input.get().strip()
        if not cmd:
            return
        self._send_shell_cmd(cmd)

    def _handle_param_response(self, text: str):
        if not hasattr(self, '_param_output'):
            return
        self._param_output.insert('end', text)
        self._param_output.see('end')

        m2 = re.match(r'^\s*(\S+?)\s*:\s*(\S+)\s*$', text.strip())
        if m2:
            name, val = m2.group(1), m2.group(2)
            self._param_output.insert('end', f"{name} = {val}\n")
            self._param_output.see('end')
            if name not in self._param_names:
                self._param_names.append(name)
                self._param_name['values'] = tuple(self._param_names)
            return

        lines = text.strip().splitlines()
        names = []
        for line in lines:
            m = re.match(r'^\s*:\s*(\S+)\s*$', line)
            if m:
                names.append(m.group(1))
        if names:
            self._param_names = names
            self._param_name['values'] = tuple(self._param_names)

    def _on_param_polling_tick(self):
        if not self._param_polling_enabled:
            return
        if not self._param_polling_name:
            self._send_shell_cmd("param")
        else:
            self._send_shell_cmd(f"param {self._param_polling_name}")
        # re-arm
        self.after(self._param_polling_interval_ms, self._on_param_polling_tick)

    def _on_param_polling_start(self):
        if self._param_polling_enabled:
            self._on_param_polling_stop()
            return
        if not self._uart.uartGetOpenStatus():
            messagebox.showwarning("提示", "请先打开串口")
            return
        try:
            interval = int(self._param_polling_interval.get() or '1000')
            if interval < 100:
                interval = 100
        except ValueError:
            interval = 1000
        self._param_polling_interval_ms = interval
        self._param_polling_name = self._param_name.get().strip()
        self._param_polling_enabled = True
        self._param_polling_btn.configure(text="停止监控")
        self._append_log(f"[param] 定时查询已启动 (间隔 {interval}ms)")
        # kick off the loop
        self.after(self._param_polling_interval_ms, self._on_param_polling_tick)

    def _on_param_polling_stop(self):
        if not self._param_polling_enabled:
            return
        self._param_polling_enabled = False
        self._param_polling_btn.configure(text="定时查询")
        self._append_log("[param] 定时查询已停止")

    # ------------------------------------------------------------------
    # Build UI
    # ------------------------------------------------------------------
    def _setup_ui(self):
        # Use ttk for modern-looking widgets where possible.
        try:
            style = ttk.Style()
            if 'vista' in style.theme_names():
                style.theme_use('vista')
            elif 'clam' in style.theme_names():
                style.theme_use('clam')
        except tk.TclError:
            pass

        # Top: tabbed area
        notebook = ttk.Notebook(self)

        notebook.add(self._build_tab_serial(),     text="串口控制")
        notebook.add(self._build_tab_ota(),         text="OTA升级")
        notebook.add(self._build_tab_trans(),       text="文件传输")
        notebook.add(self._build_tab_xymodem(),     text="Xmodem/Ymodem")
        notebook.add(self._build_tab_devinfo(),     text="设备信息")
        notebook.add(self._build_tab_param(),       text="参数调节")
        notebook.add(self._build_tab_http(),        text="HTTP调试")

        notebook.pack(fill='both', expand=True, padx=6, pady=6)

        # Bottom: log + controls
        bottom = ttk.Frame(self)
        bottom.pack(fill='x', padx=6, pady=(0, 4))

        self._encrypt_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(bottom, text="加密传输", variable=self._encrypt_var).pack(side='left')

        ttk.Button(bottom, text="清空日志", command=self._on_clear).pack(side='left', padx=(8, 0))

        self._log_file_btn = ttk.Button(bottom, text="保存到文件", command=self._on_open_log_file)
        self._log_file_btn.pack(side='left', padx=(8, 0))

        self._log_file_label = ttk.Label(bottom, text="", foreground="#888")
        self._log_file_label.pack(side='left', padx=8)

        self._rec_text = _TkLogText(self, height=10)
        self._rec_text.pack(fill='x', padx=6, pady=(0, 6))

    # ----- tab builders ------------------------------------------------
    def _labeled_entry(self, parent, label, row, col, **entry_kwargs):
        ttk.Label(parent, text=label).grid(row=row, column=col, sticky='w', padx=4, pady=2)
        var = tk.StringVar()
        ent = ttk.Entry(parent, textvariable=var, **entry_kwargs)
        ent.grid(row=row, column=col + 1, sticky='we', padx=4, pady=2)
        return var, ent

    def _build_tab_serial(self) -> ttk.Frame:
        tab = ttk.Frame(self)
        tab.columnconfigure(1, weight=1)

        serial = ttk.LabelFrame(tab, text="串口设置")
        serial.grid(row=0, column=0, sticky='nsew', padx=6, pady=6, columnspan=2)
        serial.columnconfigure(1, weight=1)

        ttk.Label(serial, text="串口:").grid(row=0, column=0, sticky='w', padx=4, pady=4)
        self._com_combo = ttk.Combobox(serial, state='readonly', values=tuple(self._uart.uartComAvailable))
        if self._uart.uartComAvailable:
            self._com_combo.set(self._uart.uartComAvailable[0])
        self._com_combo.grid(row=0, column=1, sticky='we', padx=4, pady=4)
        ttk.Button(serial, text="刷新", command=self._on_refresh_com).grid(row=0, column=2, padx=4, pady=4)

        self._bps_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(serial, text="9600波特率", variable=self._bps_var).grid(
            row=1, column=0, columnspan=2, sticky='w', padx=4, pady=4
        )

        self._com_btn = ttk.Button(serial, text="打开串口", command=self._on_com_clicked)
        self._com_btn.grid(row=2, column=0, columnspan=3, sticky='we', padx=4, pady=4)

        test = ttk.LabelFrame(tab, text="协议测试")
        test.grid(row=1, column=0, sticky='nsew', padx=6, pady=6, columnspan=2)
        ttk.Button(test, text="发送测试指令", command=self._on_test).grid(row=0, column=0, padx=4, pady=4)
        ttk.Button(test, text="设置时间", command=self._on_set_time).grid(row=0, column=1, padx=4, pady=4)
        return tab

    def _build_tab_ota(self) -> ttk.Frame:
        tab = ttk.Frame(self)
        tab.columnconfigure(1, weight=1)

        g = ttk.LabelFrame(tab, text="固件升级")
        g.grid(row=0, column=0, sticky='nsew', padx=6, pady=6, columnspan=2)
        g.columnconfigure(1, weight=1)

        ttk.Label(g, text="固件文件:").grid(row=0, column=0, sticky='w', padx=4, pady=4)
        self._file_label = ttk.Label(g, text="")
        self._file_label.grid(row=0, column=1, sticky='we', padx=4, pady=4)
        ttk.Button(g, text="选择文件", command=self._on_open_file).grid(row=0, column=2, padx=4, pady=4)

        ttk.Label(g, text="固件名称:").grid(row=1, column=0, sticky='w', padx=4, pady=4)
        self._fw_name = ttk.Entry(g)
        self._fw_name.grid(row=1, column=1, columnspan=2, sticky='we', padx=4, pady=4)

        ttk.Button(g, text="开始OTA升级", command=self._on_upgrade).grid(
            row=2, column=0, columnspan=3, sticky='we', padx=4, pady=4
        )

        self._progress_bar = ttk.Progressbar(g, orient='horizontal', mode='determinate')
        self._progress_bar.grid(row=3, column=0, columnspan=3, sticky='we', padx=4, pady=4)

        return tab

    def _build_tab_trans(self) -> ttk.Frame:
        tab = ttk.Frame(self)
        tab.columnconfigure(1, weight=1)

        g = ttk.LabelFrame(tab, text="文件传输")
        g.grid(row=0, column=0, sticky='nsew', padx=6, pady=6, columnspan=2)
        g.columnconfigure(1, weight=1)

        ttk.Label(g, text="文件:").grid(row=0, column=0, sticky='w', padx=4, pady=4)
        self._trans_file_label = ttk.Label(g, text="")
        self._trans_file_label.grid(row=0, column=1, sticky='we', padx=4, pady=4)
        ttk.Button(g, text="选择文件", command=self._on_trans_file_open).grid(row=0, column=2, padx=4, pady=4)
        ttk.Button(g, text="选择文件夹", command=self._on_trans_folder_open).grid(row=0, column=3, padx=4, pady=4)

        ttk.Label(g, text="设备号:").grid(row=1, column=0, sticky='w', padx=4, pady=4)
        self._dev_no = ttk.Entry(g, width=10)
        self._dev_no.insert(0, "0")
        self._dev_no.grid(row=1, column=1, sticky='w', padx=4, pady=4)

        ttk.Label(g, text="偏移地址:").grid(row=2, column=0, sticky='w', padx=4, pady=4)
        self._file_offset = ttk.Entry(g, width=10)
        self._file_offset.insert(0, "0")
        self._file_offset.grid(row=2, column=1, sticky='w', padx=4, pady=4)

        ttk.Button(g, text="开始传输", command=self._on_trans_file).grid(row=3, column=0, padx=4, pady=4)
        ttk.Button(g, text="停止传输", command=self._on_stop_trans_file).grid(
            row=3, column=1, columnspan=2, sticky='we', padx=4, pady=4
        )
        return tab

    def _build_tab_xymodem(self) -> ttk.Frame:
        tab = ttk.Frame(self)

        # Xmodem
        xm = ttk.LabelFrame(tab, text="Xmodem-128 (128字节块 + checksum)")
        xm.grid(row=0, column=0, sticky='nsew', padx=6, pady=6)
        xm.columnconfigure(1, weight=1)
        ttk.Label(xm, text="文件:").grid(row=0, column=0, sticky='w', padx=4, pady=4)
        self._xmodem_file_label = ttk.Label(xm, text="")
        self._xmodem_file_label.grid(row=0, column=1, sticky='we', padx=4, pady=4)
        ttk.Button(xm, text="选择文件", command=self._on_xmodem_open_file).grid(row=0, column=2, padx=4, pady=4)
        self._xmodem_progress = ttk.Progressbar(xm, orient='horizontal', mode='determinate')
        self._xmodem_progress.grid(row=1, column=0, columnspan=3, sticky='we', padx=4, pady=4)
        btn_row = ttk.Frame(xm)
        btn_row.grid(row=2, column=0, columnspan=3, sticky='we', padx=4, pady=4)
        ttk.Button(btn_row, text="开始发送", command=self._on_xmodem_send).pack(side='left', padx=2)
        ttk.Button(btn_row, text="取消", command=self._on_xmodem_cancel).pack(side='left', padx=2)

        # Ymodem
        ym = ttk.LabelFrame(tab, text="Ymodem-1K (1K块 + CRC16, 带文件名)")
        ym.grid(row=1, column=0, sticky='nsew', padx=6, pady=6)
        ym.columnconfigure(1, weight=1)
        ttk.Label(ym, text="文件:").grid(row=0, column=0, sticky='w', padx=4, pady=4)
        self._ymodem_file_label = ttk.Label(ym, text="")
        self._ymodem_file_label.grid(row=0, column=1, sticky='we', padx=4, pady=4)
        ttk.Button(ym, text="选择文件", command=self._on_ymodem_open_file).grid(row=0, column=2, padx=4, pady=4)
        self._ymodem_progress = ttk.Progressbar(ym, orient='horizontal', mode='determinate')
        self._ymodem_progress.grid(row=1, column=0, columnspan=3, sticky='we', padx=4, pady=4)
        btn_row2 = ttk.Frame(ym)
        btn_row2.grid(row=2, column=0, columnspan=3, sticky='we', padx=4, pady=4)
        ttk.Button(btn_row2, text="开始发送", command=self._on_ymodem_send).pack(side='left', padx=2)
        ttk.Button(btn_row2, text="取消", command=self._on_ymodem_cancel).pack(side='left', padx=2)

        tab.columnconfigure(0, weight=1)
        tab.rowconfigure(0, weight=1)
        tab.rowconfigure(1, weight=1)
        return tab

    def _build_tab_devinfo(self) -> ttk.Frame:
        tab = ttk.Frame(self)
        g = ttk.LabelFrame(tab, text="设备信息")
        g.grid(row=0, column=0, sticky='nsew', padx=6, pady=6)

        ttk.Button(g, text="获取UID", command=self._on_get_uid).grid(row=0, column=0, columnspan=2, sticky='we', padx=4, pady=4)
        ttk.Label(g, text="Orval:").grid(row=1, column=0, sticky='w', padx=4, pady=4)
        self._orval_edit = ttk.Entry(g, width=10)
        self._orval_edit.insert(0, "0")
        self._orval_edit.grid(row=1, column=1, sticky='w', padx=4, pady=4)
        ttk.Button(g, text="写入SN", command=self._on_set_sn).grid(row=2, column=0, columnspan=2, sticky='we', padx=4, pady=4)
        ttk.Button(g, text="获取设备信息", command=self._on_get_device_info).grid(
            row=3, column=0, columnspan=2, sticky='we', padx=4, pady=4
        )
        return tab

    def _build_tab_param(self) -> ttk.Frame:
        tab = ttk.Frame(self)
        tab.columnconfigure(1, weight=1)

        instr = ttk.Label(
            tab,
            text=(
                "BabyOS参数调节 — 通过Shell命令读写MCU运行时变量\n"
                "用法: param [name [value]]\n"
                "  param          — 列出所有参数\n"
                "  param <name>   — 读取参数\n"
                "  param <name> <val> — 设置参数"
            ),
            foreground="#555",
            justify='left',
        )
        instr.grid(row=0, column=0, columnspan=2, sticky='we', padx=6, pady=4)

        g = ttk.LabelFrame(tab, text="参数读写")
        g.grid(row=1, column=0, columnspan=2, sticky='nsew', padx=6, pady=6)
        g.columnconfigure(1, weight=1)

        ttk.Label(g, text="参数名称:").grid(row=0, column=0, sticky='w', padx=4, pady=4)
        self._param_name = ttk.Combobox(g, values=tuple(self._param_names))
        self._param_name.grid(row=0, column=1, sticky='we', padx=4, pady=4)

        ttk.Label(g, text="参数值:").grid(row=1, column=0, sticky='w', padx=4, pady=4)
        self._param_value = ttk.Entry(g)
        self._param_value.grid(row=1, column=1, sticky='we', padx=4, pady=4)

        btn_row = ttk.Frame(g)
        btn_row.grid(row=2, column=0, columnspan=2, sticky='we', padx=4, pady=4)
        ttk.Button(btn_row, text="列出全部", command=self._on_param_list).pack(side='left', padx=2)
        ttk.Button(btn_row, text="读取参数", command=self._on_param_get).pack(side='left', padx=2)
        ttk.Button(btn_row, text="设置参数", command=self._on_param_set).pack(side='left', padx=2)

        # Polling row
        poll_row = ttk.Frame(g)
        poll_row.grid(row=3, column=0, columnspan=2, sticky='we', padx=4, pady=4)
        ttk.Label(poll_row, text="定时查询:").pack(side='left')
        self._param_polling_interval = ttk.Entry(poll_row, width=8)
        self._param_polling_interval.insert(0, "1000")
        self._param_polling_interval.pack(side='left', padx=2)
        ttk.Label(poll_row, text="ms").pack(side='left')
        self._param_polling_btn = ttk.Button(poll_row, text="定时查询", command=self._on_param_polling_start)
        self._param_polling_btn.pack(side='left', padx=4)

        # Raw input
        raw_row = ttk.Frame(g)
        raw_row.grid(row=4, column=0, columnspan=2, sticky='we', padx=4, pady=4)
        ttk.Label(raw_row, text="自定义命令:").pack(side='left')
        self._param_raw_input = ttk.Entry(raw_row)
        self._param_raw_input.pack(side='left', fill='x', expand=True, padx=2)
        ttk.Button(raw_row, text="发送", command=self._on_param_send_raw).pack(side='left', padx=2)

        ttk.Label(tab, text="响应输出:").grid(row=2, column=0, columnspan=2, sticky='w', padx=6)
        self._param_output = ScrolledText(tab, height=10, background='#f5f5f5')
        self._param_output.grid(row=3, column=0, columnspan=2, sticky='nsew', padx=6, pady=4)
        tab.rowconfigure(3, weight=1)
        return tab

    def _build_tab_http(self) -> ttk.Frame:
        tab = ttk.Frame(self)
        tab.columnconfigure(0, weight=1)
        tab.rowconfigure(2, weight=1)

        # Server control
        srv = ttk.LabelFrame(tab, text="Mock HTTP/HTTPS 服务器 (供设备HTTP客户端连接)")
        srv.grid(row=0, column=0, sticky='we', padx=6, pady=6)
        srv.columnconfigure(1, weight=1)

        ttk.Label(srv, text="端口:").grid(row=0, column=0, sticky='w', padx=4, pady=4)
        self._http_port = ttk.Entry(srv, width=8)
        self._http_port.insert(0, "8080")
        self._http_port.grid(row=0, column=1, sticky='w', padx=4, pady=4)

        self._https_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(srv, text="HTTPS (自签名证书)", variable=self._https_var).grid(
            row=0, column=2, sticky='w', padx=4, pady=4
        )

        self._http_srv_status = ttk.Label(srv, text="未运行", foreground="#888")
        self._http_srv_status.grid(row=0, column=3, sticky='w', padx=4, pady=4)

        self._http_srv_btn = ttk.Button(srv, text="启动服务器", command=self._on_http_srv_toggle)
        self._http_srv_btn.grid(row=0, column=4, padx=4, pady=4)

        ttk.Label(srv, text="响应内容:").grid(row=1, column=0, sticky='w', padx=4, pady=4)
        self._http_resp_body = ttk.Entry(srv)
        self._http_resp_body.insert(0, '{"status":"ok","msg":"hello from BabyOS Mock Server"}')
        self._http_resp_body.grid(row=1, column=1, columnspan=4, sticky='we', padx=4, pady=4)

        ttk.Label(srv, text="Content-Type:").grid(row=2, column=0, sticky='w', padx=4, pady=4)
        self._http_resp_type = ttk.Entry(srv, width=20)
        self._http_resp_type.insert(0, "application/json")
        self._http_resp_type.grid(row=2, column=1, sticky='w', padx=4, pady=4)

        ttk.Label(srv, text="状态码:").grid(row=2, column=2, sticky='e', padx=4, pady=4)
        self._http_resp_code = ttk.Entry(srv, width=8)
        self._http_resp_code.insert(0, "200")
        self._http_resp_code.grid(row=2, column=3, sticky='w', padx=4, pady=4)

        # Device request trigger
        req = ttk.LabelFrame(tab, text="触发设备发送HTTP请求")
        req.grid(row=1, column=0, sticky='we', padx=6, pady=6)
        req.columnconfigure(1, weight=1)

        ttk.Label(req, text="URL:").grid(row=0, column=0, sticky='w', padx=4, pady=4)
        self._http_url = ttk.Entry(req)
        self._http_url.insert(0, "http://192.168.1.100:8080/test")
        self._http_url.grid(row=0, column=1, columnspan=3, sticky='we', padx=4, pady=4)

        ttk.Label(req, text="Method:").grid(row=1, column=0, sticky='w', padx=4, pady=4)
        self._http_method = ttk.Combobox(req, values=("GET", "POST", "PUT", "DELETE"), state='readonly', width=10)
        self._http_method.set("GET")
        self._http_method.grid(row=1, column=1, sticky='w', padx=4, pady=4)

        ttk.Label(req, text="Headers:").grid(row=1, column=2, sticky='e', padx=4, pady=4)
        self._http_headers = ttk.Entry(req)
        self._http_headers.grid(row=1, column=3, sticky='we', padx=4, pady=4)

        ttk.Label(req, text="Body:").grid(row=2, column=0, sticky='w', padx=4, pady=4)
        self._http_body = ttk.Entry(req)
        self._http_body.grid(row=2, column=1, columnspan=3, sticky='we', padx=4, pady=4)

        btn_row = ttk.Frame(req)
        btn_row.grid(row=3, column=0, columnspan=4, sticky='we', padx=4, pady=4)
        ttk.Button(btn_row, text="初始化HTTP客户端", command=self._on_http_init).pack(side='left', padx=2)
        ttk.Button(btn_row, text="发送请求", command=self._on_http_send).pack(side='left', padx=2)
        ttk.Button(btn_row, text="反初始化", command=self._on_http_deinit).pack(side='left', padx=2)

        # Request log
        ttk.Label(tab, text="Mock服务器收到的请求:").grid(
            row=2, column=0, sticky='w', padx=6
        )
        self._http_req_log = _ReadOnlyLogText(tab, height=10)
        self._http_req_log.grid(row=3, column=0, sticky='nsew', padx=6, pady=4)
        ttk.Button(tab, text="清空请求日志", command=self._on_http_clear_log).grid(
            row=4, column=0, sticky='w', padx=6, pady=4
        )
        tab.rowconfigure(3, weight=1)

        # Kick off the periodic HTTP log refresh
        self._http_log_timer_tick()
        return tab

    # ------------------------------------------------------------------
    # HTTP tab actions
    # ------------------------------------------------------------------
    def _on_http_srv_toggle(self):
        if self._mock_http.is_running():
            self._mock_http.stop()
            self._http_srv_btn.configure(text="启动服务器")
            self._http_srv_status.configure(text="未运行", foreground="#888")
        else:
            use_https = bool(self._https_var.get())
            try:
                port = int(self._http_port.get() or '8080')
            except ValueError:
                port = 8080

            if use_https and port == 8080:
                port = 8443
                self._http_port.delete(0, 'end')
                self._http_port.insert(0, "8443")
                self._append_log("[MockHttp] HTTPS detected port 8080, switched to 8443")
            elif not use_https and port == 8443:
                port = 8080
                self._http_port.delete(0, 'end')
                self._http_port.insert(0, "8080")
                self._append_log("[MockHttp] HTTP detected port 8443, switched to 8080")

            try:
                code = int(self._http_resp_code.get() or '200')
            except ValueError:
                code = 200
            ctype = self._http_resp_type.get() or 'application/json'
            body = self._http_resp_body.get() or '{}'
            self._mock_http.set_response_config(code, ctype, body)

            ok = self._mock_http.start(port=port, use_https=use_https)
            if ok:
                scheme = "https" if use_https else "http"
                self._http_srv_btn.configure(text="停止服务器")
                self._http_srv_status.configure(
                    text=f"运行中: {scheme}://0.0.0.0:{port}",
                    foreground='green',
                )
            else:
                self._http_srv_status.configure(text="启动失败", foreground='red')

    def _http_log_timer_tick(self):
        try:
            self._on_http_log_timer_tick()
        finally:
            self.after(self.HTTP_LOG_REFRESH_MS, self._http_log_timer_tick)

    def _on_http_log_timer_tick(self):
        if not hasattr(self, '_http_req_log'):
            return
        records = self._mock_http.get_request_log()
        if not records:
            return
        if len(records) <= self._http_log_seen_count:
            return
        new_records = records[self._http_log_seen_count:]
        self._http_log_seen_count = len(records)
        for rec in new_records:
            self._http_req_log._append_ui("---")
            self._http_req_log._append_ui(
                f"[{rec.timestamp}] {rec.source} {rec.method} {rec.path}"
            )
            if rec.headers:
                self._http_req_log._append_ui(f"  headers: {rec.headers}")
            if rec.body:
                preview = rec.body[:200] + ('...' if len(rec.body) > 200 else '')
                self._http_req_log._append_ui(f"  body: {preview}")

    def _on_http_clear_log(self):
        self._http_req_log.clear()
        self._mock_http.clear_request_log()
        self._http_log_seen_count = 0

    def _on_http_init(self):
        self._pack_and_send(INVALID_ID, CMD_HTTP_INIT, b'\x00')
        self._append_log("[HTTP] init -> device")

    def _on_http_deinit(self):
        self._pack_and_send(INVALID_ID, CMD_HTTP_DEINIT, b'\x00')
        self._append_log("[HTTP] deinit -> device")

    def _on_http_send(self):
        url = self._http_url.get().strip()
        if not url:
            messagebox.showwarning("提示", "请输入URL")
            return
        method = self._http_method.get()
        headers = self._http_headers.get().strip()
        body = self._http_body.get().strip()

        method_byte = {'GET': 0, 'POST': 1, 'PUT': 2, 'DELETE': 3}.get(method, 0)
        url_bytes = url.encode('utf-8')
        headers_bytes = headers.encode('utf-8') if headers else b''
        body_bytes = body.encode('utf-8') if body else b''

        param = bytearray()
        param.append(method_byte)
        param.extend(struct.pack('<H', len(url_bytes)))
        param.extend(url_bytes)
        param.extend(struct.pack('<H', len(headers_bytes)))
        param.extend(headers_bytes)
        param.extend(body_bytes)
        self._pack_and_send(INVALID_ID, CMD_HTTP_REQUEST, bytes(param))
        self._append_log(f"[HTTP] {method} {url} -> device")

    # ------------------------------------------------------------------
    # Tk lifecycle
    # ------------------------------------------------------------------
    def destroy(self):
        """Make sure the mock server is stopped on close."""
        try:
            if self._mock_http.is_running():
                self._mock_http.stop()
        except Exception:
            pass
        try:
            with self._log_lock:
                if self._log_file is not None:
                    self._log_file.close()
                    self._log_file = None
        except Exception:
            pass
        super().destroy()
