"""训练运行注册表 + AutoML worker（design.md §3/§6.4）。

取消协议：cancel.flag 文件存在即取消（API 创建，worker 边界检查自删）；
run_state.json 为 worker 独占写。
"""
import json
import shutil
import threading
import time
import traceback
from pathlib import Path

import joblib
import numpy as np

from ..deps import atomic_write_json, project_dir, read_json
from ..schemas import TrainConfig


class RunRegistry:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._active: dict[str, threading.Thread] = {}

    def is_active(self, pid: str) -> bool:
        with self._lock:
            return pid in self._active

    def register(self, pid: str, t: threading.Thread) -> bool:
        with self._lock:
            if pid in self._active:
                return False
            self._active[pid] = t
            return True

    def unregister(self, pid: str) -> None:
        with self._lock:
            self._active.pop(pid, None)


registry = RunRegistry()


# ---------- 状态读写（worker 独占写） ----------

STATE_IDLE = {"status": "idle", "done": 0, "total": 0, "current": "", "current_cv_score": None, "error": ""}


def state_path(pid: str) -> Path:
    return project_dir(pid) / "training" / "run_state.json"


def read_state(pid: str) -> dict:
    from ..deps import read_json

    return read_json(state_path(pid), default=dict(STATE_IDLE))


def write_state(pid: str, **kw) -> None:
    st = read_state(pid)
    st.update(kw)
    atomic_write_json(state_path(pid), st)


def request_cancel(pid: str) -> bool:
    """创建 cancel.flag。无活跃训练时返回 False（API 层转 404，P3-8）。"""
    if not registry.is_active(pid):
        return False
    flag = project_dir(pid) / "training" / "cancel.flag"
    flag.parent.mkdir(parents=True, exist_ok=True)
    flag.touch()
    return True


def clear_cancel(pid: str) -> None:
    flag = project_dir(pid) / "training" / "cancel.flag"
    if flag.exists():
        flag.unlink()


def cancel_requested(pid: str) -> bool:
    return (project_dir(pid) / "training" / "cancel.flag").exists()


# ---------- AutoML worker（FR-6） ----------

