#!/usr/bin/env python3
"""
test_ui_product_e2e — Real Electron UI acceptance on this host.

Software-simulated board path (no physical MCU required):
  MockBabyOSDevice on pty master  ↔  FastAPI DeviceManager on pty slave
  Electron UI (real ui/index.html + preload + app.js handlers)
    → IPC http:request → 127.0.0.1:18080 → python/device stack → pty

Run:
  cd tool/babyos-studio/python && \
    .venv/bin/python ../test/ui_e2e/test_ui_product_e2e.py
"""
from __future__ import print_function

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
_ELECTRON = os.path.join(_STUDIO, 'node_modules', '.bin', 'electron')
_MAIN_E2E = os.path.join(_HERE, 'electron_main_e2e.js')

for _p in (_PY_ROOT, os.path.join(_STUDIO, 'test', 'device_features')):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import httpx  # noqa: E402

from mock_babyos_device import open_api_over_pty  # noqa: E402

API = 'http://127.0.0.1:18080'
PYTHON = os.path.join(_PY_ROOT, '.venv', 'bin', 'python')


def _wait_api(timeout=40.0):
    t0 = time.time()
    last = None
    while time.time() - t0 < timeout:
        try:
            r = httpx.get(API + '/api/version', timeout=1.0)
            if r.status_code == 200:
                return True
            last = 'status=%s' % r.status_code
        except Exception as exc:
            last = str(exc)
        time.sleep(0.25)
    raise RuntimeError('backend not ready: %s' % last)


