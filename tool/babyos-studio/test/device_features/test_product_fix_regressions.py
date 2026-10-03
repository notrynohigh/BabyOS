#!/usr/bin/env python3
"""Product regression lock-in for adversarial-hunt fixes (2026-10-03).

Authority:
  - python/device/webconfig_tool.py
  - python/app/api/device.py
  - python/app/services/{trainer,dataset_service,export_service,feature_service,
    project_service,metrics_service}.py
  - python/app/api/datasets.py

These tests lock the CONFIRMED product bugs found by adversarial hunting so
they cannot silently return. They are product acceptance paths, not mocks of
UI shells.

Run:
  cd /home/yyds/code/BabyOS/tool/babyos-studio && \
    PYTHONPATH=python python3 test/device_features/test_product_fix_regressions.py -v
"""
from __future__ import print_function

import json
import os
import shutil
import stat
import sys
import tempfile
import time
import unittest
from pathlib import Path

_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
for _p in (_PY_ROOT, _HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

_DATA_ROOT = tempfile.mkdtemp(prefix="automl_fix_regress_")
os.environ["AUTOML_DATA_ROOT"] = _DATA_ROOT

from device.webconfig_tool import WebConfigTool  # noqa: E402

MAKE_SUCCESS = r"""
.PHONY: all
all:
	mkdir -p BabyOS
	printf ':020000040000FA\n:00000001FF\n' > BabyOS/BabyOS.hex
	printf '0 Error(s), 0 Warning(s)\n' > BabyOS/build_log.txt
"""

MAKE_STALE_LOG = r"""
.PHONY: all
all:
	mkdir -p BabyOS
	# leave build_log.txt untouched — caller must truncate first
	printf ':020000040000FA\n' > BabyOS/BabyOS.hex
"""

MAKE_INCREMENTAL_NOOP = r"""
.PHONY: all
all:
	mkdir -p BabyOS
	# incremental: do not rewrite hex if it already exists
	if [ ! -f BabyOS/BabyOS.hex ]; then \
		printf ':020000040000FA\n' > BabyOS/BabyOS.hex; \
	fi
	if [ ! -f BabyOS/build_log.txt ]; then \
		printf '0 Error(s), 0 Warning(s)\n' > BabyOS/build_log.txt; \
	fi
"""

MAKE_FAIL = r"""
.PHONY: all
all:
	echo compile error
	exit 1
"""


def _write(path, body, mode=0o755):
    parent = os.path.dirname(path)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent, exist_ok=True)
    if isinstance(body, bytes):
        with open(path, 'wb') as f:
            f.write(body)
    else:
        with open(path, 'w', encoding='utf-8') as f:
            f.write(body)
    os.chmod(path, mode)
    return path


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


def _write_script(path, body):
    return _write(path, body, 0o755)


class _WcBase(unittest.TestCase):
    def setUp(self):
        self.wd = tempfile.mkdtemp(prefix='wc_fix_')
        self.ini = os.path.join(self.wd, 'webconfig_tool.ini')
        self.wc = WebConfigTool(config_path=self.ini)

    def tearDown(self):
        shutil.rmtree(self.wd, ignore_errors=True)

    def _make_project(self, makefile=MAKE_SUCCESS, target='BabyOS'):
        proj_dir = os.path.join(self.wd, 'proj')
        os.makedirs(proj_dir, exist_ok=True)
        _write(os.path.join(proj_dir, 'Makefile'), makefile)
        hex_path = os.path.join(proj_dir, target, '%s.hex' % target)
        return proj_dir, hex_path

    def _hex_path(self, proj_dir, target='BabyOS'):
        return os.path.join(proj_dir, target, '%s.hex' % target)

    def _build_log(self, proj_dir, target='BabyOS'):
        return os.path.join(proj_dir, target, 'build_log.txt')


