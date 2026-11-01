"""工程存档（FR-1.5~1.7，design §6.6）：`.bosml` 导出与导入。

格式约定：
  manifest.json        = {"format":"bosml","version":1,"exported_at":...,"project_id":...}
  project/             = 工程目录全量（meta.json + raw/ + labeling/ + features/ + training/ + export/）
- 服务端自构 zip 条目，**禁止**用户路径入 zip（防路径穿越，评审 P2-4/FR-1.5）
- 导入临时目录用 mkdtemp(dir=data_root) 保证同卷 → shutil.move 真原子（评审 P2-4）
"""
from __future__ import annotations

import io
import re
import shutil
import tempfile
import uuid
import zipfile
from datetime import datetime, timezone
from pathlib import Path

from ..config import projects_root
from ..deps import AppError, atomic_write_json, project_dir, save_meta, utc_now
from ..schemas import ProjectMeta
from .project_service import ProjectService, check_project_id

CURRENT_VERSION = 1
MAX_ARCHIVE_BYTES = 500 * 1024 * 1024  # 500MB 兜底

_svc = ProjectService()


def _sanitize_filename(name: str) -> str:
    """下载文件名展示（不入 zip）：剔除 / \\ 与控制字符，保留 Unicode。

    不替换中文等可显示 Unicode（评审 P2-*：测试期望中文工程名可下载）。
    仍防御路径分隔符与 NUL。
    """
    s = re.sub(r"[\\/:\x00]", "_", name).strip()
    return s or "project"


def _is_safe_entry(name: str) -> bool:
    """zip 条目安全校验：拒绝绝对路径、..、反斜杠（设计评审 P2-4）。"""
    if not name or name.startswith("/") or "\\" in name:
        return False
    parts = Path(name).parts
    return not any(p == ".." for p in parts)


def export_archive(pid: str) -> tuple[bytes, str]:
    """导出 `.bosml` 二进制 + 下载文件名。"""
    check_project_id(pid)
    meta = _svc.get(pid)
    pdir = project_dir(pid)
    buf = io.BytesIO()
    manifest = {
        "format": "bosml",
        "version": CURRENT_VERSION,
        "exported_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "project_id": pid,
    }
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json",
                   __import__("json").dumps(manifest, ensure_ascii=False, indent=2))
        if pdir.is_dir():
            for p in sorted(pdir.rglob("*")):
                if not p.is_file():
                    continue
                rel = "project/" + str(p.relative_to(pdir)).replace("\\", "/")
                if not _is_safe_entry(rel):
                    continue  # 防御：服务端自构不应出现
                z.write(p, rel)
    return buf.getvalue(), f"{_sanitize_filename(meta.name)}.bosml"


def _validate_manifest(d: dict) -> None:
    if not isinstance(d, dict):
        raise AppError(422, "BAD_FORMAT", "manifest.json 非法")
    if d.get("format") != "bosml":
        raise AppError(422, "BAD_FORMAT", f"format != bosml: {d.get('format')!r}")
    v = d.get("version")
    # 严格 int 校验（防 0/负数/JSON true 混入，评审 P2-3）
    if not isinstance(v, int) or isinstance(v, bool) or not (1 <= v <= CURRENT_VERSION):
        raise AppError(422, "UNSUPPORTED_VERSION",
                       f"version {v!r} 不受支持（当前 {CURRENT_VERSION}）")


def _dedupe_name(base: str) -> str:
    """name 重名去重：与现存工程名冲突时加 '-导入' / '-导入2' / ...（AC-16 契约）。"""
    root = projects_root()
    used = set()
    if root.is_dir():
        for d in root.iterdir():
            try:
                used.add(_svc.get(d.name).name)
            except Exception:
                continue
    if base not in used:
        return base
    suffix = "-导入"
    cand = base + suffix
    if cand not in used:
        return cand
    i = 2
    while True:
        cand = f"{base}{suffix}{i}"
        if cand not in used:
            return cand
        i += 1


