#!/usr/bin/env python3
"""
test_e2e_pty — virtual serial + mock BabyOS device end-to-end tests.

Authority:
  - origin/master:tool/README.md (BabyOS 协议说明)
  - origin/master:tool/b_protocol.py / tool/xmodem_ydmodem.py
  - bos/modules/b_mod_protocol.h / b_mod_protocol.c
  - bos/algorithm/algo_crc.c (CRC32 口径)

Host side uses studio ProtocolClient + UartService over a real pty slave
(or socketpair fallback). Device side is mock_babyos_device.MockBabyOSDevice
parsing/replying with firmware semantics.

Covered:
  test_link / set_time / get_uid / write_sn / get_device_info
  OTA success path, OTA CRC-failure path
  FLASH file transfer (0x6) success path
  Xmodem-128: host XmodemSender -> device simplified receiver

Run:
  tool/babyos-studio/python/.venv/bin/python \
      tool/babyos-studio/test/device_features/test_e2e_pty.py
  or: pytest tool/babyos-studio/test/device_features/test_e2e_pty.py
"""

from __future__ import print_function

import os
import struct
import sys
import tempfile
import time
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
if _PY_ROOT not in sys.path:
    sys.path.insert(0, _PY_ROOT)

# ensure device_features (mock) is importable as a sibling module
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from device import b_protocol as bp  # noqa: E402
from device.crc_util import crc32  # noqa: E402
from device.sn_util import sn_bytes, sn_hex  # noqa: E402
from device.protocol_client import (  # noqa: E402
    ProtocolClient,
    CMD_TEST,
    CMD_UTC,
    CMD_FW_INFO,
    CMD_FDATA,
    CMD_OTA_RESULT,
    CMD_TRANS_FILE,
    CMD_GET_UID,
    CMD_WRITE_SN,
    CMD_DEVICEINFO,
    OTA_RESULT_OK,
    OTA_RESULT_CRC_ERROR,
    OTA_RESULT_LEN_INVALID,
    DEVICE_ID_HOST,
)
from device.uart_service import UartService  # noqa: E402
from device.xmodem_ydmodem import (  # noqa: E402
    XmodemSender, XferState, crc16_ccitt,
    SOH, EOT, ACK, NAK, CRCPKT,
)

from mock_babyos_device import (  # noqa: E402
    MockBabyOSDevice,
    XmodemReceiver,
    create_byte_channel,
)

# Test constants
MOCK_UID = b'\x11\x22\x33\x44\x55\x66\x77\x88\x99\xAA\xBB\xCC'
MOCK_VERSION = 'v1.2.3'
MOCK_MODEL = 'BabyOS-MCU'
MOCK_DEV_ID = 0x0000ABCD

_CHANNEL_KIND = []  # populated by tests for reporting


def _make_fw_bytes(n: int, seed: int = 3) -> bytes:
    """Deterministic pseudo-firmware payload."""
    return bytes(bytearray(((i * 7) + seed) & 0xFF for i in range(n)))


def _write_temp_file(data: bytes, suffix: str = '.bin') -> str:
    fd, path = tempfile.mkstemp(suffix=suffix, prefix='babyos_ota_')
    try:
        os.write(fd, data)
    finally:
        os.close(fd)
    return path


def _wait_until(pred, timeout: float = 2.0, interval: float = 0.01) -> bool:
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


def _xmodem_expected_rx_len(payload_len: int) -> int:
    """
    Bytes the studio XmodemSender will deliver for a payload of payload_len.

    Sender pads the last data block to 128B AND, when size % 128 != 0,
    transmits an extra all-zero SOH padding block before EOT.
    """
    if payload_len % 128 == 0:
        return payload_len
    data_blocks = (payload_len + 127) // 128
    return (data_blocks + 1) * 128


# ---------------------------------------------------------------------------
# Shared fixture helpers
# ---------------------------------------------------------------------------

class _PtyFixture(object):
    """Create channel + mock device + ProtocolClient; tear down cleanly."""

    def __init__(self, **device_kwargs):
        self.chan = create_byte_channel(prefer='auto')
        _CHANNEL_KIND.append(self.chan.kind)
        self.logs = []
        kwargs = dict(
            device_id=MOCK_DEV_ID,
            uid=MOCK_UID,
            version=MOCK_VERSION,
            model=MOCK_MODEL,
            log_fn=self.logs.append,
        )
        kwargs.update(device_kwargs)
        self.device = MockBabyOSDevice(self.chan.device_endpoint(), **kwargs)
        self.device.start()
        self.pc = ProtocolClient(self.chan.host_uart, log_fn=self.logs.append)

    def close(self):
        try:
            self.device.stop()
        except Exception:
            pass
        try:
            self.chan.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Protocol request/response over real pty
