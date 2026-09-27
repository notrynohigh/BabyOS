"""训练编排（FR-6.6/6.8/6.9）：发起/状态/leaderboard/设为最佳/训练报告。"""
from __future__ import annotations

import json
import shutil
import threading

import joblib
import numpy as np

from ..deps import AppError, atomic_write_json, project_dir, read_json, utc_now
from ..schemas import Candidate, TrainConfig
from . import feature_service, trainer
from .project_service import ProjectService, require_stage

_svc = ProjectService()


def start(pid: str, cfg: TrainConfig) -> dict:
    meta = _svc.get(pid)
    # 训练 worker 内部自动构建特征矩阵，故门槛为 labeled（FR-6.2 一键训练）
    require_stage(meta, "labeled")
    if trainer.registry.is_active(pid):
        raise AppError(409, "TRAINING_ACTIVE", "已有训练进行中")
    t = threading.Thread(target=trainer.run_training, args=(pid, cfg), daemon=True, name=f"train-{pid}")
    if not trainer.registry.register(pid, t):
        raise AppError(409, "TRAINING_ACTIVE", "已有训练进行中")
    atomic_write_json(project_dir(pid) / "training" / "config.json", cfg.model_dump())
    # Write initial running state BEFORE starting thread to avoid race condition
    # where client polls status and sees "idle" before the thread writes "running"
    trainer.write_state(pid, status="running", done=0, total=cfg.n_iter,
                        current="", current_cv_score=None, error="")
    t.start()
    return trainer.read_state(pid)


def status(pid: str) -> dict:
    _svc.get(pid)
    return trainer.read_state(pid)


def leaderboard(pid: str, limit: int = 20) -> dict:
    meta = _svc.get(pid)
    rows = []
    f = project_dir(pid) / "training" / "leaderboard.jsonl"
    if f.exists():
        for line in f.read_text(encoding="utf-8").splitlines():
            if line.strip():
                rows.append(json.loads(line))
    ok = [r for r in rows if r.get("status") == "ok"]
    cfg = read_json(project_dir(pid) / "training" / "config.json") or {}
    metric = cfg.get("metric", "f1_macro")
    task_type = cfg.get("task_type", "classification")

    # 排序方向：回归 mse/mae/mape 越小越好，其他越大越好
    from .metrics_service import is_lower_better
    if is_lower_better(metric):
        ok.sort(key=lambda r: r.get("cv_mean", 0))  # 升序
    else:
        ok.sort(key=lambda r: -r.get("cv_mean", 0))  # 降序

    best_id = _best_cand_id(pid)
    top = ok[:limit]
    return {
        "metric": metric,
        "task_type": task_type,
        "best_cand_id": best_id,
        "candidates": [
            Candidate(
                cand_id=r["cand_id"],
                model_type=r["model_type"],
                hyperparams=r["hyperparams"],
                feature_indices=r["feature_indices"],
                cv_mean=r["cv_mean"],
                cv_std=r["cv_std"],
                metric=r["metric"],
                is_best=r["cand_id"] == best_id,
            ).model_dump()
            for r in top
        ],
        "errors": [r for r in rows if r.get("status") == "error"][:limit],
        "n_total": len(rows),
    }


def _best_cand_id(pid: str) -> int | None:
    snap = read_json(project_dir(pid) / "training" / "best" / "snapshot.json")
    return snap.get("cand_id") if snap else None


