#!/usr/bin/env python3
"""
BabyOS 配网 Web 调试工具 — Tab 插件

被 mainwindow.py 嵌入到主窗口的 "配网Web调试" Tab 中。
提供: 一键 编译 + 烧录 + 抓日志,以及单步执行 build/flash/log。

依赖: pyserial (pip install pyserial)
"""
import os
import re
import time
import threading
from datetime import datetime
import tkinter as tk
from tkinter import ttk, filedialog, scrolledtext


# ---- 默认配置 (会被 GUI 持久化覆盖) ----
DEFAULTS = {
    "project_dir": "",                       # 启动时自动定位 main.py 同级目录的父目录
    "keil_uv4": r"C:\Keil_v5\UV4\UV4.exe",
    "openocd_dir": r"C:\Users\lkl92\Documents\tools\OpenOCD-20240916-0.12.0",
    "openocd_scripts_subpath": "share\\openocd\\scripts",
    "stlink_cfg": "interface\\stlink.cfg",
    "target_cfg": "target\\stm32l4x.cfg",
    "project_file_rel": "Examples\\wifi\\MDK-ARM\\BearPi.uvprojx",
    "target_name": "BearPi",
    "log_dir_rel": "Doc",
}


class WebConfigTab:
    """配网 Web 调试 Tab 插件,作为 Mixin 挂到 MainWindow 上。

    期望 MainWindow 提供:
        - self._append_log(text: str)   # 日志追加到底部统一日志框
    """

    CFG_FILE = "webconfig_tool.ini"

    def _build_tab_webconfig(self):
        """在主窗口中构建 '配网Web调试' Tab。
        在 MainWindow.__init__() 末尾通过 notebook.add() 调用结果。
        """
        tab = ttk.Frame(self)
        tab.columnconfigure(0, weight=1)
        tab.rowconfigure(2, weight=1)

        # ---- 配置区 ----
        cfg = ttk.LabelFrame(tab, text="配置")
        cfg.grid(row=0, column=0, sticky='we', padx=6, pady=4)
        cfg.columnconfigure(1, weight=1)
        cfg.columnconfigure(4, weight=1)

        self._wc_vars = {}
        rows = [
            # (row, col, label, key, dir_picker)
            (0, 0, "工程根目录:", "project_dir", True),
            (0, 3, "Keil UV4:",   "keil_uv4", True),
            (1, 0, "OpenOCD 目录:", "openocd_dir", True),
            (1, 3, "uvprojx (相对):", "project_file_rel", False),
            (2, 0, "目标名:",      "target_name", False),
            (2, 3, "日志目录 (相对):", "log_dir_rel", False),
        ]
        for r, c, label, key, picker in rows:
            ttk.Label(cfg, text=label).grid(row=r, column=c, padx=4, pady=3, sticky='w')
            v = tk.StringVar(value=DEFAULTS[key])
            self._wc_vars[key] = v
            e = ttk.Entry(cfg, textvariable=v)
            e.grid(row=r, column=c + 1, sticky='we', padx=4, pady=3)
            if picker:
                ttk.Button(cfg, text="...", width=3,
                           command=lambda k=key: self._wc_pick_dir(k)).grid(
                    row=r, column=c + 2, padx=2)

        # ---- 按钮区 ----
        bar = ttk.Frame(tab)
        bar.grid(row=1, column=0, sticky='we', padx=6, pady=4)
        self._wc_btns = {}
        for name, label in [
            ("all",   "▶ 一键 (编译+烧录+抓日志)"),
            ("build", "⚙ 编译"),
            ("flash", "⚡ 烧录"),
            ("log",   "📡 抓日志"),
            ("stop",  "■ 停止"),
        ]:
            b = ttk.Button(bar, text=label, command=lambda n=name: self._wc_on_action(n))
            b.pack(side='left', padx=4)
            self._wc_btns[name] = b

        ttk.Button(bar, text="📂 打开日志目录", command=self._wc_open_log_dir).pack(side='right', padx=4)
        ttk.Button(bar, text="💾 保存配置", command=self._wc_save_settings).pack(side='right', padx=4)

        # ---- 日志区 (本 Tab 专用) ----
        log_frame = ttk.LabelFrame(tab, text="运行日志")
        log_frame.grid(row=2, column=0, sticky='nsew', padx=6, pady=4)
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)
        self._wc_log = scrolledtext.ScrolledText(log_frame, wrap='none', font=("Consolas", 10), height=18)
        self._wc_log.grid(row=0, column=0, sticky='nsew')

        # 状态
        self._wc_status = tk.StringVar(value="就绪")
        ttk.Label(tab, textvariable=self._wc_status, anchor='w', relief='sunken').grid(
            row=3, column=0, sticky='we', padx=6, pady=2)

        # 任务队列 & 串口停止信号
        self._wc_jobs = []
        self._wc_serial_stop = threading.Event()

        # 加载持久化配置
        self._wc_load_settings()

        # 启动时按钮状态
        self._wc_set_buttons_busy(False)
        return tab

    # ------------------------------------------------------------------
    # 配置持久化
    # ------------------------------------------------------------------
    def _wc_cfg_path(self):
        # 与 main.py 同级目录
        here = os.path.dirname(os.path.abspath(__file__))
        return os.path.join(here, self.CFG_FILE)

    def _wc_load_settings(self):
        path = self._wc_cfg_path()
        if not os.path.isfile(path):
            # 默认 project_dir = BabyOS 仓库根 (main.py 在 BabyOS/tool/)
            default_proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            default_proj = os.path.dirname(default_proj)  # 上两级 -> BabyOS_Example
            self._wc_vars["project_dir"].set(default_proj)
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith("#") or "=" not in line:
                        continue
                    k, v = line.split("=", 1)
                    if k in self._wc_vars:
                        self._wc_vars[k].set(v.strip())
        except Exception as e:
            self._wc_log_msg(f"[warn] 读取配置失败: {e}\n")

    def _wc_save_settings(self):
        path = self._wc_cfg_path()
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write("# BabyOS 配网 Web 调试 Tab 配置\n")
                for k, v in self._wc_vars.items():
                    f.write(f"{k}={v.get()}\n")
            self._wc_log_msg(f"[ok] 配置已保存: {path}\n")
        except Exception as e:
            self._wc_log_msg(f"[error] 保存配置失败: {e}\n")

    # ------------------------------------------------------------------
    # 日志
    # ------------------------------------------------------------------
    def _wc_log_msg(self, msg):
        # 写到本 Tab 自己的日志框
        self._wc_log.insert('end', msg)
        self._wc_log.see('end')
        # 同步到主窗口的统一日志框 (脱掉末尾换行再由 _append_log 加时间戳)
        if hasattr(self, "_append_log"):
            tail = msg.rstrip("\n")
            for ln in tail.split("\n"):
                self._append_log(f"[webconfig] {ln}")

    # ------------------------------------------------------------------
    # 按钮
    # ------------------------------------------------------------------
    def _wc_pick_dir(self, key):
        cur = self._wc_vars[key].get()
        d = filedialog.askdirectory(initialdir=cur or os.getcwd(), title=f"选择 {key}")
        if d:
            self._wc_vars[key].set(d)

    def _wc_on_action(self, name):
        if name == "stop":
            self._wc_serial_stop.set()
            self._wc_log_msg("[stop] 已请求停止串口读取\n")
            return
        steps = ["build", "flash", "log"] if name == "all" else [name]
        runners = {
            "build": self._wc_run_build,
            "flash": self._wc_run_flash,
            "log":   self._wc_run_log,
        }
        for s in steps:
            self._wc_jobs.append((s, runners[s]))
        self._wc_pump()

    def _wc_pump(self):
        if not self._wc_jobs:
            self._wc_set_buttons_busy(False)
            self._wc_status.set("就绪")
            return
        name, runner = self._wc_jobs[0]
        self._wc_set_buttons_busy(True)
        self._wc_status.set(f"执行中: {name}")
        self._wc_log_msg(f"\n===== {name} =====\n")

        def work():
            ok = False
            try:
                ok = runner(self._wc_log_msg)
            except Exception as e:
                self._wc_log_msg(f"[exception] {e}\n")
            self.after(50, self._wc_after_job, name, ok)

        threading.Thread(target=work, daemon=True).start()

    def _wc_after_job(self, name, ok):
        self._wc_log_msg(f"[done] {name}: {'OK' if ok else 'FAIL'}\n")
        if not ok:
            self._wc_jobs.clear()
            self._wc_set_buttons_busy(False)
            self._wc_status.set(f"失败: {name}")
            return
        self._wc_jobs.pop(0)
        self._wc_pump()

    def _wc_set_buttons_busy(self, busy):
        for n, b in self._wc_btns.items():
            if n == "stop":
                b.state(["!disabled"] if busy else ["disabled"])
            else:
                b.state(["disabled"] if busy else ["!disabled"])

    # ------------------------------------------------------------------
    # 步骤实现
    # ------------------------------------------------------------------
    def _wc_cfg(self):
        return {k: v.get() for k, v in self._wc_vars.items()}

    def _wc_run_build(self, log):
        cfg = self._wc_cfg()
        keil = cfg["keil_uv4"]
        proj = os.path.join(cfg["project_dir"], cfg["project_file_rel"])
        target = cfg["target_name"]
        out_dir = os.path.join(os.path.dirname(proj), target)
        os.makedirs(out_dir, exist_ok=True)
        log_file = os.path.join(out_dir, "build_log.txt")
        hex_file = os.path.join(out_dir, f"{target}.hex")

        if not os.path.isfile(keil):
            log(f"[error] Keil 未找到: {keil}\n"); return False
        if not os.path.isfile(proj):
            log(f"[error] 工程文件未找到: {proj}\n"); return False

        log(f"[build] keil={keil}\n")
        log(f"[build] project={proj}\n")
        hx_before = os.path.getmtime(hex_file) if os.path.isfile(hex_file) else 0

        cwd = os.path.dirname(proj)
        args = ["-j0", "-r", f'"{proj}"', f"-t{target}", "-o", f'"{log_file}"']
        import subprocess
        try:
            subprocess.run([keil, *args], cwd=cwd, check=False)
        except FileNotFoundError as e:
            log(f"[error] 启动 Keil 失败: {e}\n"); return False

        if not os.path.isfile(hex_file):
            log(f"[error] 编译失败: 未生成 {hex_file}\n"); return False
        if hx_before and os.path.getmtime(hex_file) <= hx_before:
            log(f"[error] 编译失败: hex 未更新\n"); return False
        if os.path.isfile(log_file):
            try:
                with open(log_file, "r", errors="ignore") as f:
                    txt = f.read()
                m = re.search(r"(\d+)\s*Error\(s\)", txt)
                if m and m.group(1) != "0":
                    log(f"[error] 编译报告 {m.group(1)} 错误\n"); return False
            except Exception:
                pass
        log(f"[build] OK: {hex_file}\n")
        return True

    def _wc_run_flash(self, log):
        cfg = self._wc_cfg()
        proj = os.path.join(cfg["project_dir"], cfg["project_file_rel"])
        target = cfg["target_name"]
        out_dir = os.path.join(os.path.dirname(proj), target)
        hex_file = os.path.join(out_dir, f"{target}.hex")
        oc_dir = cfg["openocd_dir"]
        oc_exe = os.path.join(oc_dir, "bin", "openocd.exe")
        scripts = os.path.join(oc_dir, cfg["openocd_scripts_subpath"])
        if1 = os.path.join(scripts, cfg["stlink_cfg"])
        if2 = os.path.join(scripts, cfg["target_cfg"])

        for p in (oc_exe, hex_file, if1, if2):
            if not os.path.isfile(p):
                log(f"[error] 缺少文件: {p}\n"); return False

        log(f"[flash] {hex_file}\n")
        cmd = f'"{oc_exe}" -f "{if1}" -f "{if2}" -c "program \\"{hex_file}\\" verify reset exit"'
        import subprocess
        log(f"[flash] $ {cmd}\n")
        rc = subprocess.call(cmd, shell=True)
        if rc != 0:
            log(f"[error] OpenOCD 退出码 {rc}\n"); return False
        log("[flash] OK\n")
        return True

    def _wc_run_log(self, log):
        try:
            import serial
        except ImportError:
            log("[error] pyserial 未安装,  请运行:  pip install pyserial\n")
            return False

        cfg = self._wc_cfg()
        port = cfg.get("serial_port", "COM4")
        baud = int(cfg.get("serial_baud", "115200"))
        seconds = int(cfg.get("log_seconds", "30"))

        # 串口/波特率/时长: 如果用户没在配置区暴露, 直接用 mainwindow 的串口设置
        if hasattr(self, "_com_combo") and self._uart and self._uart.uartGetOpenStatus():
            port = self._com_combo.get()
            bps = 9600 if self._bps_var.get() else 115200
            baud = bps

        log_dir = os.path.join(cfg["project_dir"], cfg["log_dir_rel"])
        os.makedirs(log_dir, exist_ok=True)
        out_file = os.path.join(log_dir, f"auto_log_{datetime.now():%Y%m%d_%H%M%S}.txt")

        log(f"[log] port={port} baud={baud} seconds={seconds}\n")
        log(f"[log] -> {out_file}\n")

        try:
            ser = serial.Serial(port, baud, timeout=1)
        except Exception as e:
            log(f"[error] 打开串口失败: {e}\n"); return False

        self._wc_serial_stop.clear()
        buf = bytearray()
        deadline = time.time() + seconds
        try:
            while time.time() < deadline:
                if self._wc_serial_stop.is_set():
                    break
                chunk = ser.read(1024)
                if chunk:
                    buf.extend(chunk)
                    log(chunk.decode("utf-8", errors="replace"))
        finally:
            ser.close()

        try:
            with open(out_file, "wb") as f:
                f.write(buf)
        except Exception as e:
            log(f"[warn] 写文件失败: {e}\n")
        log(f"[log] 保存 {len(buf)} 字节\n")
        return True

    def _wc_open_log_dir(self):
        cfg = self._wc_cfg()
        d = os.path.join(cfg["project_dir"], cfg["log_dir_rel"])
        os.makedirs(d, exist_ok=True)
        try:
            os.startfile(d)
        except Exception as e:
            self._wc_log_msg(f"[error] 打开日志目录失败: {e}\n")