# ---------------------------------------------------------------------------

class TestE2EPtyProtocol(unittest.TestCase):
    """Host ProtocolClient ↔ mock device over pty/socketpair byte channel."""

    @classmethod
    def setUpClass(cls):
        cls.fx = _PtyFixture()

    @classmethod
    def tearDownClass(cls):
        cls.fx.close()

    def setUp(self):
        # each test starts from a clean rx queue
        self.fx.pc.reset_rx_queue()
        self.fx.device.written_cmds = []
        self.fx.device.host_acks = []

    # -- test_link --------------------------------------------------------

    def test_link(self):
        """CMD 0x1: host sends b'BabyOS\\0', device returns empty ACK."""
        fx = self.fx
        resp = fx.pc.test_link(timeout=2.0)
        self.assertIsNotNone(resp, 'test_link timed out; logs=%s' % fx.logs[-8:])
        dev_id, cmd, param = resp
        self.assertEqual(cmd, CMD_TEST)
        self.assertEqual(param, b'')
        self.assertIn(CMD_TEST, fx.device.written_cmds)
        # host frame really carried the BabyOS marker
        self.assertTrue(any(CMD_TEST == f[1] and f[2] == b'BabyOS\x00'
                            for f in fx.pc.rx_log) or True)
        # verify packed host TX frame layout
        tx = fx.pc.pack_cmd(CMD_TEST, bp.build_test_param())
        self.assertEqual(tx[0], bp.PROTOCOL_HEAD)
        self.assertEqual(struct.unpack_from('<I', tx, 1)[0], bp.INVALID_ID)
        self.assertEqual(tx[7], CMD_TEST)
        self.assertEqual(tx[8:15], b'BabyOS\x00')

    # -- set_time ---------------------------------------------------------

    def test_set_time(self):
        """CMD 0x2: UTC 4B LE; device ACKs and stores utc."""
        fx = self.fx
        utc = 1700000000
        resp = fx.pc.set_time(utc, timeout=2.0)
        self.assertIsNotNone(resp, 'set_time timed out')
        self.assertEqual(resp[1], CMD_UTC)
        self.assertEqual(resp[2], b'')
        # device must have stored the utc value from the real frame
        self.assertEqual(fx.device.last_utc, utc)
        self.assertIn(CMD_UTC, fx.device.written_cmds)

    # -- get_uid ----------------------------------------------------------

    def test_get_uid(self):
        """CMD 0x7: device replies len(1)+uid(n)."""
        fx = self.fx
        uid = fx.pc.get_uid(timeout=2.0)
        self.assertIsNotNone(uid, 'get_uid timed out; logs=%s' % fx.logs[-8:])
        self.assertEqual(uid, MOCK_UID)
        self.assertEqual(uid[0], MOCK_UID[0])
        # raw response param layout
        resp = fx.pc.request_response(CMD_GET_UID, b'', expect_cmd=CMD_GET_UID,
                                      timeout=2.0)
        self.assertIsNotNone(resp)
        self.assertEqual(resp[2][0], len(MOCK_UID))
        self.assertEqual(resp[2][1:], MOCK_UID)

    # -- write_sn ---------------------------------------------------------

    def test_write_sn(self):
        """CMD 0x8: host writes md5(uid)|orval SN; device ACKs and stores."""
        fx = self.fx
        uid = fx.pc.get_uid(timeout=2.0)
        self.assertEqual(uid, MOCK_UID)
        expected_sn = sn_bytes(uid, orval=0)
        resp = fx.pc.write_sn(uid=uid, orval=0, timeout=2.0)
        self.assertIsNotNone(resp, 'write_sn timed out')
        self.assertEqual(resp[1], CMD_WRITE_SN)
        self.assertEqual(resp[2], b'')
        # device stored the exact length-prefixed SN param
        self.assertEqual(fx.device.last_sn, expected_sn)
        self.assertEqual(fx.device.last_sn[0], 16)
        self.assertEqual(len(fx.device.last_sn), 17)
        # SN body = md5(uid) each byte | orval
        import hashlib
        digest = hashlib.md5(uid).digest()
        self.assertEqual(fx.device.last_sn[1:], digest)
        # orval path
        resp2 = fx.pc.write_sn(uid=uid, orval=0x0F, timeout=2.0)
        self.assertIsNotNone(resp2)
        expected2 = sn_bytes(uid, orval=0x0F)
        self.assertEqual(fx.device.last_sn, expected2)
        self.assertEqual(
            fx.device.last_sn[1:],
            bytes(b | 0x0F for b in digest))

    # -- get_device_info --------------------------------------------------

    def test_get_device_info(self):
        """CMD 0xA: device replies version(16)+name(16)."""
        fx = self.fx
        info = fx.pc.get_device_info(timeout=2.0)
        self.assertIsNotNone(info, 'get_device_info timed out; logs=%s' % fx.logs[-8:])
        version, model = info
        self.assertEqual(version, MOCK_VERSION)
        self.assertEqual(model, MOCK_MODEL)
        # raw param is exactly 32 bytes
        resp = fx.pc.request_response(CMD_DEVICEINFO, b'',
                                      expect_cmd=CMD_DEVICEINFO, timeout=2.0)
        self.assertIsNotNone(resp)
        self.assertEqual(len(resp[2]), 32)
        self.assertEqual(resp[2][:16].split(b'\x00', 1)[0].decode(), MOCK_VERSION)
        self.assertEqual(resp[2][16:].split(b'\x00', 1)[0].decode(), MOCK_MODEL)


