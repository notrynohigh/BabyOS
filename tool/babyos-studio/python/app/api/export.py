"""导出 API（FR-8）：触发导出（含自检）、状态、zip 下载。"""
from __future__ import annotations

from fastapi import APIRouter
from fastapi.responses import FileResponse

from ..services import export_service
from ..services.trainer import registry
from ..deps import AppError, project_dir as _project_dir
from .project_service import ProjectService, check_project_id

router = APIRouter(prefix="/api/projects", tags=["export"])
svc = ProjectService()


def _meta(pid: str):
    check_project_id(pid)
    return svc.get(pid)


@router.post("/{pid}/export")
def do_export(pid: str):
    meta = _meta(pid)
    if registry.is_active(pid):
        raise AppError(409, "TRAINING_ACTIVE", "训练进行中，请等待完成后再导出")
    return export_service.export(pid)


@router.get("/{pid}/export")
def export_status(pid: str):
    _meta(pid)
    return export_service.export_status(pid)


@router.get("/{pid}/export/download/{filename}")
def download(pid: str, filename: str):
    _meta(pid)
    p = export_service.download_path(pid, filename)
    return FileResponse(p, media_type="application/zip", filename=p.name)


@router.get("/{pid}/export/dir")
def export_dir(pid: str):
    """返回导出目录绝对路径（供 Electron 打开文件管理器）。"""
    _meta(pid)
    exp = _project_dir(pid) / "export"
    if not exp.is_dir():
        raise AppError(404, "NO_EXPORT", "暂无导出记录")
    return {"path": str(exp.resolve())}
