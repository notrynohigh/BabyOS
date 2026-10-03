#!/usr/bin/env python3
"""Lock-in for adversarial device+UI hunt findings (2026-10-04).

Authority:
  - python/device/http_mock.py      (C6 race, C7 file_log leak)
  - python/device/device_manager.py (C3/C4/C5 transfer exclusivity, re-open cancel)
  - python/device/protocol_client.py (C2 rx/tx ring caps)
  - python/app/api/device.py        (409 guards, serial re-open)
  - ui/app.js                       (C1 duplicate btn-http-server listeners)

Run:
  cd tool/babyos-studio/python && \
    .venv/bin/python ../test/device_features/test_product_adversarial_device.py -v
"""
from __future__ import print_function

import os
import re
import shutil
import sys
import tempfile
import threading
import time
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
for _p in (_PY_ROOT, _HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from mock_babyos_device import open_api_over_pty  # noqa: E402

MOCK_DEV_ID = 0x0000ABCD


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


def _detail_code(r):
    try:
        detail = r.json().get('detail')
        if isinstance(detail, dict):
            return detail.get('code')
    except Exception:
        pass
    return None


def _assert_structured(testcase, r, status, code):
    testcase.assertEqual(r.status_code, status,
                         'status=%s body=%s' % (r.status_code, r.text))
    detail = r.json().get('detail')
    testcase.assertIsInstance(detail, dict, r.text)
    testcase.assertEqual(detail.get('code'), code, r.text)


def _write_temp(data: bytes, suffix: str = '.bin') -> str:
    fd, path = tempfile.mkstemp(suffix=suffix, prefix='babyos_adv_')
    try:
        os.write(fd, data)
    finally:
        os.close(fd)
    return path


class _PtyApiBase(unittest.TestCase):
    def setUp(self):
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

        self.link = open_api_over_pty(
            encrypt=False,
            open_host_uart=False,
            bind_device_manager=False,
            device_kwargs={
                'device_id': MOCK_DEV_ID,
                'encrypt': False,
            },
            require_pty=True,
        )
        self.addCleanup(self.link.close)
        self.assertEqual(self.link.kind, 'pty')
        self.device = self.link.device

        from app.main import create_app
        from fastapi.testclient import TestClient
        self.app = create_app()
        self.client = TestClient(self.app)

        r = self.client.post('/api/device/serial/open', json={
            'path': self.link.host_port,
            'baud': 115200,
            'encrypt': False,
        })
        self.assertEqual(r.status_code, 200, r.text)

        from device.device_manager import get_device_manager
        self.dm = get_device_manager()
        self.assertTrue(self.dm.is_open())

    def _hold_transfer(self):
        """Simulate an in-flight OTA on the protocol client (pc._xfer_busy)."""
        pc = self.dm.protocol_client
        self.assertIsNotNone(pc)
        pc._xfer_busy = True
        self.assertTrue(pc.transfer_active)

    def _release_transfer(self):
        pc = self.dm.protocol_client
        if pc is not None:
            pc._xfer_busy = False


# ---------------------------------------------------------------------------
# C3: shell / param / poll must 409 during active transfer
# ---------------------------------------------------------------------------

class TestTransferBlocksShellParam(_PtyApiBase):

    def test_shell_param_poll_reject_during_transfer(self):
        self.device.switch_to_shell()
        self._hold_transfer()

        r = self.client.post('/api/device/shell/cmd', json={'cmd': 'param'})
        _assert_structured(self, r, 409, 'TRANSFER_BUSY')

        r = self.client.post('/api/device/param/list')
        _assert_structured(self, r, 409, 'TRANSFER_BUSY')

        r = self.client.post('/api/device/param/get',
                             json={'name': 'g_param_test_val'})
        _assert_structured(self, r, 409, 'TRANSFER_BUSY')

        r = self.client.post('/api/device/param/set',
                             json={'name': 'g_param_test_val', 'value': 1})
        _assert_structured(self, r, 409, 'TRANSFER_BUSY')

        r = self.client.post('/api/device/param/poll/start',
                             json={'name': 'g_param_test_val',
                                   'interval_ms': 100})
        _assert_structured(self, r, 409, 'TRANSFER_BUSY')

        self._release_transfer()
        # after release, shell ops work again
        r = self.client.post('/api/device/param/list')
        self.assertEqual(r.status_code, 200, r.text)


# ---------------------------------------------------------------------------
# C4: xmodem/ymodem start must 409 during active OTA (pc.transfer_active)
# ---------------------------------------------------------------------------

class TestXferBusyBlocksXmodem(_PtyApiBase):

    def test_xmodem_ymodem_reject_during_protocol_transfer(self):
        fw = _write_temp(b'\x01\x02\x03\x04' * 32, '.bin')
        self.addCleanup(lambda: os.path.exists(fw) and os.unlink(fw))
        self._hold_transfer()

        r = self.client.post('/api/device/xmodem/start', json={'path': fw})
        _assert_structured(self, r, 409, 'TRANSFER_BUSY')

        r = self.client.post('/api/device/ymodem/start', json={'path': fw})
        _assert_structured(self, r, 409, 'TRANSFER_BUSY')

        self._release_transfer()


# ---------------------------------------------------------------------------
# C5: serial re-open cancels in-flight transfer + param poll
# ---------------------------------------------------------------------------

class TestSerialReopenCancelsInflight(_PtyApiBase):

    def test_reopen_stops_param_poll_and_transfer(self):
        self.device.switch_to_shell()
        r = self.client.post('/api/device/param/poll/start',
                             json={'name': 'g_param_test_val',
                                   'interval_ms': 200})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(self.dm.param_polling_status()['enabled'])

        self._hold_transfer()

        r = self.client.post('/api/device/serial/open', json={
            'path': self.link.host_port,
            'baud': 115200,
            'encrypt': False,
        })
        self.assertEqual(r.status_code, 200, r.text)

        # poll must be gone; transfer flag must not survive rebind
        st = self.dm.param_polling_status()
        self.assertFalse(st.get('enabled'),
                         'serial re-open must stop param polling: %s' % st)
        pc = self.dm.protocol_client
        self.assertIsNotNone(pc)
        self.assertFalse(bool(pc.transfer_active),
                         're-open must cancel in-flight protocol transfer')
        # old jobs must not still claim running transfer_active on new pc
        self.assertFalse(self.dm._transfer_busy())


# ---------------------------------------------------------------------------
# C6: concurrent HttpMock.start must not orphan LISTEN sockets
# ---------------------------------------------------------------------------

class TestHttpMockConcurrentStart(unittest.TestCase):

    def setUp(self):
        from device.http_mock import HttpMock
        self.mock = HttpMock()
        self.addCleanup(self.mock.stop)

    @staticmethod
    def _listen_ports():
        ports = set()
        try:
            with open('/proc/net/tcp', 'r', encoding='ascii') as f:
                next(f, None)
                for line in f:
                    parts = line.split()
                    if len(parts) < 4:
                        continue
                    local = parts[1]
                    state = parts[3]
                    if state != '0A':  # TCP_LISTEN
                        continue
                    if ':' not in local:
                        continue
                    ports.add(int(local.split(':', 1)[1], 16))
        except Exception:
            return None
        return ports

    def test_concurrent_start_no_orphan_listen(self):
        before = self._listen_ports()
        results = []
        errors = []
        barrier = threading.Barrier(6)

        def _start():
            try:
                barrier.wait(timeout=5.0)
                port = self.mock.start(port=0, file_log=False)
                results.append(port)
            except Exception as exc:
                errors.append(exc)

        threads = [threading.Thread(target=_start, daemon=True) for _ in range(6)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=10.0)

        # exactly one successful start must win
        self.assertEqual(len(results), 1,
                         'expected 1 success, got %s errors=%s' % (results, errors))
        self.assertTrue(all(isinstance(e, RuntimeError) for e in errors),
                        'losers must get RuntimeError already-running: %s' % errors)
        port = results[0]
        self.assertTrue(port > 0)

        # after stop, the tracked port must be closed — no orphan LISTEN
        self.mock.stop()
        time.sleep(0.2)
        after = self._listen_ports()
        if before is None or after is None:
            self.skipTest('/proc/net/tcp unavailable')
        self.assertNotIn(port, after,
                         'tracked HttpMock port %d must be closed after stop' % port)
        # no new LISTEN beyond the original set (orphans would show up)
        leaked = after - before
        self.assertEqual(leaked, set(),
                         'orphan LISTEN sockets leaked: %s' % sorted(leaked))


# ---------------------------------------------------------------------------
# C7: HttpMock.stop / close_all must close file_log handle
# ---------------------------------------------------------------------------

class TestHttpMockFileLogClosedOnStop(unittest.TestCase):

    def test_stop_closes_file_log(self):
        from device import http_mock as hm
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

        wd = tempfile.mkdtemp(prefix='wc_adv_log_')
        self.addCleanup(shutil.rmtree, wd, True)
        log_path = os.path.join(wd, 'mock_http.log')

        dm = DeviceManager.get()
        dm.http_mock._file_log_path = None  # force fresh open via start
        # point mock log path by monkeypatching resolver
        orig = hm._resolve_log_path
        hm._resolve_log_path = lambda: log_path
        try:
            port = dm.start_http_mock(port=0, file_log=True)
            self.assertTrue(port > 0)
            st = hm.mock_log_status()
            self.assertTrue(st.get('enabled'), st)
            dm.stop_http_mock()
            st2 = hm.mock_log_status()
            self.assertFalse(st2.get('enabled'),
                             'stop() must close file_log: %s' % st2)
            self.assertIsNone(st2.get('path'))
        finally:
            hm._resolve_log_path = orig

    def test_close_all_closes_file_log(self):
        from device import http_mock as hm
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)

        wd = tempfile.mkdtemp(prefix='wc_adv_log2_')
        self.addCleanup(shutil.rmtree, wd, True)
        log_path = os.path.join(wd, 'mock_http.log')
        orig = hm._resolve_log_path
        hm._resolve_log_path = lambda: log_path
        try:
            dm = DeviceManager.get()
            dm.start_http_mock(port=0, file_log=True)
            self.assertTrue(hm.mock_log_status().get('enabled'))
            dm.close_all()
            st = hm.mock_log_status()
            self.assertFalse(st.get('enabled'),
                             'close_all must close file_log: %s' % st)
        finally:
            hm._resolve_log_path = orig


