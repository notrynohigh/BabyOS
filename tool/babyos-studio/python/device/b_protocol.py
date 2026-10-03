"""
b_protocol — BabyOS device protocol: frame pack/parse + TEA encrypt/decrypt.

Authority:
  - origin/master:tool/b_protocol.py
  - bos/modules/b_mod_protocol.c / b_mod_protocol.h
  - origin/master:tool/README.md

Frame format (little-endian fields):
  HEAD(0xFE) + DeviceID(4B) + Length(2B) + CMD(1B) + Params(nB) + Checksum(1B)

  Length     = 1 + param_size          (CMD byte + params)
  Checksum   = sum of all bytes except the checksum byte, modulo 256
  DeviceID   = host 0x1314; INVALID_ID 0xFFFFFFFF means "any id accepted"
  TEA        = classic TEA, 16 rounds, key = (1, 22, 333, 4444),
               delta = 0x9E3779B9, applied to whole-frame 8-byte blocks;
               trailing bytes when len % 8 != 0 are left untouched.
"""

import struct
from typing import Callable, List, Optional, Tuple

# ---------------------------------------------------------------------------
# Frame constants (b_mod_protocol.h)
# ---------------------------------------------------------------------------

PROTOCOL_HEAD = 0xFE
INVALID_ID = 0xFFFFFFFF
DEVICE_ID_HOST = 0x1314

PROTO_FID_SIZE = 4
PROTO_FLEN_SIZE = 2
# head(1) + id(4) + len(2) + cmd(1)
FRAME_HEADER_SIZE = 8
FRAME_MIN_SIZE = FRAME_HEADER_SIZE + 1  # + checksum

# Commands (bProtocolCmd_t)
CMD_TEST = 0x01
CMD_UTC = 0x02
CMD_FW_INFO = 0x03
CMD_FDATA = 0x04
CMD_OTA_RESULT = 0x05
CMD_TRANS_FILE = 0x06
CMD_GET_UID = 0x07
CMD_WRITE_SN = 0x08
CMD_TSL_INVOKE = 0x09
CMD_DEVICEINFO = 0x0A
CMD_SETCFGNET_MODE = 0x30
CMD_GET_NETINFO = 0x31
CMD_SET_VOICE_SWITCH = 0x40
CMD_SET_VOICE_VOLUME = 0x41
CMD_GET_VOICE_VOLUME = 0x42
CMD_GET_VOICE_STAT = 0x43
CMD_TTS_CONTENT = 0x44

# OTA / transfer-file result codes (tool/README.md §1.5)
OTA_RESULT_OK = 0
OTA_RESULT_CRC_ERROR = 1
OTA_RESULT_NAME_MISMATCH = 2
OTA_RESULT_LEN_INVALID = 3
OTA_RESULT_TIMEOUT = 4

# Fixed payload sizes
FW_NAME_SIZE = 64
FDATA_CHUNK_SIZE = 512
UID_MAX_SIZE = 64
DEVINFO_FIELD_SIZE = 16

# Dispatch callback: (device_id, cmd, param, param_len) -> int
DispatchFn = Callable[[int, int, bytes, int], int]


# ---------------------------------------------------------------------------
# TEA (Tiny Encryption Algorithm) — mirrors _bProtocolEncryptGroup / DecryptGroup
# ---------------------------------------------------------------------------

TEA_DELTA = 0x9E3779B9
TEA_ROUNDS = 16
TEA_KEY = (1, 22, 333, 4444)  # SECRET_KEY1..4, bos/modules/Kconfig defaults


def _tea_crypt_block(v0: int, v1: int, key: Tuple[int, int, int, int],
                     encrypt: bool, rounds: int = TEA_ROUNDS) -> Tuple[int, int]:
    """Encrypt/decrypt one 64-bit TEA block. All ops are uint32."""
    k0, k1, k2, k3 = key
    v0 &= 0xFFFFFFFF
    v1 &= 0xFFFFFFFF

    if encrypt:
        sum_val = 0
        for _ in range(rounds):
            sum_val = (sum_val + TEA_DELTA) & 0xFFFFFFFF
            # C precedence: + binds tighter than ^
            #   v0 += (v1<<4)+k0 ^ v1+sum ^ (v1>>5)+k1
            v0 = (v0 + ((((v1 << 4) + k0) ^ (v1 + sum_val) ^ ((v1 >> 5) + k1)))) & 0xFFFFFFFF
            v1 = (v1 + ((((v0 << 4) + k2) ^ (v0 + sum_val) ^ ((v0 >> 5) + k3)))) & 0xFFFFFFFF
    else:
        sum_val = (TEA_DELTA * rounds) & 0xFFFFFFFF
        for _ in range(rounds):
            v1 = (v1 - ((((v0 << 4) + k2) ^ (v0 + sum_val) ^ ((v0 >> 5) + k3)))) & 0xFFFFFFFF
            v0 = (v0 - ((((v1 << 4) + k0) ^ (v1 + sum_val) ^ ((v1 >> 5) + k1)))) & 0xFFFFFFFF
            sum_val = (sum_val - TEA_DELTA) & 0xFFFFFFFF

    return v0, v1


