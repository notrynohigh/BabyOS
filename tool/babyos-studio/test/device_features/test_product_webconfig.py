#!/usr/bin/env python3
"""
test_product_webconfig — product-grade WebConfigTool acceptance (SUCCESS + failure).

Authority:
  - python/device/webconfig_tool.py  (build / flash / serial log / INI config)
  - python/app/api/device.py          (/api/device/webconfig/* FastAPI routes)
  - python/device/device_manager.py   (DeviceManager.webconfig singleton)

Product paths exercised (real host tooling, no UI shells):
  1. build() SUCCESS  — fake UV4.exe / Makefile that writes hex + "0 Error(s)"
  2. build() FAILURE  — log has "3 Error(s)", hex missing, hex stale, tool missing
  3. flash() SUCCESS  — fake OpenOCD script accepts args, exits 0
  4. flash() FAILURE  — openocd / hex / cfg files missing (structured error)
  5. serial log       — real pyserial on a real pty: start → capture → stop → file
  6. save_config/load_config INI roundtrip (paths/build/log sections)
  7. FastAPI /api/device/webconfig/* return structured JSON + AppError codes

Hard rules:
  - NEW test file only; do not modify existing suites or bos/thirdparty.
  - Do not commit/push. Python 3.8 compatible.
  - Fake UV4/OpenOCD scripts are the REAL host-tool invocation path
    (subprocess + argv), not protocol stubs.

Run:
  cd /home/yyds/code/BabyOS/tool/babyos-studio/python && \
    .venv/bin/python -m pytest ../test/device_features/test_product_webconfig.py -v
"""

from __future__ import print_function

import os
import pty
import shutil
import stat
import sys
import tempfile
import time
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
for _p in (_PY_ROOT, _HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from device.webconfig_tool import WebConfigTool  # noqa: E402


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def _wait(pred, timeout=5.0, interval=0.02):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            if pred():
                return True
        except Exception:
            pass
        time.sleep(interval)
    try:
        return bool(pred())
    except Exception:
        return False


def _assert_structured_error(testcase, r, status, code):
    testcase.assertEqual(r.status_code, status,
                         'status=%s body=%s' % (r.status_code, r.text))
    detail = r.json().get('detail')
    testcase.assertIsInstance(detail, dict,
                             'AppError must return structured detail, got %r'
                             % (detail,))
    testcase.assertEqual(detail.get('code'), code, r.text)
    testcase.assertTrue(str(detail.get('detail') or '').strip(),
                        'detail.detail must be non-empty: %r' % (detail,))


def _write_script(path, body, mode=0o755):
    parent = os.path.dirname(path)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent, exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(body)
    os.chmod(path, mode)
    return path


def _write_file(path, data=b''):
    parent = os.path.dirname(path)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent, exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data if isinstance(data, (bytes, bytearray)) else str(data).encode('utf-8'))
    return path


UV4_SCRIPT_TEMPLATE = '''#!/bin/sh
# Fake Keil UV4: argv shape is [-j0, -r, <proj>, -t<target>, -o, <log>]
MODE="{mode}"
log=""
prev=""
for a in "$@"; do
  case "$prev" in
    -o) log="$a" ;;
  esac
  prev="$a"
done
if [ -z "$log" ]; then
  echo "UV4: missing -o log path" >&2
  exit 2
fi
out_dir=$(dirname "$log")
target=$(basename "$out_dir")
hex="$out_dir/$target.hex"
mkdir -p "$out_dir"
case "$MODE" in
  success)
    echo ':020000040000FA' > "$hex"
    printf '0 Error(s), 0 Warning(s)\\n' > "$log"
    ;;
  errors)
    echo ':020000040000FA' > "$hex"
    printf '3 Error(s), 0 Warning(s)\\n' > "$log"
    ;;
  hex_missing)
    printf '0 Error(s), 0 Warning(s)\\n' > "$log"
    ;;
  stale_hex)
    # Do not touch pre-created hex; rewrite log only.
    printf '0 Error(s), 0 Warning(s)\\n' > "$log"
    ;;
  *)
    echo "UV4: unknown MODE=$MODE" >&2
    exit 3
    ;;
esac
exit 0
'''

