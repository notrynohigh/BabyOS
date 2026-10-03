#!/usr/bin/env python3
"""
Device protocol core unit tests for BabyOS Studio.

Authority:
  - origin/master:tool/README.md, tool/b_protocol.py, tool/xmodem_ydmodem.py
  - bos/modules/b_mod_protocol.c / b_mod_protocol.h
  - bos/algorithm/algo_crc.c (CRC32 口径)

Run:
  tool/babyos-studio/python/.venv/bin/python \
      tool/babyos-studio/test/device_features/test_protocol_core.py
  or: pytest tool/babyos-studio/test/device_features/test_protocol_core.py
"""

from __future__ import print_function

import hashlib
import os
import struct
import sys
import unittest
import zlib

# Make python/ importable when run as a script
_HERE = os.path.dirname(os.path.abspath(__file__))
_STUDIO = os.path.abspath(os.path.join(_HERE, '..', '..'))
_PY_ROOT = os.path.join(_STUDIO, 'python')
if _PY_ROOT not in sys.path:
    sys.path.insert(0, _PY_ROOT)

from device.b_protocol import (  # noqa: E402
    PROTOCOL_HEAD,
    INVALID_ID,
    DEVICE_ID_HOST,
    CMD_TEST,
    CMD_FW_INFO,
    CMD_FDATA,
    CMD_OTA_RESULT,
    CMD_TRANS_FILE,
    CMD_GET_UID,
    CMD_WRITE_SN,
    CMD_DEVICEINFO,
    TEA_KEY,
    pack_frame,
    parse_frame,
    calc_checksum,
    tea_encrypt,
    tea_decrypt,
    build_fw_info_param,
    parse_fw_info_param,
    build_fdata_param,
    parse_fdata_param,
    build_trans_file_param,
    parse_trans_file_param,
    parse_uid_response,
    build_sn_param,
    parse_devinfo_response,
    build_devinfo_param,
    bProtocolRegist,
    bProtocolParse,
    bProtocolPackBytes,
)
from device.crc_util import (  # noqa: E402
    crc32,
    crc32_d,
    crc32_equals_zlib,
    CRC32_SPEC,
    crc32_sbs_start,
    crc32_sbs_update,
    crc16_xmodem,
)
from device.sn_util import md5_hex_16, sn_bytes  # noqa: E402
from device.xmodem_ydmodem import crc16_ccitt, CRC16_YMODEM_VECTORS  # noqa: E402