# ---------------------------------------------------------------------------
# C2: ProtocolClient rx/tx logs are ring-capped
# ---------------------------------------------------------------------------

class TestProtocolClientRingCaps(unittest.TestCase):

    class _NullUart(object):
        def __init__(self):
            self.written = bytearray()
            self.is_open = True

        def write(self, data):
            self.written.extend(data)
            return len(data)

        def read_available(self):
            return b''

    def test_rx_log_and_frames_capped(self):
        from device.protocol_client import (
            ProtocolClient, _RX_LOG_MAX, _RX_FRAMES_MAX, _TX_LOG_MAX,
        )
        import device.b_protocol as bp

        pc = ProtocolClient(self._NullUart())
        # craft N valid frames with unknown cmd (no waiter) via real packer
        n = _RX_LOG_MAX + 50
        raw = bytearray()
        for i in range(n):
            raw.extend(bp.pack_frame(0x0000ABCD, 0xEE, bytes([i & 0xFF])))
        completed = pc.feed_bytes(bytes(raw))
        self.assertEqual(len(completed), n)
        self.assertLessEqual(len(pc._rx_log), _RX_LOG_MAX,
                             'rx_log must be ring-capped')
        self.assertLessEqual(len(pc._rx_frames), _RX_FRAMES_MAX,
                             'rx_frames must be ring-capped')

        # TX ring: send_cmd appends packed frames to _tx_log
        for _ in range(_TX_LOG_MAX + 20):
            pc.send_cmd(0x01, b'\x00')
        self.assertLessEqual(len(pc._tx_log), _TX_LOG_MAX,
                             'tx_log must be ring-capped')


