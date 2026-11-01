"""工程 API（design.md §5）。训练写锁检查经由 RunRegistry。"""
from __future__ import annotations

import os
import shutil
from typing import List
from urllib.parse import quote

from fastapi import APIRouter, File, UploadFile
from fastapi.responses import Response

from ..config import trash_root
from ..deps import AppError
from ..schemas import ProjectCreate, ProjectMeta, ProjectPatch, ProjectSummary
from ..services import archive_service
from ..services.trainer import registry
from .project_service import ProjectService, check_project_id

router = APIRouter(prefix="/api/projects", tags=["projects"])
svc = ProjectService()


def _guard_training(pid: str) -> None:
    if registry.is_active(pid):
        raise AppError(409, "TRAINING_ACTIVE", "训练进行中，请先取消或等待完成")


@router.get("", response_model=List[ProjectSummary])
def list_projects() -> list[ProjectSummary]:
    return svc.list()


@router.post("", response_model=ProjectMeta, status_code=201)
def create_project(body: ProjectCreate) -> ProjectMeta:
    return svc.create(body)


# -------- 工程导入（必须在 /{pid} 之前以避免 path 抢占） --------


@router.post("/import", status_code=201)
async def import_project(file: UploadFile = File(...)) -> dict:
    """上传 `.bosml`，校验/解压/落盘，返回 {"meta": ProjectMeta, "skipped": [...]}。

    评审 P2-FX-7：聚合 skipped 条目（不安全/重复/空目录），前端弹 ElMessage 提示。
    """
    content = await file.read()
    return archive_service.import_archive(content)


# -------- 回收站（必须在 /{pid} 之前） --------


@router.post("/clear-all", status_code=200)
def clear_all_projects():
    """清空所有工程（软删除到回收站），返回清理条数。"""
    projects = svc.list()
    count = 0
    for p in projects:
        try:
            svc.delete(p.project_id, hard=False)
            count += 1
        except Exception:
            pass
    return {"deleted": count}


@router.get("/trash", response_model=List[dict])
def list_trash():
    """列出回收站工程（新→旧）。"""
    return svc.list_trashed()


@router.post("/trash/sweep", response_model=dict)
def sweep_trash(days: int | None = None):
    """清理超过 N 天的回收站条目，返回清理条数。默认走配置项 trash_retention_days。"""
    n = svc.sweep_trash(days=days)
    return {"swept": n}


@router.post("/trash/{trash_id}/restore", response_model=ProjectMeta, status_code=200)
def restore_trash(trash_id: str) -> ProjectMeta:
    """从回收站恢复工程。"""
    return svc.restore(trash_id)


@router.delete("/trash/{trash_id}", status_code=204)
def delete_from_trash(trash_id: str):
    """从回收站永久删除单条（不可恢复）。"""
    if ".." in trash_id or "/" in trash_id or "\\" in trash_id:
        raise AppError(400, "INVALID_TRASH_ID", "无效的回收站ID")
    target = trash_root() / trash_id
    resolved = target.resolve()
    if not str(resolved).startswith(str(trash_root().resolve()) + os.sep):
        raise AppError(400, "INVALID_TRASH_ID", "无效的回收站ID")
    if not target.is_dir():
        raise AppError(404, "TRASH_NOT_FOUND", f"回收站条目不存在: {trash_id}")
    shutil.rmtree(target)


# -------- 工程级操作 --------


@router.get("/{pid}", response_model=ProjectMeta)
def get_project(pid: str) -> ProjectMeta:
    check_project_id(pid)
    return svc.get(pid)


@router.patch("/{pid}", response_model=ProjectMeta)
def patch_project(pid: str, body: ProjectPatch) -> ProjectMeta:
    check_project_id(pid)
    _guard_training(pid)
    return svc.patch(pid, body)


@router.delete("/{pid}", status_code=204)
def delete_project(pid: str, hard: bool = False):
    """删除工程。评审 P0-FX-5：默认软删除（移到 .trash，可恢复）；
    `?hard=true` 走永久删除（需用户主动确认）。"""
    check_project_id(pid)
    _guard_training(pid)
    svc.delete(pid, hard=hard)


@router.post("/{pid}/copy", response_model=ProjectMeta, status_code=201)
def copy_project(pid: str) -> ProjectMeta:
    check_project_id(pid)
    _guard_training(pid)
    return svc.copy(pid)


@router.post("/{pid}/archive")
def archive_project(pid: str) -> Response:
    """导出 `.bosml`（zip）；训练中 409。"""
    check_project_id(pid)
    _guard_training(pid)
    body, filename = archive_service.export_archive(pid)
    encoded = quote(filename)
    return Response(
        content=body,
        media_type="application/zip",
        headers={
            "Content-Disposition":
                f"attachment; filename=\"bosml.zip\"; filename*=UTF-8''{encoded}",
        },
    )
