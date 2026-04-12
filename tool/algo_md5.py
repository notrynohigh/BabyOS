"""
MD5 implementation — matching algo_md5.c interface.
md5_hex_16(data, length) -> 16-byte raw digest bytes.
"""

import hashlib


def md5_hex_16(data: bytes, length: int = None) -> bytes:
    """
    Compute raw 16-byte MD5 digest of data[:length].
    Returns exactly 16 bytes.
    """
    if length is not None:
        data = data[:length]
    return hashlib.md5(data).digest()