def run_training(pid: str, cfg: TrainConfig) -> None:
    """训练线程主体。异常不得逃逸（线程内无调用方），全部落 run_state/leaderboard。"""
    from . import automl, feature_service, metrics_service
    from .project_service import ProjectService

    svc = ProjectService()
    pdir = project_dir(pid)
    tdir = pdir / "training"
    t0 = time.time()
    task_type = cfg.task_type
    try:
        clear_cancel(pid)
        meta = svc.get(pid)
        feat_cfg = feature_service.get_config(meta)
        matrix_info = feature_service.compute(meta, feat_cfg)  # 复用或构建
        m = feature_service.load_matrix(meta)
        # Note: get_or_create_split handles < 3 groups with fallback strategies
        # (random split for 1 group, stratified split for 2 groups), so strict
        # GROUPS_TOO_FEW check is removed to support single-file workflows.
        split = feature_service.get_or_create_split(meta, seed=cfg.seed)
        _validate(meta, m, split, task_type)

        X = m["X"]
        # 回归任务使用浮点目标值
        if task_type == "regression" and "y_float" in m:
            y = m["y_float"]
        else:
            y = m["y"]
        train_idx = np.asarray(split["train_idx"])
        test_idx = np.asarray(split["test_idx"])
        folds = split["folds"]
        data_hash = f"{matrix_info['key']}:{cfg.seed}"

        # 打分池（FR-6.5）：仅在训练部分上拟合（FR-5.4）
        pool = None
        if cfg.auto_feature_select:
            ranking = feature_service.scoring(meta, cfg.scoring, task_type=task_type)
            pool = [r["index"] for r in ranking[: cfg.top_n]]

        rng = np.random.default_rng(cfg.seed)
        lb_path = tdir / "leaderboard.jsonl"
        lb_path.write_text("", encoding="utf-8")
        (tdir / "candidates").mkdir(parents=True, exist_ok=True)
        shutil.rmtree(tdir / "candidates")
        (tdir / "candidates").mkdir()

        write_state(pid, status="running", done=0, total=cfg.n_iter, current="", current_cv_score=None, error="")
        best: dict | None = None
        budget_hit = False
        for cand_id in range(cfg.n_iter):
            if cancel_requested(pid):
                _cancelled(pid, tdir)
                return
            if time.time() - t0 > cfg.budget_s:
                budget_hit = True
                break
            cand = automl.sample_candidate(rng, cand_id, task_type=task_type)
            feat_idx = automl.sample_feature_subset(rng, X.shape[1], cfg.auto_feature_select, pool)
            cand_rng = np.random.default_rng([cfg.seed, cand_id])
            write_state(pid, current=f'{cand["model_type"]}#{cand_id}', done=cand_id)
            row: dict = {
                "cand_id": cand_id,
                "model_type": cand["model_type"],
                "hyperparams": cand["hyperparams"],
                "feature_indices": feat_idx,
                "metric": cfg.metric,
            }
            try:
                cv_mean, cv_std, _ = automl.cv_score(
                    cand["model_type"], cand["hyperparams"], X, y,
                    folds, feat_idx, cfg, feat_cfg.norm, cand_rng,
                    cancel_check=lambda: cancel_requested(pid),
                )
                est, scaler = automl.train_one(
                    cand["model_type"], cand["hyperparams"], X[train_idx], y[train_idx],
                    feat_idx, feat_cfg.norm, cand_rng,
                )
                payload = {
                    "estimator": est,
                    "model_type": cand["model_type"],
                    "hyperparams": cand["hyperparams"],
                    "feature_indices": feat_idx,
                    "norm": feat_cfg.norm,
                    "scaler": scaler.snapshot(),
                    "labels": [l.model_dump() for l in meta.labels],
                    "seed": cfg.seed,
                    "data_hash": data_hash,
                    "feature_names": [matrix_info["feature_names"][i] for i in feat_idx],
                    "task_type": task_type,
                }
                joblib.dump(payload, tdir / "candidates" / f"{cand_id}.joblib")
                row.update(status="ok", cv_mean=cv_mean, cv_std=cv_std, elapsed_s=round(time.time() - t0, 3))
                if _is_better(cv_mean, best["cv_mean"] if best else None, cfg.metric):
                    best = {"cand_id": cand_id, "cv_mean": cv_mean, "payload": payload, "row": row}
            except KeyboardInterrupt:
                _cancelled(pid, tdir)
                return
            except Exception as e:  # 单候选失败不终止整轮（FR-6.9）
                row.update(status="error", error=f"{type(e).__name__}: {e}",
                           cv_mean=0.0, cv_std=0.0, elapsed_s=round(time.time() - t0, 3))
            write_state(pid, done=cand_id + 1, current_cv_score=row.get("cv_mean"))
            with lb_path.open("a", encoding="utf-8") as f:
                f.write(json.dumps(row, ensure_ascii=False) + "\n")

        if best is None:
            write_state(pid, status="failed", error="所有候选均失败，详见 leaderboard")
            return
        _write_best(pid, tdir, best, X, y, test_idx, len(meta.labels), budget_hit, task_type)
        svc.advance(pid, "trained")
        write_state(pid, status="done", current="", error="")
    except Exception as e:
        write_state(pid, status="failed", error=f"{type(e).__name__}: {e}\n{traceback.format_exc()}")
    finally:
        clear_cancel(pid)
        registry.unregister(pid)


def _is_better(new_score: float, old_score, metric: str) -> bool:
    """判断 new_score 是否优于 old_score。回归 mse/mae/mape 越小越好，其他越大越好。"""
    if old_score is None:
        return True
    from .metrics_service import is_lower_better
    if is_lower_better(metric):
        return new_score < old_score
    return new_score > old_score