def tea_encrypt(data: bytes) -> bytes:
    """
    TEA-encrypt whole-frame data in-place-equivalent (returns new bytes).
    Operates on complete 8-byte blocks only; leftover tail bytes unchanged.
    """
    if data is None:
        return b''
    raw = bytearray(data)
    n_blocks = len(raw) // 8
    for i in range(n_blocks):
        v0, v1 = struct.unpack_from('<II', raw, i * 8)
        v0, v1 = _tea_crypt_block(v0, v1, TEA_KEY, encrypt=True)
        struct.pack_into('<II', raw, i * 8, v0, v1)
    return bytes(raw)


def tea_decrypt(data: bytes) -> bytes:
    """TEA-decrypt whole-frame data. Leftover tail bytes when len%8!=0 unchanged."""
    if data is None:
        return b''
    raw = bytearray(data)
    n_blocks = len(raw) // 8
    for i in range(n_blocks):
        v0, v1 = struct.unpack_from('<II', raw, i * 8)
        v0, v1 = _tea_crypt_block(v0, v1, TEA_KEY, encrypt=False)
        struct.pack_into('<II', raw, i * 8, v0, v1)
    return bytes(raw)


# Backward-compatible aliases matching master tool/b_protocol.py names
bProtocolEncrypt = tea_encrypt
bProtocolDecrypt = tea_decrypt


# ---------------------------------------------------------------------------
# Checksum
# ---------------------------------------------------------------------------

def calc_checksum(buf: bytes) -> int:
    """1-byte additive checksum: sum of all bytes, mod 256. Matches _bProtocolCalCheck."""
    if not buf:
        return 0
    return sum(buf) & 0xFF


# ---------------------------------------------------------------------------
# Pack / Parse
# ---------------------------------------------------------------------------

def pack_frame(device_id: int, cmd: int, param: bytes = b'',
               encrypt: bool = False) -> bytes:
    """
    Pack a BabyOS protocol frame.

    Layout:
      HEAD(1) + DeviceID(4 LE) + Length(2 LE = 1+len(param)) + CMD(1)
      + param(n) + Checksum(1)

    Returns packed frame bytes. When encrypt=True the finished frame is
    TEA-encrypted (same as firmware _bProtocolPack when _PROTO_ENCRYPT_ENABLE).
    """
    if param is None:
        param = b''
    if not isinstance(param, (bytes, bytearray)):
        raise TypeError('param must be bytes')
    if not (0 <= cmd <= 0xFF):
        raise ValueError('cmd must be 0..255')

    frame_len = 1 + len(param)  # CMD + params
    total_len = FRAME_HEADER_SIZE + frame_len
    buf = bytearray(total_len)
    buf[0] = PROTOCOL_HEAD
    struct.pack_into('<I', buf, 1, device_id & 0xFFFFFFFF)
    struct.pack_into('<H', buf, 5, frame_len)
    buf[7] = cmd & 0xFF
    if param:
        buf[8:8 + len(param)] = param
    buf[total_len - 1] = calc_checksum(bytes(buf[:total_len - 1]))

    out = bytes(buf)
    if encrypt:
        out = tea_encrypt(out)
    return out


