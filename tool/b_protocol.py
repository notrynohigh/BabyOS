"""
b_protocol — BabyOS protocol frame packing, parsing, and TEA encryption.
Matches b_protocol.c / b_protocol.h.

Protocol frame format:
| HEAD=0xFE | DeviceID(4B) | Length(2B) | CMD(1B) | Params(nB) | Checksum(1B) |
Length = 1 + param_size  (CMD byte + params)
Checksum = sum of all bytes excluding the checksum byte itself
"""

import os
import struct
from typing import Callable, Optional, Tuple

# Fixed sizes
_PROTO_FID_SIZE = 4   # device_id field size in bytes
_PROTO_FLEN_SIZE = 2   # length field size in bytes

PROTOCOL_HEAD = 0xFE
INVALID_ID = 0xFFFFFFFF
DEVICE_ID_HOST = 0x1314

# Dispatch callback signature:
#   callback(id: int, cmd: int, param: bytes, param_len: int) -> int
pdispatch = Callable[[int, int, bytes, int], int]

# ---------------------------------------------------------------------------
# Internal state
# ---------------------------------------------------------------------------

_protocol_info = []          # list of (id, dispatch_func)
_protocol_info_index = 0    # number of registered instances (max 1)


# ---------------------------------------------------------------------------
# TEA encryption (Tiny Encryption Algorithm)
# ---------------------------------------------------------------------------

_TEA_DELTA = 0x9E3779B9
# M-NEW-13 fix: TEA key 不能用源码硬编码默认值 — 任何拿到这份代码的人都能解密
# 上位机 ↔ MCU 的通信. 现在从环境变量 BABYOS_TEA_KEY 读 (4 个十进制 uint32,
# 用空格分隔), 缺失则用占位 (128, 256, 384, 512) 但打 WARNING 提醒.
# 推荐: export BABYOS_TEA_KEY="<k1> <k2> <k3> <k4>"  # 与 MCU 端 bProtocolKey 一致.
def _load_tea_key() -> tuple:
    raw = os.environ.get('BABYOS_TEA_KEY')
    if not raw:
        # 兜底占位 key (测试用, 上线前必须替换). 打印 WARNING 提醒开发者.
        print('[b_protocol][WARNING] BABYOS_TEA_KEY unset, using placeholder key. '
              'Set BABYOS_TEA_KEY="<k1> <k2> <k3> <k4>" to match firmware.')
        return (128, 256, 384, 512)
    try:
        parts = [int(p) & 0xFFFFFFFF for p in raw.split()]
        if len(parts) != 4:
            raise ValueError(f'need 4 uint32 values, got {len(parts)}')
        return tuple(parts)
    except ValueError as e:
        print(f'[b_protocol][ERROR] BABYOS_TEA_KEY parse failed: {e}, using placeholder')
        return (128, 256, 384, 512)

_TEA_KEY = _load_tea_key()


def _tea_crypt_block(v: list, key: tuple, encrypt: bool, rounds: int = 16) -> list:
    """
    TEA encrypt (encrypt=True) or decrypt (encrypt=False) two uint32 values.
    v = [v0, v1]
    """
    v0, v1 = v
    k0, k1, k2, k3 = key

    if encrypt:
        sum_val = 0
        for _ in range(rounds):
            sum_val = (sum_val + _TEA_DELTA) & 0xFFFFFFFF
            v0 = (v0 + (((((v1 << 4) + k0) ^ (v1 + sum_val)) ^ ((v1 >> 5) + k1)))) & 0xFFFFFFFF
            v1 = (v1 + (((((v0 << 4) + k2) ^ (v0 + sum_val)) ^ ((v0 >> 5) + k3)))) & 0xFFFFFFFF
    else:
        sum_val = (_TEA_DELTA * rounds) & 0xFFFFFFFF
        for _ in range(rounds):
            v1 = (v1 - (((((v0 << 4) + k2) ^ (v0 + sum_val)) ^ ((v0 >> 5) + k3)))) & 0xFFFFFFFF
            v0 = (v0 - (((((v1 << 4) + k0) ^ (v1 + sum_val)) ^ ((v1 >> 5) + k1)))) & 0xFFFFFFFF
            sum_val = (sum_val - _TEA_DELTA) & 0xFFFFFFFF

    return [v0, v1]


def _bProtocolEncryptGroup(text: list) -> None:
    """In-place TEA encrypt. text = [uint32 v0, uint32 v1]"""
    result = _tea_crypt_block(text, _TEA_KEY, encrypt=True)
    text[0], text[1] = result[0], result[1]


def _bProtocolDecryptGroup(text: list) -> None:
    """In-place TEA decrypt. text = [uint32 v0, uint32 v1]"""
    result = _tea_crypt_block(text, _TEA_KEY, encrypt=False)
    text[0], text[1] = result[0], result[1]


