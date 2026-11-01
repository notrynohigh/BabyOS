"""
crc_util — CRC32 matching BabyOS firmware bos/algorithm/algo_crc.c.

Firmware 口径 (crc_calculate(ALGO_CRC32, ...) → crc32_d(crc_val, data, len, 0)):

  - Polynomial:  x^32+x^26+x^23+x^22+x^16+x^12+x^11+x^10+x^8+x^7+x^5+x^4+x^2+x+1
  - Reflected poly: 0xEDB88320  (reverse of 0x04C11DB7)
  - Initial value:  0xFFFFFFFF   (CRC_INITIAL_VALUE_IS_FF includes ALGO_CRC32)
  - flag=0 at one-shot: NO pre-invert of the initial register
  - Final: always return ~crc  (xorout = 0xFFFFFFFF)
  - Empty buffer: firmware crc_calculate returns 0

  Equivalent to zlib.crc32(data) & 0xFFFFFFFF  (standard CRC-32/ISO-HDLC).
  Master tool/algo_crc.py ALGO_CRC32 uses the same path.

  Streaming (crc_calculate_sbs): first chunk uses flag=0 with init 0xFFFFFFFF
  (register starts at 0xFFFFFFFF, no pre-invert); subsequent chunks pass the
  running register with flag=1 which pre-inverts — see crc32_sbs_update().
"""

from typing import Optional

# CRC type ids matching algo_crc.h (only CRC32 exposed here; keep id for parity)
ALGO_CRC32 = 13
ALGO_CRC32_MPEG2 = 14

CRC32_POLY_REFLECTED = 0xEDB88320
CRC32_INIT = 0xFFFFFFFF
CRC32_XOROUT = 0xFFFFFFFF

# Documented 口径 snapshot for tests / reviews
CRC32_SPEC = {
    'name': 'CRC-32 (ISO-HDLC / IEEE 802.3) — BabyOS algo_crc.c ALGO_CRC32',
    'poly': '0x04C11DB7',
    'poly_reflected': '0xEDB88320',
    'init': '0xFFFFFFFF',
    'xorout': '0xFFFFFFFF',
    'refin': True,
    'refout': True,
    'firmware_fn': 'crc32_d(crc_val=0xFFFFFFFF, data, len, flag=0)',
    'firmware_type': 'ALGO_CRC32 = 13',
    'empty_input': 0,
    'check_value_123456789': '0xCBF43926',
}


def crc32_d(crc_32: int, data: bytes, flag: int) -> int:
    """
    Direct port of firmware crc32_d().

    crc_32 : incoming register value
    data   : bytes to fold in
    flag   : non-zero → pre-invert register before folding (sbs continuation)

    Always returns ~crc at the end (matches C `return ~crc`).
    """
    if data is None:
        data = b''
    crc = crc_32 & 0xFFFFFFFF
    if flag:
        crc = (~crc) & 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = ((crc >> 1) ^ CRC32_POLY_REFLECTED) & 0xFFFFFFFF
            else:
                crc = (crc >> 1) & 0xFFFFFFFF
    return (~crc) & 0xFFFFFFFF


def crc32(data: bytes) -> int:
    """
    One-shot CRC32 matching firmware crc_calculate(ALGO_CRC32, pbuf, len).

    init = 0xFFFFFFFF, flag = 0, return ~crc.
    Empty input → 0 (firmware returns 0 when pbuf==NULL || len==0).
    """
    if not data:
        return 0
    return crc32_d(CRC32_INIT, data, 0)


def crc32_equals_zlib(data: bytes) -> bool:
    """Convenience: True if crc32(data) equals zlib.crc32(data) & 0xFFFFFFFF."""
    import zlib
    if not data:
        return True  # both 0
    return crc32(data) == (zlib.crc32(data) & 0xFFFFFFFF)


def crc32_sbs_start() -> int:
    """Initial streaming register for crc_calculate_sbs ALGO_CRC32 (0xFFFFFFFF)."""
    return CRC32_INIT


def crc32_sbs_update(register: int, chunk: bytes, first: bool) -> int:
    """
    Streaming update matching firmware crc_calculate_sbs for ALGO_CRC32.

    First chunk: crc32_d(0xFFFFFFFF, chunk, flag=0)  → returns final ~crc
    Later chunks: crc32_d(prev_result, chunk, flag=1) → continue with pre-invert
    """
    flag = 0 if first else 1
    return crc32_d(register, chunk, flag)


# Optional full algo_crc parity table (subset used by Studio tooling)
_CRC16_XMODEM_POLY = 0x1021


def crc16_xmodem(data: bytes) -> int:
    """
    Standard CRC-16/XMODEM — matches firmware algo_crc.c crc16_xmodem
    (ALGO_CRC16_XMODEM): `crc ^= (uint16_t)(*data++) << 8`.

    Check value for b'123456789' is 0x31C3.

    IMPORTANT: BabyOS Ymodem file transfer does NOT use this function.
    Ymodem uses b_mod_ymodem.c _bYmodemCalCheck = (crc ^ byte) << 8 form
    (see device.xmodem_ydmodem.crc16_ccitt). Two different CRC16s coexist.
    """
    crc = 0
    if not data:
        return 0
    for byte in data:
        crc ^= (byte << 8) & 0xFFFF
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ _CRC16_XMODEM_POLY) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# Master tool/algo_crc.py ALGO_* ids (kept for import parity)
ALGO_CRC8 = 0
ALGO_CRC8_ITU = 1
ALGO_CRC8_ROHC = 2
ALGO_CRC8_MAXIM = 3
ALGO_CRC16_IBM = 4
ALGO_CRC16_MAXIM = 5
ALGO_CRC16_USB = 6
ALGO_CRC16_MODBUS = 7
ALGO_CRC16_CCITT = 8
ALGO_CRC16_CCITT_FALSE = 9
ALGO_CRC16_X25 = 10
ALGO_CRC16_XMODEM = 11
ALGO_CRC16_DNP = 12


def crc_calculate(crc_type: int, data: bytes) -> int:
    """
    Master-tool parity entry: crc_calculate(ALGO_CRC32, data) → firmware-equivalent CRC32.
    Unknown/unsupported types return 0 (matches C default: break → retval 0).
    """
    if crc_type == ALGO_CRC32:
        return crc32(data)
    return 0