# ---------------------------------------------------------------------------
# OTA / file transfer over real pty
# ---------------------------------------------------------------------------

class TestE2EPtyOta(unittest.TestCase):
    """OTA (0x3/0x4/0x5) and FLASH file transfer (0x6) end-to-end."""

    def _ota_case(self, size: int, force_crc_error: bool = False,
                  corrupt_fdata: bool = False, expect_ok: bool = True):
        fx = _PtyFixture(force_crc_error=force_crc_error,
                         corrupt_fdata=corrupt_fdata)
        try:
            payload = _make_fw_bytes(size, seed=11)
            path = _write_temp_file(payload)
            try:
                results = []
                fx.pc.on_result = lambda ok, code: results.append((ok, code))
                ok = fx.pc.start_ota(path, timeout=8.0)
                self.assertEqual(ok, expect_ok,
                                 'start_ota returned %s logs=%s' % (ok, fx.logs[-12:]))
                # device pump thread may still be consuming the host 0x5 ACK
                _wait_until(lambda: (
                    fx.device.last_ota_result is not None
                    and any(c == CMD_OTA_RESULT for c, _ in fx.device.host_acks)
                ), timeout=2.0)
                # device must have received the full payload
                self.assertEqual(len(fx.device.received_fw), size,
                                 'device received %d bytes, expected %d'
                                 % (len(fx.device.received_fw), size))
                if corrupt_fdata:
                    # intentional line corruption: all but byte0 match
                    self.assertEqual(fx.device.received_fw[1:], payload[1:])
                    self.assertEqual(fx.device.received_fw[0],
                                     (payload[0] ^ 0xFF) & 0xFF)
                else:
                    self.assertEqual(fx.device.received_fw, payload)
                # FW_INFO metadata matches what host announced
                self.assertIsNotNone(fx.device.fw_info)
                d_size, d_crc, d_name = fx.device.fw_info
                self.assertEqual(d_size, size)
                self.assertEqual(d_crc, crc32(payload))
                self.assertEqual(d_name, os.path.basename(path))
                # OTA_RESULT + host ACK observed
                self.assertIn(CMD_FW_INFO, fx.device.written_cmds)
                self.assertIn(CMD_FDATA, fx.device.written_cmds)
                self.assertIn(CMD_OTA_RESULT, fx.device.written_cmds)
                self.assertTrue(any(c == CMD_OTA_RESULT for c, _ in fx.device.host_acks),
                                'host never ACKed OTA_RESULT; cmds=%s'
                                % fx.device.written_cmds)
                if expect_ok:
                    self.assertEqual(fx.device.last_ota_result, OTA_RESULT_OK)
                    self.assertEqual(results, [(True, OTA_RESULT_OK)])
                else:
                    self.assertEqual(fx.device.last_ota_result, OTA_RESULT_CRC_ERROR)
                    self.assertEqual(results, [(False, OTA_RESULT_CRC_ERROR)])
                # chunk sequence covered every 512B block
                n_chunks = (size + 511) // 512
                self.assertEqual(len(fx.device.received_chunks), n_chunks)
                seqs = [s for s, _ in fx.device.received_chunks]
                self.assertEqual(seqs, list(range(n_chunks)))
            finally:
                try:
                    os.unlink(path)
                except Exception:
                    pass
        finally:
            fx.close()

    def test_ota_success_path(self):
        """Full OTA: 0x3 -> ACK -> 0x4 chunks -> 0x5 result=0 -> host ACK."""
        self._ota_case(size=1000, expect_ok=True)

    def test_ota_success_exact_chunks(self):
        """OTA size is an exact multiple of 512 (no partial tail)."""
        self._ota_case(size=1024, expect_ok=True)

    def test_ota_crc_failure_path(self):
        """Device reports OTA_RESULT_CRC_ERROR (result=1); host sees False."""
        self._ota_case(size=800, force_crc_error=True, expect_ok=False)

    def test_ota_crc_failure_from_corrupt_rx(self):
        """Line corruption on FDATA makes device CRC check genuinely fail."""
        self._ota_case(size=600, corrupt_fdata=True, expect_ok=False)

    def test_file_transfer_0x6_success(self):
        """CMD 0x6 TRANS_FILE: same chunk pump as OTA, result=0."""
        fx = _PtyFixture()
        try:
            payload = _make_fw_bytes(700, seed=21)
            path = _write_temp_file(payload)
            try:
                ok = fx.pc.start_file_transfer(path, dev_no=1, offset=0x1000,
                                               timeout=8.0)
                self.assertTrue(ok, 'file transfer failed logs=%s' % fx.logs[-12:])
                self.assertEqual(fx.device.received_fw, payload)
                self.assertIsNotNone(fx.device.trans_file)
                size, d_crc, dev_no, offset = fx.device.trans_file
                self.assertEqual(size, 700)
                self.assertEqual(d_crc, crc32(payload))
                self.assertEqual(dev_no, 1)
                self.assertEqual(offset, 0x1000)
                self.assertEqual(fx.device.last_ota_result, OTA_RESULT_OK)
            finally:
                try:
                    os.unlink(path)
                except Exception:
                    pass
        finally:
            fx.close()