class TestPackParseRoundtrip(unittest.TestCase):
    """pack/parse 往返"""

    def test_pack_empty_param_layout(self):
        frame = pack_frame(DEVICE_ID_HOST, CMD_TEST, b'')
        self.assertEqual(len(frame), 9)
        self.assertEqual(frame[0], PROTOCOL_HEAD)
        self.assertEqual(struct.unpack_from('<I', frame, 1)[0], DEVICE_ID_HOST)
        self.assertEqual(struct.unpack_from('<H', frame, 5)[0], 1)  # cmd only
        self.assertEqual(frame[7], CMD_TEST)
        self.assertEqual(frame[8], calc_checksum(frame[:8]))

    def test_pack_parse_roundtrip(self):
        cases = [
            (DEVICE_ID_HOST, CMD_TEST, b'BabyOS\x00'),
            (INVALID_ID, CMD_FW_INFO, build_fw_info_param(1024, 0xCBF43926, 'app.bin')),
            (INVALID_ID, CMD_FDATA, build_fdata_param(3, bytes(range(256)) * 2)),
            (INVALID_ID, CMD_TRANS_FILE, build_trans_file_param(4096, 0x11223344, 0, 0x1000)),
            (0x0000ABCD, CMD_DEVICEINFO, build_devinfo_param('v1.0.0', 'BabyOS')),
            (DEVICE_ID_HOST, CMD_WRITE_SN, build_sn_param(bytes(range(16)))),
        ]
        for device_id, cmd, param in cases:
            frame = pack_frame(device_id, cmd, param)
            parsed = parse_frame(frame)
            self.assertIsNotNone(parsed, 'parse failed for cmd=0x%02X' % cmd)
            got_id, got_cmd, got_param = parsed
            self.assertEqual(got_id, device_id)
            self.assertEqual(got_cmd, cmd)
            self.assertEqual(got_param, param)

    def test_parse_rejects_short_buffer(self):
        self.assertIsNone(parse_frame(b''))
        self.assertIsNone(parse_frame(b'\xFE\x13'))
        self.assertIsNone(parse_frame(None))

    def test_parse_rejects_bad_head(self):
        frame = bytearray(pack_frame(DEVICE_ID_HOST, CMD_TEST, b'BabyOS\x00'))
        frame[0] = 0x00
        self.assertIsNone(parse_frame(bytes(frame)))

    def test_parse_rejects_truncated_length(self):
        # length field claims more bytes than present
        frame = bytearray(pack_frame(DEVICE_ID_HOST, CMD_TEST, b'BabyOS\x00'))
        struct.pack_into('<H', frame, 5, 50)  # claims 50 bytes of cmd+param
        self.assertIsNone(parse_frame(bytes(frame)))

    def test_command_payload_builders(self):
        fw = build_fw_info_param(1234, 0xAABBCCDD, 'n1')
        self.assertEqual(len(fw), 4 + 4 + 64)
        size, crc, name = parse_fw_info_param(fw)
        self.assertEqual((size, crc, name), (1234, 0xAABBCCDD, 'n1'))

        fd = build_fdata_param(7, b'\x01\x02\x03')
        self.assertEqual(len(fd), 2 + 512)
        seq, data = parse_fdata_param(fd)
        self.assertEqual(seq, 7)
        self.assertEqual(data[:3], b'\x01\x02\x03')
        self.assertEqual(data[3:], b'\x00' * (512 - 3))

        tf = build_trans_file_param(99, 0x01020304, 1, 4096)
        self.assertEqual(parse_trans_file_param(tf), (99, 0x01020304, 1, 4096))

        uid_resp = bytes([4, 0xDE, 0xAD, 0xBE, 0xEF])
        self.assertEqual(parse_uid_response(uid_resp), b'\xDE\xAD\xBE\xEF')
        self.assertIsNone(parse_uid_response(b''))  # len 0 / empty

        di = build_devinfo_param('1.2.3', 'MCU-X')
        self.assertEqual(len(di), 32)
        ver, name = parse_devinfo_response(di)
        self.assertTrue(ver.startswith(b'1.2.3'))
        self.assertTrue(name.startswith(b'MCU-X'))


class TestChecksumReject(unittest.TestCase):
    """checksum 错误拒绝"""

    def test_checksum_mismatch_rejected(self):
        frame = bytearray(pack_frame(DEVICE_ID_HOST, CMD_TEST, b'BabyOS\x00'))
        frame[-1] = (frame[-1] + 1) & 0xFF
        self.assertIsNone(parse_frame(bytes(frame)))

    def test_checksum_zeroed_rejected(self):
        frame = bytearray(pack_frame(DEVICE_ID_HOST, CMD_GET_UID, b''))
        frame[-1] = 0x00
        # only rejected if real checksum != 0
        if calc_checksum(bytes(frame[:-1])) != 0:
            self.assertIsNone(parse_frame(bytes(frame)))

    def test_checksum_covers_head_id_len_cmd_param(self):
        param = b'\x11\x22\x33'
        frame = pack_frame(0x11223344, 0x5A, param)
        expected = (PROTOCOL_HEAD + 0x11 + 0x22 + 0x33 + 0x44
                    + 1 + len(param) + 0x5A
                    + sum(param)) & 0xFF
        # Length field is 2-byte LE: 1+len(param) = 4
        self.assertEqual(frame[5], 4)
        self.assertEqual(frame[6], 0)
        self.assertEqual(frame[-1], expected)

    def test_bitflip_in_param_rejected(self):
        frame = bytearray(pack_frame(DEVICE_ID_HOST, CMD_FW_INFO,
                                     build_fw_info_param(1, 2, 'x')))
        frame[20] ^= 0xFF
        self.assertIsNone(parse_frame(bytes(frame)))


