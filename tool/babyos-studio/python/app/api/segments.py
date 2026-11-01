"""片段 API（FR-3）：时序区间标注。"""
from __future__ import annotations

from fastapi import APIRouter

from ..schemas import SegmentIn
from ..services import segment_service
from ..services.trainer import registry
from .project_service import ProjectService, check_project_id
from ..deps import AppError

router = APIRouter(prefix="/api/projects", tags=["segments"])
svc = ProjectService()


def _meta(pid: str):
    check_project_id(pid)
    return svc.get(pid)


def _guard(pid: str) -> None:
    if registry.is_active(pid):
        raise AppError(409, "TRAINING_ACTIVE", "训练进行中，请先取消或等待完成")


@router.get("/{pid}/segments")
def list_segments(pid: str):
    return segment_service.list_segments(_meta(pid))


@router.post("/{pid}/segments", status_code=201)
def add_segment(pid: str, body: SegmentIn):
    meta = _meta(pid)
    _guard(pid)
    if meta.mode != "timeseries":
        raise AppError(422, "NOT_TIMESERIES", "表格工程无片段概念")
    return segment_service.add_segment(meta, body)


@router.patch("/{pid}/segments/{seg_id}")
def update_segment(pid: str, seg_id: str, body: SegmentIn):
    meta = _meta(pid)
    _guard(pid)
    return segment_service.update_segment(meta, seg_id, body)


@router.delete("/{pid}/segments/{seg_id}", status_code=204)
def delete_segment(pid: str, seg_id: str):
    meta = _meta(pid)
    _guard(pid)
    segment_service.delete_segment(meta, seg_id)
