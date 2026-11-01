"""AutoML 搜索 worker（FR-6）：随机超参采样 + GroupKFold/StratifiedKFold CV。

确定性要点（AC-10）：
- 候选序列由 default_rng(seed) 生成（模型类型 + 超参 + 特征子集）
- 每个候选内部随机性（RF/ET/MLP）用 default_rng([seed, cand_id]) 派生
- 同 seed 重跑 → 已完成候选分数逐一一致
"""
from __future__ import annotations

import time

import numpy as np
from sklearn.ensemble import (
    ExtraTreesClassifier,
    ExtraTreesRegressor,
    RandomForestClassifier,
    RandomForestRegressor,
)
from sklearn.linear_model import LinearRegression, LogisticRegression
from sklearn.naive_bayes import GaussianNB
from sklearn.neural_network import MLPClassifier
from sklearn.tree import DecisionTreeClassifier, DecisionTreeRegressor

try:
    from xgboost import XGBClassifier, XGBRegressor
    HAS_XGB = True
except ImportError:
    HAS_XGB = False

try:
    from lightgbm import LGBMClassifier, LGBMRegressor
    HAS_LGBM = True
except ImportError:
    HAS_LGBM = False

from ..schemas import TrainConfig
from .metrics_service import metric_value
from .scaler import Scaler
from .simple_nn import SimpleNNClassifier, SimpleNNRegressor

# ---------- 模型池 ----------
CLASSIFICATION_MODELS = ["dt", "rf", "et", "lr", "nb", "mlp", "xgb", "lgbm", "simple_nn"]
REGRESSION_MODELS = ["dt_r", "rf_r", "et_r", "lr_r", "xgb_r", "lgbm_r", "simple_nn_r"]


def _model_pool(task_type: str) -> list[str]:
    """根据任务类型返回可用模型池。"""
    return CLASSIFICATION_MODELS if task_type == "classification" else REGRESSION_MODELS