def _tea_transform(data: bytearray, encrypt: bool = True) -> bytearray:
    """
    Apply TEA to data in-place, operating on 8-byte (64-bit) blocks.
    If size < 8, no transformation is performed.
    Modifies data in-place and returns it.
    """
    if len(data) < 8:
        return data
    blocks = len(data) // 8
    for i in range(blocks):
        v = list(struct.unpack('<II', bytes(data[i*8:(i+1)*8])))
        if encrypt:
            _bProtocolEncryptGroup(v)
        else:
            _bProtocolDecryptGroup(v)
        data[i*8:(i+1)*8] = struct.pack('<II', v[0], v[1])
    return data


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def bProtocolRegist(device_id: int, dispatch_func: pdispatch) -> int:
    """
    Register a protocol instance.
    Only ONE instance is supported (bProtocolInfo[1] in the C code).
    Returns instance ID (0) on success, -1 on failure.
    """
    global _protocol_info_index
    if _protocol_info_index >= 1 or dispatch_func is None:
        return -1
    _protocol_info.append((device_id, dispatch_func))
    _protocol_info_index += 1
    return 0


def bProtocolSetID(instance: int, device_id: int) -> int:
    """Update the device ID for a registered instance."""
    if instance < 0 or instance >= _protocol_info_index:
        return -1
    old_id, f = _protocol_info[instance]
    _protocol_info[instance] = (device_id, f)
    return 0


def bProtocolParse(instance: int, raw_buf: bytes) -> int:
    """
    Parse a received frame.
    Validates HEAD, length, and checksum, then calls the registered dispatch.

    Returns 0 on success, -1 on error.
    Dispatch callback receives (device_id, cmd, param_bytes, param_len).
    """
    if (instance < 0 or instance >= _protocol_info_index
            or raw_buf is None or len(raw_buf) < 9):  # 8 (head) + 1 (checksum)
        return -1

    head = raw_buf[0]
    if head != PROTOCOL_HEAD:
        return -1

    # bProtocolHead_t layout (packed struct, no padding):
    #   uint8_t  head;       // offset 0,  1 byte
    #   uint32_t device_id;  // offset 1,  4 bytes
    #   uint16_t len;        // offset 5,  2 bytes
    #   uint8_t  cmd;        // offset 7,  1 byte
    # Total header = 8 bytes
    device_id = struct.unpack('<I', raw_buf[1:5])[0]
    frame_len = struct.unpack('<H', raw_buf[5:7])[0]
    cmd = raw_buf[7]

    total_len = frame_len + 8   # header (8) = head+id+len+cmd + frame_len
    if total_len > len(raw_buf):
        return -1

    # Checksum: sum of all bytes except last (checksum byte)
    crc = sum(raw_buf[:total_len - 1]) & 0xFF
    if crc != raw_buf[total_len - 1]:
        return -1

    param = raw_buf[8:total_len - 1]   # everything between CMD and checksum
    _, dispatch_func = _protocol_info[instance]
    return dispatch_func(device_id, cmd, param, len(param))


def bProtocolPack(instance: int, device_id: int, cmd: int,
                  param: bytes, pbuf: bytearray) -> int:
    """
    Pack a protocol frame into pbuf.

    Frame layout:
      HEAD(1) + device_id(4) + len(2) + cmd(1) + param(n) + checksum(1)

    Returns total frame length on success, -1 on error.
    pbuf must be pre-allocated with sufficient space.
    """
    if (instance < 0 or instance >= _protocol_info_index
            or (param is None and False)  # param may be empty
            or pbuf is None):
        return -1

    if param is None:
        param = b''

    pbuf[0] = PROTOCOL_HEAD
    struct.pack_into('<I', pbuf, 1, device_id)
    frame_len = 1 + len(param)   # CMD byte + params
    struct.pack_into('<H', pbuf, 5, frame_len)
    pbuf[7] = cmd
    if param:
        pbuf[8:8 + len(param)] = param

    total_len = 8 + frame_len   # header(8) + frame_len
    checksum = sum(pbuf[:total_len - 1]) & 0xFF
    pbuf[total_len - 1] = checksum
    return total_len


def bProtocolPackBytes(instance: int, device_id: int, cmd: int,
                        param: bytes = b'') -> bytes:
    """
    Convenience wrapper: pack into a newly allocated bytes object.
    Returns the packed frame bytes.
    """
    # Max possible: 1+4+2+1+512+1 = 521
    buf = bytearray(1024)
    length = bProtocolPack(instance, device_id, cmd, param, buf)
    if length < 0:
        return b''
    return bytes(buf[:length])


def bProtocolEncrypt(data: bytearray) -> bytearray:
    """
    Apply TEA encryption to data in-place (8-byte aligned blocks).
    """
    return _tea_transform(data, encrypt=True)


def bProtocolDecrypt(data: bytearray) -> bytearray:
    """
    Apply TEA decryption to data in-place (8-byte aligned blocks).
    """
    return _tea_transform(data, encrypt=False)


# Expose underscore-prefixed names for compatibility with C naming
_bProtocolEncrypt = bProtocolEncrypt
_bProtocolDecrypt = bProtocolDecrypt