MAKE_SUCCESS = '''all:
\t@mkdir -p BabyOS
\t@echo ':020000040000FA' > BabyOS/BabyOS.hex
\t@printf '0 Error(s)\\n' > BabyOS/build_log.txt
\t@echo 'Build OK'
'''

MAKE_FAIL = '''all:
\t@echo 'error: compile failed'
\t@exit 1
'''

MAKE_RC0_NO_HEX = '''all:
\t@printf '0 Error(s)\\n' > /dev/null
\t@exit 0
'''

OPENOCD_OK = '''#!/bin/sh
echo "OpenOCD: fake program OK args=$*"
exit 0
'''

OPENOCD_FAIL = '''#!/bin/sh
echo "OpenOCD: probe fail" >&2
exit 1
'''

STATUS_KEYS = (
    'config_path', 'config_loaded', 'cfg', 'keil_uv4_exists',
    'project_file', 'project_exists', 'hex_file', 'openocd_exe',
    'openocd_exists', 'makefile', 'makefile_exists', 'pyserial',
    'busy', 'serial_log', 'can_build', 'can_flash',
)


def _make_keil_project(wd, mode='success', target='BabyOS'):
    """Create temp project + fake UV4. Returns (project_dir, keil, proj_file)."""
    proj_dir = os.path.join(wd, 'proj')
    os.makedirs(proj_dir, exist_ok=True)
    proj = os.path.join(proj_dir, 'app.uvprojx')
    _write_file(proj, b'<Project/>')
    keil = os.path.join(proj_dir, 'UV4.exe')
    _write_script(keil, UV4_SCRIPT_TEMPLATE.format(mode=mode))
    return proj_dir, keil, proj


def _make_project(wd, target='BabyOS', makefile=MAKE_SUCCESS, with_hex=False):
    """Create temp project with Makefile. Returns (project_dir, hex_path)."""
    proj_dir = os.path.join(wd, 'makeproj')
    os.makedirs(proj_dir, exist_ok=True)
    _write_file(os.path.join(proj_dir, 'Makefile'), makefile)
    _write_file(os.path.join(proj_dir, 'app.uvprojx'), b'<Project/>')
    hex_path = os.path.join(proj_dir, target, '%s.hex' % target)
    if with_hex:
        _write_file(hex_path, b':020000040000FA')
    return proj_dir, hex_path


def _make_openocd_tree(wd, with_hex=True, with_cfg=True, script=OPENOCD_OK):
    """Create openocd_dir tree + optional hex/cfg. Returns paths dict."""
    proj_dir = os.path.join(wd, 'flashproj')
    target = 'BabyOS'
    proj = os.path.join(proj_dir, 'app.uvprojx')
    _write_file(proj, b'<Project/>')
    hex_path = os.path.join(proj_dir, target, '%s.hex' % target)
    if with_hex:
        _write_file(hex_path, b':020000040000FA')

    oc_dir = os.path.join(wd, 'openocd')
    oc_exe = os.path.join(oc_dir, 'bin', 'openocd')
    _write_script(oc_exe, script)
    scripts_sub = os.path.join('share', 'openocd', 'scripts')
    stlink = os.path.join(oc_dir, scripts_sub, 'interface', 'stlink.cfg')
    target_cfg = os.path.join(oc_dir, scripts_sub, 'target', 'stm32l4x.cfg')
    if with_cfg:
        _write_file(stlink, b'# stlink\n')
        _write_file(target_cfg, b'# stm32l4x\n')
    return {
        'proj_dir': proj_dir,
        'proj': proj,
        'hex': hex_path,
        'oc_dir': oc_dir,
        'oc_exe': oc_exe,
        'stlink': stlink,
        'target_cfg': target_cfg,
        'scripts_sub': scripts_sub,
        'target': target,
    }


class _WcBase(unittest.TestCase):
    """Temp workspace + isolated WebConfigTool (never the real ini)."""

    def setUp(self):
        self.wd = tempfile.mkdtemp(prefix='babyos_wc_prod_')
        self.addCleanup(shutil.rmtree, self.wd, True)
        self.ini = os.path.join(self.wd, 'webconfig_tool.ini')
        self.wc = WebConfigTool(config_path=self.ini)

    def _hex_path(self, proj_dir, target='BabyOS'):
        return os.path.join(proj_dir, target, '%s.hex' % target)

    def _build_log_path(self, proj_dir, target='BabyOS'):
        return os.path.join(proj_dir, target, 'build_log.txt')


