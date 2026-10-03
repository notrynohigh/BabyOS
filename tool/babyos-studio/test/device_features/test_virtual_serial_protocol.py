#!/usr/bin/env python3
"""
test_virtual_serial_protocol — SDK-layer virtual-serial (pty) protocol acceptance.

Authority (硬性验收):
  1. 串口/协议必须走真实 pty 双向字节通道 — 禁止只测接口/内存 mock。
  2. 主机侧真实栈: UartService / ProtocolClient / ShellClient /
     Xmodem / DeviceManager → pty 一端。
  3. 设备侧 mock_babyos_device 在 pty 另一端真实解析 BabyOS 帧并回包。
  4. 覆盖: open/close、CMD 0x1/0x2/0x7/0x8/0xA、0x3/0x4/0x5 OTA 成功
     + 至少 2 种失败、0x6 文件、Xmodem-128 小文件/边界块、Ymodem-1K、
     Shell param list/get/set、TEA 加密路径。
  5. 每个用例断言:
       - 主机发出的字节可被 Mock 设备独立 parse
       - 设备回包可被主机独立 parse
       - 最终状态与文件内容一致
  6. 通道必须 kind=pty（socketpair 降级单独分类，不混入主结果）。
  7. HTTPS 证书必须是 origin/dev tool 目录证书（校验，不自签）。
  8. Python 3.8 兼容；禁止 stub；不提交 git。

Run:
  /home/yyds/code/BabyOS/tool/babyos-studio/python/.venv/bin/python \\
      /home/yyds/code/BabyOS/tool/babyos-studio/test/device_features/test_virtual_serial_protocol.py
  or pytest on the same path.

Emits TEST_SCHEMA JSON on stdout at the end (see main()).
"""

from __future__ import print_function