class TestWebconfigFixes(_WcBase):

    def test_make_incremental_noop_is_success(self):
        """rc=0 + hex present + mtime unchanged must still be OK."""
        proj_dir, hex_path = self._make_project(MAKE_INCREMENTAL_NOOP)
        os.makedirs(os.path.dirname(hex_path), exist_ok=True)
        _write(hex_path, b':020000040000FA\n', mode=0o644)
        old = 1000000000
        os.utime(hex_path, (old, old))
        self.wc.save_config({'project_dir': proj_dir, 'target_name': 'BabyOS'})
        res = self.wc.build(timeout_sec=30)
        self.assertTrue(res.get('ok'), res)
        self.assertEqual(res.get('tool'), 'make')
        self.assertTrue(os.path.isfile(hex_path))
        # incremental make must not require a fresh hex mtime
        self.assertEqual(int(os.path.getmtime(hex_path)), old)

    def test_make_truncates_stale_build_log(self):
        """A leftover 'N Error(s)' in build_log must not poison a fresh make."""
        proj_dir, hex_path = self._make_project(MAKE_STALE_LOG)
        log = self._build_log(proj_dir)
        os.makedirs(os.path.dirname(log), exist_ok=True)
        _write(log, '3 Error(s), 0 Warning(s)\n', mode=0o644)
        self.wc.save_config({'project_dir': proj_dir, 'target_name': 'BabyOS'})
        res = self.wc.build(timeout_sec=30)
        self.assertTrue(res.get('ok'), res)
        # stale error count must be gone from the log after a successful build
        self.assertFalse(os.path.isfile(log) and
                         '3 Error(s)' in open(log, errors='ignore').read())

    def test_relative_project_dir_abspath(self):
        proj_dir, hex_path = self._make_project(MAKE_SUCCESS)
        # save relative path — tool must abspath it
        rel = os.path.relpath(proj_dir, os.getcwd())
        self.wc.save_config({'project_dir': rel, 'target_name': 'BabyOS'})
        self.assertTrue(os.path.isabs(self.wc.cfg['project_dir']),
                        self.wc.cfg)
        self.assertEqual(self.wc.cfg['project_dir'], os.path.abspath(proj_dir))
        res = self.wc.build(timeout_sec=30)
        self.assertTrue(res.get('ok'), res)

    def test_save_config_empty_string_clears_path(self):
        self.wc.save_config({'project_dir': '/tmp/x', 'target_name': 'T'})
        self.assertEqual(self.wc.cfg['project_dir'], os.path.abspath('/tmp/x'))
        self.wc.save_config({'project_dir': '', 'target_name': ''})
        self.assertEqual(self.wc.cfg['project_dir'], '')
        self.assertEqual(self.wc.cfg['target_name'], '')

    def test_build_busy_rejects_start_log(self):
        self.wc._busy = True
        res = self.wc.start_log(port='/dev/null', seconds=1)
        self.assertFalse(res.get('ok'))
        self.assertIn('busy', str(res.get('error')).lower())

    def test_start_log_concurrent_second_fails(self):
        import pty
        master, slave = pty.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        slave_path = os.ttyname(slave)
        out1 = os.path.join(self.wd, 'log1.txt')
        out2 = os.path.join(self.wd, 'log2.txt')
        r1 = self.wc.start_log(port=slave_path, seconds=3, out_path=out1)
        self.assertTrue(r1.get('ok'), r1)
        r2 = self.wc.start_log(port=slave_path, seconds=3, out_path=out2)
        self.assertFalse(r2.get('ok'))
        self.assertIn('already running', str(r2.get('error')))
        os.write(master, b'X\n')
        self.assertTrue(_wait(lambda: (self.wc.log_status().get('bytes') or 0) > 0,
                              timeout=3.0))
        self.wc.stop_log()
        self.assertTrue(_wait(lambda: os.path.isfile(out1), timeout=3.0))