# ---------------------------------------------------------------------------
# 1. build() — UV4 SUCCESS / failure paths
# ---------------------------------------------------------------------------

class TestBuildKeil(_WcBase):

    def test_build_keil_success_hex_and_zero_errors(self):
        proj_dir, keil, proj = _make_keil_project(self.wd, mode='success')
        self.wc.save_config({
            'project_dir': proj_dir,
            'keil_uv4': keil,
            'project_file_rel': 'app.uvprojx',
            'target_name': 'BabyOS',
        })
        st = self.wc.status()
        self.assertTrue(st['keil_uv4_exists'])
        self.assertTrue(st['project_exists'])
        self.assertTrue(st['can_build'])
        self.assertFalse(st['makefile_exists'])

        res = self.wc.build(timeout_sec=30)
        self.assertTrue(res.get('ok'), res)
        self.assertEqual(res.get('tool'), 'keil')
        hex_path = self._hex_path(proj_dir)
        self.assertEqual(res.get('hex'), hex_path)
        self.assertTrue(os.path.isfile(hex_path))
        log_path = self._build_log_path(proj_dir)
        self.assertTrue(os.path.isfile(log_path))
        with open(log_path, 'r', errors='ignore') as f:
            txt = f.read()
        self.assertIn('0 Error(s)', txt)
        self.assertIsInstance(res.get('logs'), list)
        self.assertTrue(res['logs'])

    def test_build_keil_3_errors_structured_failure(self):
        proj_dir, keil, proj = _make_keil_project(self.wd, mode='errors')
        self.wc.save_config({
            'project_dir': proj_dir,
            'keil_uv4': keil,
            'project_file_rel': 'app.uvprojx',
            'target_name': 'BabyOS',
        })
        res = self.wc.build(timeout_sec=30)
        self.assertFalse(res.get('ok'))
        self.assertIn('error', res)
        self.assertIn('3', str(res['error']))
        self.assertIn('error(s)', str(res['error']).lower())

    def test_build_keil_hex_missing_structured_failure(self):
        proj_dir, keil, proj = _make_keil_project(self.wd, mode='hex_missing')
        self.wc.save_config({
            'project_dir': proj_dir,
            'keil_uv4': keil,
            'project_file_rel': 'app.uvprojx',
            'target_name': 'BabyOS',
        })
        res = self.wc.build(timeout_sec=30)
        self.assertFalse(res.get('ok'))
        self.assertIn('error', res)
        self.assertFalse(os.path.isfile(self._hex_path(proj_dir)))

    def test_build_keil_hex_stale_structured_failure(self):
        proj_dir, keil, proj = _make_keil_project(self.wd, mode='stale_hex')
        hex_path = self._hex_path(proj_dir)
        _write_file(hex_path, b':020000040000FA')
        os.utime(hex_path, (1000000000, 1000000000))  # fixed old mtime
        self.wc.save_config({
            'project_dir': proj_dir,
            'keil_uv4': keil,
            'project_file_rel': 'app.uvprojx',
            'target_name': 'BabyOS',
        })
        res = self.wc.build(timeout_sec=30)
        self.assertFalse(res.get('ok'))
        self.assertIn('error', res)
        self.assertIn('hex', str(res['error']).lower())
        # mtime must still be the pre-build one (UV4 did not refresh it)
        self.assertEqual(int(os.path.getmtime(hex_path)), 1000000000)

    def test_build_keil_not_found(self):
        proj_dir, keil, proj = _make_keil_project(self.wd, mode='success')
        self.wc.save_config({
            'project_dir': proj_dir,
            'keil_uv4': os.path.join(self.wd, 'no_such_UV4.exe'),
            'project_file_rel': 'app.uvprojx',
            'target_name': 'BabyOS',
        })
        res = self.wc.build(timeout_sec=30)
        self.assertFalse(res.get('ok'))
        self.assertIn('keil not found', str(res.get('error')))

    def test_build_project_not_found(self):
        proj_dir, keil, proj = _make_keil_project(self.wd, mode='success')
        self.wc.save_config({
            'project_dir': proj_dir,
            'keil_uv4': keil,
            'project_file_rel': 'no_such.uvprojx',
            'target_name': 'BabyOS',
        })
        res = self.wc.build(timeout_sec=30)
        self.assertFalse(res.get('ok'))
        self.assertIn('project not found', str(res.get('error')))

    def test_build_busy_structured_error(self):
        self.wc._busy = True
        res = self.wc.build(timeout_sec=10)
        self.assertFalse(res.get('ok'))
        self.assertIn('busy', str(res.get('error')).lower())