import hashlib
import json
import os
import ssl
import subprocess
import sys
import tempfile
import time
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
_REPO = os.path.abspath(os.path.join(_STUDIO, '..', '..'))
for _p in (_PY_ROOT, _HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from device import b_protocol as bp  # noqa: E402
from device.crc_util import crc32  # noqa: E402
from device.protocol_client import (  # noqa: E402
    ProtocolClient,
    CMD_TEST, CMD_UTC, CMD_FW_INFO, CMD_FDATA, CMD_OTA_RESULT,
    CMD_TRANS_FILE, CMD_GET_UID, CMD_WRITE_SN, CMD_DEVICEINFO,
    OTA_RESULT_OK, OTA_RESULT_CRC_ERROR, OTA_RESULT_NAME_MISMATCH,
    OTA_RESULT_LEN_INVALID,
    DEVICE_ID_HOST,
)
from device.shell_client import ShellClient  # noqa: E402
from device.uart_service import UartService  # noqa: E402
from device.xmodem_ydmodem import (  # noqa: E402
    XmodemSender, YmodemSender, XferState,
)
from mock_babyos_device import (  # noqa: E402
    MockBabyOSDevice,
    create_byte_channel,
    open_api_over_pty,
    DEFAULT_SHELL_PARAMS,
)

MOCK_UID = b'\x11\x22\x33\x44\x55\x66\x77\x88\x99\xAA\xBB\xCC'
MOCK_VERSION = 'v1.2.3'
MOCK_MODEL = 'BabyOS-MCU'
MOCK_DEV_ID = 0x0000ABCD

TEST_SCHEMA_ID = 'babyos_virtual_serial_protocol_v1'

# ---------------------------------------------------------------------------
# Coverage matrix registry
# ---------------------------------------------------------------------------

_MATRIX = []  # type: list
_CERT_INFO = {}  # type: dict


def _matrix_add(case_id, channel, status, detail=''):
    _MATRIX.append({
        'id': case_id,
        'channel': channel,
        'status': status,
        'detail': detail,
    })


def _write_temp(data, suffix='.bin'):
    fd, path = tempfile.mkstemp(suffix=suffix, prefix='babyos_vsproto_')
    try:
        os.write(fd, data)
    finally:
        os.close(fd)
    return path


def _make_fw(n, seed=3):
    return bytes(bytearray(((i * 7) + seed) & 0xFF for i in range(n)))


def _wait(pred, timeout=3.0, interval=0.01):
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


def _parse_one_frame(raw, encrypt=False):
    """
    Independently parse one BabyOS frame from raw bytes.

    Returns (device_id, cmd, param, encrypted_bool) or None.
    TEA-decrypts first when encrypt=True (firmware _bProtocolDecrypt).
    """
    if not raw:
        return None
    data = bytes(raw)
    if encrypt:
        data = bp.tea_decrypt(data)
    parsed = bp.parse_frame(data)
    if parsed is None:
        return None
    dev_id, cmd, param = parsed
    return dev_id, cmd, param, bool(encrypt)


def _parse_stream_frames(raw, encrypt=False):
    """Parse all complete frames from a raw byte stream (plaintext replies)."""
    frames = []
    buf = bytearray(raw)
    while buf:
        parsed = _parse_one_frame(bytes(buf), encrypt=False)
        if parsed is not None:
            dev_id, cmd, param, _enc = parsed
            frame_len = 1 + len(param)
            total = bp.FRAME_HEADER_SIZE + frame_len
            if total > len(buf):
                break
            del buf[:total]
            frames.append((dev_id, cmd, param, False))
            continue
        if encrypt:
            dec = bp.tea_decrypt(bytes(buf))
            parsed = _parse_one_frame(dec, encrypt=False)
            if parsed is not None:
                dev_id, cmd, param, _enc = parsed
                frame_len = 1 + len(param)
                total = bp.FRAME_HEADER_SIZE + frame_len
                if total > len(buf):
                    break
                del buf[:total]
                frames.append((dev_id, cmd, param, True))
                continue
        idx = buf.find(bytes([bp.PROTOCOL_HEAD]))
        if idx < 0:
            break
        if idx == 0:
            del buf[0]
            continue
        del buf[:idx]
    return frames


class _CapturingDevice(MockBabyOSDevice):
    """
    MockBabyOSDevice that records every raw TX frame byte-for-byte.

    TX capture happens inside _send_frame (same path as firmware replies),
    so DeviceEndpoint still owns the real pty master fd.
    """

    def __init__(self, *args, **kwargs):
        MockBabyOSDevice.__init__(self, *args, **kwargs)
        self.tx_bytes = bytearray()

    def _send_frame(self, cmd, param=b''):
        frame = bp.pack_frame(self.device_id, cmd, param or b'',
                              encrypt=False)
        self.tx_bytes.extend(frame)
        self.endpoint.write(frame)
        self._log('[mock-device] tx cmd=0x%02X len=%d'
                  % (cmd & 0xFF, len(frame)))


class _MockOtaNameMismatch(_CapturingDevice):
    """Device always reports NAME_MISMATCH after the FDATA stream finishes."""

    def _handle_fdata(self, param):
        parsed = bp.parse_fdata_param(param)
        if parsed is None:
            return
        seq, data = parsed
        size, _crc = self._transfer_size_crc()
        self.received_chunks.append((seq, bytes(data)))
        base = seq * 512
        take = max(0, size - base) if size else len(data)
        self.received_fw_bytes.extend(data[:take])
        next_seq = seq + 1
        if size and next_seq * 512 < size:
            self._send_frame(CMD_FDATA, bp.build_fdata_req_param(next_seq))
            return
        result = OTA_RESULT_NAME_MISMATCH
        self.last_ota_result = result
        self._send_frame(CMD_OTA_RESULT, bytes([result & 0xFF]))


class _MockOtaTimeout(_CapturingDevice):
    """Device ACKs FW_INFO and streams FDATA but never sends OTA_RESULT."""

    def _handle(self, cmd, param):
        if cmd == CMD_FW_INFO:
            parsed = bp.parse_fw_info_param(param)
            self.fw_info = parsed
            self._reset_transfer_state()
            self._send_frame(CMD_FW_INFO, b'')
            if parsed is not None and parsed[0] > 0:
                self._send_frame(CMD_FDATA, bp.build_fdata_req_param(0))
            return
        if cmd == CMD_FDATA:
            parsed = bp.parse_fdata_param(param)
            if parsed is None:
                return
            seq, data = parsed
            size = self.fw_info[0] if self.fw_info else 0
            self.received_chunks.append((seq, bytes(data)))
            base = seq * 512
            take = max(0, size - base) if size else len(data)
            self.received_fw_bytes.extend(data[:take])
            if size and (seq + 1) * 512 < size:
                self._send_frame(CMD_FDATA, bp.build_fdata_req_param(seq + 1))
            return
        MockBabyOSDevice._handle(self, cmd, param)


def _assert_host_tx_parseable(pc, encrypt, expect_cmds=None):
    """Assert every packed host TX frame is independently parseable."""
    tx = pc.tx_log
    assert tx, 'ProtocolClient.tx_log empty — nothing written to UART'
    parsed_cmds = []
    for raw in tx:
        parsed = _parse_one_frame(raw, encrypt=encrypt)
        assert parsed is not None, (
            'host TX frame not independently parseable '
            '(encrypt=%s): %s' % (encrypt, raw[:24].hex()))
        _dev_id, cmd, _param, _enc = parsed
        parsed_cmds.append(cmd)
        plain = bp.tea_decrypt(raw) if encrypt else raw
        again = bp.parse_frame(plain)
        assert again is not None
        assert again[1] == cmd
    if expect_cmds:
        for c in expect_cmds:
            assert c in parsed_cmds, (
                'expected cmd 0x%02X in host TX parse; got %s'
                % (c, parsed_cmds))
    return parsed_cmds


def _assert_device_tx_parseable(device, expect_cmds=None):
    """Assert mock-device raw TX bytes parse as BabyOS frames."""
    raw = bytes(getattr(device, 'tx_bytes', b'') or b'')
    assert raw, 'device TX capture empty'
    frames = _parse_stream_frames(raw, encrypt=False)
    assert frames, 'device TX not parseable as BabyOS frames: %s' % raw[:32].hex()
    cmds = [f[1] for f in frames]
    if expect_cmds:
        for c in expect_cmds:
            assert c in cmds or c in device.written_cmds, (
                'expected device cmd 0x%02X; tx_cmds=%s written=%s'
                % (c, cmds, device.written_cmds))
    return cmds


def _cert_paths():
    from device import http_mock as hm
    return hm._resolve_https_cert()


def _verify_origin_dev_certs():
    """Ensure on-disk HTTPS certs match origin/dev tool/ (no openssl gen)."""
    global _CERT_INFO
    info = {
        'ok': False,
        'repo_tool_cert': os.path.join(_REPO, 'tool', 'mock_https_cert.pem'),
        'repo_tool_key': os.path.join(_REPO, 'tool', 'mock_https_key.pem'),
        'pkg_cert': os.path.join(_PY_ROOT, 'device', 'certs',
                                 'mock_https_cert.pem'),
        'pkg_key': os.path.join(_PY_ROOT, 'device', 'certs',
                                'mock_https_key.pem'),
        'resolved': None,
    }
    try:
        cert, key = _cert_paths()
        info['resolved'] = [cert, key]
        assert os.path.isfile(cert) and os.path.isfile(key)
        assert os.path.isfile(info['repo_tool_cert'])
        assert os.path.isfile(info['repo_tool_key'])
        assert os.path.isfile(info['pkg_cert'])
        assert os.path.isfile(info['pkg_key'])
        dev_cert = subprocess.check_output(
            ['git', '-C', _REPO, 'show',
             'origin/dev:tool/mock_https_cert.pem'])
        dev_key = subprocess.check_output(
            ['git', '-C', _REPO, 'show',
             'origin/dev:tool/mock_https_key.pem'])
        with open(info['repo_tool_cert'], 'rb') as f:
            disk_cert = f.read()
        with open(info['repo_tool_key'], 'rb') as f:
            disk_key = f.read()
        with open(info['pkg_cert'], 'rb') as f:
            pkg_cert = f.read()
        with open(info['pkg_key'], 'rb') as f:
            pkg_key = f.read()
        assert disk_cert == dev_cert, 'repo tool cert != origin/dev'
        assert disk_key == dev_key, 'repo tool key != origin/dev'
        assert pkg_cert == dev_cert, 'package cert != origin/dev'
        assert pkg_key == dev_key, 'package key != origin/dev'
        assert os.path.abspath(cert) in (
            os.path.abspath(info['repo_tool_cert']),
            os.path.abspath(info['pkg_cert']),
        ), 'runtime resolved cert is not a repo/dev cert: %s' % cert
        der = ssl.PEM_cert_to_DER_cert(disk_cert.decode('ascii'))
        info['cert_sha256'] = hashlib.sha256(der).hexdigest().upper()
        info['ok'] = True
    except Exception as exc:
        info['error'] = str(exc)
        info['ok'] = False
    _CERT_INFO = info
    return info


# ---------------------------------------------------------------------------
# Base fixture — every case uses real pty bytes
# ---------------------------------------------------------------------------

class _PtyBase(unittest.TestCase):
    """Shared pty fixture. Channel kind must be 'pty' for acceptance."""

    prefer = 'pty'
    require_pty = True
    case_id = 'unset'
    device_cls = _CapturingDevice
    device_extra = None  # type: dict

    def setUp(self):
        self.logs = []
        self.chan = create_byte_channel(
            prefer=self.prefer,
            open_host_uart=True,
            require_pty=self.require_pty,
        )
        self.assertEqual(
            self.chan.kind, 'pty',
            'acceptance requires kind=pty, got %s' % self.chan.kind)
        self.channel_kind = self.chan.kind
        self.assertTrue(self.chan.host_uart is not None
                        and isinstance(self.chan.host_uart, UartService))
        kwargs = dict(
            device_id=MOCK_DEV_ID, uid=MOCK_UID,
            version=MOCK_VERSION, model=MOCK_MODEL,
            log_fn=self.logs.append,
        )
        extra = self.device_extra or {}
        kwargs.update(extra)
        self.device = self.device_cls(self.chan.device_endpoint(), **kwargs)
        self.device.start()
        self.pc = ProtocolClient(self.chan.host_uart, log_fn=self.logs.append)
        self._marked = False

    def tearDown(self):
        # If the test method raised before _mark(), still record the miss
        # so the coverage matrix cannot silently omit a failing case.
        if not getattr(self, '_marked', False) and self.case_id \
                and self.case_id != 'unset':
            _matrix_add(self.case_id, self.channel_kind, 'FAIL',
                        'assertion aborted before _mark')
        try:
            self.device.stop()
        except Exception:
            pass
        try:
            self.chan.close()
        except Exception:
            pass

    def _mark(self, ok, detail=''):
        self._marked = True
        status = 'PASS' if ok else 'FAIL'
        _matrix_add(self.case_id, self.channel_kind, status, detail)


# ---------------------------------------------------------------------------
# 1. Serial open/close
# ---------------------------------------------------------------------------

class TestSerialOpenClose(_PtyBase):
    case_id = 'serial_open_close'

    def test_open_close_cycle_over_pty(self):
        uart = self.chan.host_uart
        self.assertTrue(uart.is_open)
        self.assertTrue(os.path.exists(self.chan.host_port))
        port = self.chan.host_port

        uart.close()
        self.assertFalse(uart.is_open)
        n = uart.write(b'\x01\x02\x03')
        self.assertTrue(n is None or n < 0)

        ok = uart.open(port, 115200, timeout=0.02, write_timeout=1.0)
        self.assertTrue(ok, 'reopen failed on %s logs=%s'
                        % (port, self.logs[-6:]))
        self.assertTrue(uart.is_open)

        resp = self.pc.test_link(timeout=2.0)
        self.assertIsNotNone(resp, self.logs[-8:])
        self.assertEqual(resp[1], CMD_TEST)
        _assert_host_tx_parseable(self.pc, encrypt=False,
                                  expect_cmds=[CMD_TEST])
        self.assertIn(CMD_TEST, self.device.written_cmds)
        _assert_device_tx_parseable(self.device, expect_cmds=[CMD_TEST])
        self._mark(True, 'reopen + test_link ok')


# ---------------------------------------------------------------------------
# 2. Protocol CMD 0x1 / 0x2 / 0x7 / 0x8 / 0xA
# ---------------------------------------------------------------------------

class TestProtocolCmds(_PtyBase):
    case_id = 'protocol_cmds'

    def _assert_roundtrip(self, host_cmds, device_cmds):
        _assert_host_tx_parseable(self.pc, encrypt=False, expect_cmds=host_cmds)
        _assert_device_tx_parseable(self.device, expect_cmds=device_cmds)
        assert self.device.frames_handled > 0

    def test_cmd_0x1_test_link(self):
        self.case_id = 'protocol_cmd_0x1'
        resp = self.pc.test_link(timeout=2.0)
        self.assertIsNotNone(resp, self.logs[-8:])
        self.assertEqual(resp[1], CMD_TEST)
        self.assertEqual(resp[2], b'')
        self.assertIn(CMD_TEST, self.device.written_cmds)
        self._assert_roundtrip([CMD_TEST], [CMD_TEST])
        self._mark(True, 'bidirectional parse + empty ACK')

    def test_cmd_0x2_utc(self):
        self.case_id = 'protocol_cmd_0x2'
        utc = 1700000000
        resp = self.pc.set_time(utc, timeout=2.0)
        self.assertIsNotNone(resp)
        self.assertEqual(resp[1], CMD_UTC)
        self.assertEqual(self.device.last_utc, utc)
        self._assert_roundtrip([CMD_UTC], [CMD_UTC])
        self._mark(True, 'utc=%d stored' % utc)

    def test_cmd_0x7_get_uid(self):
        self.case_id = 'protocol_cmd_0x7'
        uid = self.pc.get_uid(timeout=2.0)
        self.assertEqual(uid, MOCK_UID)
        self._assert_roundtrip([CMD_GET_UID], [CMD_GET_UID])
        self._mark(True, 'uid=%s' % uid.hex())

    def test_cmd_0x8_write_sn(self):
        self.case_id = 'protocol_cmd_0x8'
        sn_body = bytes(bytearray(range(16)))
        param = bytes([len(sn_body)]) + sn_body
        resp = self.pc.write_sn(sn_bytes=param, timeout=2.0)
        self.assertIsNotNone(resp)
        self.assertEqual(resp[1], CMD_WRITE_SN)
        self.assertEqual(self.device.last_sn, param)
        self._assert_roundtrip([CMD_WRITE_SN], [CMD_WRITE_SN])
        self._mark(True, 'sn stored len=%d' % len(param))

    def test_cmd_0xA_device_info(self):
        self.case_id = 'protocol_cmd_0xA'
        info = self.pc.get_device_info(timeout=2.0)
        self.assertEqual(info, (MOCK_VERSION, MOCK_MODEL))
        self._assert_roundtrip([CMD_DEVICEINFO], [CMD_DEVICEINFO])
        self._mark(True, 'version=%s model=%s' % info)


# ---------------------------------------------------------------------------
# 3. OTA 0x3/0x4/0x5 — success + failure modes
# ---------------------------------------------------------------------------

class TestOtaSuccess(_PtyBase):
    case_id = 'protocol_ota_0x3_0x4_0x5_success'

    def test_ota_success_roundtrip(self):
        payload = _make_fw(600, seed=33)
        path = _write_temp(payload)
        try:
            ok = self.pc.start_ota(path, timeout=8.0)
            self.assertTrue(ok, self.logs[-12:])
        finally:
            os.unlink(path)
        self.assertEqual(self.device.received_fw[:len(payload)], payload)
        self.assertEqual(len(self.device.received_fw), len(payload))
        self.assertEqual(self.device.last_ota_result, OTA_RESULT_OK)
        self.assertEqual(self.device.fw_info[0], len(payload))
        self.assertEqual(self.device.fw_info[1], crc32(payload))
        self.assertIn(CMD_FW_INFO, self.device.written_cmds)
        self.assertIn(CMD_FDATA, self.device.written_cmds)
        # Host ACK (0x5) may still be in flight when start_ota returns —
        # wait until the mock device has parsed it on the wire.
        self.assertTrue(
            _wait(lambda: any(a[0] == CMD_OTA_RESULT
                              for a in self.device.host_acks)
                  or CMD_OTA_RESULT in self.device.written_cmds,
                  timeout=2.0),
            'host OTA ACK never parsed by device; snapshot=%s'
            % self.device.snapshot())
        _assert_host_tx_parseable(self.pc, encrypt=False,
                                  expect_cmds=[CMD_FW_INFO, CMD_FDATA,
                                               CMD_OTA_RESULT])
        _assert_device_tx_parseable(self.device,
                                    expect_cmds=[CMD_FW_INFO, CMD_FDATA,
                                                 CMD_OTA_RESULT])
        self._mark(True, 'payload=%d crc=0x%08X'
                   % (len(payload), crc32(payload)))


class TestOtaFailureCrcError(_PtyBase):
    case_id = 'protocol_ota_failure_crc_error'

    def test_ota_failure_crc_error(self):
        self.device.force_crc_error = True
        payload = _make_fw(200, seed=1)
        path = _write_temp(payload)
        try:
            ok = self.pc.start_ota(path, timeout=8.0)
        finally:
            os.unlink(path)
        self.assertFalse(ok)
        self.assertEqual(self.device.last_ota_result, OTA_RESULT_CRC_ERROR)
        _assert_host_tx_parseable(self.pc, encrypt=False)
        _assert_device_tx_parseable(self.device)
        self._mark(True, 'CRC_ERROR reported')


class TestOtaFailureLenInvalid(_PtyBase):
    case_id = 'protocol_ota_failure_len_invalid'

    def test_ota_failure_len_invalid(self):
        param = bp.build_fw_info_param(0, 0, 'empty.bin')
        resp = self.pc.request_response(CMD_FW_INFO, param,
                                        expect_cmd=CMD_FW_INFO, timeout=2.0)
        self.assertIsNotNone(resp)
        got = None
        deadline = time.time() + 2.0
        while time.time() < deadline and got is None:
            for _dev, cmd, p in self.pc.rx_log:
                if cmd == CMD_OTA_RESULT and p:
                    got = p[0]
            if got is None:
                raw = self.chan.host_uart.read_available()
                if raw:
                    self.pc.feed_bytes(raw)
                else:
                    time.sleep(0.01)
        self.assertEqual(got, OTA_RESULT_LEN_INVALID)
        self.assertEqual(self.device.last_ota_result, OTA_RESULT_LEN_INVALID)
        _assert_host_tx_parseable(self.pc, encrypt=False,
                                  expect_cmds=[CMD_FW_INFO])
        _assert_device_tx_parseable(self.device,
                                    expect_cmds=[CMD_FW_INFO, CMD_OTA_RESULT])
        self._mark(True, 'LEN_INVALID reported')


class TestOtaFailureNameMismatch(_PtyBase):
    case_id = 'protocol_ota_failure_name_mismatch'
    device_cls = _MockOtaNameMismatch

    def test_ota_failure_name_mismatch(self):
        payload = _make_fw(300, seed=2)
        path = _write_temp(payload, suffix='.wrong_name.bin')
        try:
            ok = self.pc.start_ota(path, name='firmware_v2.bin', timeout=8.0)
        finally:
            os.unlink(path)
        self.assertFalse(ok)
        self.assertEqual(self.device.last_ota_result, OTA_RESULT_NAME_MISMATCH)
        _assert_host_tx_parseable(self.pc, encrypt=False)
        _assert_device_tx_parseable(self.device)
        self._mark(True, 'NAME_MISMATCH reported')


class TestOtaFailureTimeout(_PtyBase):
    case_id = 'protocol_ota_failure_timeout'
    device_cls = _MockOtaTimeout

    def test_ota_failure_timeout(self):
        payload = _make_fw(400, seed=4)
        path = _write_temp(payload)
        t0 = time.time()
        try:
            ok = self.pc.start_ota(path, timeout=2.0)
        finally:
            os.unlink(path)
        elapsed = time.time() - t0
        self.assertFalse(ok)
        self.assertGreaterEqual(elapsed, 1.5, 'timeout returned too early')
        self.assertIn(CMD_FW_INFO, self.device.written_cmds)
        _assert_host_tx_parseable(self.pc, encrypt=False,
                                  expect_cmds=[CMD_FW_INFO, CMD_FDATA])
        _assert_device_tx_parseable(self.device,
                                    expect_cmds=[CMD_FW_INFO, CMD_FDATA])
        self._mark(True, 'host timed out after %.2fs without OTA_RESULT'
                   % elapsed)


class TestFileTransferCmd6(_PtyBase):
    case_id = 'protocol_file_0x6'

    def test_file_xfer_success(self):
        payload = _make_fw(300, seed=9)
        path = _write_temp(payload)
        try:
            ok = self.pc.start_file_transfer(path, dev_no=1, offset=0x10,
                                             timeout=8.0)
        finally:
            os.unlink(path)
        self.assertTrue(ok, self.logs[-12:])
        self.assertEqual(self.device.received_fw[:len(payload)], payload)
        self.assertEqual(self.device.trans_file[0], len(payload))
        self.assertEqual(self.device.trans_file[1], crc32(payload))
        self.assertEqual(self.device.trans_file[2], 1)
        self.assertEqual(self.device.trans_file[3], 0x10)
        self.assertEqual(self.device.last_ota_result, OTA_RESULT_OK)
        self.assertIn(CMD_TRANS_FILE, self.device.written_cmds)
        self.assertIn(CMD_FDATA, self.device.written_cmds)
        _assert_host_tx_parseable(self.pc, encrypt=False,
                                  expect_cmds=[CMD_TRANS_FILE, CMD_FDATA])
        _assert_device_tx_parseable(self.device,
                                    expect_cmds=[CMD_TRANS_FILE, CMD_FDATA])
        self._mark(True, 'file=%d dev_no=1 offset=0x10' % len(payload))


# ---------------------------------------------------------------------------
# 4. TEA encrypt path (ProtocolClient.encrypt=True)
# ---------------------------------------------------------------------------

class TestTeaEncrypt(_PtyBase):
    case_id = 'tea_encrypt'
    device_extra = {'encrypt': True}

    def test_tea_cmd_roundtrip_and_ciphertext(self):
        self.case_id = 'tea_encrypt_cmds'
        self.assertTrue(self.device.encrypt)
        self.pc.encrypt = True

        resp = self.pc.test_link(timeout=2.0)
        self.assertIsNotNone(resp, self.logs[-10:])
        self.assertEqual(resp[1], CMD_TEST)
        self.assertIn(CMD_TEST, self.device.written_cmds)

        uid = self.pc.get_uid(timeout=2.0)
        self.assertEqual(uid, MOCK_UID)

        info = self.pc.get_device_info(timeout=2.0)
        self.assertEqual(info, (MOCK_VERSION, MOCK_MODEL))

        frames = self.pc.tx_log
        self.assertTrue(frames)
        cipher_heads = [f[0] for f in frames]
        self.assertLessEqual(cipher_heads.count(bp.PROTOCOL_HEAD), 1,
                             'encrypt=True frames still look plaintext: %s'
                             % cipher_heads)
        for raw in frames:
            parsed = _parse_one_frame(raw, encrypt=True)
            self.assertIsNotNone(parsed,
                                 'encrypted host TX not decrypt-parseable')
        dev_frames = _parse_stream_frames(bytes(self.device.tx_bytes))
        self.assertTrue(dev_frames)
        _assert_device_tx_parseable(self.device,
                                    expect_cmds=[CMD_TEST, CMD_GET_UID,
                                                 CMD_DEVICEINFO])
        self._mark(True, 'TEA TX ciphertext + RX plaintext parse ok')

    def test_tea_ota_over_pty(self):
        self.case_id = 'tea_encrypt_ota'
        self.pc.encrypt = True
        payload = _make_fw(512, seed=21)
        path = _write_temp(payload)
        try:
            ok = self.pc.start_ota(path, timeout=8.0)
        finally:
            os.unlink(path)
        self.assertTrue(ok, self.logs[-12:])
        self.assertEqual(self.device.received_fw[:len(payload)], payload)
        self.assertEqual(self.device.last_ota_result, OTA_RESULT_OK)
        self.assertIn(CMD_FW_INFO, self.device.written_cmds)
        _assert_host_tx_parseable(self.pc, encrypt=True,
                                  expect_cmds=[CMD_FW_INFO, CMD_FDATA])
        self._mark(True, 'encrypted OTA payload=%d' % len(payload))


# ---------------------------------------------------------------------------
# 5. Xmodem-128 — small file + boundary blocks
# ---------------------------------------------------------------------------

class TestXmodem128(_PtyBase):
    case_id = 'xmodem_128'

    def _run(self, payload, case_id):
        self.case_id = case_id
        self.device.switch_to_xmodem()
        uart = self.chan.host_uart

        def _send(buf):
            n = uart.write(buf)
            if n is None or n < 0:
                raise IOError('host uart write failed')

        sender = XmodemSender(_send, log_fn=self.logs.append)
        sender.start(payload)
        deadline = time.time() + 8.0
        while time.time() < deadline and sender.is_active:
            raw = uart.read_available()
            if raw:
                for b in raw:
                    sender.on_uart_byte(b)
            sender.on_timer_tick()
            time.sleep(0.002)
        raw = uart.read_available()
        if raw:
            for b in raw:
                sender.on_uart_byte(b)
        self.assertEqual(sender.state, XferState.DONE,
                         'xmodem state=%s logs=%s' % (sender.state, self.logs[-10:]))
        got = bytes(self.device.received_xmodem)
        self.assertGreaterEqual(len(got), len(payload))
        self.assertEqual(got[:len(payload)], payload,
                         'xmodem content mismatch len got=%d want=%d'
                         % (len(got), len(payload)))
        self.assertGreaterEqual(self.device.xmodem.ack_count, 1)
        self.assertTrue(self.device.xmodem.done)
        self._mark(True, 'bytes=%d acks=%d blocks=%d'
                   % (len(payload), self.device.xmodem.ack_count,
                      self.device.xmodem.received_blocks))

    def test_xmodem_small_file(self):
        self._run(_make_fw(200, seed=5), 'xmodem_128_small')

    def test_xmodem_boundary_exact_128(self):
        self._run(_make_fw(128, seed=6), 'xmodem_128_boundary_exact_128')

    def test_xmodem_boundary_129(self):
        self._run(_make_fw(129, seed=7), 'xmodem_128_boundary_129')

    def test_xmodem_boundary_256(self):
        self._run(_make_fw(256, seed=8), 'xmodem_128_boundary_256')


# ---------------------------------------------------------------------------
# 6. Ymodem-1K — name + size + padding
# ---------------------------------------------------------------------------

class TestYmodem1K(_PtyBase):
    case_id = 'ymodem_1k'

    def _run(self, payload, filename, case_id):
        self.case_id = case_id
        self.device.switch_to_ymodem()
        uart = self.chan.host_uart

        def _send(buf):
            n = uart.write(buf)
            if n is None or n < 0:
                raise IOError('host uart write failed')

        sender = YmodemSender(_send, log_fn=self.logs.append)
        sender.start(payload, filename=filename)
        deadline = time.time() + 10.0
        while time.time() < deadline and sender.is_active:
            raw = uart.read_available()
            if raw:
                for b in raw:
                    sender.on_uart_byte(b)
            sender.on_timer_tick()
            time.sleep(0.002)
        raw = uart.read_available()
        if raw:
            for b in raw:
                sender.on_uart_byte(b)
        self.assertEqual(sender.state, XferState.DONE,
                         'ymodem state=%s logs=%s' % (sender.state, self.logs[-10:]))
        self.assertEqual(self.device.ymodem.filename, filename)
        self.assertEqual(self.device.ymodem.file_size, len(payload))
        got = bytes(self.device.received_file)
        self.assertEqual(got[:len(payload)], payload)
        self.assertEqual(self.device.received_file_name, filename)
        self._mark(True, 'name=%r size=%d acks=%d'
                   % (filename, len(payload), self.device.ymodem.ack_count))

    def test_ymodem_1k_with_name_size(self):
        self._run(_make_fw(1300, seed=11), 'fw_update.bin',
                  'ymodem_1k_name_size')

    def test_ymodem_1k_exact_1024(self):
        self._run(_make_fw(1024, seed=12), 'exact1k.bin',
                  'ymodem_1k_exact_1024')

    def test_ymodem_1k_1500_pad(self):
        self._run(_make_fw(1500, seed=13), 'padded.bin',
                  'ymodem_1k_1500_pad')


# ---------------------------------------------------------------------------
# 7. Shell param list / get / set (text protocol over same pty)
# ---------------------------------------------------------------------------

class TestShellParam(_PtyBase):
    case_id = 'shell_param'

    def test_param_list_get_set(self):
        self.case_id = 'shell_param_list_get_set'
        self.device.switch_to_shell()
        sh = ShellClient(self.chan.host_uart)
        names = sh.param_list(timeout=1.5)
        self.assertIn('g_param_test_val', names)
        self.assertIn('g_volume', names)

        val = sh.param_get('g_param_test_val', timeout=1.5)
        self.assertEqual(val, DEFAULT_SHELL_PARAMS['g_param_test_val'])

        ok = sh.param_set('g_volume', 80, timeout=1.5, verify=True)
        self.assertTrue(ok, self.logs[-8:])
        self.assertEqual(self.device.shell_params['g_volume'], 80)

        val2 = sh.param_get('g_volume', timeout=1.5)
        self.assertEqual(val2, 80)

        self.assertTrue(any(cmd == 'param' for cmd, _ in self.device.shell_log))
        self._mark(True, 'list/get/set over pty text path')

    def test_param_set_and_reread(self):
        self.case_id = 'shell_param_set_reread'
        self.device.switch_to_shell(params={'g_new_key': 1})
        sh = ShellClient(self.chan.host_uart)
        ok = sh.param_set('g_new_key', 42, timeout=1.5, verify=True)
        self.assertTrue(ok)
        self.assertEqual(self.device.shell_params['g_new_key'], 42)
        val = sh.param_get('g_new_key', timeout=1.5)
        self.assertEqual(val, 42)
        self._mark(True, 'new key set+verify')


# ---------------------------------------------------------------------------
# 8. DeviceManager → real UART → pty (SDK production path)
# ---------------------------------------------------------------------------

class TestDeviceManagerOverPty(unittest.TestCase):
    case_id = 'device_manager_over_pty'

    def setUp(self):
        from device.device_manager import DeviceManager
        DeviceManager.reset()
        self.addCleanup(DeviceManager.reset)
        self.link = open_api_over_pty(
            encrypt=False, open_host_uart=False, bind_device_manager=False)
        self.addCleanup(self.link.close)
        self.assertEqual(self.link.kind, 'pty')
        self.channel_kind = self.link.kind
        self._marked = False

    def tearDown(self):
        if not self._marked:
            _matrix_add(self.case_id, getattr(self, 'channel_kind', 'pty'),
                        'FAIL', 'assertion aborted before _mark')

    def _mark(self, ok, detail=''):
        self._marked = True
        _matrix_add(self.case_id, self.channel_kind,
                    'PASS' if ok else 'FAIL', detail)

    def test_device_manager_sdk_path(self):
        from device.device_manager import DeviceManager
        dm = DeviceManager.get()
        ok = dm.open_port(self.link.host_port, 115200, encrypt=False)
        self.assertTrue(ok, 'open_port failed on %s' % self.link.host_port)
        try:
            self.assertTrue(dm.is_open())
            resp = dm.test_link(timeout=2.0)
            self.assertIsNotNone(resp)
            uid = dm.get_uid(timeout=2.0)
            self.assertEqual(uid, MOCK_UID)
            info = dm.get_device_info(timeout=2.0)
            self.assertEqual(info, (MOCK_VERSION, MOCK_MODEL))

            payload = _make_fw(512, seed=33)
            path = _write_temp(payload)
            try:
                ok_ota = dm.start_ota(path, timeout=8.0)
            finally:
                os.unlink(path)
            self.assertTrue(ok_ota)
            self.assertEqual(self.link.device.received_fw, payload)

            self.assertTrue(
                _wait(lambda: any(a[0] == CMD_OTA_RESULT
                                  for a in self.link.device.host_acks),
                      timeout=2.0))
            time.sleep(0.05)
            try:
                dm.uart.read_available()
            except Exception:
                pass
            self.link.device.switch_to_shell()
            names = dm.param_list(timeout=2.0)
            self.assertIn('g_param_test_val', names)
            ok_set = dm.param_set('g_volume', 66, timeout=2.0, verify=True)
            self.assertTrue(ok_set)
            self._mark(True, 'DM open/uid/info/ota/shell over pty')
        finally:
            dm.close_port()


# ---------------------------------------------------------------------------
# 9. HTTPS cert authority check (origin/dev tool/ — not runtime openssl)
# ---------------------------------------------------------------------------

class TestHttpsCertsOriginDev(unittest.TestCase):
    case_id = 'https_certs_origin_dev'

    def test_certs_match_origin_dev(self):
        info = _verify_origin_dev_certs()
        _matrix_add(self.case_id, 'n/a(cert-file)',
                    'PASS' if info.get('ok') else 'FAIL',
                    info.get('error', info.get('cert_sha256', '')))
        self.assertTrue(info.get('ok'), info)
        from device import http_mock as hm
        cert, key = hm._resolve_https_cert()
        self.assertIn(os.path.abspath(cert), (
            os.path.abspath(info['repo_tool_cert']),
            os.path.abspath(info['pkg_cert']),
        ))
        self.assertIn(os.path.abspath(key), (
            os.path.abspath(info['repo_tool_key']),
            os.path.abspath(info['pkg_key']),
        ))


# ---------------------------------------------------------------------------
# Runner + TEST_SCHEMA
# ---------------------------------------------------------------------------

def _build_test_schema(result):
    pty_cases = [m for m in _MATRIX if m['channel'] == 'pty']
    sock_cases = [m for m in _MATRIX if m['channel'] == 'socketpair']
    other_cases = [m for m in _MATRIX
                   if m['channel'] not in ('pty', 'socketpair')]
    pty_pass = sum(1 for m in pty_cases if m['status'] == 'PASS')
    pty_fail = sum(1 for m in pty_cases if m['status'] == 'FAIL')
    total = len(_MATRIX)
    failed = sum(1 for m in _MATRIX if m['status'] == 'FAIL')
    acceptance = (
        pty_fail == 0 and pty_pass > 0
        and result.wasSuccessful()
        and _CERT_INFO.get('ok') is True
    )
    return {
        'schema': TEST_SCHEMA_ID,
        'area': 'virtual-serial-protocol-sdk',
        'channel_required': 'pty',
        'python': sys.version.split()[0],
        'protocol_host_id': '0x%X' % DEVICE_ID_HOST,
        'certs': {
            'ok': _CERT_INFO.get('ok'),
            'repo_tool_cert': _CERT_INFO.get('repo_tool_cert'),
            'repo_tool_key': _CERT_INFO.get('repo_tool_key'),
            'pkg_cert': _CERT_INFO.get('pkg_cert'),
            'pkg_key': _CERT_INFO.get('pkg_key'),
            'cert_sha256': _CERT_INFO.get('cert_sha256'),
            'origin': 'origin/dev tool/mock_https_{cert,key}.pem',
        },
        'total': total,
        'passed': total - failed,
        'failed': failed,
        'virtual_serial': {
            'pty_total': len(pty_cases),
            'pty_passed': pty_pass,
            'pty_failed': pty_fail,
            'socketpair_total': len(sock_cases),
            'socketpair_passed': sum(1 for m in sock_cases
                                     if m['status'] == 'PASS'),
            'socketpair_failed': sum(1 for m in sock_cases
                                     if m['status'] == 'FAIL'),
            'other_total': len(other_cases),
        },
        'coverage_matrix': _MATRIX,
        'unittest': {
            'run': result.testsRun,
            'failures': len(result.failures),
            'errors': len(result.errors),
            'skipped': len(result.skipped),
        },
        'acceptance': acceptance,
        'notes': (
            'Main acceptance path is kind=pty only. socketpair cases are '
            'classified separately and never counted as pty results. '
            'HTTPS certs are origin/dev tool/ files — no runtime openssl.'
        ),
    }


def _print_matrix(schema):
    print('')
    print('==== virtual-serial coverage matrix ====')
    print('%-42s %-12s %s' % ('CASE', 'CHANNEL', 'STATUS'))
    for m in schema['coverage_matrix']:
        print('%-42s %-12s %s%s' % (
            m['id'], m['channel'], m['status'],
            ('  ' + m['detail']) if m['detail'] else ''))
    vs = schema['virtual_serial']
    print('')
    print('virtual-serial pty total/passed/failed: %d/%d/%d'
          % (vs['pty_total'], vs['pty_passed'], vs['pty_failed']))
    print('socketpair (fallback, separate class): %d/%d/%d'
          % (vs['socketpair_total'], vs['socketpair_passed'],
             vs['socketpair_failed']))
    print('total=%d passed=%d failed=%d'
          % (schema['total'], schema['passed'], schema['failed']))
    print('certs ok=%s sha256=%s'
          % (schema['certs']['ok'], schema['certs'].get('cert_sha256')))
    print('acceptance=%s' % schema['acceptance'])


class _CountingResult(unittest.TextTestResult):
    def addSuccess(self, test):
        super(_CountingResult, self).addSuccess(test)

    def addFailure(self, test, err):
        super(_CountingResult, self).addFailure(test, err)

    def addError(self, test, err):
        super(_CountingResult, self).addError(test, err)

    def addSkip(self, test, reason):
        super(_CountingResult, self).addSkip(test, reason)


def main():
    print('BabyOS Studio virtual-serial protocol acceptance (SDK, pty)')
    print('python: %s' % sys.version.split()[0])
    print('protocol host id: 0x%X' % DEVICE_ID_HOST)
    print('repo: %s' % _REPO)

    ch = create_byte_channel(prefer='pty', require_pty=True)
    print('byte channel kind: %s host_port=%s' % (ch.kind, ch.host_port))
    ch.close()

    cinfo = _verify_origin_dev_certs()
    print('https certs from origin/dev: %s' % cinfo.get('ok'))
    if not cinfo.get('ok'):
        print('  cert error: %s' % cinfo.get('error'))

    loader = unittest.defaultTestLoader
    suite = loader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2, resultclass=_CountingResult)
    result = runner.run(suite)

    _verify_origin_dev_certs()

    schema = _build_test_schema(result)
    _print_matrix(schema)
    print('')
    print('==== TEST_SCHEMA ====')
    print(json.dumps(schema, indent=2, ensure_ascii=False))

    ok = schema['acceptance'] and schema['failed'] == 0
    print('')
    print('virtual-serial protocol acceptance: %s'
          % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