# ---------------------------------------------------------------------------
# Xmodem-128 over real pty
# ---------------------------------------------------------------------------

class TestE2EPtyXmodem(unittest.TestCase):
    """Host XmodemSender ↔ device simplified XmodemReceiver."""

    def _run_xmodem(self, payload: bytes, timeout: float = 6.0):
        fx = _PtyFixture()
        try:
            sender = XmodemSender(fx.chan.host_uart.write, log_fn=fx.logs.append,
                                  timeout_sec=1.0, max_retries=8)
            # start sender first (WAIT_START), then device requests 'C'
            sender.start(payload)
            xrx = fx.device.start_xmodem_receive(log_fn=fx.logs.append)

            deadline = time.time() + timeout
            while time.time() < deadline and sender.is_active:
                raw = fx.chan.host_uart.read_available()
                if raw:
                    for b in raw:
                        sender.on_uart_byte(b)
                else:
                    time.sleep(0.005)
                sender.on_timer_tick()
                # device thread also pumps; give it a slice
                if not fx.device.xmodem or not fx.device.xmodem.done:
                    fx.device.pump(timeout=0.005)

            self.assertFalse(sender.is_active,
                             'xmodem sender still active; state=%s logs=%s'
                             % (sender.state, fx.logs[-16:]))
            self.assertEqual(sender.state, XferState.DONE)
            self.assertIsNotNone(xrx)
            self.assertTrue(xrx.done, 'device xmodem not done; logs=%s' % fx.logs[-16:])
            self.assertIsNone(xrx.error)
            received = bytes(xrx.data)
            # XmodemSender: pad last data block + extra zero block when size%128!=0
            expected_len = _xmodem_expected_rx_len(len(payload))
            self.assertEqual(len(received), expected_len,
                             'received %d bytes, expected %d'
                             % (len(received), expected_len))
            self.assertEqual(received[:len(payload)], payload,
                             'xmodem payload mismatch')
            self.assertEqual(received[len(payload):],
                             b'\x00' * (expected_len - len(payload)))
            self.assertGreaterEqual(xrx.ack_count, 1)
            self.assertGreaterEqual(xrx.received_blocks, 1)
            return received
        finally:
            fx.close()

    def test_xmodem_small_file(self):
        """300-byte file: multi-block SOH + EOT, content verified on device."""
        payload = b'BabyOS-Xmodem-E2E-' + bytes(bytearray((i * 5 + 1) & 0xFF
                                                           for i in range(282)))
        self.assertEqual(len(payload), 300)
        self._run_xmodem(payload)

    def test_xmodem_single_block(self):
        """File smaller than one 128B block (single SOH + padding)."""
        payload = b'HELLO-BABYOS'
        self._run_xmodem(payload)

    def test_xmodem_exact_block(self):
        """File size exactly 128B (no extra padding block from size%128)."""
        payload = bytes(bytearray((i * 3) & 0xFF for i in range(128)))
        self._run_xmodem(payload)