# ---------------------------------------------------------------------------
# 2. build() — Makefile path (real `make -C`)
# ---------------------------------------------------------------------------

class TestBuildMake(_WcBase):

    def test_make_success_produces_hex_and_zero_errors(self):
        proj_dir, hex_path = _make_project(self.wd, makefile=MAKE_SUCCESS,
                                           with_hex=False)
        self.wc.save_config({
            'project_dir': proj_dir,
            'project_file_rel': 'app.uvprojx',
            'target_name': 'BabyOS',
        })
        st = self.wc.status()
        self.assertTrue(st['makefile_exists'])
        self.assertTrue(st['can_build'])

        res = self.wc.build(timeout_sec=30)
        self.assertTrue(res.get('ok'), res)
        self.assertEqual(res.get('tool'), 'make')
        self.assertTrue(os.path.isfile(hex_path))
        log_path = self._build_log_path(proj_dir)
        self.assertTrue(os.path.isfile(log_path))
        with open(log_path, 'r', errors='ignore') as f:
            self.assertIn('0 Error(s)', f.read())

    def test_make_failure_rc_nonzero_structured_error(self):
        proj_dir, hex_path = _make_project(self.wd, makefile=MAKE_FAIL,
                                           with_hex=False)
        self.wc.save_config({'project_dir': proj_dir})
        res = self.wc.build(timeout_sec=30)
        self.assertFalse(res.get('ok'))
        self.assertIn('error', res)
        self.assertIn('make rc=', str(res['error']))

    def test_make_rc0_without_hex_fails_product_spec(self):
        """Product spec: build fails when hex is missing even if rc==0."""
        proj_dir, hex_path = _make_project(self.wd, makefile=MAKE_RC0_NO_HEX,
                                           with_hex=False)
        self.wc.save_config({'project_dir': proj_dir})
        res = self.wc.build(timeout_sec=30)
        self.assertFalse(res.get('ok'),
                         'product spec requires hex produced; got %r' % (res,))
        self.assertIn('error', res)


# ---------------------------------------------------------------------------
# 3. flash() — fake OpenOCD
# ---------------------------------------------------------------------------

