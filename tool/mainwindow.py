"""
MainWindow — PyQt5 GUI for BabyOS_Protocol upper-computer.
All protocol commands, UART I/O, and UI logic are implemented here.
"""

import os
import struct
from datetime import datetime
from PyQt5.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QTabWidget, QGroupBox, QLabel, QLineEdit, QPushButton,
    QComboBox, QTextEdit, QProgressBar, QCheckBox, QMessageBox,
    QFileDialog, QTableWidget, QTableWidgetItem, QHeaderView, QTextBrowser
)
from PyQt5.QtCore import QTimer, Qt
from PyQt5.QtGui import QTextCursor

from uart_driver import UartDriver
from b_protocol import (
    bProtocolRegist, bProtocolParse, bProtocolPack,
    bProtocolEncrypt, bProtocolDecrypt,
    DEVICE_ID_HOST, INVALID_ID
)
from algo_crc import crc_calculate, ALGO_CRC32
from algo_md5 import md5_hex_16
from b_mod_utc import bStruct2UTC
from xmodem_ydmodem import XmodemSender, YmodemSender


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


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("BabyOS_Protocol - 上位机")
        self.setMinimumSize(800, 650)

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
        self._active_xfer = None   # currently active sender (xmodem or ymodem)
        self._xmodem_data = b''
        self._xmodem_filename = ''
        self._ymodem_data = b''
        self._ymodem_filename = ''

        # Timer for polling UART rx
        self._timer = QTimer()
        self._timer.timeout.connect(self._on_timer)
        self._timer.start(100)

        # UI
        self._setup_ui()

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
        return 0

    # ------------------------------------------------------------------
    # UART polling
    # ------------------------------------------------------------------
    def _on_timer(self):
        buf = bytearray(10240)
        n = self._uart.uartReadBuff(buf)
        if n <= 0:
            return
        data = buf[:n]          # keep as bytearray so _bProtocolDecrypt can modify in-place

        # Feed bytes to active Xmodem/Ymodem sender
        if self._active_xfer is not None:
            for b in buf[:n]:
                self._active_xfer.on_uart_byte(b)
            self._active_xfer.on_timer_tick()
            if not self._active_xfer.is_active:
                self._active_xfer = None
            return

        # For param shell commands, data comes back as plain text
        # Try to decode as UTF-8 text first (for param responses)
        try:
            text = bytes(data).decode('utf-8', errors='replace')
            # Check if it looks like a shell response (contains : or \r\n)
            if any(c in text for c in [':', '\r', '\n']):
                self._append_log(text)
                # Update param list if we get a list response
                self._handle_param_response(text)
                return
        except Exception:
            pass

        if self._encrypt_checked():
            bProtocolDecrypt(data)
        ret = bProtocolParse(self._protocol_n, bytes(data))
        if ret < 0:
            # Not a protocol frame — display as raw text
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
        # Pad to 512 bytes if needed
        if len(chunk) < 512:
            chunk = chunk + b'\x00' * (512 - len(chunk))
        param = struct.pack('<H', num) + chunk
        self._pack_and_send(INVALID_ID, CMD_UPGRADE_DATA, param)
        pct = min(100, index * 100 // self._bin_len)
        self._progress_bar.setValue(pct)

    def _ack_result(self, result: int):
        self._progress_bar.setValue(100)
        self._append_log(f"Upgrade result: {result}")
        names = {0: "success", 1: "crc_error", 2: "name_mismatch", 3: "len_invalid", 4: "timeout"}
        self._append_log(names.get(result, f"unknown({result})"))

    def _ack_fw_info(self):
        if not self._bin_len or not self._fw_name.text():
            return -1
        name_bytes = self._fw_name.text().encode('utf-8')
        param = struct.pack('<II', self._bin_len, self._bin_crc) + name_bytes[:64].ljust(64, b'\x00')
        self._pack_and_send(INVALID_ID, CMD_FW_INFO, param)
        return 0

    def _show_log(self, dev_id: int, cmd: int, param: bytes):
        self._append_log(f"r-> id:{dev_id} cmd:{cmd:02X} param:{hex_to_string(param)}")

    def _append_log(self, text: str):
        self._rec_text.append(text)
        self._rec_text.moveCursor(QTextCursor.End)

    # ------------------------------------------------------------------
    # UI actions
    # ------------------------------------------------------------------
    def _encrypt_checked(self) -> bool:
        return self._encrypt_box.isChecked()

    def _on_com_clicked(self):
        if self._uart.uartGetOpenStatus():
            self._uart.uartClosePort()
            self._com_combo.setEnabled(True)
            self._com_btn.setText("打开串口")
        else:
            bps = 9600 if self._bps_check.isChecked() else 115200
            port = self._com_combo.currentText()
            if not port:
                return
            if self._uart.uartOpenPort(port, bps):
                self._com_combo.setEnabled(False)
                self._com_btn.setText("关闭串口")

    def _on_refresh_com(self):
        self._uart.uartRefreshCOM()
        self._com_combo.clear()
        self._com_combo.addItems(self._uart.uartComAvailable)

    def _on_clear(self):
        self._rec_text.clear()

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
        path, _ = QFileDialog.getOpenFileName(self, "选择固件文件", "", "Bin Files (*.bin)")
        if not path:
            return
        self._file_label.setText(path)
        with open(path, 'rb') as f:
            self._bin_data = f.read()
        self._bin_len = len(self._bin_data)
        self._bin_crc = crc_calculate(ALGO_CRC32, self._bin_data)
        self._append_log(f"file loaded: len={self._bin_len} crc32={self._bin_crc:08X}")

    def _load_bin(self, path: str):
        """Load a single bin file into _bin_data/_bin_len/_bin_crc."""
        with open(path, 'rb') as f:
            self._bin_data = f.read()
        self._bin_len = len(self._bin_data)
        self._bin_crc = crc_calculate(ALGO_CRC32, self._bin_data)
        self._append_log(f"file loaded: len={self._bin_len} crc32={self._bin_crc:08X}")

    def _load_folder(self, folder_path: str) -> bool:
        """
        Read all files in folder_path, concatenate into one bin using BabyOS KLV format:
          key=0xAA01 big-endian len(4B) + filename UTF-8 bytes
          key=0xAA02 big-endian len(4B) + file content bytes
        Saves merged output as <folder_path>/allfile.bin and loads it.
        Returns True on success.
        """
        if not os.path.isdir(folder_path):
            return False
        out_path = os.path.join(folder_path, 'allfile.bin')
        try:
            with open(out_path, 'wb') as out_f:
                for fname in sorted(os.listdir(folder_path)):
                    fpath = os.path.join(folder_path, fname)
                    if not os.path.isfile(fpath):
                        continue
                    # KLV filename
                    name_bytes = fname.encode('utf-8')
                    out_f.write(struct.pack('>HI', 0xAA01, len(name_bytes)))
                    out_f.write(name_bytes)
                    # KLV file content
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
        """Open a single file for Tab3 file transfer (separate from Tab2 OTA)."""
        path, _ = QFileDialog.getOpenFileName(self, "选择文件", "", "所有文件 (*.*)")
        if not path:
            return
        self._trans_file_label.setText(path)
        self._load_bin(path)

    def _on_trans_folder_open(self):
        """Open a folder for Tab3: all files inside are concatenated via KLV."""
        folder = QFileDialog.getExistingDirectory(
            self, "选择文件夹（多文件自动拼接）", "")
        if not folder:
            return
        self._trans_file_label.setText(folder + "/allfile.bin")
        self._load_folder(folder)

    def _on_upgrade(self):
        if not self._bin_len:
            QMessageBox.warning(self, "提示", "请先选择固件文件")
            return
        if not self._fw_name.text():
            QMessageBox.warning(self, "提示", "请输入固件名称")
            return
        name_bytes = self._fw_name.text().encode('utf-8')
        param = struct.pack('<II', self._bin_len, self._bin_crc) + name_bytes[:64].ljust(64, b'\x00')
        self._pack_and_send(INVALID_ID, CMD_FW_INFO, param)

    def _on_trans_file(self):
        if not self._bin_len:
            QMessageBox.warning(self, "提示", "请先选择文件")
            return
        dev_no = int(self._dev_no.text() or '0')
        offset = int(self._file_offset.text() or '0')
        param = struct.pack('<IIII', self._bin_len, self._bin_crc, dev_no, offset)
        self._pack_and_send(INVALID_ID, CMD_TRANS_FILE, param)

    def _on_stop_trans_file(self):
        param = struct.pack('<IIII', 0, 0, 0, 0)
        self._pack_and_send(INVALID_ID, CMD_TRANS_FILE, param)

    def _on_get_uid(self):
        self._pack_and_send(INVALID_ID, CMD_GET_UID)

    def _on_set_sn(self):
        orval = int(self._orval_edit.text() or '0')
        if self._uid_len == 0:
            QMessageBox.warning(self, "提示", "请先获取UID")
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
        path, _ = QFileDialog.getOpenFileName(self, "选择文件 (Xmodem)", "", "所有文件 (*.*)")
        if not path:
            return
        self._xmodem_file_label.setText(path)
        with open(path, 'rb') as f:
            self._xmodem_data = f.read()
        self._append_log(f"[Xmodem] file loaded: {len(self._xmodem_data)} bytes")

    def _on_xmodem_send(self):
        if not self._xmodem_data:
            QMessageBox.warning(self, "提示", "请先选择文件")
            return
        if self._active_xfer is not None:
            QMessageBox.warning(self, "提示", "当前有传输正在进行")
            return
        self._xmodem = XmodemSender(
            uart_send=lambda d: self._uart.uartSendBuff(d),
            log_fn=self._append_log,
            timeout_sec=10.0,
            max_retries=16
        )
        self._xmodem.start(self._xmodem_data)
        self._active_xfer = self._xmodem
        self._xmodem_progress.setValue(0)
        self._xmodem.status = "传输中..."

    def _on_xmodem_cancel(self):
        if self._active_xfer is not None:
            self._active_xfer.cancel()
            self._active_xfer = None
            self._xmodem = None
            self._xmodem_progress.setValue(0)

    def _on_ymodem_open_file(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择文件 (Ymodem)", "", "所有文件 (*.*)")
        if not path:
            return
        self._ymodem_file_label.setText(path)
        filename = path.split('/')[-1].split('\\')[-1]
        with open(path, 'rb') as f:
            self._ymodem_data = f.read()
        self._ymodem_filename = filename
        self._append_log(f"[Ymodem] file loaded: '{filename}' {len(self._ymodem_data)} bytes")

    def _on_ymodem_send(self):
        if not self._ymodem_data:
            QMessageBox.warning(self, "提示", "请先选择文件")
            return
        if self._active_xfer is not None:
            QMessageBox.warning(self, "提示", "当前有传输正在进行")
            return
        self._ymodem = YmodemSender(
            uart_send=lambda d: self._uart.uartSendBuff(d),
            log_fn=self._append_log,
            timeout_sec=10.0,
            max_retries=16
        )
        self._ymodem.start(self._ymodem_data, filename=self._ymodem_filename)
        self._active_xfer = self._ymodem
        self._ymodem_progress.setValue(0)

    def _on_ymodem_cancel(self):
        if self._active_xfer is not None:
            self._active_xfer.cancel()
            self._active_xfer = None
            self._ymodem = None
            self._ymodem_progress.setValue(0)

    # ------------------------------------------------------------------
    # Param (参数调节) actions
    # ------------------------------------------------------------------
    def _send_shell_cmd(self, cmd: str):
        """Send a raw shell command (no protocol framing) via UART."""
        if not self._uart.uartGetOpenStatus():
            QMessageBox.warning(self, "提示", "请先打开串口")
            return
        data = cmd.encode('utf-8') + b'\r\n'
        self._uart.uartSendBuff(data)
        self._append_log(f"[shell] >> {cmd}")
        # Also display in param tab output
        if hasattr(self, '_param_output'):
            self._param_output.append(f">> {cmd}")
            self._param_output.moveCursor(QTextCursor.End)

    def _on_param_list(self):
        """List all registered parameters: param"""
        self._send_shell_cmd("param")

    def _on_param_get(self):
        """Read a specific parameter: param <name>"""
        name = self._param_name.text().strip()
        if not name:
            QMessageBox.warning(self, "提示", "请输入参数名称")
            return
        self._send_shell_cmd(f"param {name}")

    def _on_param_set(self):
        """Set a parameter value: param <name> <value>"""
        name = self._param_name.text().strip()
        value = self._param_value.text().strip()
        if not name:
            QMessageBox.warning(self, "提示", "请输入参数名称")
            return
        if not value:
            QMessageBox.warning(self, "提示", "请输入参数值")
            return
        self._send_shell_cmd(f"param {name} {value}")

    def _on_param_send_raw(self):
        """Send a raw shell command (custom command)."""
        cmd = self._param_raw_input.text().strip()
        if not cmd:
            return
        self._send_shell_cmd(cmd)

    def _handle_param_response(self, text: str):
        """Parse and display param response in the param tab output."""
        if not hasattr(self, '_param_output'):
            return
        # BabyOS param response format:
        # - List mode: ": <name1>\r\n: <name2>\r\n..."
        # - Read mode: "<name>:<value>\r\n"
        # - Write mode: usually echoes or confirms
        self._param_output.append(text)
        self._param_output.moveCursor(QTextCursor.End)

    # ------------------------------------------------------------------
    # Build UI
    # ------------------------------------------------------------------
    def _setup_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)

        tabs = QTabWidget()

        # ---- Tab 1: 串口控制 ----
        tab1 = QWidget()
        t1_layout = QVBoxLayout(tab1)

        # Serial port group
        group_serial = QGroupBox("串口设置")
        g_serial = QGridLayout()

        g_serial.addWidget(QLabel("串口:"), 0, 0)
        self._com_combo = QComboBox()
        self._com_combo.addItems(self._uart.uartComAvailable)
        g_serial.addWidget(self._com_combo, 0, 1)

        self._refresh_btn = QPushButton("刷新")
        self._refresh_btn.clicked.connect(self._on_refresh_com)
        g_serial.addWidget(self._refresh_btn, 0, 2)

        self._bps_check = QCheckBox("9600波特率")
        g_serial.addWidget(self._bps_check, 1, 0, 1, 2)

        self._com_btn = QPushButton("打开串口")
        self._com_btn.clicked.connect(self._on_com_clicked)
        g_serial.addWidget(self._com_btn, 2, 0, 1, 3)

        group_serial.setLayout(g_serial)
        t1_layout.addWidget(group_serial)

        # Protocol test group
        group_test = QGroupBox("协议测试")
        g_test = QGridLayout()

        self._test_btn = QPushButton("发送测试指令")
        self._test_btn.clicked.connect(self._on_test)
        g_test.addWidget(self._test_btn, 0, 0)

        self._set_time_btn = QPushButton("设置时间")
        self._set_time_btn.clicked.connect(self._on_set_time)
        g_test.addWidget(self._set_time_btn, 0, 1)

        group_test.setLayout(g_test)
        t1_layout.addWidget(group_test)

        t1_layout.addStretch()

        # ---- Tab 2: OTA升级 ----
        tab2 = QWidget()
        t2_layout = QVBoxLayout(tab2)

        g_ota = QGridLayout()
        g_ota.addWidget(QLabel("固件文件:"), 0, 0)
        self._file_label = QLabel("")
        g_ota.addWidget(self._file_label, 0, 1)
        open_btn = QPushButton("选择文件")
        open_btn.clicked.connect(self._on_open_file)
        g_ota.addWidget(open_btn, 0, 2)

        g_ota.addWidget(QLabel("固件名称:"), 1, 0)
        self._fw_name = QLineEdit()
        g_ota.addWidget(self._fw_name, 1, 1, 1, 2)

        self._upgrade_btn = QPushButton("开始OTA升级")
        self._upgrade_btn.clicked.connect(self._on_upgrade)
        g_ota.addWidget(self._upgrade_btn, 2, 0, 1, 3)

        self._progress_bar = QProgressBar()
        g_ota.addWidget(self._progress_bar, 3, 0, 1, 3)

        t2_layout.addLayout(g_ota)
        t2_layout.addStretch()

        # ---- Tab 3: 文件传输 ----
        tab3 = QWidget()
        t3_layout = QVBoxLayout(tab3)

        g_trans = QGridLayout()
        g_trans.addWidget(QLabel("文件:"), 0, 0)
        self._trans_file_label = QLabel("")
        g_trans.addWidget(self._trans_file_label, 0, 1)
        trans_open_btn = QPushButton("选择文件")
        trans_open_btn.clicked.connect(self._on_trans_file_open)
        g_trans.addWidget(trans_open_btn, 0, 2)
        trans_folder_btn = QPushButton("选择文件夹")
        trans_folder_btn.clicked.connect(self._on_trans_folder_open)
        g_trans.addWidget(trans_folder_btn, 0, 3)

        g_trans.addWidget(QLabel("设备号:"), 1, 0)
        self._dev_no = QLineEdit("0")
        g_trans.addWidget(self._dev_no, 1, 1, 1, 3)

        g_trans.addWidget(QLabel("偏移地址:"), 2, 0)
        self._file_offset = QLineEdit("0")
        g_trans.addWidget(self._file_offset, 2, 1, 1, 3)

        self._trans_btn = QPushButton("开始传输")
        self._trans_btn.clicked.connect(self._on_trans_file)
        g_trans.addWidget(self._trans_btn, 3, 0)

        self._stop_trans_btn = QPushButton("停止传输")
        self._stop_trans_btn.clicked.connect(self._on_stop_trans_file)
        g_trans.addWidget(self._stop_trans_btn, 3, 1, 1, 2)

        t3_layout.addLayout(g_trans)
        t3_layout.addStretch()

        # ---- Tab 4: Xmodem/Ymodem ----
        tab4 = QWidget()
        t4_layout = QVBoxLayout(tab4)

        # Xmodem section
        group_xm = QGroupBox("Xmodem-128 (128字节块 + checksum)")
        g_xm = QGridLayout()
        g_xm.addWidget(QLabel("文件:"), 0, 0)
        self._xmodem_file_label = QLabel("")
        g_xm.addWidget(self._xmodem_file_label, 0, 1)
        xm_open_btn = QPushButton("选择文件")
        xm_open_btn.clicked.connect(self._on_xmodem_open_file)
        g_xm.addWidget(xm_open_btn, 0, 2)

        self._xmodem_progress = QProgressBar()
        g_xm.addWidget(self._xmodem_progress, 1, 0, 1, 3)

        xm_hbox = QHBoxLayout()
        xm_send_btn = QPushButton("开始发送")
        xm_send_btn.clicked.connect(self._on_xmodem_send)
        xm_hbox.addWidget(xm_send_btn)
        xm_cancel_btn = QPushButton("取消")
        xm_cancel_btn.clicked.connect(self._on_xmodem_cancel)
        xm_hbox.addWidget(xm_cancel_btn)
        g_xm.addLayout(xm_hbox, 2, 0, 1, 3)
        group_xm.setLayout(g_xm)
        t4_layout.addWidget(group_xm)

        # Ymodem section
        group_ym = QGroupBox("Ymodem-1K (1K块 + CRC16, 带文件名)")
        g_ym = QGridLayout()
        g_ym.addWidget(QLabel("文件:"), 0, 0)
        self._ymodem_file_label = QLabel("")
        g_ym.addWidget(self._ymodem_file_label, 0, 1)
        ym_open_btn = QPushButton("选择文件")
        ym_open_btn.clicked.connect(self._on_ymodem_open_file)
        g_ym.addWidget(ym_open_btn, 0, 2)

        self._ymodem_progress = QProgressBar()
        g_ym.addWidget(self._ymodem_progress, 1, 0, 1, 3)

        ym_hbox = QHBoxLayout()
        ym_send_btn = QPushButton("开始发送")
        ym_send_btn.clicked.connect(self._on_ymodem_send)
        ym_hbox.addWidget(ym_send_btn)
        ym_cancel_btn = QPushButton("取消")
        ym_cancel_btn.clicked.connect(self._on_ymodem_cancel)
        ym_hbox.addWidget(ym_cancel_btn)
        g_ym.addLayout(ym_hbox, 2, 0, 1, 3)
        group_ym.setLayout(g_ym)
        t4_layout.addWidget(group_ym)

        t4_layout.addStretch()

        # ---- Tab 5: 设备信息 ----
        tab5 = QWidget()
        t5_layout = QVBoxLayout(tab5)

        g_info = QGridLayout()

        self._get_uid_btn = QPushButton("获取UID")
        self._get_uid_btn.clicked.connect(self._on_get_uid)
        g_info.addWidget(self._get_uid_btn, 0, 0, 1, 2)

        g_info.addWidget(QLabel("Orval:"), 1, 0)
        self._orval_edit = QLineEdit("0")
        g_info.addWidget(self._orval_edit, 1, 1)

        self._set_sn_btn = QPushButton("写入SN")
        self._set_sn_btn.clicked.connect(self._on_set_sn)
        g_info.addWidget(self._set_sn_btn, 2, 0, 1, 2)

        self._get_dev_info_btn = QPushButton("获取设备信息")
        self._get_dev_info_btn.clicked.connect(self._on_get_device_info)
        g_info.addWidget(self._get_dev_info_btn, 3, 0, 1, 2)

        t5_layout.addLayout(g_info)
        t5_layout.addStretch()

        # ---- Tab 6: 参数调节 ----
        tab6 = QWidget()
        t6_layout = QVBoxLayout(tab6)

        # Instruction label
        instructions = QLabel(
            "<b>BabyOS参数调节</b> — 通过Shell命令读写MCU运行时变量<br/>"
            "用法: param [name [value]]<br/>"
            "&nbsp;&nbsp;param &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;— 列出所有参数<br/>"
            "&nbsp;&nbsp;param &lt;name&gt; &nbsp;&nbsp;&nbsp;— 读取参数<br/>"
            "&nbsp;&nbsp;param &lt;name&gt; &lt;val&gt; — 设置参数"
        )
        instructions.setStyleSheet("color: #555; padding: 5px;")
        t6_layout.addWidget(instructions)

        # Control panel
        g_param = QGridLayout()

        g_param.addWidget(QLabel("参数名称:"), 0, 0)
        self._param_name = QLineEdit()
        self._param_name.setPlaceholderText("输入参数名，如 g_test_value")
        g_param.addWidget(self._param_name, 0, 1)

        g_param.addWidget(QLabel("参数值:"), 1, 0)
        self._param_value = QLineEdit()
        self._param_value.setPlaceholderText("输入要设置的值（仅设置时需要）")
        g_param.addWidget(self._param_value, 1, 1)

        # Buttons row
        btn_list = QPushButton("列出全部")
        btn_list.clicked.connect(self._on_param_list)
        g_param.addWidget(btn_list, 2, 0)

        btn_get = QPushButton("读取参数")
        btn_get.clicked.connect(self._on_param_get)
        g_param.addWidget(btn_get, 2, 1)

        btn_set = QPushButton("设置参数")
        btn_set.clicked.connect(self._on_param_set)
        g_param.addWidget(btn_set, 2, 2)

        t6_layout.addLayout(g_param)

        # Raw command input
        raw_hbox = QHBoxLayout()
        raw_hbox.addWidget(QLabel("自定义命令:"))
        self._param_raw_input = QLineEdit()
        self._param_raw_input.setPlaceholderText("输入自定义Shell命令（不含换行）")
        raw_hbox.addWidget(self._param_raw_input)
        btn_raw_send = QPushButton("发送")
        btn_raw_send.clicked.connect(self._on_param_send_raw)
        raw_hbox.addWidget(btn_raw_send)
        t6_layout.addLayout(raw_hbox)

        # Output area
        output_label = QLabel("响应输出:")
        t6_layout.addWidget(output_label)
        self._param_output = QTextBrowser()
        self._param_output.setMaximumHeight(200)
        self._param_output.setStyleSheet("background: #f5f5f5; font-family: monospace;")
        t6_layout.addWidget(self._param_output)

        t6_layout.addStretch()

        # ---- Assemble tabs ----
        tabs.addTab(tab1, "串口控制")
        tabs.addTab(tab2, "OTA升级")
        tabs.addTab(tab3, "文件传输")
        tabs.addTab(tab4, "Xmodem/Ymodem")
        tabs.addTab(tab5, "设备信息")
        tabs.addTab(tab6, "参数调节")
        layout.addWidget(tabs)

        # ---- Bottom: Log + controls ----
        bottom = QHBoxLayout()
        self._encrypt_box = QCheckBox("加密传输")
        bottom.addWidget(self._encrypt_box)

        clear_btn = QPushButton("清空日志")
        clear_btn.clicked.connect(self._on_clear)
        bottom.addWidget(clear_btn)

        bottom.addStretch()
        layout.addLayout(bottom)

        self._rec_text = QTextEdit()
        self._rec_text.setReadOnly(True)
        self._rec_text.setMaximumHeight(200)
        layout.addWidget(self._rec_text)