class TestTEA(unittest.TestCase):
    """TEA 加解密往返"""

    def test_key_matches_firmware_defaults(self):
        self.assertEqual(TEA_KEY, (1, 22, 333, 4444))

    def test_roundtrip_various_sizes(self):
        samples = [
            b'',  # < 8: unchanged
            b'\x01',  # < 8
            b'1234567',  # 7 bytes
            b'12345678',  # exactly 8
            pack_frame(DEVICE_ID_HOST, CMD_TEST, b'BabyOS\x00'),  # 15 bytes
            pack_frame(INVALID_ID, CMD_FW_INFO,
                       build_fw_info_param(512, 0xDEADBEEF, 'fw.bin')),  # 72+9
            bytes(range(256)),
        ]
        for data in samples:
            enc = tea_encrypt(data)
            if len(data) >= 8:
                self.assertNotEqual(enc, data, 'expected change for %r' % data[:16])
            dec = tea_decrypt(enc)
            self.assertEqual(dec, data)

    def test_encrypt_only_full_blocks(self):
        data = b'ABCDEFGHXXXXX'  # 13 bytes → only first 8 transformed
        enc = tea_encrypt(data)
        self.assertEqual(enc[8:], data[8:])
        self.assertNotEqual(enc[:8], data[:8])

    def test_tea_known_block(self):
        """Single 8-byte block vector — cross-checked against master b_protocol TEA."""
        # Independently implement master tool/b_protocol.py TEA here for comparison
        DELTA = 0x9E3779B9
        K = (1, 22, 333, 4444)

        def master_encrypt_block(v0, v1):
            s = 0
            k0, k1, k2, k3 = K
            for _ in range(16):
                s = (s + DELTA) & 0xFFFFFFFF
                v0 = (v0 + ((((v1 << 4) + k0) ^ (v1 + s) ^ ((v1 >> 5) + k1)))) & 0xFFFFFFFF
                v1 = (v1 + ((((v0 << 4) + k2) ^ (v0 + s) ^ ((v0 >> 5) + k3)))) & 0xFFFFFFFF
            return v0, v1

        plain = b'\x01\x02\x03\x04\x05\x06\x07\x08'
        v0, v1 = struct.unpack('<II', plain)
        ev0, ev1 = master_encrypt_block(v0, v1)
        expected = struct.pack('<II', ev0, ev1)
        self.assertEqual(tea_encrypt(plain), expected)

    def test_registry_pack_parse(self):
        got = {}

        def _dispatch(dev_id, cmd, param, plen):
            got['id'] = dev_id
            got['cmd'] = cmd
            got['param'] = param
            return 0

        # reset registry if a previous test polluted it
        import device.b_protocol as bp
        bp._protocol_info = []
        inst = bProtocolRegist(DEVICE_ID_HOST, _dispatch)
        self.assertEqual(inst, 0)
        frame = bProtocolPackBytes(inst, DEVICE_ID_HOST, CMD_TEST, b'BabyOS\x00')
        self.assertEqual(bProtocolParse(inst, frame), 0)
        self.assertEqual(got['cmd'], CMD_TEST)
        self.assertEqual(got['param'], b'BabyOS\x00')
        # bad checksum must not dispatch
        bad = bytearray(frame)
        bad[-1] ^= 0x55
        self.assertEqual(bProtocolParse(inst, bytes(bad)), -1)
        bp._protocol_info = []


