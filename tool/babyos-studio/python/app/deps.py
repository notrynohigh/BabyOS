"""通用依赖：路径、原子 JSON、统一错误。"""
from __future__ import annotations

import json
import os
import shutil
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

from fastapi import HTTPException

from .config import projects_root
from .schemas import ProjectMeta


class AppError(HTTPException):
    """统一错误：{"detail": ..., "code": ...}（design.md §4.1）

    Optional extra keys (e.g. tool logs) are merged into the HTTP detail
    payload so clients can surface build/flash diagnostics without a second
    API round-trip.
    """

    def __init__(self, status: int, code: str, detail: str, extra: Optional[dict] = None):
        payload = {"detail": detail, "code": code}
        if extra:
            payload.update(extra)
        super().__init__(status_code=status, detail=payload)
        self.code = code


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def project_dir(project_id: str) -> Path:
    p = projects_root() / project_id
    if not p.is_dir():
        raise AppError(404, "PROJECT_NOT_FOUND", f"工程不存在: {project_id}")
    return p


def project_exists(project_id: str) -> bool:
    return (projects_root() / project_id).is_dir()


def atomic_write_json(path: Path, obj) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=str(path.parent), suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump(obj, f, ensure_ascii=False, indent=2)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def read_json(path: Path, default=None):
    if not path.exists():
        return default
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def load_meta(project_id: str) -> ProjectMeta:
    d = project_dir(project_id)
    raw = read_json(d / "meta.json")
    if raw is None:
        raise AppError(404, "PROJECT_NOT_FOUND", f"工程元数据缺失: {project_id}")
    return ProjectMeta(**raw)


def save_meta(meta: ProjectMeta) -> None:
    meta.updated_at = utc_now()
    atomic_write_json(project_dir(meta.project_id) / "meta.json", meta.model_dump())


def sanitize_filename(name: str) -> str:
    """zip/文件命名消毒（design.md §7.4）：仅保留 \w 与 -，剔除路径成分"""
    import re

    s = re.sub(r"[^\w\-]", "_", name)
    s = s.replace("..", "_").strip("_")
    return s or "bundle"


def copytree_clean(src: Path, dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