class TestFlash(_WcBase):

    def test_flash_success_fake_openocd_exits_0(self):
        t = _make_openocd_tree(self.wd, with_hex=True, with_cfg=True,
                               script=OPENOCD_OK)
        self.wc.save_config({
            'project_dir': t['proj_dir'],
            'project_file_rel': 'app.uvprojx',
            'target_name': t['target'],
            'openocd_dir': t['oc_dir'],
        })
        st = self.wc.status()
        self.assertTrue(st['openocd_exists'])
        self.assertTrue(st['can_flash'])

        res = self.wc.flash(timeout_sec=30)
        self.assertTrue(res.get('ok'), res)
        self.assertEqual(res.get('hex'), t['hex'])
        self.assertTrue(res.get('logs'))

    def test_flash_openocd_missing_structured_error(self):
        t = _make_openocd_tree(self.wd, with_hex=True, with_cfg=True)
        os.remove(t['oc_exe'])
        self.wc.save_config({
            'project_dir': t['proj_dir'],
            'project_file_rel': 'app.uvprojx',
            'target_name': t['target'],
            'openocd_dir': t['oc_dir'],
        })
        res = self.wc.flash(timeout_sec=30)
        self.assertFalse(res.get('ok'))
        self.assertIn('missing file', str(res.get('error')))

    def test_flash_hex_missing_structured_error(self):
        t = _make_openocd_tree(self.wd, with_hex=False, with_cfg=True)
        self.wc.save_config({
            'project_dir': t['proj_dir'],
            'project_file_rel': 'app.uvprojx',
            'target_name': t['target'],
            'openocd_dir': t['oc_dir'],
        })
        res = self.wc.flash(timeout_sec=30)
        self.assertFalse(res.get('ok'))
        self.assertIn('missing file', str(res.get('error')))
        self.assertIn(t['hex'], str(res.get('error')))

    def test_flash_cfg_missing_structured_error(self):
        t = _make_openocd_tree(self.wd, with_hex=True, with_cfg=False)
        self.wc.save_config({
            'project_dir': t['proj_dir'],
            'project_file_rel': 'app.uvprojx',
            'target_name': t['target'],
            'openocd_dir': t['oc_dir'],
        })
        res = self.wc.flash(timeout_sec=30)
        self.assertFalse(res.get('ok'))
        self.assertIn('missing file', str(res.get('error')))

    def test_flash_openocd_rc_nonzero_structured_error(self):
        t = _make_openocd_tree(self.wd, with_hex=True, with_cfg=True,
                               script=OPENOCD_FAIL)
        self.wc.save_config({
            'project_dir': t['proj_dir'],
            'project_file_rel': 'app.uvprojx',
            'target_name': t['target'],
            'openocd_dir': t['oc_dir'],
        })
        res = self.wc.flash(timeout_sec=30)
        self.assertFalse(res.get('ok'))
        self.assertIn('openocd rc=', str(res.get('error')))

    def test_flash_openocd_dir_empty_structured_error(self):
        self.wc.save_config({
            'project_dir': os.path.join(self.wd, 'p'),
            'openocd_dir': '',
        })
        _write_file(os.path.join(self.wd, 'p', 'app.uvprojx'), b'<Project/>')
        res = self.wc.flash(timeout_sec=10)
        self.assertFalse(res.get('ok'))
        self.assertIn('missing file', str(res.get('error')))


# ---------------------------------------------------------------------------
# 4. serial log — real pyserial on a real pty
# ---------------------------------------------------------------------------

class TestSerialLog(_WcBase):

    def test_start_log_empty_port_structured_error(self):
        res = self.wc.start_log(port='', baud=115200, seconds=1)
        self.assertFalse(res.get('ok'))
        self.assertIn('serial port empty', str(res.get('error')))

    def test_start_log_bad_port_structured_error(self):
        res = self.wc.start_log(port='/dev/no_such_tty_babyos_wc',
                                baud=115200, seconds=1)
        self.assertFalse(res.get('ok'))
        self.assertIn('error', res)

    def test_serial_log_real_pty_start_capture_stop(self):
        st = self.wc.status()
        self.assertTrue(st.get('pyserial'), 'pyserial required for this test')
        master, slave = pty.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        slave_path = os.ttyname(slave)

        out_path = os.path.join(self.wd, 'auto_log.txt')
        started = self.wc.start_log(port=slave_path, baud=115200, seconds=3,
                                    out_path=out_path)
        self.assertTrue(started.get('ok'), started)
        self.assertEqual(started.get('path'), out_path)

        self.assertTrue(_wait(lambda: self.wc._serial_info.get('running') is True,
                              timeout=2.0))
        os.write(master, b'HELLO_BABYOS_WEBCONFIG\n')
        self.assertTrue(
            _wait(lambda: (self.wc._serial_info.get('bytes') or 0) > 0,
                  timeout=3.0),
            'serial log must capture pty bytes, info=%r' % (self.wc._serial_info,))

        stopped = self.wc.stop_log()
        self.assertTrue(stopped.get('ok'), stopped)
        self.assertFalse(self.wc._serial_info.get('running'))
        self.assertTrue(os.path.isfile(out_path), 'log file must be written')
        with open(out_path, 'rb') as f:
            data = f.read()
        self.assertIn(b'HELLO_BABYOS_WEBCONFIG', data)

    def test_serial_log_already_running_structured_error(self):
        master, slave = pty.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        slave_path = os.ttyname(slave)
        out_path = os.path.join(self.wd, 'auto_log2.txt')
        r1 = self.wc.start_log(port=slave_path, baud=115200, seconds=5,
                               out_path=out_path)
        self.assertTrue(r1.get('ok'), r1)
        r2 = self.wc.start_log(port=slave_path, baud=115200, seconds=5,
                               out_path=out_path + '.2')
        self.assertFalse(r2.get('ok'))
        self.assertIn('already running', str(r2.get('error')))
        self.wc.stop_log()

    def test_log_status_shape(self):
        self.assertIsInstance(self.wc.log_status(), dict)
        master, slave = pty.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        slave_path = os.ttyname(slave)
        out_path = os.path.join(self.wd, 'auto_log3.txt')
        r = self.wc.start_log(port=slave_path, baud=115200, seconds=2,
                              out_path=out_path)
        self.assertTrue(r.get('ok'), r)
        info = self.wc.log_status()
        self.assertEqual(info.get('port'), slave_path)
        self.assertEqual(info.get('path'), out_path)
        self.assertTrue(info.get('running'))
        self.wc.stop_log()
        info2 = self.wc.log_status()
        self.assertFalse(info2.get('running'))


