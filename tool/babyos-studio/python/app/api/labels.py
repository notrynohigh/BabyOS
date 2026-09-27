"""标签 API（FR-2.5）。"""
from __future__ import annotations

from typing import List

from fastapi import APIRouter
from pydantic import BaseModel

from ..schemas import LabelDef
from ..services import label_service
from ..services.trainer import registry
from .project_service import ProjectService, check_project_id
from ..deps import AppError

router = APIRouter(prefix="/api/projects", tags=["labels"])
svc = ProjectService()


class LabelIn(BaseModel):
    name: str
    color: str | None = None


def _meta(pid: str):
    check_project_id(pid)
    return svc.get(pid)


def _guard(pid: str) -> None:
    if registry.is_active(pid):
        raise AppError(409, "TRAINING_ACTIVE", "训练进行中，请先取消或等待完成")


@router.get("/{pid}/labels", response_model=List[LabelDef])
def list_labels(pid: str):
    return _meta(pid).labels


@router.post("/{pid}/labels", response_model=LabelDef, status_code=201)
def add_label(pid: str, body: LabelIn):
    meta = _meta(pid)
    _guard(pid)
    return label_service.add_label(meta, body.name, body.color)


@router.patch("/{pid}/labels/{label_id}", response_model=LabelDef)
def rename_label(pid: str, label_id: int, body: LabelIn):
    meta = _meta(pid)
    _guard(pid)
    return label_service.rename_label(meta, label_id, body.name, body.color)


@router.delete("/{pid}/labels/{label_id}", status_code=204)
def delete_label(pid: str, label_id: int):
    meta = _meta(pid)
    _guard(pid)
    label_service.delete_label(meta, label_id)
