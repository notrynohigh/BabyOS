"""
file_util — host-side file helpers for BabyOS Studio file-transfer (CMD 0x6).

Authority: origin/dev tool/mainwindow.py `_load_folder` — multi-file folder
merge packs each file as:

  name record : 0xAA01 (2B BE) + name_len(4B BE) + name_bytes
  data record : 0xAA02 (2B BE) + data_len(4B BE) + data_bytes

files sorted by name; output default `allfile.bin` inside the folder.
Python 3.8 compatible.
"""

from __future__ import print_function

import os
import struct
from typing import List, Optional, Tuple

RECORD_NAME = 0xAA01
RECORD_DATA = 0xAA02
DEFAULT_OUT_NAME = 'allfile.bin'
crc32_type = int  # crc_util.crc32 return type (avoids circular import)


def merge_folder(folder_path: str, out_name: str = DEFAULT_OUT_NAME
                 ) -> Tuple[str, int, int, crc32_type]:
    """
    Concatenate folder files (sorted) into `folder_path/out_name` using the
    BabyOS multi-file record format from origin/dev tool.

    Returns (out_path, file_count, total_bytes, crc32).
    Raises ValueError / IOError on bad input or IO failure.
    """
    if not folder_path or not os.path.isdir(folder_path):
        raise ValueError('folder not found: %r' % (folder_path,))
    out_path = os.path.join(folder_path, out_name or DEFAULT_OUT_NAME)
    entries: List[str] = []
    for fname in sorted(os.listdir(folder_path)):
        fpath = os.path.join(folder_path, fname)
        if not os.path.isfile(fpath):
            continue
        # skip previously merged output
        if os.path.abspath(fpath) == os.path.abspath(out_path):
            continue
        entries.append(fpath)
    if not entries:
        raise ValueError('folder has no mergeable files: %r' % (folder_path,))

    total = 0
    with open(out_path, 'wb') as out_f:
        for fpath in entries:
            fname = os.path.basename(fpath)
            name_bytes = fname.encode('utf-8')
            with open(fpath, 'rb') as in_f:
                content = in_f.read()
            out_f.write(struct.pack('>HI', RECORD_NAME, len(name_bytes)))
            out_f.write(name_bytes)
            out_f.write(struct.pack('>HI', RECORD_DATA, len(content)))
            out_f.write(content)
            total += (2 + 4 + len(name_bytes)) + (2 + 4 + len(content))
    # CRC32 via crc_util to stay aligned with protocol CRC32 口径
    from .crc_util import crc32 as _crc32
    with open(out_path, 'rb') as f:
        data = f.read()
    return out_path, len(entries), total or len(data), _crc32(data)


def parse_merged_file(path: str) -> List[dict]:
    """
    Parse an allfile.bin merge product back into entries
    [{name, data_len, data}]. Used by mock-device / tests.
    """
    if not path or not os.path.isfile(path):
        raise ValueError('merged file not found: %r' % (path,))
    with open(path, 'rb') as f:
        raw = f.read()
    entries: List[dict] = []
    off = 0
    n = len(raw)
    while off + 6 <= n:
        rec = struct.unpack_from('>H', raw, off)[0]
        if rec == RECORD_NAME:
            name_len = struct.unpack_from('>I', raw, off + 2)[0]
            off += 6
            if off + name_len > n:
                break
            name = raw[off:off + name_len].decode('utf-8', errors='replace')
            off += name_len
            if off + 6 > n:
                break
            rec2 = struct.unpack_from('>H', raw, off)[0]
            if rec2 != RECORD_DATA:
                break
            data_len = struct.unpack_from('>I', raw, off + 2)[0]
            off += 6
            if off + data_len > n:
                break
            data = raw[off:off + data_len]
            off += data_len
            entries.append({
                'name': name,
                'data_len': data_len,
                'data_hex': data.hex() if data else '',
                'data': data,
            })
        else:
            break
    return entries