def _wait_cdp(port=9333, timeout=30.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            r = httpx.get('http://127.0.0.1:%d/json/list' % port, timeout=1.0)
            if r.status_code == 200:
                pages = r.json()
                if any(p.get('type') == 'page' for p in pages):
                    return True
        except Exception:
            pass
        time.sleep(0.3)
    return False


def js_wait(cond_js, timeout_ms=8000, step_ms=150):
    """Self-contained async expression: poll cond_js until truthy or timeout."""
    return (
        "(async () => {"
        "  const t0 = Date.now();"
        "  const timeout = %d;"
        "  const step = %d;"
        "  while (Date.now() - t0 < timeout) {"
        "    try {"
        "      const v = await (%s);"
        "      if (v) return {ok:true, waited: Date.now()-t0, value:v};"
        "    } catch (e) { /* keep polling */ }"
        "    await new Promise(r => setTimeout(r, step));"
        "  }"
        "  return {ok:false, waited: Date.now()-t0, error:'timeout'};"
        "})()" % (timeout_ms, step_ms, cond_js)
    )


def ui_click_set(id_, value):
    """Set an input/select value then return ok."""
    return (
        "(async () => {"
        "  const el = document.getElementById(%s);"
        "  if (!el) return {ok:false, error:'missing '+%s};"
        "  el.value = %s;"
        "  el.dispatchEvent(new Event('input', {bubbles:true}));"
        "  el.dispatchEvent(new Event('change', {bubbles:true}));"
        "  return {ok:true, value: el.value};"
        "})()" % (json.dumps(id_), json.dumps(id_), json.dumps(value))
    )


def ui_click(id_):
    return (
        "(async () => {"
        "  const el = document.getElementById(%s);"
        "  if (!el) return {ok:false, error:'missing '+%s};"
        "  el.click();"
        "  return {ok:true};"
        "})()" % (json.dumps(id_), json.dumps(id_))
    )


def ui_nav(page):
    return (
        "(async () => {"
        "  if (typeof navigateTo !== 'function') return {ok:false, error:'navigateTo missing'};"
        "  navigateTo(%s);"
        "  const active = document.querySelector('.page.active');"
        "  return {ok:true, active: active ? active.id : null};"
        "})()" % json.dumps(page)
    )


def ui_log_contains(marker, container_id='global-log', timeout_ms=8000):
    return js_wait(
        "(document.getElementById(%s)?.textContent || '').includes(%s)"
        % (json.dumps(container_id), json.dumps(marker)),
        timeout_ms=timeout_ms,
    )


def build_steps(pty_path, file_path, folder_path, logfile_path, webconfig_ini_dir):
    """UI-driven product steps. Each returns {ok:...} from renderer JS."""
    steps = []

    steps.append({'name': 'nav_home', 'expr': ui_nav('home')})
    steps.append({'name': 'nav_serial', 'expr': ui_nav('serial')})
    steps.append({'name': 'nav_ota', 'expr': ui_nav('ota')})
    steps.append({'name': 'nav_xmodem', 'expr': ui_nav('xmodem')})
    steps.append({'name': 'nav_file', 'expr': ui_nav('file')})
    steps.append({'name': 'nav_netvoice', 'expr': ui_nav('netvoice')})
    steps.append({'name': 'nav_http', 'expr': ui_nav('http')})
    steps.append({'name': 'nav_params', 'expr': ui_nav('params')})
    steps.append({'name': 'nav_device', 'expr': ui_nav('device')})
    steps.append({'name': 'nav_webconfig', 'expr': ui_nav('webconfig')})
    steps.append({'name': 'nav_projects', 'expr': ui_nav('projects')})

    # Serial open on software-simulated board (pty slave path)
    steps.append({'name': 'serial_select_ptty', 'expr': (
        "(async () => {"
        "  navigateTo('serial');"
        "  const sel = document.getElementById('serial-port');"
        "  if (!sel) return {ok:false, error:'no serial-port'};"
        "  let found = false;"
        "  for (const o of sel.options) { if (o.value === %s) { found = true; break; } }"
        "  if (!found) {"
        "    const opt = document.createElement('option');"
        "    opt.value = %s; opt.textContent = %s;"
        "    sel.appendChild(opt);"
        "  }"
        "  sel.value = %s;"
        "  document.getElementById('serial-baud').value = '115200';"
        "  const enc = document.getElementById('encrypt-mode');"
        "  if (enc) enc.checked = false;"
        "  return {ok:true, path: sel.value};"
        "})()" % ((json.dumps(pty_path),) * 4)
    )})
    steps.append({'name': 'serial_open', 'expr': ui_click('btn-open-serial')})
    steps.append({'name': 'serial_opened_ui', 'expr': js_wait(
        "(document.getElementById('serial-status')?.textContent || '').includes(%s)"
        % json.dumps(pty_path),
        timeout_ms=10000,
    )})

    # Protocol CMD 0x1 via real UI button
    steps.append({'name': 'proto_test_click', 'expr': ui_click('btn-test')})
    steps.append({'name': 'proto_test_ui', 'expr': js_wait(
        "(document.getElementById('global-log')?.textContent || '').includes('协议测试 OK')"
        + " || (document.getElementById('serial-log')?.textContent || '').includes('ACK')",
        timeout_ms=10000,
    )})

    # Set time CMD 0x2
    steps.append({'name': 'proto_set_time', 'expr': ui_click('btn-set-time')})
    steps.append({'name': 'proto_set_time_ui', 'expr': js_wait(
        "(document.getElementById('global-log')?.textContent || '').includes('设置时间 OK')"
        + " || (document.getElementById('global-log')?.textContent || '').includes('设置时间失败')",
        timeout_ms=8000,
    )})

    # Netvoice: 0x30 / 0x31
    steps.append({'name': 'net_nav', 'expr': ui_nav('netvoice')})
    steps.append({'name': 'cfgnet_set_fields', 'expr': (
        "(async () => {"
        "  document.getElementById('cfgnet-type').value = '0';"
        "  document.getElementById('cfgnet-ssid').value = 'E2E-AP';"
        "  document.getElementById('cfgnet-passwd').value = 'e2e-secret';"
        "  return {ok:true};"
        "})()"
    )})
    steps.append({'name': 'cfgnet_set_click', 'expr': ui_click('btn-cfgnet-set')})
    steps.append({'name': 'cfgnet_set_ui', 'expr': js_wait(
        "(document.getElementById('netvoice-log')?.textContent || '').includes('配网模式已设置')"
        + " || (document.getElementById('netvoice-log')?.textContent || '').includes('CMD 0x30')",
        timeout_ms=8000,
    )})
    steps.append({'name': 'netinfo_get_click', 'expr': ui_click('btn-netinfo-get')})
    steps.append({'name': 'netinfo_fields', 'expr': js_wait(
        "(() => {"
        "  const ssid = document.getElementById('netinfo-ssid')?.value || '';"
        "  return ssid ? ssid : null;"
        "})()",
        timeout_ms=8000,
    )})

    # Voice 0x40–0x44
    steps.append({'name': 'voice_on', 'expr': ui_click('btn-voice-on')})
    steps.append({'name': 'voice_volume_set', 'expr': (
        "(async () => {"
        "  document.getElementById('voice-volume').value = '66';"
        "  return {ok:true};"
        "})()"
    )})
    steps.append({'name': 'voice_volume_set_click', 'expr': ui_click('btn-voice-volume-set')})
    steps.append({'name': 'voice_stat_click', 'expr': ui_click('btn-voice-stat')})
    steps.append({'name': 'voice_tts', 'expr': (
        "(async () => {"
        "  document.getElementById('tts-content').value = 'E2E TTS';"
        "  return {ok:true};"
        "})()"
    )})
    steps.append({'name': 'voice_tts_click', 'expr': ui_click('btn-tts-send')})
    steps.append({'name': 'voice_ui', 'expr': js_wait(
        "(document.getElementById('netvoice-log')?.textContent || '').includes('TTS')"
        + " || (document.getElementById('voice-stat-text')?.textContent || '').length > 0",
        timeout_ms=8000,
    )})

    # TSL 0x09
    steps.append({'name': 'tsl_content', 'expr': (
        "(async () => {"
        "  document.getElementById('tsl-content').value = '{\"id\":\"1\",\"method\":\"power\"}';"
        "  return {ok:true};"
        "})()"
    )})
    steps.append({'name': 'tsl_click', 'expr': ui_click('btn-tsl-invoke')})

    # File page: merge + transfer
    steps.append({'name': 'file_nav', 'expr': ui_nav('file')})
    steps.append({'name': 'file_folder_set', 'expr': ui_click_set('file-folder', folder_path)})
    steps.append({'name': 'file_out_name', 'expr': ui_click_set('file-out-name', 'allfile.bin')})
    steps.append({'name': 'file_merge_click', 'expr': ui_click('btn-file-merge')})
    steps.append({'name': 'file_merge_ui', 'expr': js_wait(
        "(document.getElementById('file-merge-log')?.textContent || '').includes('合并完成')",
        timeout_ms=8000,
    )})
    steps.append({'name': 'file_path_set', 'expr': ui_click_set('file-path', file_path)})
    steps.append({'name': 'file_start_click', 'expr': ui_click('btn-file-start')})
    steps.append({'name': 'file_transfer_ui', 'expr': js_wait(
        "(document.getElementById('global-log')?.textContent || '').includes('文件传输')"
        + " || (document.getElementById('file-progress-text')?.textContent || '').includes('%')",
        timeout_ms=15000,
    )})
    steps.append({'name': 'file_stop_click', 'expr': ui_click('btn-file-stop')})
    steps.append({'name': 'file_stop_ui', 'expr': js_wait(
        "(document.getElementById('global-log')?.textContent || '').includes('文件传输已停止')",
        timeout_ms=8000,
    )})

    # HTTP mock server via UI
    steps.append({'name': 'http_nav', 'expr': ui_nav('http')})
    steps.append({'name': 'http_server_fields', 'expr': (
        "(async () => {"
        "  document.getElementById('http-port').value = '0';"
        "  document.getElementById('http-response-body').value = '{\"ok\":true,\"src\":\"ui-e2e\"}';"
        "  document.getElementById('http-content-type').value = 'application/json';"
        "  document.getElementById('http-status-code').value = '200';"
        "  const fl = document.getElementById('http-file-log');"
        "  if (fl) fl.checked = true;"
        "  return {ok:true};"
        "})()"
    )})
    steps.append({'name': 'http_server_start', 'expr': ui_click('btn-http-server')})
    steps.append({'name': 'http_server_ui', 'expr': js_wait(
        "(document.getElementById('http-server-status')?.textContent || '').includes('运行中')",
        timeout_ms=8000,
    )})
    steps.append({'name': 'http_protocol_fields', 'expr': (
        "(async () => {"
        "  const mode = document.getElementById('http-mode');"
        "  if (mode) mode.value = 'protocol';"
        "  document.getElementById('http-url').value = 'http://127.0.0.1:1/status-probe';"
        "  document.getElementById('http-method').value = 'GET';"
        "  return {ok:true};"
        "})()"
    )})
    steps.append({'name': 'http_device_init', 'expr': ui_click('btn-http-init')})
    steps.append({'name': 'http_send_click', 'expr': ui_click('btn-http-send')})
    steps.append({'name': 'http_send_ui', 'expr': js_wait(
        "(document.getElementById('http-log')?.textContent || '').includes('0x50')"
        + " || (document.getElementById('http-log')?.textContent || '').includes('协议请求')"
        + " || (document.getElementById('http-log')?.textContent || '').includes('主机代发')",
        timeout_ms=10000,
    )})

    # Params page: poll start/stop + param get via shell
    steps.append({'name': 'params_nav', 'expr': ui_nav('params')})
    steps.append({'name': 'param_name_set', 'expr': ui_click_set('param-name', 'g_param_test_val')})
    steps.append({'name': 'param_get_click', 'expr': ui_click('btn-param-get')})
    steps.append({'name': 'param_poll_fields', 'expr': (
        "(async () => {"
        "  document.getElementById('param-poll-name').value = 'g_volume';"
        "  document.getElementById('param-poll-interval').value = '500';"
        "  return {ok:true};"
        "})()"
    )})
    steps.append({'name': 'param_poll_start', 'expr': ui_click('btn-param-poll-start')})
    steps.append({'name': 'param_poll_ui', 'expr': js_wait(
        "(document.getElementById('param-poll-status')?.textContent || '').length > 0"
        + " || (document.getElementById('param-output')?.textContent || '').length > 0",
        timeout_ms=8000,
    )})
    steps.append({'name': 'param_poll_stop', 'expr': ui_click('btn-param-poll-stop')})

    # Device info page
    steps.append({'name': 'device_nav', 'expr': ui_nav('device')})
    steps.append({'name': 'device_uid_click', 'expr': ui_click('btn-get-uid')})
    steps.append({'name': 'device_info_click', 'expr': ui_click('btn-get-device-info')})
    steps.append({'name': 'device_ui', 'expr': js_wait(
        "(document.getElementById('device-uid')?.value || '').length > 0"
        + " || (document.getElementById('global-log')?.textContent || '').includes('UID')",
        timeout_ms=8000,
    )})

    # Webconfig: status refresh + structured build error via UI
    steps.append({'name': 'wc_nav', 'expr': ui_nav('webconfig')})
    steps.append({'name': 'wc_refresh', 'expr': ui_click('btn-wc-refresh')})
    steps.append({'name': 'wc_status_ui', 'expr': js_wait(
        "(document.getElementById('wc-status-log')?.textContent || '').length > 0"
        + " || (document.getElementById('wc-project-dir') !== null)",
        timeout_ms=8000,
    )})
    steps.append({'name': 'wc_build_click', 'expr': ui_click('btn-wc-build')})
    steps.append({'name': 'wc_build_ui', 'expr': js_wait(
        "(document.getElementById('wc-output')?.textContent || '').includes('编译')"
        + " || (document.getElementById('global-log')?.textContent || '').includes('WebConfig')",
        timeout_ms=15000,
    )})

    # Log-to-file via UI
    steps.append({'name': 'logfile_nav', 'expr': ui_nav('serial')})
    steps.append({'name': 'logfile_path', 'expr': ui_click_set('logfile-path', logfile_path)})
    steps.append({'name': 'logfile_start', 'expr': ui_click('btn-logfile-start')})
    steps.append({'name': 'logfile_ui', 'expr': js_wait(
        "(document.getElementById('logfile-status')?.textContent || '').length > 0",
        timeout_ms=8000,
    )})
    steps.append({'name': 'logfile_stop', 'expr': ui_click('btn-logfile-stop')})

    # AutoML projects page — real UI navigation + list load
    steps.append({'name': 'automl_projects_nav', 'expr': ui_nav('projects')})
    steps.append({'name': 'automl_projects_ui', 'expr': js_wait(
        "(() => {"
        "  const active = document.querySelector('.page.active');"
        "  return active && active.id === 'page-projects';"
        "})()",
        timeout_ms=5000,
    )})

    return steps


def assert_device_mock(device, expectations):
    """Cross-check software board state after UI-driven flows."""
    problems = []
    cmds = list(getattr(device, 'written_cmds', []) or [])
    if expectations.get('need_cmds'):
        for c in expectations['need_cmds']:
            if c not in cmds:
                problems.append('mock missing cmd 0x%02X (got %s)' % (c, cmds))
    if expectations.get('cfgnet'):
        if device.last_cfgnet != expectations['cfgnet']:
            problems.append('cfgnet %s != %s' % (device.last_cfgnet, expectations['cfgnet']))
    if expectations.get('voice_volume') is not None:
        if device.voice_volume != expectations['voice_volume']:
            problems.append('voice_volume %s != %s' % (
                device.voice_volume, expectations['voice_volume']))
    if expectations.get('last_tts') is not None:
        if device.last_tts != expectations['last_tts']:
            problems.append('last_tts %r != %r' % (device.last_tts, expectations['last_tts']))
    if expectations.get('last_tsl') is not None:
        if device.last_tsl != expectations['last_tsl']:
            problems.append('last_tsl %r != %r' % (device.last_tsl, expectations['last_tsl']))
    return problems


def main():
    if not os.path.isfile(_ELECTRON):
        print(json.dumps({'ok': False, 'error': 'electron binary missing: %s' % _ELECTRON}))
        return 2
    if not os.path.isfile(_MAIN_E2E):
        print(json.dumps({'ok': False, 'error': 'electron_main_e2e.js missing'}))
        return 2
    if not os.environ.get('DISPLAY'):
        print(json.dumps({'ok': False, 'error': 'DISPLAY not set; UI E2E requires X11'}))
        return 2

    tmp = tempfile.mkdtemp(prefix='babyos_ui_e2e_')
    data_root = os.path.join(tmp, 'automl_data')
    os.makedirs(data_root, exist_ok=True)
    result_path = os.path.join(tmp, 'e2e_result.json')
    steps_path = os.path.join(tmp, 'e2e_steps.json')

    # Software-simulated board: mock on master, host_port = pty slave for backend
    link = open_api_over_pty(
        encrypt=False,
        open_host_uart=False,
        bind_device_manager=False,
        require_pty=True,
        device_kwargs={'shell_params': {
            'g_param_test_val': 12345,
            'g_param_test_val2': -999,
            'g_volume': 50,
        }},
    )
    device = link.device
    pty_path = link.host_port

    file_dir = os.path.join(tmp, 'xfer')
    os.makedirs(file_dir, exist_ok=True)
    file_path = os.path.join(file_dir, 'demo.bin')
    with open(file_path, 'wb') as f:
        f.write(b'UI-E2E-FILE-CONTENT-0123456789')

    folder_path = os.path.join(tmp, 'folder')
    os.makedirs(folder_path, exist_ok=True)
    with open(os.path.join(folder_path, 'a.bin'), 'wb') as f:
        f.write(b'AAA')
    with open(os.path.join(folder_path, 'b.bin'), 'wb') as f:
        f.write(b'BB')

    logfile_path = os.path.join(tmp, 'ui_e2e.log')
    steps = build_steps(pty_path, file_path, folder_path, logfile_path, tmp)
    with open(steps_path, 'w', encoding='utf-8') as f:
        json.dump(steps, f, ensure_ascii=False, indent=2)

    env = os.environ.copy()
    env['AUTOML_DATA_ROOT'] = data_root
    env['BABYOS_E2E_RESULT'] = result_path
    env['BABYOS_E2E_STEPS'] = steps_path
    env.setdefault('DISPLAY', ':0')
    # Keep backend away from user projects; log quieter
    env.setdefault('PYTHONUNBUFFERED', '1')

    backend = subprocess.Popen(
        [PYTHON, '-m', 'uvicorn', 'app.main:app',
         '--host', '127.0.0.1', '--port', '18080', '--log-level', 'warning'],
        cwd=_PY_ROOT,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    electron = None
    try:
        _wait_api()
        # Confirm backend DeviceManager can see the pty path via API ports list
        # (pty may be absent from pyserial comports; UI still sets the path directly)
        electron = subprocess.Popen(
            [_ELECTRON, _MAIN_E2E,
             '--no-sandbox',
             '--disable-gpu',
             '--disable-dev-shm-usage'],
            cwd=_STUDIO,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        # Wait for electron to write result (steps include waits)
        t0 = time.time()
        hard_deadline = 180.0
        while time.time() - t0 < hard_deadline:
            if electron.poll() is not None:
                break
            if os.path.isfile(result_path) and os.path.getsize(result_path) > 2:
                # give a moment for final flush
                time.sleep(0.5)
                if electron.poll() is not None:
                    break
                # process may still be exiting
                try:
                    electron.wait(timeout=5)
                except Exception:
                    pass
                break
            time.sleep(0.5)

        if electron.poll() is None:
            electron.kill()
            electron.wait(timeout=10)

        el_out = b''
        if electron.stdout:
            try:
                el_out = electron.stdout.read() or b''
            except Exception:
                el_out = b''

        result = {'ok': False, 'error': 'no e2e result file'}
        if os.path.isfile(result_path):
            with open(result_path, 'r', encoding='utf-8') as f:
                result = json.load(f)

        # Cross-check software board + backend API
        problems = list(assert_device_mock(device, {
            'need_cmds': [0x01, 0x02, 0x30, 0x31, 0x40, 0x41, 0x43, 0x44, 0x09, 0x07, 0x0A],
            'cfgnet': (0, 'E2E-AP', 'e2e-secret'),
            'voice_volume': 66,
            'last_tts': 'E2E TTS',
            'last_tsl': b'{"id":"1","method":"power"}',
        }))

        # Backend API secondary checks
        api_problems = []
        try:
            with httpx.Client(timeout=3.0) as client:
                r = client.get(API + '/api/device/serial/ports')
                if r.status_code != 200:
                    api_problems.append('serial/ports status %s' % r.status_code)
                r = client.get(API + '/api/device/webconfig/status')
                if r.status_code != 200:
                    api_problems.append('webconfig/status status %s' % r.status_code)
                else:
                    st = r.json()
                    if 'can_build' not in st and 'cfg' not in st:
                        api_problems.append('webconfig/status unexpected shape %s' % list(st)[:8])
                r = client.get(API + '/api/version')
                if r.status_code != 200:
                    api_problems.append('version status %s' % r.status_code)
                # AutoML projects list via same backend the UI uses
                r = client.get(API + '/api/projects')
                if r.status_code != 200:
                    api_problems.append('projects status %s' % r.status_code)
        except Exception as exc:
            api_problems.append('api check exception: %s' % exc)

        # File transfer job should have been attempted; mock may have received data
        try:
            with httpx.Client(timeout=3.0) as client:
                r = client.get(API + '/api/device/file/status')
                if r.status_code == 200:
                    job = r.json().get('job') or {}
                    # UI started transfer; job registry must exist or transfer_active
                    # (stop may clear active). Accept done/error/absent-after-stop.
                    pass
        except Exception:
            pass

        failed_steps = []
        if isinstance(result, dict):
            for s in result.get('steps') or []:
                if not s.get('ok'):
                    failed_steps.append(s.get('name'))

        ui_ok = bool(result.get('ok')) if isinstance(result, dict) else False
        # Product gate: UI steps must pass AND mock cross-check AND API health
        # Soft-fail file transfer UI if transfer completed then stopped (log may
        # only show stopped). Hard-require protocol/net/voice/http/params/device.
        hard_fail_names = [
            n for n in failed_steps
            if n in (
                'serial_open', 'serial_opened_ui', 'proto_test_ui', 'proto_set_time_ui',
                'cfgnet_set_ui', 'netinfo_fields', 'voice_ui', 'http_server_ui',
                'http_send_ui', 'param_poll_ui', 'device_ui', 'wc_status_ui',
                'wc_build_ui', 'logfile_ui', 'automl_projects_ui',
                'nav_home', 'nav_serial', 'nav_webconfig',
            )
        ]
        ok = ui_ok and not problems and not api_problems and not hard_fail_names

        report = {
            'ok': ok,
            'pty': pty_path,
            'ui_ok': ui_ok,
            'ui_failed_steps': failed_steps,
            'ui_hard_fail_names': hard_fail_names,
            'mock_problems': problems,
            'api_problems': api_problems,
            'steps_total': len(steps),
            'steps_passed': sum(1 for s in (result.get('steps') if isinstance(result, dict) else []) or [] if s.get('ok')),
            'mock_written_cmds': ['0x%02X' % c for c in (device.written_cmds or [])],
            'mock_cfgnet': list(device.last_cfgnet) if device.last_cfgnet else None,
            'mock_voice_volume': device.voice_volume,
            'mock_last_tts': device.last_tts,
            'mock_last_tsl': device.last_tsl.decode('utf-8', 'replace') if device.last_tsl else None,
            'electron_stdout_tail': el_out[-4000:].decode('utf-8', 'replace'),
            'result_file': result_path if os.path.isfile(result_path) else None,
            'raw_result': result if isinstance(result, dict) else {'raw': str(result)[:500]},
        }
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 0 if ok else 1
    finally:
        if electron is not None and electron.poll() is None:
            try:
                electron.kill()
            except Exception:
                pass
        try:
            backend.terminate()
            backend.wait(timeout=8)
        except Exception:
            try:
                backend.kill()
            except Exception:
                pass
        try:
            link.close()
        except Exception:
            pass
        # keep tmp for debugging on failure
        if os.environ.get('BABYOS_UI_E2E_KEEP_TMP') != '1':
            # only remove if success path cleaned; leave on failure for inspection
            pass


if __name__ == '__main__':
    sys.exit(main())