def parse_frame(raw: bytes) -> Optional[Tuple[int, int, bytes]]:
    """
    Parse one BabyOS protocol frame.

    Returns (device_id, cmd, param_bytes) on success, None on any format /
    checksum error (mirrors firmware _bProtocolParse returning -1).
    Does not decrypt — call tea_decrypt first if the transport is encrypted.
    """
    if raw is None or len(raw) < FRAME_MIN_SIZE:
        return None
    if raw[0] != PROTOCOL_HEAD:
        return None

    device_id = struct.unpack_from('<I', raw, 1)[0]
    frame_len = struct.unpack_from('<H', raw, 5)[0]
    cmd = raw[7]

    total_len = FRAME_HEADER_SIZE + frame_len  # includes checksum byte
    if total_len > len(raw) or total_len < FRAME_MIN_SIZE:
        return None

    expected = calc_checksum(raw[:total_len - 1])
    if expected != raw[total_len - 1]:
        return None

    param = bytes(raw[8:total_len - 1])
    return device_id, cmd, param


# ---------------------------------------------------------------------------
# Command payload builders / parsers (tool/README.md)
# ---------------------------------------------------------------------------

def build_test_param() -> bytes:
    """CMD_TEST: fixed 7-byte marker 'BabyOS\\0'."""
    return b'BabyOS\x00'


def build_utc_param(utc: int) -> bytes:
    """CMD_UTC: 4-byte little-endian Unix timestamp."""
    return struct.pack('<I', utc & 0xFFFFFFFF)


def build_fw_info_param(size: int, crc32: int, filename: str) -> bytes:
    """CMD_FW_INFO (host→device): size(4) + crc32(4) + filename(64, zero-padded)."""
    name = (filename or '').encode('utf-8')[:FW_NAME_SIZE]
    name = name.ljust(FW_NAME_SIZE, b'\x00')
    return struct.pack('<II', size & 0xFFFFFFFF, crc32 & 0xFFFFFFFF) + name


def parse_fw_info_param(param: bytes) -> Optional[Tuple[int, int, str]]:
    """Parse CMD_FW_INFO → (size, crc32, filename_str). None if malformed."""
    if param is None or len(param) < 8 + FW_NAME_SIZE:
        return None
    size, crc32 = struct.unpack_from('<II', param, 0)
    name_raw = param[8:8 + FW_NAME_SIZE]
    name = name_raw.split(b'\x00', 1)[0].decode('utf-8', errors='replace')
    return size, crc32, name


def build_fdata_req_param(seq: int) -> bytes:
    """CMD_FDATA device→host request: seq(2 LE)."""
    return struct.pack('<H', seq & 0xFFFF)


def parse_fdata_req_param(param: bytes) -> Optional[int]:
    """Parse device FDATA request → seq. None if malformed."""
    if param is None or len(param) < 2:
        return None
    return struct.unpack_from('<H', param, 0)[0]


def build_fdata_param(seq: int, data: bytes) -> bytes:
    """CMD_FDATA host→device: seq(2 LE) + data(512, zero-padded if short)."""
    if data is None:
        data = b''
    chunk = bytes(data[:FDATA_CHUNK_SIZE])
    chunk = chunk.ljust(FDATA_CHUNK_SIZE, b'\x00')
    return struct.pack('<H', seq & 0xFFFF) + chunk


def parse_fdata_param(param: bytes) -> Optional[Tuple[int, bytes]]:
    """Parse CMD_FDATA host→device payload → (seq, 512-byte data)."""
    if param is None or len(param) < 2 + FDATA_CHUNK_SIZE:
        return None
    seq = struct.unpack_from('<H', param, 0)[0]
    return seq, bytes(param[2:2 + FDATA_CHUNK_SIZE])


def build_ota_result_param(result: int) -> bytes:
    """CMD_OTA_RESULT device→host: 1-byte result."""
    return bytes([result & 0xFF])


def parse_ota_result_param(param: bytes) -> Optional[int]:
    if param is None or len(param) < 1:
        return None
    return param[0]


def build_trans_file_param(size: int, crc32: int, dev_no: int, offset: int) -> bytes:
    """CMD_TRANS_FILE (host→device): size + crc32 + dev_no + offset (4×4B LE)."""
    return struct.pack('<IIII',
                       size & 0xFFFFFFFF,
                       crc32 & 0xFFFFFFFF,
                       dev_no & 0xFFFFFFFF,
                       offset & 0xFFFFFFFF)


def parse_trans_file_param(param: bytes) -> Optional[Tuple[int, int, int, int]]:
    if param is None or len(param) < 16:
        return None
    return struct.unpack_from('<IIII', param, 0)


def parse_uid_response(param: bytes) -> Optional[bytes]:
    """CMD_GET_UID device→host: uid_len(1) + uid(n)."""
    if param is None or len(param) < 1:
        return None
    n = param[0]
    if n == 0 or len(param) < 1 + n:
        return None
    return bytes(param[1:1 + n])


