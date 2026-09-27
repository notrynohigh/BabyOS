"""数据 API（FR-2/FR-4）：预览、导入（逐文件独立校验）、信息统计。"""
from __future__ import annotations

import json

from fastapi import APIRouter, File, Form, UploadFile

from ..deps import AppError
from ..schemas import ProjectMeta
from ..services import dataset_service
from ..services.trainer import registry
from .project_service import ProjectService, check_project_id

router = APIRouter(prefix="/api/projects", tags=["dataset"])
svc = ProjectService()


def _meta(pid: str) -> ProjectMeta:
    check_project_id(pid)
    return svc.get(pid)


def _guard_training(pid: str) -> None:
    if registry.is_active(pid):
        raise AppError(409, "TRAINING_ACTIVE", "训练进行中，请先取消或等待完成")


@router.post("/{pid}/dataset/preview")
async def preview(pid: str, file: UploadFile = File(...)) -> dict:
    _meta(pid)
    return dataset_service.preview_csv(await file.read(), file.filename or "upload.csv")


@router.post("/{pid}/dataset")
async def import_dataset(
    pid: str,
    files: list[UploadFile] = File(...),
    mapping: str = Form(...),
    import_kind: str = Form("append"),
) -> dict:
    meta = _meta(pid)
    _guard_training(pid)
    if import_kind not in ("append", "replace"):
        raise AppError(422, "BAD_KIND", "import_kind 必须为 append|replace")
    try:
        m = json.loads(mapping)
    except json.JSONDecodeError:
        raise AppError(422, "BAD_MAPPING", "mapping 不是合法 JSON")
    if meta.mode == "table" and not m.get("label_col"):
        raise AppError(422, "LABEL_COL_REQUIRED", "表格模式必须指定标签列")
    if meta.mode == "table" and not (m.get("features") or m.get("channels")):
        raise AppError(422, "NO_FEATURES", "表格模式必须选择至少 1 个特征列")
    if meta.mode == "timeseries" and not m.get("channels"):
        raise AppError(422, "NO_CHANNELS", "时序模式必须选择至少 1 个通道列")
    return dataset_service.import_files(meta, files, m, import_kind)


@router.get("/{pid}/dataset")
def dataset_info(pid: str) -> dict:
    meta = _meta(pid)
    return dataset_service.dataset_info(meta)


@router.get("/{pid}/dataset/file/{fid}/data")
def file_data(pid: str, fid: str, limit: int = 200, offset: int = 0) -> dict:
    meta = _meta(pid)
    return dataset_service.file_data(meta, fid, limit, offset)


@router.delete("/{pid}/dataset/file/{fid}")
def delete_file(pid: str, fid: str) -> dict:
    meta = _meta(pid)
    _guard_training(pid)
    return dataset_service.delete_file(meta, fid)
