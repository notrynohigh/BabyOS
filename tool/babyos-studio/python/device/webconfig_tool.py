"""
webconfig_tool — 配网 Web 调试 host tooling (BabyOS config-web firmware).

Parity with origin/dev tool/webconfig_tool.py:
  - Configurable project_dir / Keil UV4 / OpenOCD / uvprojx / target / log_dir
  - build(): invoke UV4 (or `make` if project_dir/Makefile exists)
  - flash(): invoke OpenOCD program {hex} verify reset exit
  - log():  pyserial capture to auto_log_*.txt under log_dir
  - status(): resolved paths + tool availability

Host engineering tool — not compiled into firmware. Config persisted in
webconfig_tool.ini next to this module (or BABYOS_WEBCONFIG_INI env).
Python 3.8 compatible.
"""

from __future__ import print_function

import os
import re
import subprocess
import threading
import time
from datetime import datetime
from typing import Any, Dict, List, Optional

try:
    import configparser
except ImportError:  # pragma: no cover - py2 fallback unused
    configparser = None

DEFAULTS = {
    'project_dir': '',
    'keil_uv4': r'C:\Keil_v5\UV4\UV4.exe',
    'openocd_dir': '',
    'openocd_scripts_subpath': os.path.join('share', 'openocd', 'scripts'),
    'stlink_cfg': os.path.join('interface', 'stlink.cfg'),
    'target_cfg': os.path.join('target', 'stm32l4x.cfg'),
    'project_file_rel': '',
    'target_name': 'BabyOS',
    'log_dir_rel': 'Doc',
    'serial_port': '',
    'serial_baud': '115200',
    'log_seconds': '30',
}

_INI_NAME = 'webconfig_tool.ini'
_INI_SECTIONS = {
    'paths': ['project_dir', 'keil_uv4', 'openocd_dir',
              'openocd_scripts_subpath', 'stlink_cfg', 'target_cfg',
              'project_file_rel', 'log_dir_rel'],
    'build': ['target_name'],
    'log': ['serial_port', 'serial_baud', 'log_seconds'],
}


def _default_ini_path() -> str:
    env = (os.environ.get('BABYOS_WEBCONFIG_INI') or '').strip()
    if env:
        return env
    try:
        here = os.path.dirname(os.path.abspath(__file__))
    except Exception:
        here = os.getcwd()
    return os.path.join(here, _INI_NAME)