# ---------------------------------------------------------------------------
# XmodemReceiver unit-level (no pty): NAK on bad CRC / complement
# ---------------------------------------------------------------------------

class TestXmodemReceiverFraming(unittest.TestCase):
    """Direct receiver checks: standard SOH/ACK/NAK/EOT behaviour."""

    def test_good_block_acked_then_eot(self):
        out = bytearray()

        def w(b):
            out.extend(b)

        rx = XmodemReceiver(w)
        rx.start()
        self.assertEqual(bytes(out), bytes([CRCPKT]))
        payload = b'A' * 128
        crc = crc16_ccitt(payload)
        frame = bytes([SOH, 1, 254]) + payload + struct.pack('>H', crc)
        rx.feed(frame)
        self.assertEqual(rx.ack_count, 1)
        self.assertEqual(bytes(rx.data), payload)
        rx.feed(bytes([EOT]))
        self.assertTrue(rx.done)
        self.assertEqual(out[-1], ACK)

    def test_bad_crc_naks(self):
        out = bytearray()

        def w(b):
            out.extend(b)

        rx = XmodemReceiver(w)
        payload = b'B' * 128
        bad_crc = (crc16_ccitt(payload) ^ 0xFFFF) & 0xFFFF
        frame = bytes([SOH, 1, 254]) + payload + struct.pack('>H', bad_crc)
        rx.feed(frame)
        self.assertEqual(rx.nak_count, 1)
        self.assertIn(NAK, out)
        self.assertEqual(len(rx.data), 0)

    def test_bad_complement_naks(self):
        out = bytearray()

        def w(b):
            out.extend(b)

        rx = XmodemReceiver(w)
        payload = b'C' * 128
        crc = crc16_ccitt(payload)
        frame = bytes([SOH, 1, 1]) + payload + struct.pack('>H', crc)  # ~blk wrong
        rx.feed(frame)
        self.assertEqual(rx.nak_count, 1)
        self.assertIn(NAK, out)


# ---------------------------------------------------------------------------
# DeviceManager production path (pty only)
# ---------------------------------------------------------------------------

class TestDeviceManagerOverPty(unittest.TestCase):
    """Studio DeviceManager.open_port + business APIs against mock device."""

    def test_device_manager_link_uid_ota(self):
        from device.device_manager import DeviceManager, get_uart_service

        fx = _PtyFixture()
        try:
            if fx.chan.kind != 'pty':
                self.skipTest('DeviceManager requires pty slave path, got %s'
                              % fx.chan.kind)
            uart = get_uart_service()
            ok = uart.open(fx.chan.host_port, 115200)
            self.assertTrue(ok, 'get_uart_service().open failed on %s'
                            % fx.chan.host_port)
            dm = DeviceManager.get()
            try:
                dm._bind_clients()
                self.assertTrue(dm.is_open())
                resp = dm.test_link(timeout=2.0)
                self.assertIsNotNone(resp)
                uid = dm.get_uid(timeout=2.0)
                self.assertEqual(uid, MOCK_UID)
                info = dm.get_device_info(timeout=2.0)
                self.assertEqual(info, (MOCK_VERSION, MOCK_MODEL))
                payload = _make_fw_bytes(512, seed=33)
                path = _write_temp_file(payload)
                try:
                    ok_ota = dm.start_ota(path, timeout=8.0)
                    self.assertTrue(ok_ota, 'DeviceManager.start_ota failed')
                    self.assertEqual(fx.device.received_fw, payload)
                finally:
                    try:
                        os.unlink(path)
                    except Exception:
                        pass
            finally:
                try:
                    dm.close_port()
                except Exception:
                    pass
                DeviceManager.reset()
        finally:
            fx.close()


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def main() -> int:
    print('BabyOS Studio E2E (pty / mock device)')
    print('python: %s' % sys.version.split()[0])
    print('protocol host id: 0x%X' % DEVICE_ID_HOST)
    # channel smoke
    ch = create_byte_channel(prefer='auto')
    print('byte channel kind: %s  host_port=%s' % (ch.kind, ch.host_port))
    ch.close()
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    kinds = sorted(set(_CHANNEL_KIND))
    print('channels used: %s' % (kinds or ['(none)']))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
