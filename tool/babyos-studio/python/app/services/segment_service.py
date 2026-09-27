"""片段 CRUD（FR-3）：重叠校验、最短长度警告、级联失效。"""
from __future__ import annotations

import uuid

from ..config import STAGE_INDEX
from ..deps import AppError, atomic_write_json, project_dir, read_json
from ..schemas import ProjectMeta, SegmentIn
from .project_service import ProjectService

_svc = ProjectService()


def _segs(pdir) -> list:
    return read_json(pdir / "labeling" / "segments.json", default=[])


def _save(pdir, segs: list) -> None:
    atomic_write_json(pdir / "labeling" / "segments.json", segs)


def list_segments(meta: ProjectMeta) -> list:
    return _segs(project_dir(meta.project_id))


def _check_overlap(segs: list, file_id: str, start: int, end: int, ignore_id: str | None = None) -> None:
    for s in segs:
        if s["file_id"] != file_id or s["id"] == ignore_id:
            continue
        if start < s["end"] and end > s["start"]:
            raise AppError(422, "SEGMENT_OVERLAP", f"与片段 {s['id']}({s['start']}~{s['end']}) 重叠")


def _check_label(meta: ProjectMeta, label_id: int) -> None:
    if not any(l.label_id == label_id for l in meta.labels):
        raise AppError(422, "LABEL_NOT_FOUND", f"标签不存在: {label_id}")


def add_segment(meta: ProjectMeta, body: SegmentIn) -> dict:
    pdir = project_dir(meta.project_id)
    segs = _segs(pdir)
    _check_label(meta, body.label_id)
    _check_overlap(segs, body.file_id, body.start, body.end)
    if body.end <= body.start:
        raise AppError(422, "BAD_RANGE", "end 必须大于 start")
    seg = {
        "id": uuid.uuid4().hex[:12],
        "file_id": body.file_id,
        "start": int(body.start),
        "end": int(body.end),
        "label_id": int(body.label_id),
        "source": "manual",
    }
    segs.append(seg)
    _save(pdir, segs)
    _after_segments_changed(meta, pdir)
    return seg


def update_segment(meta: ProjectMeta, seg_id: str, body: SegmentIn) -> dict:
    pdir = project_dir(meta.project_id)
    segs = _segs(pdir)
    _check_label(meta, body.label_id)
    _check_overlap(segs, body.file_id, body.start, body.end, ignore_id=seg_id)
    for s in segs:
        if s["id"] == seg_id:
            s.update(
                {
                    "file_id": body.file_id,
                    "start": int(body.start),
                    "end": int(body.end),
                    "label_id": int(body.label_id),
                }
            )
            _save(pdir, segs)
            _after_segments_changed(meta, pdir)
            return s
    raise AppError(404, "SEGMENT_NOT_FOUND", f"片段不存在: {seg_id}")


def delete_segment(meta: ProjectMeta, seg_id: str) -> None:
    pdir = project_dir(meta.project_id)
    segs = [s for s in _segs(pdir) if s["id"] != seg_id]
    if len(segs) == len(_segs(pdir)):
        raise AppError(404, "SEGMENT_NOT_FOUND", f"片段不存在: {seg_id}")
    _save(pdir, segs)
    _after_segments_changed(meta, pdir)


def _after_segments_changed(meta: ProjectMeta, pdir) -> None:
    # 标注变更 → 级联失效（FR-1.4）
    # BUG-6: When stage is 'featured', target 'featured' (not just 'labeled')
    # so that features/training/export are also cleared.
    was_featured = STAGE_INDEX[meta.stage] >= STAGE_INDEX["featured"]
    if was_featured:
        _svc.invalidate(meta.project_id, "featured")
    else:
        _svc.invalidate(meta.project_id, "labeled")
    segs = _segs(pdir)
    if segs or meta.mode == "table":
        # BUG-6 fix: advance to 'featured' when original stage was >= featured,
        # so the stage returns to 'featured' (not just 'labeled')
        _svc.advance(meta.project_id, "featured" if was_featured else "labeled")
    else:
        # 片段清空 → 回到 data_imported（无标注可训练）
        _svc.invalidate(meta.project_id, "data_imported")