def sample_candidate(rng: np.random.Generator, cand_id: int,
                     task_type: str = "classification") -> dict:
    """生成一个候选配置（纯函数，确定性由 rng 保证）。"""
    pool = _model_pool(task_type)
    mt = pool[int(rng.integers(0, len(pool)))]

    # 如果选到的模型未安装，回退到基础模型
    if mt == "xgb" and not HAS_XGB:
        mt = "dt" if task_type == "classification" else "dt_r"
    elif mt == "lgbm" and not HAS_LGBM:
        mt = "dt" if task_type == "classification" else "dt_r"
    elif mt == "xgb_r" and not HAS_XGB:
        mt = "dt_r"
    elif mt == "lgbm_r" and not HAS_LGBM:
        mt = "dt_r"

    hp: dict = {}
    if mt == "dt":
        hp = {
            "max_depth": int(rng.integers(2, 17)),
            "min_samples_split": int(rng.integers(2, 21)),
            "class_weight": [None, "balanced"][int(rng.integers(0, 2))],
        }
    elif mt in ("rf", "et"):
        hp = {
            "n_estimators": int(rng.integers(10, 101)),
            "max_depth": int(rng.integers(2, 17)),
            "class_weight": [None, "balanced"][int(rng.integers(0, 2))],
        }
    elif mt == "lr":
        hp = {"C": [0.01, 0.1, 1.0, 10.0][int(rng.integers(0, 4))],
              "class_weight": [None, "balanced"][int(rng.integers(0, 2))]}
    elif mt == "nb":
        hp = {}
    elif mt == "mlp":
        hp = {
            "hidden": [8, 16, 32][int(rng.integers(0, 3))],
            "alpha": [0.0001, 0.001][int(rng.integers(0, 2))],
        }
    elif mt == "xgb":
        hp = {
            "n_estimators": int(rng.integers(20, 201)),
            "max_depth": int(rng.integers(2, 11)),
            "learning_rate": [0.01, 0.05, 0.1, 0.2][int(rng.integers(0, 4))],
            "subsample": [0.7, 0.8, 0.9, 1.0][int(rng.integers(0, 4))],
            "colsample_bytree": [0.7, 0.8, 0.9, 1.0][int(rng.integers(0, 4))],
            "reg_alpha": [0, 0.01, 0.1, 1.0][int(rng.integers(0, 4))],
            "reg_lambda": [0, 0.01, 0.1, 1.0][int(rng.integers(0, 4))],
        }
    elif mt == "lgbm":
        hp = {
            "n_estimators": int(rng.integers(20, 201)),
            "max_depth": int(rng.integers(2, 11)),
            "learning_rate": [0.01, 0.05, 0.1, 0.2][int(rng.integers(0, 4))],
            "num_leaves": int(rng.integers(8, 65)),
            "subsample": [0.7, 0.8, 0.9, 1.0][int(rng.integers(0, 4))],
            "colsample_bytree": [0.7, 0.8, 0.9, 1.0][int(rng.integers(0, 4))],
            "reg_alpha": [0, 0.01, 0.1, 1.0][int(rng.integers(0, 4))],
            "reg_lambda": [0, 0.01, 0.1, 1.0][int(rng.integers(0, 4))],
        }
    elif mt == "simple_nn":
        hp = {
            "hidden": [8, 16, 32, 64][int(rng.integers(0, 4))],
            "learning_rate": [0.005, 0.01, 0.02, 0.05][int(rng.integers(0, 4))],
            "alpha": [0.0001, 0.001, 0.01][int(rng.integers(0, 3))],
            "max_iter": [300, 500, 800][int(rng.integers(0, 3))],
        }
    # ---------- 回归模型 ----------
    elif mt == "dt_r":
        hp = {
            "max_depth": int(rng.integers(2, 17)),
            "min_samples_split": int(rng.integers(2, 21)),
        }
    elif mt in ("rf_r", "et_r"):
        hp = {
            "n_estimators": int(rng.integers(10, 101)),
            "max_depth": int(rng.integers(2, 17)),
        }
    elif mt == "lr_r":
        hp = {}
    elif mt == "xgb_r":
        hp = {
            "n_estimators": int(rng.integers(20, 201)),
            "max_depth": int(rng.integers(2, 11)),
            "learning_rate": [0.01, 0.05, 0.1, 0.2][int(rng.integers(0, 4))],
            "subsample": [0.7, 0.8, 0.9, 1.0][int(rng.integers(0, 4))],
            "colsample_bytree": [0.7, 0.8, 0.9, 1.0][int(rng.integers(0, 4))],
            "reg_alpha": [0, 0.01, 0.1, 1.0][int(rng.integers(0, 4))],
            "reg_lambda": [0, 0.01, 0.1, 1.0][int(rng.integers(0, 4))],
        }
    elif mt == "lgbm_r":
        hp = {
            "n_estimators": int(rng.integers(20, 201)),
            "max_depth": int(rng.integers(2, 11)),
            "learning_rate": [0.01, 0.05, 0.1, 0.2][int(rng.integers(0, 4))],
            "num_leaves": int(rng.integers(8, 65)),
            "subsample": [0.7, 0.8, 0.9, 1.0][int(rng.integers(0, 4))],
            "colsample_bytree": [0.7, 0.8, 0.9, 1.0][int(rng.integers(0, 4))],
            "reg_alpha": [0, 0.01, 0.1, 1.0][int(rng.integers(0, 4))],
            "reg_lambda": [0, 0.01, 0.1, 1.0][int(rng.integers(0, 4))],
        }
    elif mt == "simple_nn_r":
        hp = {
            "hidden": [8, 16, 32, 64][int(rng.integers(0, 4))],
            "learning_rate": [0.005, 0.01, 0.02, 0.05][int(rng.integers(0, 4))],
            "alpha": [0.0001, 0.001, 0.01][int(rng.integers(0, 3))],
            "max_iter": [300, 500, 800][int(rng.integers(0, 3))],
        }
    return {"cand_id": cand_id, "model_type": mt, "hyperparams": hp}