def _validate(meta, m: dict, split: dict, task_type: str = "classification") -> None:
    from ..deps import AppError

    y = m["y"]
    groups = m["groups"]

    if task_type == "regression":
        # 回归任务：检查目标值方差 > 0 且样本量足够
        if len(y) < 10:
            raise AppError(422, "INSUFFICIENT_DATA",
                           f"回归任务需要至少 10 个样本，当前仅有 {len(y)} 个")
        y_float = y.astype(np.float64)
        if np.var(y_float) == 0:
            raise AppError(422, "CONSTANT_TARGET",
                           "回归目标值方差为 0（所有值相同），无法训练")
        return

    # 分类任务
    if meta.mode == "timeseries":
        # GROUPS_TOO_FEW check removed: get_or_create_split handles < 3 groups
        # with fallback strategies (random split for 1 group, stratified split
        # for 2 groups), supporting single-file workflows.
        counts = np.bincount(y, minlength=len(meta.labels))
        for lab, c in zip(meta.labels, counts):
            if c < 5:
                raise AppError(422, "CLASS_TOO_FEW", f"标签「{lab.name}」窗口数不足（{c}<5），请补充标注")
    else:
        counts = np.bincount(y[split["train_idx"]], minlength=len(meta.labels))
        if counts.min() < 5:
            # 评审 A3：每类逐项提示 + 给"多少类样本算够"的可执行指引
            short = [
                f"「{lab.name}」{int(c)}/{5}"
                for lab, c in zip(meta.labels, counts) if c < 5
            ]
            raise AppError(
                422, "CLASS_TOO_FEW",
                "训练集每类样本不足（最少 5/类）：" + "、".join(short) + "。"
                "建议上传 ≥3 个文件 × 3 类 × 30+ 行，或在标注页补充片段",
            )


def _cancelled(pid: str, tdir: Path) -> None:
    shutil.rmtree(tdir / "candidates", ignore_errors=True)
    write_state(pid, status="cancelled", current="", error="已取消（不产生结果，FR-6.6）")
    clear_cancel(pid)


def _write_best(pid: str, tdir: Path, best: dict, X, y, test_idx, n_classes: int,
                budget_hit: bool, task_type: str = "classification") -> None:
    from . import metrics_service

    bdir = tdir / "best"
    shutil.rmtree(bdir, ignore_errors=True)
    bdir.mkdir(parents=True)
    payload = best["payload"]
    joblib.dump(payload, bdir / "model.joblib")
    snapshot = {k: payload[k] for k in (
        "model_type", "hyperparams", "feature_indices", "norm", "scaler",
        "seed", "data_hash", "feature_names",
    )}
    snapshot["label_map"] = payload["labels"]
    snapshot["cand_id"] = best["cand_id"]
    snapshot["cv_mean"] = best["cv_mean"]
    snapshot["budget_hit"] = budget_hit
    snapshot["task_type"] = task_type
    atomic_write_json(bdir / "snapshot.json", snapshot)

    # 独立测试集指标（FR-6.4：结果页所有指标在 test set 上计算）
    from .scaler import Scaler

    scaler = Scaler.from_snapshot(payload["scaler"])
    Xt = scaler.transform(X[test_idx][:, payload["feature_indices"]])
    y_pred = payload["estimator"].predict(Xt)

    if task_type == "regression":
        # 回归：使用浮点目标值
        y_test = y[test_idx].astype(np.float64) if y.dtype != np.float64 else y[test_idx]
        report = metrics_service.regression_full_report(y_test, y_pred.astype(np.float64))
    else:
        report = metrics_service.full_report(y[test_idx], y_pred, n_classes)
    report["main_metric"] = best["row"]["metric"]
    report["main_value"] = report[best["row"]["metric"]]
    report["cand_id"] = best["cand_id"]
    report["task_type"] = task_type
    atomic_write_json(bdir / "metrics_test.json", report)