# ---------------------------------------------------------------------------
# 5. save_config / load_config INI roundtrip
# ---------------------------------------------------------------------------

class TestIniRoundtrip(_WcBase):

    def test_full_ini_roundtrip_all_sections(self):
        updates = {
            'project_dir': '/tmp/wc_proj',
            'keil_uv4': '/opt/keil/UV4.exe',
            'openocd_dir': '/opt/openocd',
            'openocd_scripts_subpath': 'share/openocd/scripts',
            'stlink_cfg': 'interface/stlink.cfg',
            'target_cfg': 'target/stm32l4x.cfg',
            'project_file_rel': 'BabyOS.uvprojx',
            'target_name': 'BabyOSL4',
            'log_dir_rel': 'Doc',
            'serial_port': '/dev/ttyUSB9',
            'serial_baud': '921600',
            'log_seconds': '60',
        }
        self.wc.save_config(updates)
        self.assertTrue(os.path.isfile(self.ini))
        reloaded = WebConfigTool(config_path=self.ini)
        for key, val in updates.items():
            self.assertEqual(reloaded.cfg.get(key), str(val),
                             'INI roundtrip mismatch for %s' % key)
        self.assertEqual(reloaded.cfg.get('serial_baud'), '921600')
        self.assertEqual(reloaded.cfg.get('log_seconds'), '60')

    def test_save_skips_none_and_preserves_defaults(self):
        self.wc.save_config({'target_name': 'MyBoard', 'project_dir': None})
        reloaded = WebConfigTool(config_path=self.ini)
        self.assertEqual(reloaded.cfg.get('target_name'), 'MyBoard')
        # None must not wipe other defaults
        self.assertEqual(reloaded.cfg.get('serial_baud'), '115200')

    def test_load_missing_ini_returns_defaults(self):
        gone = os.path.join(self.wd, 'missing.ini')
        wc2 = WebConfigTool(config_path=gone)
        self.assertEqual(wc2.cfg.get('target_name'), 'BabyOS')
        self.assertFalse(os.path.isfile(gone))

    def test_status_reflects_saved_ini(self):
        proj_dir, keil, proj = _make_keil_project(self.wd, mode='success')
        self.wc.save_config({
            'project_dir': proj_dir,
            'keil_uv4': keil,
            'project_file_rel': 'app.uvprojx',
            'target_name': 'BabyOS',
        })
        wc2 = WebConfigTool(config_path=self.ini)
        st = wc2.status()
        self.assertTrue(st['keil_uv4_exists'])
        self.assertTrue(st['project_exists'])
        self.assertTrue(st['can_build'])
        self.assertEqual(st['hex_file'], self._hex_path(proj_dir))


# ---------------------------------------------------------------------------
# 6. FastAPI /api/device/webconfig/* — structured JSON over TestClient
# ---------------------------------------------------------------------------

