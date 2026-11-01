"""评价指标（FR-6.4）：分类 / 回归指标统一管理。

分类：accuracy / macro-P / macro-R / macro-F1 / 混淆矩阵
回归：MSE / MAE / R2 / MAPE

统一 zero_division=0，保证类别缺失时指标有定义（FR-6.1 最少类样本约束的配套）。
"""
from __future__ import annotations

import numpy as np
from sklearn.metrics import (
    accuracy_score,
    confusion_matrix,
    f1_score,
    mean_absolute_error,
    mean_squared_error,
    precision_score,
    r2_score,
    recall_score,
)

# ---------- 指标名常量 ----------
CLASSIFICATION_METRICS = ("accuracy", "f1_macro", "precision_macro", "recall_macro")
REGRESSION_METRICS = ("mse", "mae", "r2", "mape")
METRICS = CLASSIFICATION_METRICS + REGRESSION_METRICS  # 向后兼容

# 回归指标中"越小越好"的集合（leaderboard 排序用）
LOWER_IS_BETTER = {"mse", "mae", "mape"}


def metric_value(name: str, y_true: np.ndarray, y_pred: np.ndarray) -> float:
    """计算单个指标值。分类/回归统一入口。"""
    # 分类指标
    if name == "accuracy":
        return float(accuracy_score(y_true, y_pred))
    if name == "f1_macro":
        return float(f1_score(y_true, y_pred, average="macro", zero_division=0))
    if name == "precision_macro":
        return float(precision_score(y_true, y_pred, average="macro", zero_division=0))
    if name == "recall_macro":
        return float(recall_score(y_true, y_pred, average="macro", zero_division=0))
    # 回归指标
    if name == "mse":
        return float(mean_squared_error(y_true, y_pred))
    if name == "mae":
        return float(mean_absolute_error(y_true, y_pred))
    if name == "r2":
        return float(r2_score(y_true, y_pred))
    if name == "mape":
        mask = y_true != 0
        if mask.sum() == 0:
            return 0.0
        return float(np.mean(np.abs((y_true[mask] - y_pred[mask]) / y_true[mask])) * 100)
    raise ValueError(f"未知指标: {name}")


def is_lower_better(metric_name: str) -> bool:
    """判断指标是否越小越好（回归指标排序用）。"""
    return metric_name in LOWER_IS_BETTER


def full_report(y_true: np.ndarray, y_pred: np.ndarray, n_classes: int,
                labels=None) -> dict:
    """分类任务完整报告：混淆矩阵 + 四项分类指标。

    labels: optional explicit class-id order (label_id may have gaps).
    Defaults to range(n_classes).
    """
    if labels is None:
        labels = list(range(max(int(n_classes), 1)))
    cm = confusion_matrix(y_true, y_pred, labels=list(labels))
    return {
        "accuracy": metric_value("accuracy", y_true, y_pred),
        "f1_macro": metric_value("f1_macro", y_true, y_pred),
        "precision_macro": metric_value("precision_macro", y_true, y_pred),
        "recall_macro": metric_value("recall_macro", y_true, y_pred),
        "confusion_matrix": cm.tolist(),
        "n_samples": int(len(y_true)),
        "class_labels": list(labels),
    }


def regression_full_report(y_true: np.ndarray, y_pred: np.ndarray) -> dict:
    """回归任务完整报告：MSE / MAE / R2 / MAPE + 残差统计。"""
    residuals = y_true - y_pred
    return {
        "mse": metric_value("mse", y_true, y_pred),
        "mae": metric_value("mae", y_true, y_pred),
        "r2": metric_value("r2", y_true, y_pred),
        "mape": metric_value("mape", y_true, y_pred),
        "residual_mean": float(np.mean(residuals)),
        "residual_std": float(np.std(residuals)),
        "n_samples": int(len(y_true)),
    }