def import_archive(content: bytes) -> dict:
    """导入 `.bosml` 二进制，返回 {"meta": ProjectMeta, "skipped": [name, ...]}。

    评审 P2-FX-7：聚合 skipped 条目（不安全/重复/空目录），让前端能提示
    "导入了 N 个文件，跳过 M 个"。"""
    if len(content) > MAX_ARCHIVE_BYTES:
        raise AppError(422, "ARCHIVE_TOO_LARGE",
                       f"存档超过 {MAX_ARCHIVE_BYTES // (1024*1024)} MB 上限")
    if not zipfile.is_zipfile(io.BytesIO(content)):
        raise AppError(422, "BAD_ARCHIVE", "上传不是有效 zip 文件")

    skipped: list[str] = []
    with zipfile.ZipFile(io.BytesIO(content)) as z:
        names = z.namelist()
        unsafe = [n for n in names if not _is_safe_entry(n)]
        if unsafe:
            raise AppError(422, "UNSAFE_ENTRY",
                           f"zip 含 {len(unsafe)} 个不安全条目，首个: {unsafe[0]!r}")
        if "manifest.json" not in names:
            raise AppError(422, "BAD_ARCHIVE", "缺少 manifest.json")
        try:
            manifest = __import__("json").loads(z.read("manifest.json"))
        except Exception as e:  # noqa: BLE001 - JSON 解析失败转 422
            raise AppError(422, "BAD_ARCHIVE", f"manifest.json 解析失败: {e}")
        _validate_manifest(manifest)
        if not any(n.startswith("project/meta.json") for n in names):
            raise AppError(422, "BAD_PROJECT", "缺少 project/meta.json")

        data_root = projects_root().parent
        data_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="bosml_import_", dir=data_root) as td:
            tmp = Path(td)
            # 手动解压：空目录 / 重复条目聚合到 skipped
            seen: set[str] = set()
            for n in names:
                if n.endswith("/"):
                    # 空目录或显式目录条目：跳过但记录
                    if not z.read(n):  # noqa: 不存在的目录信息会抛空
                        skipped.append(n)
                    continue
                if n in seen:
                    skipped.append(n)
                    continue
                seen.add(n)
                z.extract(n, tmp)
            tmp_project = tmp / "project"
            if not (tmp_project / "meta.json").is_file():
                raise AppError(422, "BAD_PROJECT", "project/meta.json 不存在")
            try:
                meta_dict = __import__("json").loads((tmp_project / "meta.json").read_text())
            except Exception as e:  # noqa: BLE001
                raise AppError(422, "BAD_PROJECT", f"meta.json 解析失败: {e}")
            for k in ("project_id", "name", "mode", "stage"):
                if k not in meta_dict:
                    raise AppError(422, "BAD_PROJECT", f"meta.json 缺少字段 {k}")
            if meta_dict["mode"] not in ("timeseries", "table"):
                raise AppError(422, "BAD_PROJECT",
                               f"mode 非法: {meta_dict['mode']!r}")

            # 收集产物目录中的非文件条目（空目录等）作为 skipped 兜底
            for p in tmp_project.rglob("*"):
                if not p.is_file():
                    rel = "project/" + str(p.relative_to(tmp_project)).replace("\\", "/")
                    if rel not in seen:
                        skipped.append(rel + "/")

            # 新 id + 重命名（去重）
            new_id = str(uuid.uuid4())
            new_name = _dedupe_name(str(meta_dict["name"]))
            meta_dict["project_id"] = new_id
            meta_dict["name"] = new_name
            meta_dict["updated_at"] = utc_now()
            # created_at 保留导入前的值，仅 updated_at 刷新（design §6.6）

            dst = projects_root() / new_id
            # 同卷移动——tempfile.mkdtemp(dir=data_root) 已保证，但若 data_root
            # 跨 mount（同机不同卷 / 容器 bind mount）可能抛 shutil.Error(EXDEV)，
            # 退化为 copytree + rmtree（评审 P0-4）
            try:
                shutil.move(str(tmp_project), str(dst))
            except shutil.Error:
                shutil.copytree(str(tmp_project), str(dst))
                shutil.rmtree(str(tmp_project), ignore_errors=True)
            save_meta(ProjectMeta(**meta_dict))
    return {"meta": _svc.get(new_id), "skipped": skipped}
