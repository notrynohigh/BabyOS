"""数据模板服务（FR-2.8，design §6.7）：zip 即时打包返回。

资产位于 `backend/app/assets/templates/{timeseries,table}/`：
- data_template.csv（CSV 全列示范）
- 模板说明.md（三种导入形态、列定义）
"""
from __future__ import annotations

import io
import zipfile
from pathlib import Path

from ..deps import AppError

ASSETS = Path(__file__).resolve().parents[1] / "assets" / "templates"
KINDS = ("timeseries", "table")


def _check_kind(kind: str) -> str:
    if kind not in KINDS:
        raise AppError(404, "BAD_KIND", f"未知模板类型: {kind!r}（可选 {KINDS}）")
    d = ASSETS / kind
    if not d.is_dir():
        raise AppError(500, "TEMPLATE_MISSING", f"模板资产缺失: {d}")
    return kind


def render_zip(kind: str) -> tuple[bytes, str]:
    """返回 (zip 字节, 下载文件名)。"""
    _check_kind(kind)
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted((ASSETS / kind).iterdir()):
            if p.is_file():
                z.write(p, p.name)
    return buf.getvalue(), f"{kind}_template.zip"