def make_estimator(model_type: str, hp: dict, rng: np.random.Generator):
    """根据模型类型实例化 sklearn estimator（分类/回归统一入口）。"""
    rs = int(rng.integers(0, 2**31 - 1))
    # ---------- 分类模型 ----------
    if model_type == "dt":
        return DecisionTreeClassifier(random_state=rs, **hp)
    if model_type == "rf":
        return RandomForestClassifier(random_state=rs, **hp)
    if model_type == "et":
        return ExtraTreesClassifier(random_state=rs, **hp)
    if model_type == "lr":
        return LogisticRegression(max_iter=1000, random_state=rs, **hp)
    if model_type == "nb":
        return GaussianNB()
    if model_type == "mlp":
        return MLPClassifier(
            hidden_layer_sizes=(hp["hidden"],),
            alpha=hp["alpha"],
            max_iter=500,
            random_state=rs,
        )
    if model_type == "xgb":
        if not HAS_XGB:
            raise ImportError("xgboost 未安装，无法使用 xgb 模型")
        return XGBClassifier(
            use_label_encoder=False,
            eval_metric="mlogloss",
            random_state=rs,
            verbosity=0,
            **hp,
        )
    if model_type == "lgbm":
        if not HAS_LGBM:
            raise ImportError("lightgbm 未安装，无法使用 lgbm 模型")
        return LGBMClassifier(
            random_state=rs,
            verbosity=-1,
            **hp,
        )
    if model_type == "simple_nn":
        return SimpleNNClassifier(seed=rs, **hp)
    # ---------- 回归模型 ----------
    if model_type == "dt_r":
        return DecisionTreeRegressor(random_state=rs, **hp)
    if model_type == "rf_r":
        return RandomForestRegressor(random_state=rs, **hp)
    if model_type == "et_r":
        return ExtraTreesRegressor(random_state=rs, **hp)
    if model_type == "lr_r":
        return LinearRegression(**hp)
    if model_type == "xgb_r":
        if not HAS_XGB:
            raise ImportError("xgboost 未安装，无法使用 xgb_r 模型")
        return XGBRegressor(random_state=rs, verbosity=0, **hp)
    if model_type == "lgbm_r":
        if not HAS_LGBM:
            raise ImportError("lightgbm 未安装，无法使用 lgbm_r 模型")
        return LGBMRegressor(
            random_state=rs,
            verbosity=-1,
            **hp,
        )
    if model_type == "simple_nn_r":
        return SimpleNNRegressor(seed=rs, **hp)
    raise ValueError(f"未知模型类型: {model_type}")


def sample_feature_subset(
    rng: np.random.Generator,
    n_total: int,
    auto: bool,
    top_pool: list[int] | None,
) -> list[int]:
    """FR-6.5：自动特征选择 = 从打分 top-N 池随机子集；否则全集。"""
    if not auto or not top_pool:
        return list(range(n_total))
    pool = list(top_pool)
    k = int(rng.integers(max(1, len(pool) // 2), len(pool) + 1))
    return sorted(rng.choice(pool, size=k, replace=False).tolist())


def cv_score(
    model_type: str,
    hp: dict,
    X: np.ndarray,
    y: np.ndarray,
    folds: list[list[list[int]]],
    feature_indices: list[int],
    cfg: TrainConfig,
    norm: str,
    rng: np.random.Generator,
    cancel_check=None,
) -> tuple[float, float, Scaler]:
    """每折：scaler 只用折内训练部分 fit（防泄漏）；返回 (mean, std, last_scaler)。

    last_scaler 仅为快照占位——best/ 的全量 scaler 在全训练集上重 fit（train_one）。
    """
    scores = []
    last_scaler = None
    for tr, va in folds:
        if cancel_check is not None and cancel_check():
            raise KeyboardInterrupt
        scaler = Scaler(norm)
        Xtr = scaler.fit(X[tr][:, feature_indices]).transform(X[tr][:, feature_indices])
        est = make_estimator(model_type, hp, rng)
        est.fit(Xtr, y[tr])
        y_pred = est.predict(scaler.transform(X[va][:, feature_indices]))
        scores.append(metric_value(cfg.metric, y[va], y_pred))
        last_scaler = scaler
    return float(np.mean(scores)), float(np.std(scores)), last_scaler


def train_one(
    model_type: str,
    hp: dict,
    X: np.ndarray,
    y: np.ndarray,
    feature_indices: list[int],
    norm: str,
    rng: np.random.Generator,
):
    """全训练集（80%）拟合，返回 (estimator, scaler)。"""
    scaler = Scaler(norm).fit(X[:, feature_indices])
    est = make_estimator(model_type, hp, rng)
    est.fit(scaler.transform(X[:, feature_indices]), y)
    return est, scaler