class _ApiBase(unittest.TestCase):
    """FastAPI TestClient with DeviceManager.webconfig rebound to temp ini."""

    def setUp(self):
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

        self.wd = tempfile.mkdtemp(prefix='babyos_wc_api_')
        self.addCleanup(shutil.rmtree, self.wd, True)
        self.ini = os.path.join(self.wd, 'webconfig_tool.ini')

        from app.main import create_app
        from fastapi.testclient import TestClient
        self.client = TestClient(create_app())

        from device.device_manager import get_device_manager
        self.dm = get_device_manager()
        self.dm.webconfig = WebConfigTool(config_path=self.ini)
        self.wc = self.dm.webconfig

    def _hex_path(self, proj_dir, target='BabyOS'):
        return os.path.join(proj_dir, target, '%s.hex' % target)

    def _cfg_body(self, **extra):
        body = {'save': True}
        body.update(extra)
        return body


class TestApiWebConfigRoutes(_ApiBase):

    def test_status_get_structured_json(self):
        r = self.client.get('/api/device/webconfig/status')
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        for key in STATUS_KEYS:
            self.assertIn(key, body, 'status missing %r: %r' % (key, body))
        self.assertEqual(body['config_path'], self.ini)

    def test_status_post_updates_config(self):
        proj_dir, keil, proj = _make_keil_project(self.wd, mode='success')
        r = self.client.post('/api/device/webconfig/status', json=self._cfg_body(
            project_dir=proj_dir,
            keil_uv4=keil,
            project_file_rel='app.uvprojx',
            target_name='BabyOS',
        ))
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('updated', {}).get('project_dir'), proj_dir)
        st = body.get('status') or {}
        self.assertTrue(st.get('keil_uv4_exists'))
        self.assertTrue(st.get('can_build'))
        # persisted to temp ini
        reloaded = WebConfigTool(config_path=self.ini)
        self.assertEqual(reloaded.cfg.get('project_dir'), proj_dir)

    def test_api_device_status_includes_webconfig(self):
        r = self.client.get('/api/device/status')
        self.assertEqual(r.status_code, 200, r.text)
        st = r.json()
        self.assertIn('webconfig', st)
        self.assertIsInstance(st['webconfig'], dict)
        for key in STATUS_KEYS:
            self.assertIn(key, st['webconfig'])

    def test_build_make_success_via_api(self):
        proj_dir, hex_path = _make_project(self.wd, makefile=MAKE_SUCCESS)
        r = self.client.post('/api/device/webconfig/build', json=self._cfg_body(
            project_dir=proj_dir, target_name='BabyOS',
        ))
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('tool'), 'make')
        self.assertTrue(os.path.isfile(hex_path))

    def test_build_make_failure_structured_apperror(self):
        proj_dir, hex_path = _make_project(self.wd, makefile=MAKE_FAIL)
        r = self.client.post('/api/device/webconfig/build', json=self._cfg_body(
            project_dir=proj_dir, target_name='BabyOS',
        ))
        _assert_structured_error(self, r, 400, 'WEBCONFIG_BUILD_FAILED')

    def test_build_keil_success_via_api(self):
        proj_dir, keil, proj = _make_keil_project(self.wd, mode='success')
        r = self.client.post('/api/device/webconfig/build', json=self._cfg_body(
            project_dir=proj_dir,
            keil_uv4=keil,
            project_file_rel='app.uvprojx',
            target_name='BabyOS',
        ))
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('tool'), 'keil')
        self.assertTrue(os.path.isfile(self._hex_path(proj_dir)))

    def test_build_keil_3_errors_structured_apperror(self):
        proj_dir, keil, proj = _make_keil_project(self.wd, mode='errors')
        r = self.client.post('/api/device/webconfig/build', json=self._cfg_body(
            project_dir=proj_dir,
            keil_uv4=keil,
            project_file_rel='app.uvprojx',
            target_name='BabyOS',
        ))
        _assert_structured_error(self, r, 400, 'WEBCONFIG_BUILD_FAILED')
        detail = r.json()['detail']['detail']
        self.assertIn('3', detail)

    def test_build_keil_hex_stale_structured_apperror(self):
        proj_dir, keil, proj = _make_keil_project(self.wd, mode='stale_hex')
        hex_path = self._hex_path(proj_dir)
        _write_file(hex_path, b':020000040000FA')
        os.utime(hex_path, (1000000000, 1000000000))
        r = self.client.post('/api/device/webconfig/build', json=self._cfg_body(
            project_dir=proj_dir,
            keil_uv4=keil,
            project_file_rel='app.uvprojx',
            target_name='BabyOS',
        ))
        _assert_structured_error(self, r, 400, 'WEBCONFIG_BUILD_FAILED')
        detail = r.json()['detail']['detail'].lower()
        self.assertIn('hex', detail)

    def test_flash_success_via_api(self):
        t = _make_openocd_tree(self.wd, with_hex=True, with_cfg=True,
                               script=OPENOCD_OK)
        r = self.client.post('/api/device/webconfig/flash', json=self._cfg_body(
            project_dir=t['proj_dir'],
            project_file_rel='app.uvprojx',
            target_name=t['target'],
            openocd_dir=t['oc_dir'],
            timeout_sec=30,
        ))
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('hex'), t['hex'])

    def test_flash_openocd_missing_structured_apperror(self):
        t = _make_openocd_tree(self.wd, with_hex=True, with_cfg=True)
        os.remove(t['oc_exe'])
        r = self.client.post('/api/device/webconfig/flash', json=self._cfg_body(
            project_dir=t['proj_dir'],
            project_file_rel='app.uvprojx',
            target_name=t['target'],
            openocd_dir=t['oc_dir'],
            timeout_sec=30,
        ))
        _assert_structured_error(self, r, 400, 'WEBCONFIG_FLASH_FAILED')

    def test_flash_hex_missing_structured_apperror(self):
        t = _make_openocd_tree(self.wd, with_hex=False, with_cfg=True)
        r = self.client.post('/api/device/webconfig/flash', json=self._cfg_body(
            project_dir=t['proj_dir'],
            project_file_rel='app.uvprojx',
            target_name=t['target'],
            openocd_dir=t['oc_dir'],
            timeout_sec=30,
        ))
        _assert_structured_error(self, r, 400, 'WEBCONFIG_FLASH_FAILED')

    def test_log_start_empty_port_structured_apperror(self):
        r = self.client.post('/api/device/webconfig/log/start', json=self._cfg_body(
            serial_port='', log_seconds=1,
        ))
        _assert_structured_error(self, r, 400, 'WEBCONFIG_LOG_FAILED')

    def test_log_start_stop_status_real_pty_via_api(self):
        master, slave = pty.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        slave_path = os.ttyname(slave)
        out_path = os.path.join(self.wd, 'api_auto_log.txt')

        r = self.client.post('/api/device/webconfig/log/start', json=self._cfg_body(
            serial_port=slave_path,
            log_seconds=3,
            out_path=out_path,
        ))
        self.assertEqual(r.status_code, 200, r.text)
        body = r.json()
        self.assertTrue(body.get('ok'))
        self.assertEqual(body.get('path'), out_path)

        self.assertTrue(_wait(
            lambda: self.client.get('/api/device/webconfig/log/status').json()
            .get('running') is True, timeout=2.0))
        os.write(master, b'API_LOG_LINE\n')
        self.assertTrue(
            _wait(lambda: (self.client.get('/api/device/webconfig/log/status')
                           .json().get('bytes') or 0) > 0, timeout=3.0),
            'API serial log must capture pty bytes')

        r = self.client.post('/api/device/webconfig/log/stop')
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(r.json().get('ok'))
        st = self.client.get('/api/device/webconfig/log/status').json()
        self.assertFalse(st.get('running'))
        self.assertTrue(os.path.isfile(out_path))
        with open(out_path, 'rb') as f:
            self.assertIn(b'API_LOG_LINE', f.read())

    def test_log_seconds_out_of_range_pydantic_422(self):
        r = self.client.post('/api/device/webconfig/log/start', json={
            'serial_port': '/dev/ttyUSB0', 'log_seconds': 0, 'save': False,
        })
        self.assertEqual(r.status_code, 422, r.text)

    def test_build_timeout_out_of_range_pydantic_422(self):
        r = self.client.post('/api/device/webconfig/build', json={
            'project_dir': '/tmp/x', 'timeout_sec': 1, 'save': False,
        })
        self.assertEqual(r.status_code, 422, r.text)


def main():
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    print('tests=%s failures=%s errors=%s' % (
        result.testsRun, len(result.failures), len(result.errors)))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