class TestAutoMLFixes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = None
        try:
            from app.main import create_app  # noqa: WPS433
            cls.app = create_app()
        except Exception as exc:  # pragma: no cover
            raise unittest.SkipTest('create_app failed: %s' % exc)

    def setUp(self):
        from fastapi.testclient import TestClient
        self.client = TestClient(self.app)
        # AUTOML_DATA_ROOT is set at module import; use unique project names
        self._n = 0

    def _csv_bytes(self, header, rows):
        lines = [','.join(header)]
        for row in rows:
            lines.append(','.join(str(x) for x in row))
        return ('\n'.join(lines) + '\n').encode('utf-8')

    def _create(self, **body):
        self._n += 1
        payload = {'name': 'fixreg_%s_%s' % (os.getpid(), self._n),
                   'mode': 'table', 'task_type': 'classification',
                   'sampling_rate': 100.0}
        payload.update(body)
        return self.client.post('/api/projects', json=payload)

    def _import(self, pid, content, filename, mapping, kind='append'):
        files = {'files': (filename, content, 'text/csv')}
        data = {'mapping': json.dumps(mapping), 'import_kind': kind}
        return self.client.post('/api/projects/%s/dataset' % pid,
                                files=files, data=data)

    def _ok_rows(self):
        return [[1, 2, 'x'], [3, 4, 'y'], [5, 6, 'x'], [7, 8, 'y'],
                [9, 10, 'x'], [11, 12, 'y'], [13, 14, 'x'], [15, 16, 'y'],
                [17, 18, 'x'], [19, 20, 'y'], [21, 22, 'x'], [23, 24, 'y']]

    def _wait_training_failed(self, pid, timeout=30.0):
        deadline = time.time() + timeout
        st = {'status': 'running'}
        while time.time() < deadline:
            r = self.client.get('/api/projects/%s/training' % pid)
            st = r.json() if r.status_code == 200 else {'status': 'http_%s' % r.status_code}
            if st.get('status') not in ('running', 'idle'):
                return st
            time.sleep(0.2)
        return st

    def test_timeseries_regression_rejected(self):
        r = self._create(name='tsreg', mode='timeseries', task_type='regression')
        self.assertEqual(r.status_code, 422, r.text)
        detail = r.json().get('detail')
        self.assertEqual(detail.get('code'), 'UNSUPPORTED_TASK', r.text)

    def test_mapping_non_dict_is_422(self):
        r = self._create(name='mapnd')
        pid = r.json()['project_id']
        files = {'files': ('t.csv', self._csv_bytes(
            ['a', 'b', 'label'], self._ok_rows()), 'text/csv')}
        r = self.client.post('/api/projects/%s/dataset' % pid,
                             files=files,
                             data={'mapping': json.dumps(['a', 'b']),
                                   'import_kind': 'append'})
        self.assertEqual(r.status_code, 422, r.text)
        self.assertEqual(r.json()['detail']['code'], 'BAD_MAPPING')

    def test_append_channel_set_mismatch_422(self):
        r = self._create(name='chm')
        pid = r.json()['project_id']
        content = self._csv_bytes(['a', 'b', 'label'], self._ok_rows())
        mapping = {'features': ['a', 'b'], 'label_col': 'label'}
        imp = self._import(pid, content, 't.csv', mapping, kind='replace')
        self.assertEqual(imp.status_code, 200, imp.text)
        bad_content = self._csv_bytes(
            ['a', 'b', 'c', 'label'],
            [[1, 2, 3, 'x'], [4, 5, 6, 'y']] + self._ok_rows())
        bad_map = {'features': ['a', 'b', 'c'], 'label_col': 'label'}
        r2 = self._import(pid, bad_content, 'bad.csv', bad_map, kind='append')
        self.assertEqual(r2.status_code, 422, r2.text)
        self.assertEqual(r2.json()['detail']['code'], 'CHANNEL_SET_MISMATCH')

    def test_replace_total_fail_restores_existing_data(self):
        from app.deps import load_meta
        r = self._create(name='repl')
        pid = r.json()['project_id']
        content = self._csv_bytes(['a', 'b', 'label'], self._ok_rows())
        mapping = {'features': ['a', 'b'], 'label_col': 'label'}
        imp = self._import(pid, content, 'ok.csv', mapping, kind='replace')
        self.assertEqual(imp.status_code, 200, imp.text)
        before = self.client.get('/api/projects/%s/dataset' % pid).json()
        before_files = before.get('files') or []
        self.assertTrue(before_files)
        # mapping requires feature cols that are absent → every file fails
        bad = self._csv_bytes(['only_label'], [['x']])
        imp2 = self._import(pid, bad, 'bad.csv',
                            {'features': ['ghost_a', 'ghost_b'],
                             'label_col': 'only_label'},
                            kind='replace')
        self.assertEqual(imp2.status_code, 200, imp2.text)
        imported = sum(1 for x in imp2.json().get('results') or [] if x.get('ok'))
        self.assertEqual(imported, 0, imp2.text)
        after = self.client.get('/api/projects/%s/dataset' % pid).json()
        after_files = after.get('files') or []
        self.assertEqual(len(after_files), len(before_files),
                         'replace total-fail must restore previous raw data: '
                         'before=%s after=%s' % (before_files, after_files))
        meta = load_meta(pid)
        self.assertTrue(meta.labels)

    def test_replace_success_sets_export_stale(self):
        from app.deps import load_meta
        r = self._create(name='repstale')
        pid = r.json()['project_id']
        content = self._csv_bytes(['a', 'b', 'label'], self._ok_rows())
        mapping = {'features': ['a', 'b'], 'label_col': 'label'}
        imp = self._import(pid, content, 'ok.csv', mapping, kind='replace')
        self.assertEqual(imp.status_code, 200, imp.text)
        meta = load_meta(pid)
        self.assertTrue(meta.export_stale,
                        'successful replace-import must mark export_stale')

    def test_missing_feature_column_422(self):
        r = self._create(name='featmiss')
        pid = r.json()['project_id']
        content = self._csv_bytes(['a', 'b', 'label'], self._ok_rows())
        mapping = {'features': ['a', 'b'], 'label_col': 'label'}
        imp = self._import(pid, content, 'ok.csv', mapping, kind='replace')
        self.assertEqual(imp.status_code, 200, imp.text)
        r2 = self.client.put('/api/projects/%s/features/config' % pid,
                             json={'feature_ids': ['ghost_col'],
                                   'window_len_s': 0.001,
                                   'n_per_window': 16, 'step': 1,
                                   'freq_enabled': False, 'freq_bands': 1})
        self.assertEqual(r2.status_code, 422, r2.text)
        self.assertEqual(r2.json()['detail']['code'], 'FEATURE_COLUMN_MISSING')

    def test_feature_meta_channel_with_double_underscore(self):
        """Channel names containing '__' must not break export feature chain."""
        from app.services.export_service import _feature_meta
        from app.schemas import ProjectMeta

        class _Cfg:
            freq_enabled = False
            freq_bands = 1
            window_len_s = 0.01
            n_per_window = 16
            step = 1
            feature_ids = ['mean']

        meta = ProjectMeta(
            project_id='x', name='u', mode='timeseries',
            task_type='classification', sampling_rate=100.0,
            channels=['imu__acc', 'ch1'],
        )
        payload = {'feature_indices': [0]}
        matrix = {}
        cfg = _Cfg()
        import app.services.feature_service as fs
        orig = fs.feature_names
        fs.feature_names = lambda m, c: ['imu__acc__mean', 'ch1__mean']
        try:
            fm = _feature_meta(meta, payload, matrix, cfg)
        finally:
            fs.feature_names = orig
        self.assertIsNotNone(fm)
        self.assertEqual(fm['exported'], [('imu__acc', 'mean')])

    def test_single_class_training_fails_structured(self):
        """Single-class classification must fail training with structured state."""
        r = self._create(name='singlecls')
        pid = r.json()['project_id']
        rows = [[i, i * 2, 'x'] for i in range(1, 21)]
        content = self._csv_bytes(['a', 'b', 'label'], rows)
        mapping = {'features': ['a', 'b'], 'label_col': 'label'}
        imp = self._import(pid, content, 'one.csv', mapping, kind='replace')
        self.assertEqual(imp.status_code, 200, imp.text)
        r2 = self.client.put('/api/projects/%s/features/config' % pid,
                             json={'feature_ids': [], 'window_len_s': 0.001,
                                   'n_per_window': 16, 'step': 1,
                                   'freq_enabled': False, 'freq_bands': 1})
        self.assertEqual(r2.status_code, 200, r2.text)
        r3 = self.client.post('/api/projects/%s/features/compute' % pid)
        self.assertEqual(r3.status_code, 200, r3.text)
        r4 = self.client.post('/api/projects/%s/training' % pid,
                              json={'metric': 'accuracy',
                                    'task_type': 'classification',
                                    'auto_feature_select': False,
                                    'top_n': 4, 'scoring': 'f_test',
                                    'seed': 42})
        self.assertEqual(r4.status_code, 200, r4.text)
        st = self._wait_training_failed(pid)
        self.assertEqual(st.get('status'), 'failed', st)
        err = str(st.get('error') or '')
        self.assertTrue('SINGLE_CLASS' in err or 'CLASS_TOO_FEW' in err,
                        'training error must carry structured class code: %r' % err)