# ---------------------------------------------------------------------------
# C1: UI must register each device control listener exactly once
# ---------------------------------------------------------------------------

class TestUiDuplicateListeners(unittest.TestCase):

    def test_btn_http_server_listener_single(self):
        app_js = os.path.join(_STUDIO, 'ui', 'app.js')
        with open(app_js, 'r', encoding='utf-8') as f:
            src = f.read()
        n = len(re.findall(
            r"getElementById\('btn-http-server'\)\s*\?\s*\.addEventListener\('click'",
            src))
        self.assertEqual(n, 1,
                         'btn-http-server must have exactly 1 click listener, got %d' % n)
        n_clear = len(re.findall(
            r"getElementById\('btn-http-clear'\)\s*\?\s*\.addEventListener\('click'",
            src))
        self.assertEqual(n_clear, 1,
                         'btn-http-clear must have exactly 1 click listener, got %d' % n_clear)
        n_append = len(re.findall(r'function _appendHttpLog\s*\(', src))
        self.assertEqual(n_append, 1,
                         '_appendHttpLog must be defined once, got %d' % n_append)


# ---------------------------------------------------------------------------
# API path: OTA auto-stops param poll (poll must not steal UART bytes)
# ---------------------------------------------------------------------------

class TestOtaAutoStopsParamPoll(_PtyApiBase):

    def test_ota_start_stops_param_polling(self):
        self.device.switch_to_shell()
        r = self.client.post('/api/device/param/poll/start',
                             json={'name': 'g_param_test_val',
                                   'interval_ms': 100})
        self.assertEqual(r.status_code, 200, r.text)
        self.assertTrue(self.dm.param_polling_status()['enabled'])

        fw = _write_temp(b'\xAA\xBB\xCC\xDD' * 64, '.bin')
        self.addCleanup(lambda: os.path.exists(fw) and os.unlink(fw))
        # hold link so OTA stays in-flight long enough to observe poll stop
        self.device.link_broken = True
        r = self.client.post('/api/device/ota/start', json={
            'path': fw, 'timeout': 3.0,
        })
        self.assertEqual(r.status_code, 200, r.text)
        self.assertFalse(self.dm.param_polling_status()['enabled'],
                         'ota/start must stop param polling before transfer')

        # wait for OTA job to finish/fail, then clean
        _wait(lambda: not self.dm.protocol_client.transfer_active, timeout=8.0)
        self.client.post('/api/device/ota/cancel')


def main():
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    print('tests=%s failures=%s errors=%s' % (
        result.testsRun, len(result.failures), len(result.errors)))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
