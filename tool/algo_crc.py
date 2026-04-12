"""
CRC algorithms implementation matching the C originals in algo_crc.c
"""

# CRC type definitions (matching algo_crc.h)
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
ALGO_CRC32 = 13
ALGO_CRC32_MPEG2 = 14

# Types that use 0xffffffff as initial value
_CRC_INITIAL_VALUE_IS_FF = {
    ALGO_CRC8_ROHC, ALGO_CRC16_USB, ALGO_CRC16_MODBUS,
    ALGO_CRC16_CCITT_FALSE, ALGO_CRC16_X25, ALGO_CRC32, ALGO_CRC32_MPEG2
}


def _crc8_d(crc_8: int, data: bytes, flag: int) -> int:
    crc = crc_8 & 0xFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def _crc8_itu(crc_8: int, data: bytes, flag: int) -> int:
    crc = crc_8 & 0xFF
    if flag:
        crc ^= 0x55
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc ^ 0x55


def _crc8_rohc(crc_8: int, data: bytes, flag: int) -> int:
    crc = crc_8 & 0xFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xE0
            else:
                crc >>= 1
    return crc


def _crc8_maxim(crc_8: int, data: bytes, flag: int) -> int:
    crc = crc_8 & 0xFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8C
            else:
                crc >>= 1
    return crc


def _crc16_ibm(crc_16: int, data: bytes, flag: int) -> int:
    crc = crc_16 & 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def _crc16_maxim(crc_16: int, data: bytes, flag: int) -> int:
    crc = crc_16 & 0xFFFF
    if flag:
        crc = ~crc & 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return (~crc) & 0xFFFF


def _crc16_usb(crc_16: int, data: bytes, flag: int) -> int:
    crc = crc_16 & 0xFFFF
    if flag:
        crc = ~crc & 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return (~crc) & 0xFFFF


def _crc16_modbus(crc_16: int, data: bytes, flag: int) -> int:
    crc = crc_16 & 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def _crc16_ccitt(crc_16: int, data: bytes, flag: int) -> int:
    crc = crc_16 & 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    return crc


def _crc16_ccitt_false(crc_16: int, data: bytes, flag: int) -> int:
    crc = crc_16 & 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _crc16_x25(crc_16: int, data: bytes, flag: int) -> int:
    crc = crc_16 & 0xFFFF
    if flag:
        crc = ~crc & 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    return (~crc) & 0xFFFF


def _crc16_xmodem(crc_16: int, data: bytes, flag: int) -> int:
    crc = crc_16 & 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _crc16_dnp(crc_16: int, data: bytes, flag: int) -> int:
    crc = crc_16 & 0xFFFF
    if flag:
        crc = ~crc & 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA6BC
            else:
                crc >>= 1
    return (~crc) & 0xFFFF


def _crc32_d(crc_32: int, data: bytes, flag: int) -> int:
    crc = crc_32 & 0xFFFFFFFF
    if flag:
        crc = ~crc & 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return (~crc) & 0xFFFFFFFF


def _crc32_mpeg2(crc_32: int, data: bytes, flag: int) -> int:
    crc = crc_32 & 0xFFFFFFFF
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc


def crc_calculate(crc_type: int, data: bytes) -> int:
    """Direct CRC calculation for one-shot data."""
    if not data:
        return 0
    init_val = 0xFFFFFFFF if crc_type in _CRC_INITIAL_VALUE_IS_FF else 0
    return _dispatch_crc(crc_type, data, init_val, False)


def _dispatch_crc(crc_type: int, data: bytes, init_val: int, flag: bool) -> int:
    dispatch = {
        ALGO_CRC8: lambda d: _crc8_d(init_val & 0xFF, d, flag),
        ALGO_CRC8_ITU: lambda d: _crc8_itu(init_val & 0xFF, d, flag),
        ALGO_CRC8_ROHC: lambda d: _crc8_rohc(init_val & 0xFF, d, flag),
        ALGO_CRC8_MAXIM: lambda d: _crc8_maxim(init_val & 0xFF, d, flag),
        ALGO_CRC16_IBM: lambda d: _crc16_ibm(init_val & 0xFFFF, d, flag),
        ALGO_CRC16_MAXIM: lambda d: _crc16_maxim(init_val & 0xFFFF, d, flag),
        ALGO_CRC16_USB: lambda d: _crc16_usb(init_val & 0xFFFF, d, flag),
        ALGO_CRC16_MODBUS: lambda d: _crc16_modbus(init_val & 0xFFFF, d, flag),
        ALGO_CRC16_CCITT: lambda d: _crc16_ccitt(init_val & 0xFFFF, d, flag),
        ALGO_CRC16_CCITT_FALSE: lambda d: _crc16_ccitt_false(init_val & 0xFFFF, d, flag),
        ALGO_CRC16_X25: lambda d: _crc16_x25(init_val & 0xFFFF, d, flag),
        ALGO_CRC16_XMODEM: lambda d: _crc16_xmodem(init_val & 0xFFFF, d, flag),
        ALGO_CRC16_DNP: lambda d: _crc16_dnp(init_val & 0xFFFF, d, flag),
        ALGO_CRC32: lambda d: _crc32_d(init_val, d, flag),
        ALGO_CRC32_MPEG2: lambda d: _crc32_mpeg2(init_val, d, flag),
    }
    return dispatch.get(crc_type, lambda _: init_val)(data)


class CRCStep:
    """Incremental CRC handle, equivalent to algo_crc_sbs_t."""
    def __init__(self, crc_type: int):
        self.type = crc_type
        self.flag = 0
        self.crc = 0

    def reset(self, crc_type: int = None):
        self.flag = 0
        if crc_type is not None:
            self.type = crc_type

    def update(self, data: bytes):
        if not data:
            return
        flag = self.flag != 0
        if flag:
            init_val = self.crc
        else:
            init_val = 0xFFFFFFFF if self.type in _CRC_INITIAL_VALUE_IS_FF else 0
            self.flag = 1
        self.crc = _dispatch_crc(self.type, data, init_val, flag)

    @property
    def value(self) -> int:
        return self.crc