class TestAppErrorExtra(unittest.TestCase):
    def test_apperror_extra_merges_into_detail(self):
        from app.deps import AppError
        e = AppError(400, 'X', 'boom', extra={'logs': ['a', 'b']})
        self.assertEqual(e.detail['detail'], 'boom')
        self.assertEqual(e.detail['code'], 'X')
        self.assertEqual(e.detail['logs'], ['a', 'b'])


class TestWebconfigApiLogsInError(unittest.TestCase):
    def test_api_build_failure_includes_logs(self):
        from fastapi.testclient import TestClient
        from app.main import create_app
        from device.device_manager import DeviceManager
        app = create_app()
        client = TestClient(app)
        wd = tempfile.mkdtemp(prefix='wc_api_logs_')
        self.addCleanup(shutil.rmtree, wd, True)
        proj = os.path.join(wd, 'proj')
        os.makedirs(proj, exist_ok=True)
        _write(os.path.join(proj, 'Makefile'), MAKE_FAIL)
        dm = DeviceManager.get()
        wc = dm.webconfig
        old_cfg_path = wc.config_path
        wc.config_path = os.path.join(wd, 'wc.ini')
        wc.save_config({'project_dir': proj, 'target_name': 'BabyOS'})
        try:
            r = client.post('/api/device/webconfig/build',
                            json={'project_dir': proj, 'target_name': 'BabyOS',
                                  'save': False, 'timeout_sec': 30})
            self.assertEqual(r.status_code, 400, r.text)
            detail = r.json()['detail']
            self.assertEqual(detail['code'], 'WEBCONFIG_BUILD_FAILED')
            self.assertIn('logs', detail)
            self.assertIsInstance(detail['logs'], list)
            self.assertTrue(detail['logs'])
            r2 = client.post('/api/device/webconfig/status',
                             json={'project_dir': '', 'save': True})
            self.assertEqual(r2.status_code, 200, r2.text)
            self.assertEqual(r2.json()['updated'].get('project_dir'), '')
            st = r2.json()['status']
            self.assertFalse(st.get('project_dir'))
        finally:
            wc.config_path = old_cfg_path


def main():
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    print('tests=%s failures=%s errors=%s' % (
        result.testsRun, len(result.failures), len(result.errors)))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
