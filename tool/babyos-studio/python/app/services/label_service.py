"""标签体系 CRUD（FR-2.5）：id 永不重用，训练开始后冻结。"""
from __future__ import annotations

from ..config import STAGE_INDEX
from ..deps import AppError, project_dir, read_json, save_meta
from ..schemas import LabelDef, ProjectMeta


def _freeze_check(meta: ProjectMeta) -> None:
    if STAGE_INDEX[meta.stage] >= STAGE_INDEX["trained"]:
        raise AppError(409, "LABELS_FROZEN", "训练已开始，标签体系只读（可删除工程重来）")


def _in_use(meta: ProjectMeta, pdir, label_id: int) -> bool:
    segs = read_json(pdir / "labeling" / "segments.json", default=[])
    if any(s["label_id"] == label_id for s in segs):
        return True
    files = read_json(pdir / "raw" / "files.json", default={})
    return any(label_id in (fm.get("y") or []) for fm in files.values())


def add_label(meta: ProjectMeta, name: str, color: str | None = None) -> LabelDef:
    _freeze_check(meta)
    if any(l.name == name for l in meta.labels):
        raise AppError(422, "DUP_LABEL", f"标签已存在: {name}")
    ids = [l.label_id for l in meta.labels]
    nid = (max(ids) + 1) if ids else 0
    palette = ["#2f80ed", "#27ae60", "#e6a23c", "#eb5757", "#9b51e0", "#00b8d4", "#f2994a", "#6fcf97"]
    lbl = LabelDef(label_id=nid, name=name, color=color or palette[nid % len(palette)])
    meta.labels.append(lbl)
    save_meta(meta)
    return lbl


def rename_label(meta: ProjectMeta, label_id: int, name: str, color: str | None = None) -> LabelDef:
    _freeze_check(meta)
    for l in meta.labels:
        if l.label_id == label_id:
            l.name = name
            if color:
                l.color = color
            save_meta(meta)
            return l
    raise AppError(404, "LABEL_NOT_FOUND", f"标签不存在: {label_id}")


def delete_label(meta: ProjectMeta, label_id: int) -> None:
    _freeze_check(meta)
    from ..deps import project_dir

    pdir = project_dir(meta.project_id)
    if _in_use(meta, pdir, label_id):
        raise AppError(409, "LABEL_IN_USE", "标签被片段/样本引用，先删除相关标注")
    meta.labels = [l for l in meta.labels if l.label_id != label_id]
    save_meta(meta)