def build_sn_param(sn_body: bytes) -> bytes:
    """
    CMD_WRITE_SN (host→device): sn_len(1) + sn(n).

    sn_body is the RAW SN content WITHOUT the length prefix.
    sn_util.sn_bytes(uid, orval) already returns the full length-prefixed
    param — use that directly as the frame param instead of wrapping again.
    """
    if sn_body is None:
        sn_body = b''
    return bytes([len(sn_body) & 0xFF]) + bytes(sn_body)


def parse_devinfo_response(param: bytes) -> Optional[Tuple[bytes, bytes]]:
    """CMD_DEVICEINFO device→host: version(16) + name(16)."""
    need = DEVINFO_FIELD_SIZE * 2
    if param is None or len(param) < need:
        return None
    version = bytes(param[:DEVINFO_FIELD_SIZE])
    name = bytes(param[DEVINFO_FIELD_SIZE:need])
    return version, name


def build_devinfo_param(version: str, name: str) -> bytes:
    """CMD_DEVICEINFO response body: version(16) + name(16), zero-padded."""
    v = (version or '').encode('utf-8')[:DEVINFO_FIELD_SIZE]
    n = (name or '').encode('utf-8')[:DEVINFO_FIELD_SIZE]
    return v.ljust(DEVINFO_FIELD_SIZE, b'\x00') + n.ljust(DEVINFO_FIELD_SIZE, b'\x00')


def parse_set_cfgnet_param(param: bytes) -> Optional[Tuple[int, str, str]]:
    """CMD_SETCFGNET_MODE: type(1) + ssid(32) + passwd(64)."""
    if param is None or len(param) < 1 + 32 + 64:
        return None
    ptype = param[0]
    ssid = param[1:33].split(b'\x00', 1)[0].decode('utf-8', errors='replace')
    passwd = param[33:97].split(b'\x00', 1)[0].decode('utf-8', errors='replace')
    return ptype, ssid, passwd


def parse_netinfo_response(param: bytes) -> Optional[Tuple[str, int, int, int]]:
    """CMD_GET_NETINFO device→host: ssid(32) + ip(4) + gw(4) + mask(4)."""
    if param is None or len(param) < 32 + 12:
        return None
    ssid = param[:32].split(b'\x00', 1)[0].decode('utf-8', errors='replace')
    ip, gw, mask = struct.unpack_from('<III', param, 32)
    return ssid, ip, gw, mask


# ---------------------------------------------------------------------------
# Optional instance-based registry (master tool/b_protocol.py compatibility)
# ---------------------------------------------------------------------------

_protocol_info: List[Tuple[int, DispatchFn]] = []


def bProtocolRegist(device_id: int, dispatch_func: DispatchFn) -> int:
    """Register a protocol instance (max 1). Returns 0 on success, -1 on failure."""
    global _protocol_info
    if _protocol_info or dispatch_func is None:
        return -1
    _protocol_info.append((device_id, dispatch_func))
    return 0


def bProtocolSetID(instance: int, device_id: int) -> int:
    if instance < 0 or instance >= len(_protocol_info):
        return -1
    _, fn = _protocol_info[instance]
    _protocol_info[instance] = (device_id, fn)
    return 0


def bProtocolParse(instance: int, raw_buf: bytes) -> int:
    """Registry parse: dispatch on success, -1 on error."""
    if instance < 0 or instance >= len(_protocol_info):
        return -1
    parsed = parse_frame(raw_buf)
    if parsed is None:
        return -1
    device_id, cmd, param = parsed
    _, dispatch_func = _protocol_info[instance]
    return dispatch_func(device_id, cmd, param, len(param))


def bProtocolPack(instance: int, device_id: int, cmd: int,
                  param: bytes, pbuf: bytearray) -> int:
    """Registry pack into caller buffer. Returns total length or -1."""
    if instance < 0 or instance >= len(_protocol_info) or pbuf is None:
        return -1
    frame = pack_frame(device_id, cmd, param or b'')
    if len(frame) > len(pbuf):
        return -1
    pbuf[:len(frame)] = frame
    return len(frame)


def bProtocolPackBytes(instance: int, device_id: int, cmd: int,
                       param: bytes = b'') -> bytes:
    if instance < 0 or instance >= len(_protocol_info):
        return b''
    return pack_frame(device_id, cmd, param or b'')