class TestCRC32(unittest.TestCase):
    """CRC32 口径记录与断言 — 与 bos/algorithm/algo_crc.c 一致"""

    def test_spec_recorded(self):
        self.assertEqual(CRC32_SPEC['poly_reflected'], '0xEDB88320')
        self.assertEqual(CRC32_SPEC['init'], '0xFFFFFFFF')
        self.assertEqual(CRC32_SPEC['xorout'], '0xFFFFFFFF')
        self.assertEqual(CRC32_SPEC['check_value_123456789'], '0xCBF43926')
        self.assertEqual(CRC32_SPEC['empty_input'], 0)

    def test_empty_is_zero(self):
        # firmware crc_calculate returns 0 for empty
        self.assertEqual(crc32(b''), 0)
        self.assertEqual(crc32(None), 0)

    def test_check_vector_123456789(self):
        # CRC-32/ISO-HDLC check value
        self.assertEqual(crc32(b'123456789'), 0xCBF43926)

    def test_matches_zlib(self):
        vectors = [
            b'',
            b'1',
            b'123456789',
            b'BabyOS',
            bytes(range(256)),
            b'\x00' * 100,
            os.urandom(64),
        ]
        for data in vectors:
            self.assertEqual(crc32(data), zlib.crc32(data) & 0xFFFFFFFF)
            self.assertTrue(crc32_equals_zlib(data))

    def test_crc32_d_flag_semantics(self):
        """crc32_d matches firmware: flag=0 start, always ~crc out."""
        data = b'BabyOS'
        a = crc32_d(0xFFFFFFFF, data, 0)
        b = crc32(data)
        self.assertEqual(a, b)
        # streaming: first chunk flag=0, later flag=1
        reg = crc32_sbs_start()
        mid = len(data) // 2
        step1 = crc32_sbs_update(reg, data[:mid], first=True)
        step2 = crc32_sbs_update(step1, data[mid:], first=False)
        self.assertEqual(step2, b)

    def test_algo_crc32_id(self):
        from device.crc_util import ALGO_CRC32, crc_calculate
        self.assertEqual(ALGO_CRC32, 13)
        self.assertEqual(crc_calculate(ALGO_CRC32, b'123456789'), 0xCBF43926)


class TestXmodemCrc16(unittest.TestCase):
    """
    BabyOS Ymodem CRC16 已知向量 — firmware _bYmodemCalCheck.

    Form: crc = (crc ^ byte) << 8, poly 0x1021, init 0.
    This is NOT standard CRC-16/XMODEM (crc ^= byte<<8 → 0x31C3 for
    '123456789'); BabyOS transfer uses the (crc^byte)<<8 form → 0x2672.
    """

    def test_known_vectors(self):
        from device.xmodem_ydmodem import CRC16_YMODEM_VECTORS
        self.assertEqual(crc16_ccitt(b''), 0)
        self.assertEqual(crc16_ccitt(b'123456789'), 0x2672)
        self.assertEqual(crc16_ccitt(b'BabyOS'), 0x5424)
        for data, expect in CRC16_YMODEM_VECTORS.items():
            self.assertEqual(crc16_ccitt(data), expect)

    def test_differs_from_standard_xmodem(self):
        """Document the two coexisting CRC16s: Ymodem form vs algo_crc ALGO_CRC16_XMODEM."""
        self.assertEqual(crc16_ccitt(b'123456789'), 0x2672)   # Ymodem firmware form
        self.assertEqual(crc16_xmodem(b'123456789'), 0x31C3)  # standard algo_crc form
        self.assertNotEqual(crc16_ccitt(b'123456789'),
                            crc16_xmodem(b'123456789'))

    def test_parenthesization_guard(self):
        """
        Must use crc = (crc ^ byte) << 8.
        If written as crc ^= byte << 8 the vector diverges (0x31C3 ≠ 0x2672).
        """
        data = b'123456789'
        crc_babyos = 0
        for byte in data:
            crc_babyos = ((crc_babyos ^ byte) << 8) & 0xFFFF
            for _ in range(8):
                if crc_babyos & 0x8000:
                    crc_babyos = ((crc_babyos << 1) ^ 0x1021) & 0xFFFF
                else:
                    crc_babyos = (crc_babyos << 1) & 0xFFFF
        self.assertEqual(crc_babyos, 0x2672)
        self.assertEqual(crc16_ccitt(data), crc_babyos)

        crc_std = 0
        for byte in data:
            crc_std ^= (byte << 8)
            for _ in range(8):
                if crc_std & 0x8000:
                    crc_std = ((crc_std << 1) ^ 0x1021) & 0xFFFF
                else:
                    crc_std = (crc_std << 1) & 0xFFFF
        self.assertEqual(crc_std, 0x31C3)
        self.assertEqual(crc16_xmodem(data), crc_std)

    def test_frame_crc_is_be(self):
        """Ymodem/Xmodem trailers are big-endian CRC16 over the 128B payload."""
        import struct as st
        from device.xmodem_ydmodem import XmodemSender

        sent = []

        def _send(buf):
            sent.append(buf)

        sender = XmodemSender(_send, timeout_sec=0.01)
        file_data = b'x' * 128
        sender.start(file_data, file_size=128)
        sender.on_uart_byte(0x43)  # 'C' → CRC mode
        self.assertEqual(len(sent), 1)
        frame = sent[0]
        self.assertEqual(frame[0], 0x01)  # SOH
        self.assertEqual(frame[1], 1)     # blk
        self.assertEqual(frame[2], 254)   # ~blk
        self.assertEqual(frame[3:131], file_data)
        crc = st.unpack('>H', frame[-2:])[0]
        self.assertEqual(crc, crc16_ccitt(file_data))
        self.assertEqual(crc, crc16_ccitt(b'x' * 128))


