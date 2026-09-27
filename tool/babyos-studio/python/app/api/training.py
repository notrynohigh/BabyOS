"""训练 API（FR-6/FR-7）：发起、轮询、取消、leaderboard、设为最佳、最佳卡片。"""
from __future__ import annotations

from fastapi import APIRouter
from pydantic import BaseModel, Field

from ..deps import AppError
from ..schemas import TrainConfig, TrainState
from ..services import trainer, training_service
from .project_service import ProjectService, check_project_id

router = APIRouter(prefix="/api/projects", tags=["training"])
svc = ProjectService()


def _meta(pid: str):
    check_project_id(pid)
    return svc.get(pid)


class SetBestIn(BaseModel):
    cand_id: int = Field(ge=0)


@router.post("/{pid}/training", response_model=TrainState)
def start_training(pid: str, cfg: TrainConfig):
    _meta(pid)
    return training_service.start(pid, cfg)


@router.get("/{pid}/training", response_model=TrainState)
def training_status(pid: str):
    _meta(pid)
    return training_service.status(pid)


@router.post("/{pid}/training/cancel")
def cancel_training(pid: str):
    _meta(pid)
    if not trainer.request_cancel(pid):
        raise AppError(404, "NO_ACTIVE_TRAINING", "无活跃训练")
    return {"ok": True}


@router.get("/{pid}/training/leaderboard")
def get_leaderboard(pid: str):
    _meta(pid)
    return training_service.leaderboard(pid)


@router.post("/{pid}/training/set_best")
def set_best(pid: str, body: SetBestIn):
    _meta(pid)
    return training_service.set_best(pid, body.cand_id)


@router.get("/{pid}/training/best")
def best_info(pid: str):
    _meta(pid)
    return training_service.best_info(pid)


@router.get("/{pid}/training/feature_importance")
def feat_importance(pid: str):
    _meta(pid)
    return {"ranking": training_service.feature_importance(pid)}


@router.get("/{pid}/training/report")
def training_report(pid: str):
    """完整训练报告：配置 + 候选排行 + 最佳模型 + 测试指标 + 数据摘要。"""
    _meta(pid)
    return training_service.get_report(pid)
