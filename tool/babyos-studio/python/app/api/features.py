"""特征工程 API（FR-5）：配置、矩阵构建、打分。"""
from __future__ import annotations

from fastapi import APIRouter, Query

from ..deps import AppError, project_dir, read_json
from ..schemas import FeatureConfig
from ..services import feature_service
from ..services.trainer import registry
from .project_service import ProjectService, check_project_id

router = APIRouter(prefix="/api/projects", tags=["features"])
svc = ProjectService()


def _meta(pid: str):
    check_project_id(pid)
    return svc.get(pid)


def _guard(pid: str) -> None:
    if registry.is_active(pid):
        raise AppError(409, "TRAINING_ACTIVE", "训练进行中，请先取消或等待完成")


@router.get("/{pid}/features/config", response_model=FeatureConfig)
def get_config(pid: str):
    return feature_service.get_config(_meta(pid))


@router.get("/{pid}/features/categories")
def get_categories(pid: str):
    """返回特征分类定义和项目通道列表，供前端构建分类选择 UI。"""
    meta = _meta(pid)
    return {
        "categories": feature_service.FEATURE_CATEGORIES,
        "channels": meta.channels,
        "all_features": feature_service.ALL_FEATURES,
    }


@router.put("/{pid}/features/config", response_model=FeatureConfig)
def put_config(pid: str, cfg: FeatureConfig):
    meta = _meta(pid)
    _guard(pid)
    return feature_service.put_config(meta, cfg)


@router.post("/{pid}/features/compute")
def compute(pid: str):
    """构建（或复用缓存）特征矩阵，返回维度与丢弃统计。"""
    meta = _meta(pid)
    _guard(pid)
    return feature_service.compute(meta)


@router.get("/{pid}/features/matrix")
def matrix_info(pid: str):
    _meta(pid)
    info = read_json(project_dir(pid) / "features" / "matrix.meta.json")
    if info is None:
        raise AppError(404, "NO_MATRIX", "特征矩阵不存在，请先计算特征")
    return info


@router.get("/{pid}/features/scoring")
def scoring(pid: str, method: str = Query("f_test", pattern="^(f_test|mutual_info|variance)$")):
    meta = _meta(pid)
    try:
        ranking = feature_service.scoring(meta, method)
    except AppError:
        raise
    except Exception as e:
        raise AppError(422, "SCORING_FAILED", f"特征评分失败: {e}")
    return {"method": method, "ranking": ranking}
