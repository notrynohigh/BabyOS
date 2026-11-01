"""
sn_util — SN generation matching BabyOS Studio / master tool/mainwindow.py.

Algorithm (origin/master:tool/mainwindow.py _on_set_sn):
  1. md5_val = md5_hex_16(uid_bytes)          # 16 raw digest bytes
  2. sn_table = bytes([len(md5_val)]) + bytes(b | orval for b in md5_val)

  Length prefix is 1 byte; each digest byte is OR-ed with `orval`.
  CMD_WRITE_SN (0x8) param = sn_table.
"""

import hashlib
from typing import Union

# SN body length after the length prefix (md5 raw digest size)
SN_DIGEST_LEN = 16


def md5_hex_16(data: Union[bytes, bytearray, str]) -> bytes:
    """
    Raw 16-byte MD5 digest (NOT hex string).

    Matches master tool/algo_md5.py md5_hex_16():
      hashlib.md5(data).digest()
    """
    if isinstance(data, str):
        data = data.encode('utf-8')
    elif isinstance(data, bytearray):
        data = bytes(data)
    return hashlib.md5(data).digest()


def sn_bytes(uid: Union[bytes, bytearray, str], orval: int = 0) -> bytes:
    """
    Build the COMPLETE CMD_WRITE_SN (0x8) parameter:

        bytes([16]) + bytes(md5(uid)[i] | orval for i in range(16))

    Matches master mainwindow.py:
        sn_table = bytes([len(md5_val)]) + bytes(b | orval for b in md5_val)
        pack(..., CMD_SET_SN, sn_table)

    Use directly as pack_frame(..., CMD_WRITE_SN, sn_bytes(uid, orval)).
    Do NOT wrap again with build_sn_param() — that would double the length prefix.
    """
    if isinstance(uid, str):
        uid_bytes = uid.encode('utf-8')
    elif isinstance(uid, bytearray):
        uid_bytes = bytes(uid)
    else:
        uid_bytes = uid or b''

    orval &= 0xFF
    digest = md5_hex_16(uid_bytes)
    body = bytes(b | orval for b in digest)
    return bytes([len(body)]) + body


def sn_hex(sn: Union[bytes, bytearray]) -> str:
    """Human-readable SN hex string (space-separated, like mainwindow log)."""
    return ' '.join(f'{b:02X}' for b in sn)


def sn_body(sn: Union[bytes, bytearray]) -> bytes:
    """Strip the 1-byte length prefix; returns the 16-byte digest body."""
    if sn is None or len(sn) < 1:
        return b''
    n = sn[0]
    return bytes(sn[1:1 + n])