def set_best(pid: str, cand_id: int) -> dict:
    """FR-6.9：把指定候选复制为 best/（重算 test 指标）。"""
    meta = _svc.get(pid)
    tdir = project_dir(pid) / "training"
    src = tdir / "candidates" / f"{cand_id}.joblib"
    if not src.exists():
        raise AppError(404, "NO_CANDIDATE", f"候选 {cand_id} 不存在（可能已被清理）")
    payload = joblib.load(src)
    bdir = tdir / "best"
    shutil.rmtree(bdir, ignore_errors=True)
    bdir.mkdir(parents=True)
    shutil.copy2(src, bdir / "model.joblib")

    from .scaler import Scaler
    from .metrics_service import full_report, regression_full_report

    m = feature_service.load_matrix(meta)
    split = feature_service.get_or_create_split(meta, seed=payload["seed"])
    test_idx = np.asarray(split["test_idx"])
    scaler = Scaler.from_snapshot(payload["scaler"])
    Xt = scaler.transform(m["X"][test_idx][:, payload["feature_indices"]])
    y_pred = payload["estimator"].predict(Xt)

    # 度量名来自训练配置（payload 不含 metric）
    metric = (read_json(tdir / "config.json") or {}).get("metric", "f1_macro")
    task_type = payload.get("task_type", "classification")

    if task_type == "regression":
        y_test = m["y_float"][test_idx] if "y_float" in m else m["y"][test_idx].astype(np.float64)
        report = regression_full_report(y_test, y_pred.astype(np.float64))
    else:
        report = full_report(m["y"][test_idx], y_pred, len(meta.labels))
    report["main_metric"] = metric
    report["main_value"] = report[metric]
    report["cand_id"] = cand_id
    report["task_type"] = task_type
    atomic_write_json(bdir / "metrics_test.json", report)

    row = _find_lb_row(tdir, cand_id)
    snapshot = {
        "model_type": payload["model_type"],
        "hyperparams": payload["hyperparams"],
        "feature_indices": payload["feature_indices"],
        "norm": payload["norm"],
        "scaler": payload["scaler"],
        "seed": payload["seed"],
        "data_hash": payload["data_hash"],
        "feature_names": payload["feature_names"],
        "label_map": payload["labels"],
        "cand_id": cand_id,
        "metric": metric,
        "task_type": task_type,
        "cv_mean": row.get("cv_mean") if row else None,
    }
    atomic_write_json(bdir / "snapshot.json", snapshot)
    _svc.advance(pid, "trained")
    return snapshot


def _find_lb_row(tdir, cand_id: int) -> dict | None:
    f = tdir / "leaderboard.jsonl"
    if not f.exists():
        return None
    for line in f.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        r = json.loads(line)
        if r.get("cand_id") == cand_id and r.get("status") == "ok":
            return r
    return None


def best_info(pid: str) -> dict:
    """最佳模型卡片数据（FR-7.1/7.2）。"""
    meta = _svc.get(pid)
    bdir = project_dir(pid) / "training" / "best"
    snap = read_json(bdir / "snapshot.json")
    if snap is None:
        raise AppError(404, "NO_BEST", "尚无最佳模型，请先完成训练")
    metrics = read_json(bdir / "metrics_test.json", default={})
    snap["metrics_test"] = metrics
    return snap


def feature_importance(pid: str) -> list[dict]:
    """FR-7.3：树=impurity 重要性；线性=|coef|；其他=置换重要性（≤1000 样本，固定种子）。"""
    meta = _svc.get(pid)
    bdir = project_dir(pid) / "training" / "best"
    payload_file = bdir / "model.joblib"
    if not payload_file.exists():
        raise AppError(404, "NO_BEST", "尚无最佳模型")
    payload = joblib.load(payload_file)
    names = payload["feature_names"]
    est = payload["estimator"]
    task_type = payload.get("task_type", "classification")

    if hasattr(est, "feature_importances_"):
        vals = np.asarray(est.feature_importances_, dtype=float)
    elif hasattr(est, "coef_"):
        vals = np.abs(np.asarray(est.coef_, dtype=float))
        if vals.ndim > 1:
            vals = vals.max(axis=0)
    else:
        from sklearn.inspection import permutation_importance

        m = feature_service.load_matrix(meta)
        split = feature_service.get_or_create_split(meta, seed=payload["seed"])
        scaler = Scaler_from(payload)
        n = min(1000, len(split["test_idx"]))
        rng = np.random.default_rng(payload["seed"])
        pick = rng.choice(split["test_idx"], size=n, replace=False)
        Xp = scaler.transform(m["X"][pick][:, payload["feature_indices"]])
        # 回归用 neg_mean_squared_error，分类用 f1_macro
        scoring = "neg_mean_squared_error" if task_type == "regression" else "f1_macro"
        y_eval = m["y_float"][pick] if task_type == "regression" and "y_float" in m else m["y"][pick]
        r = permutation_importance(est, Xp, y_eval, n_repeats=5,
                                   random_state=payload["seed"], scoring=scoring)
        vals = r.importances_mean
    order = np.argsort(-vals)
    return [{"feature": names[i], "importance": float(vals[i])} for i in order]