class WebConfigTool:
    """Host-side 配网Web调试: build / flash / serial log."""

    def __init__(self, config_path: Optional[str] = None) -> None:
        self.config_path = config_path or _default_ini_path()
        self.cfg: Dict[str, str] = dict(DEFAULTS)
        self.load_config()
        self._log_lines: List[str] = []
        self._log_lock = threading.Lock()
        self._serial_lock = threading.Lock()
        self._serial_stop = threading.Event()
        self._serial_thread: Optional[threading.Thread] = None
        self._serial_path = ''
        self._serial_info: Dict[str, Any] = {}
        self._busy = False

    # ------------------------------------------------------------------
    # config
    # ------------------------------------------------------------------

    def load_config(self) -> None:
        self.cfg = dict(DEFAULTS)
        path = self.config_path
        if not path or not os.path.isfile(path):
            return
        if configparser is None:
            return
        cp = configparser.ConfigParser()
        try:
            cp.read(path, encoding='utf-8')
        except Exception:
            return
        for section, keys in _INI_SECTIONS.items():
            if not cp.has_section(section):
                continue
            for key in keys:
                if cp.has_option(section, key):
                    self.cfg[key] = cp.get(section, key)

    def save_config(self, updates: Optional[Dict[str, Any]] = None) -> None:
        """Persist config. None = keep current; '' = clear (path/port keys)."""
        if updates:
            for k, v in updates.items():
                if v is None:
                    continue
                self.cfg[k] = str(v)
            # Absolute-ize relative project_dir so Keil cwd + make -C agree
            pd = (self.cfg.get('project_dir') or '').strip()
            if pd and not os.path.isabs(pd):
                self.cfg['project_dir'] = os.path.abspath(pd)
        if configparser is None:
            return
        cp = configparser.ConfigParser()
        for section, keys in _INI_SECTIONS.items():
            cp.add_section(section)
            for key in keys:
                cp.set(section, key, str(self.cfg.get(key, '')))
        d = os.path.dirname(os.path.abspath(self.config_path))
        if d and not os.path.isdir(d):
            os.makedirs(d, exist_ok=True)
        with open(self.config_path, 'w', encoding='utf-8') as f:
            cp.write(f)

    def _log(self, msg: str) -> None:
        line = '[%s] %s' % (time.strftime('%H:%M:%S'), msg)
        with self._log_lock:
            self._log_lines.append(line)
            if len(self._log_lines) > 500:
                del self._log_lines[:len(self._log_lines) - 500]

    def get_logs(self, tail: int = 100) -> List[str]:
        with self._log_lock:
            if tail <= 0:
                return list(self._log_lines)
            return list(self._log_lines[-tail:])

    def _project_abs(self) -> str:
        rel = (self.cfg.get('project_file_rel') or '').strip()
        root = (self.cfg.get('project_dir') or '').strip()
        if rel and os.path.isabs(rel):
            return rel
        if root and rel:
            # root may be relative to the server cwd; resolve both
            if not os.path.isabs(root):
                root = os.path.abspath(root)
            return os.path.join(root, rel)
        return rel

    def _root_abs(self) -> str:
        root = (self.cfg.get('project_dir') or '').strip()
        return os.path.abspath(root) if root else ''

    def _out_dir(self) -> str:
        """Build/flash output dir: project_dir/<target_name> (make fallback) or
        dirname(project_file)/<target_name> (Keil)."""
        target = (self.cfg.get('target_name') or 'BabyOS').strip()
        proj = self._project_abs()
        root = self._root_abs()
        if proj:
            return os.path.join(os.path.dirname(os.path.abspath(proj)), target)
        if root:
            return os.path.join(root, target)
        return ''

    def _hex_path(self) -> str:
        out_dir = self._out_dir()
        if not out_dir:
            return ''
        target = (self.cfg.get('target_name') or 'BabyOS').strip()
        return os.path.join(out_dir, '%s.hex' % target)

    def _build_log_path(self) -> str:
        out_dir = self._out_dir()
        if not out_dir:
            return ''
        return os.path.join(out_dir, 'build_log.txt')

    @staticmethod
    def _parse_error_count(log_file: str) -> Optional[int]:
        """Parse 'N Error(s)' from a UV4/build log. Returns None if absent."""
        if not log_file or not os.path.isfile(log_file):
            return None
        try:
            with open(log_file, 'r', errors='ignore') as f:
                txt = f.read()
            m = re.search(r'(\d+)\s*Error\(s\)', txt)
            if m:
                return int(m.group(1))
        except Exception:
            return None
        return None

    def _reset_build_log(self, log_file: str) -> None:
        """Truncate prior build_log so leftover 'N Error(s)' cannot poison
        a fresh successful build."""
        if not log_file:
            return
        try:
            d = os.path.dirname(os.path.abspath(log_file))
            if d and not os.path.isdir(d):
                os.makedirs(d, exist_ok=True)
            with open(log_file, 'w', encoding='utf-8'):
                pass
        except Exception:
            pass

    def status(self) -> Dict[str, Any]:
        keil = self.cfg.get('keil_uv4') or ''
        proj = self._project_abs()
        oc_dir = (self.cfg.get('openocd_dir') or '').strip()
        oc_exe = ''
        if oc_dir:
            oc_root = oc_dir if os.path.isabs(oc_dir) else os.path.abspath(oc_dir)
            oc_exe = os.path.join(oc_root, 'bin', 'openocd.exe')
            if not os.path.isfile(oc_exe):
                oc_exe = os.path.join(oc_root, 'bin', 'openocd')
        makefile = ''
        root = self._root_abs()
        if root and os.path.isfile(os.path.join(root, 'Makefile')):
            makefile = os.path.join(root, 'Makefile')
        serial = None
        try:
            import serial  # noqa: F401
            serial = True
        except ImportError:
            serial = False
        keil_ok = bool(keil) and os.path.isfile(keil)
        proj_ok = bool(proj) and os.path.isfile(proj)
        return {
            'config_path': self.config_path,
            'config_loaded': os.path.isfile(self.config_path),
            'cfg': dict(self.cfg),
            'project_dir': root,
            'keil_uv4_exists': keil_ok,
            'project_file': proj,
            'project_exists': proj_ok,
            'hex_file': self._hex_path(),
            'openocd_exe': oc_exe,
            'openocd_exists': bool(oc_exe) and os.path.isfile(oc_exe),
            'makefile': makefile,
            'makefile_exists': bool(makefile),
            'pyserial': serial,
            'busy': self._busy,
            'serial_log': self._serial_info,
            'can_build': (keil_ok and proj_ok) or bool(makefile),
            'can_flash': bool(oc_exe and os.path.isfile(oc_exe)
                              and os.path.isfile(self._hex_path())),
        }

    # ------------------------------------------------------------------
    # build / flash / log
    # ------------------------------------------------------------------

    def build(self, timeout_sec: int = 300) -> Dict[str, Any]:
        """Compile config-web firmware.

        Preference matches docstring: Keil UV4 when keil+proj exist; make
        only as fallback. A configured-but-missing Keil still routes to
        _run_keil so the error is 'keil not found', not a silent make path.
        """
        if self._busy:
            return {'ok': False, 'error': 'tool busy', 'logs': self.get_logs()}
        self._busy = True
        try:
            st = self.status()
            self._log('===== build start =====')
            keil_ready = bool(st.get('keil_uv4_exists')
                               and st.get('project_exists'))
            if keil_ready:
                return self._run_keil(st, timeout_sec)
            if st['makefile_exists'] and st.get('project_dir'):
                return self._run_make(st, timeout_sec)
            if (self.cfg.get('keil_uv4') or '').strip():
                return self._run_keil(st, timeout_sec)
            return self._run_make(st, timeout_sec)
        finally:
            self._busy = False

    def _run_keil(self, st: Dict[str, Any], timeout_sec: int) -> Dict[str, Any]:
        keil = self.cfg.get('keil_uv4') or ''
        proj = st['project_file']
        target = (self.cfg.get('target_name') or 'BabyOS').strip()
        if not keil or not os.path.isfile(keil):
            self._log('[error] Keil 未找到: %s' % keil)
            return {'ok': False, 'error': 'keil not found: %s' % keil,
                    'logs': self.get_logs()}
        if not proj or not os.path.isfile(proj):
            self._log('[error] 工程文件未找到: %s' % proj)
            return {'ok': False, 'error': 'project not found: %s' % proj,
                    'logs': self.get_logs()}
        proj = os.path.abspath(proj)
        out_dir = os.path.dirname(st['hex_file'] or '')
        try:
            if out_dir and not os.path.isdir(out_dir):
                os.makedirs(out_dir, exist_ok=True)
        except Exception as exc:
            return {'ok': False, 'error': str(exc), 'logs': self.get_logs()}
        log_file = self._build_log_path()
        if log_file and not os.path.isabs(log_file):
            log_file = os.path.abspath(log_file)
        hex_file = st['hex_file']
        hx_before = os.path.getmtime(hex_file) if os.path.isfile(hex_file) else 0
        cwd = os.path.dirname(proj)
        args = ['-j0', '-r', proj, '-t' + target, '-o', log_file]
        self._log('[build] keil=%s project=%s target=%s' % (keil, proj, target))
        self._reset_build_log(log_file)
        try:
            subprocess.run([keil] + args, cwd=cwd, check=False,
                           timeout=timeout_sec)
        except (OSError, subprocess.SubprocessError) as exc:
            self._log('[error] 启动 Keil 失败: %s' % exc)
            return {'ok': False, 'error': str(exc), 'logs': self.get_logs()}
        if not os.path.isfile(hex_file):
            self._log('[error] 编译失败: 未生成 %s' % hex_file)
            return {'ok': False, 'error': 'hex not produced', 'hex': hex_file,
                    'logs': self.get_logs()}
        # Keil stale_hex contract: UV4 may report 0 Error(s) without rewriting
        # hex (fake/stub tooling). Keep mtime check for Keil.
        if hx_before and os.path.getmtime(hex_file) <= hx_before:
            self._log('[error] 编译失败: hex 未更新')
            return {'ok': False, 'error': 'hex not updated', 'hex': hex_file,
                    'logs': self.get_logs()}
        err_count = self._parse_error_count(log_file)
        if err_count is not None and err_count != 0:
            self._log('[error] 编译报告 %s 错误' % err_count)
            return {'ok': False,
                    'error': 'build reported %s error(s)' % err_count,
                    'build_log': log_file, 'logs': self.get_logs()}
        self._log('[build] OK: %s' % hex_file)
        return {'ok': True, 'hex': hex_file, 'build_log': log_file,
                'tool': 'keil', 'logs': self.get_logs()}

    def _run_make(self, st: Dict[str, Any], timeout_sec: int) -> Dict[str, Any]:
        root = self._root_abs() or (self.cfg.get('project_dir') or '').strip()
        self._log('[build] make project_dir=%s' % root)
        hex_file = st.get('hex_file') or self._hex_path()
        log_file = st.get('build_log') or self._build_log_path()
        if log_file and not os.path.isabs(log_file):
            log_file = os.path.abspath(log_file)
        self._reset_build_log(log_file)
        try:
            proc = subprocess.run(['make', '-C', root], check=False,
                                  timeout=timeout_sec,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
        except (OSError, subprocess.SubprocessError) as exc:
            self._log('[error] make 失败: %s' % exc)
            return {'ok': False, 'error': str(exc), 'logs': self.get_logs()}
        out = (proc.stdout or b'').decode('utf-8', errors='replace')
        for line in out.splitlines()[-40:]:
            self._log(line)
        if proc.returncode != 0:
            self._log('[error] make 退出码 %s' % proc.returncode)
            return {'ok': False, 'error': 'make rc=%s' % proc.returncode,
                    'logs': self.get_logs()}
        if not hex_file or not os.path.isfile(hex_file):
            self._log('[error] 编译失败: 未生成 %s' % hex_file)
            return {'ok': False, 'error': 'hex not produced', 'hex': hex_file,
                    'logs': self.get_logs()}
        # make incremental no-op is a valid success when rc=0 and hex exists.
        err_count = self._parse_error_count(log_file)
        if err_count is not None and err_count != 0:
            self._log('[error] 编译报告 %s 错误' % err_count)
            return {'ok': False,
                    'error': 'build reported %s error(s)' % err_count,
                    'build_log': log_file, 'logs': self.get_logs()}
        self._log('[build] make OK')
        return {'ok': True, 'tool': 'make', 'rc': 0, 'hex': hex_file,
                'build_log': log_file, 'logs': self.get_logs()}

    def flash(self, timeout_sec: int = 120) -> Dict[str, Any]:
        """Flash hex via OpenOCD (dev tool parity)."""
        if self._busy:
            return {'ok': False, 'error': 'tool busy', 'logs': self.get_logs()}
        self._busy = True
        try:
            return self._flash_locked(timeout_sec)
        finally:
            self._busy = False

    def _flash_locked(self, timeout_sec: int) -> Dict[str, Any]:
        st = self.status()
        oc_exe = st['openocd_exe']
        hex_file = st['hex_file']
        oc_dir = self.cfg.get('openocd_dir') or ''
        scripts = os.path.join(oc_dir, self.cfg.get('openocd_scripts_subpath')
                               or 'share/openocd/scripts')
        if1 = os.path.join(scripts, self.cfg.get('stlink_cfg') or '')
        if2 = os.path.join(scripts, self.cfg.get('target_cfg') or '')
        self._log('===== flash start =====')
        for p in (oc_exe, hex_file, if1, if2):
            if not p or not os.path.isfile(p):
                self._log('[error] 缺少文件: %s' % p)
                return {'ok': False, 'error': 'missing file: %s' % p,
                        'logs': self.get_logs()}
        self._log('[flash] %s' % hex_file)
        cmd = [oc_exe, '-f', if1, '-f', if2, '-c',
               'program "%s" verify reset exit' % hex_file]
        self._log('[flash] $ %s' % ' '.join(cmd))
        try:
            proc = subprocess.run(cmd, check=False, timeout=timeout_sec,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
        except (OSError, subprocess.SubprocessError) as exc:
            self._log('[error] OpenOCD 启动失败: %s' % exc)
            return {'ok': False, 'error': str(exc), 'logs': self.get_logs()}
        out = (proc.stdout or b'').decode('utf-8', errors='replace')
        for line in out.splitlines()[-30:]:
            self._log(line)
        if proc.returncode != 0:
            self._log('[error] OpenOCD 退出码 %s' % proc.returncode)
            return {'ok': False, 'error': 'openocd rc=%s' % proc.returncode,
                    'logs': self.get_logs()}
        self._log('[flash] OK')
        return {'ok': True, 'hex': hex_file, 'logs': self.get_logs()}

    def start_log(self, port: Optional[str] = None,
                  baud: Optional[int] = None,
                  seconds: Optional[int] = None,
                  out_path: Optional[str] = None) -> Dict[str, Any]:
        """Capture UART bytes to auto_log_*.txt (pyserial, dev tool parity)."""
        try:
            import serial
        except ImportError:
            return {'ok': False,
                    'error': 'pyserial 未安装，请运行 pip install pyserial',
                    'logs': self.get_logs()}
        if self._busy:
            return {'ok': False, 'error': 'tool busy', 'logs': self.get_logs()}
        with self._serial_lock:
            if self._serial_thread is not None and self._serial_thread.is_alive():
                return {'ok': False, 'error': 'serial log already running',
                        'logs': self.get_logs()}
            cfg = self.cfg
            port = (port or cfg.get('serial_port') or '').strip()
            try:
                baud = int(baud if baud is not None else cfg.get('serial_baud', 115200))
            except (TypeError, ValueError):
                baud = 115200
            try:
                seconds = int(seconds if seconds is not None else cfg.get('log_seconds', 30))
            except (TypeError, ValueError):
                seconds = 30
            if seconds < 1:
                seconds = 1
            if seconds > 600:
                seconds = 600
            if not port:
                return {'ok': False, 'error': 'serial port empty',
                        'logs': self.get_logs()}
            root = self._root_abs() or os.getcwd()
            log_dir = os.path.join(root, cfg.get('log_dir_rel') or 'Doc')
            try:
                if not os.path.isdir(log_dir):
                    os.makedirs(log_dir, exist_ok=True)
            except Exception as exc:
                return {'ok': False, 'error': str(exc), 'logs': self.get_logs()}
            if not out_path:
                out_path = os.path.join(
                    log_dir, 'auto_log_%s.txt' % datetime.now().strftime('%Y%m%d_%H%M%S'))
            # Open serial synchronously so a bad port is a structured error,
            # not a silent background failure.
            try:
                ser = serial.Serial(port, baud, timeout=1)
            except Exception as exc:
                self._log('[error] 打开串口失败: %s' % exc)
                return {'ok': False, 'error': str(exc), 'logs': self.get_logs()}
            self._log('[log] port=%s baud=%s seconds=%s -> %s' % (
                port, baud, seconds, out_path))
            self._serial_stop.clear()
            self._serial_path = out_path
            self._serial_info = {'port': port, 'baud': baud, 'seconds': seconds,
                                 'path': out_path, 'running': True, 'bytes': 0}

            def work() -> None:
                buf = bytearray()
                deadline = time.time() + seconds
                try:
                    while time.time() < deadline and not self._serial_stop.is_set():
                        chunk = ser.read(1024)
                        if chunk:
                            buf.extend(chunk)
                            self._serial_info['bytes'] = len(buf)
                            self._log(chunk.decode('utf-8', errors='replace'))
                finally:
                    try:
                        ser.close()
                    except Exception:
                        pass
                try:
                    with open(out_path, 'wb') as f:
                        f.write(buf)
                except Exception as exc:
                    self._serial_info['write_error'] = str(exc)
                    self._log('[warn] 写文件失败: %s' % exc)
                self._serial_info['running'] = False
                self._serial_info['bytes'] = len(buf)
                self._log('[log] 保存 %d 字节 -> %s' % (len(buf), out_path))

            self._serial_thread = threading.Thread(target=work, name='wc-log',
                                                   daemon=True)
            self._serial_thread.start()
            return {'ok': True, 'path': out_path, 'port': port, 'baud': baud,
                    'seconds': seconds, 'logs': self.get_logs()}

    def stop_log(self) -> Dict[str, Any]:
        self._serial_stop.set()
        with self._serial_lock:
            t = self._serial_thread
            self._serial_thread = None
        if t is not None and t.is_alive():
            t.join(timeout=3.0)
        if self._serial_info:
            self._serial_info['running'] = False
        return {'ok': True, 'path': self._serial_path,
                'info': self._serial_info, 'logs': self.get_logs()}

    def log_status(self) -> Dict[str, Any]:
        return dict(self._serial_info)