class TestSN(unittest.TestCase):
    """SN 生成 — md5(uid) 每字节 | orval，前缀 len"""

    def test_md5_hex_16_is_raw_digest(self):
        uid = b'\x01\x02\x03\x04'
        digest = md5_hex_16(uid)
        self.assertEqual(len(digest), 16)
        self.assertEqual(digest, hashlib.md5(uid).digest())

    def test_sn_bytes_layout(self):
        uid = b'\x01\x02\x03\x04'
        orval = 0x5A
        sn = sn_bytes(uid, orval)
        self.assertEqual(sn[0], 16)
        self.assertEqual(len(sn), 17)
        expected_body = bytes(b | orval for b in hashlib.md5(uid).digest())
        self.assertEqual(sn[1:], expected_body)

    def test_sn_bytes_orval_zero(self):
        uid = b'abc'
        sn = sn_bytes(uid, 0)
        self.assertEqual(sn[0], 16)
        self.assertEqual(sn[1:], hashlib.md5(b'abc').digest())

    def test_sn_matches_master_mainwindow(self):
        """Same construction as origin/master:tool/mainwindow.py _on_set_sn."""
        uid_bytes = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0x11])
        orval = 0x0F
        md5_val = hashlib.md5(uid_bytes).digest()
        sn_table = bytes([len(md5_val)]) + bytes(b | orval for b in md5_val)
        self.assertEqual(sn_bytes(uid_bytes, orval), sn_table)

    def test_write_sn_frame_param(self):
        # sn_bytes already returns the full length-prefixed CMD_WRITE_SN param
        sn = sn_bytes(b'\x01\x02\x03\x04', 0x22)
        self.assertEqual(sn[0], 16)
        self.assertEqual(len(sn), 17)
        frame = pack_frame(DEVICE_ID_HOST, CMD_WRITE_SN, sn)
        parsed = parse_frame(frame)
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed[1], CMD_WRITE_SN)
        self.assertEqual(parsed[2], sn)

    def test_build_sn_param_wraps_raw_body(self):
        from device.b_protocol import build_sn_param
        body = bytes(range(16))
        param = build_sn_param(body)
        self.assertEqual(param[0], 16)
        self.assertEqual(param[1:], body)
        # double-wrap would be wrong — sn_bytes is already prefixed
        self.assertNotEqual(build_sn_param(sn_bytes(b'abc', 0)),
                            sn_bytes(b'abc', 0))


class TestUARTService(unittest.TestCase):
    """UART 封装接口存在性（无硬件时 list 返回 list，close 幂等）"""

    def test_api_surface(self):
        from device.uart_service import UartService
        svc = UartService()
        self.assertFalse(svc.is_open)
        ports = UartService.list()
        self.assertIsInstance(ports, list)
        self.assertFalse(svc.open(''))  # empty port rejected
        self.assertFalse(svc.is_open)
        self.assertEqual(svc.write(b'x'), -1)
        self.assertEqual(svc.read_available(), b'')
        svc.close()
        self.assertFalse(svc.is_open)

    def test_singleton(self):
        from device.uart_service import get_uart_service
        a = get_uart_service()
        b = get_uart_service()
        self.assertIs(a, b)


if __name__ == '__main__':
    unittest.main(verbosity=2)