def Scaler_from(payload: dict):
    from .scaler import Scaler

    return Scaler.from_snapshot(payload["scaler"])


# ---------- 训练报告 ----------

def training_report(pid: str) -> dict:
    """生成完整训练报告：配置 + 候选排行 + 最佳模型 + 测试指标 + 数据摘要。"""
    meta = _svc.get(pid)
    tdir = project_dir(pid) / "training"
    pdir = project_dir(pid)

    # 1. 训练配置
    config = read_json(tdir / "config.json") or {}

    # 2. leaderboard
    rows = []
    lb_path = tdir / "leaderboard.jsonl"
    if lb_path.exists():
        for line in lb_path.read_text(encoding="utf-8").splitlines():
            if line.strip():
                rows.append(json.loads(line))
    ok_rows = [r for r in rows if r.get("status") == "ok"]
    from .metrics_service import is_lower_better
    metric = config.get("metric", "f1_macro")
    if is_lower_better(metric):
        ok_rows.sort(key=lambda r: r.get("cv_mean", 0))
    else:
        ok_rows.sort(key=lambda r: -r.get("cv_mean", 0))

    # 3. 最佳模型
    bdir = tdir / "best"
    snapshot = read_json(bdir / "snapshot.json", default={})
    metrics_test = read_json(bdir / "metrics_test.json", default={})

    # 4. 数据摘要
    matrix_meta = read_json(pdir / "features" / "matrix.meta.json", default={})

    report = {
        "generated_at": utc_now(),
        "task_type": config.get("task_type", "classification"),
        "summary": {
            "total_candidates": len(ok_rows),
            "failed_candidates": len([r for r in rows if r.get("status") == "error"]),
            "best_cand_id": snapshot.get("cand_id"),
            "best_model_type": snapshot.get("model_type"),
            "best_cv_mean": snapshot.get("cv_mean"),
            "budget_hit": snapshot.get("budget_hit", False),
            "main_metric": metric,
            "main_value": metrics_test.get("main_value"),
            "duration_s": ok_rows[-1].get("elapsed_s", 0) if ok_rows else 0,
        },
        "all_candidates": [
            {
                "rank": i + 1,
                "cand_id": r["cand_id"],
                "model_type": r["model_type"],
                "hyperparams": r.get("hyperparams", {}),
                "cv_mean": r.get("cv_mean", 0),
                "cv_std": r.get("cv_std", 0),
                "metric": r.get("metric", ""),
                "elapsed_s": r.get("elapsed_s", 0),
                "is_best": r["cand_id"] == snapshot.get("cand_id"),
                "n_features": len(r.get("feature_indices", [])),
            }
            for i, r in enumerate(ok_rows)
        ],
        "best_model": {
            "cand_id": snapshot.get("cand_id"),
            "model_type": snapshot.get("model_type"),
            "hyperparams": snapshot.get("hyperparams", {}),
            "feature_names": snapshot.get("feature_names", []),
            "n_features": len(snapshot.get("feature_indices", [])),
            "norm": snapshot.get("norm", "zscore"),
            "seed": snapshot.get("seed"),
            "metrics_test": metrics_test,
        },
        "data_summary": {
            "n_samples": matrix_meta.get("n_samples", 0),
            "n_features": matrix_meta.get("n_features", 0),
            "channels": meta.channels,
            "mode": meta.mode,
            "n_labels": len(meta.labels),
            "labels": [{"name": l.name, "id": l.label_id} for l in meta.labels],
        },
        "config_summary": {
            "k": config.get("k", 5),
            "n_iter": config.get("n_iter", 30),
            "budget_s": config.get("budget_s", 600),
            "metric": metric,
            "task_type": config.get("task_type", "classification"),
            "auto_feature_select": config.get("auto_feature_select", True),
            "scoring": config.get("scoring", "f_test"),
            "seed": config.get("seed", 42),
        },
    }

    atomic_write_json(tdir / "training_report.json", report)
    return report


def get_report(pid: str) -> dict:
    """获取训练报告（优先读缓存，否则实时生成）。"""
    tdir = project_dir(pid) / "training"
    report_path = tdir / "training_report.json"
    if report_path.exists():
        cached = read_json(report_path)
        if cached:
            return cached
    return training_report(pid)
