"""导出编排（FR-8）：best/ → bundle → 四项自检 → zip + export_report.json。

自检任一硬性步骤失败 → 422 拦截（err 码 SELFCHECK_*），报告随错误返回。
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path

import joblib
import numpy as np

from ..deps import AppError as _AppErrorCls, atomic_write_json, project_dir, read_json, utc_now
from ..schemas import ExportReport
from . import feature_service
from .project_service import ProjectService, require_stage
from .export import generator, reference, selfcheck

_svc = ProjectService()
_AppError = _AppErrorCls  # local alias for except clauses in nested scopes


def _git_describe() -> str:
    """BabyOS 仓库版本：`git describe --tags --always`，失败 → "unknown"。"""
    try:
        from .c_common import find_babyos_root
        root = find_babyos_root()
        r = subprocess.run(["git", "-C", str(root), "describe", "--tags", "--always"],
                           capture_output=True, text=True, timeout=5)
        return r.stdout.strip() or "unknown"
    except Exception:  # noqa: BLE001 - any failure → unknown (FR-10.4 契约)
        return "unknown"

SELFCHECK_STEPS = ("compile", "predict_consistency", "feature_consistency",
                    "static_scan", "symbol_map")


def _feature_meta(meta, payload: dict, matrix: dict, cfg) -> dict | None:
    """时序工程：组装 feat_extract 元数据（exported = [(ch, feat)] 导出序）。

    Feature names are `channel__feature`. Channel names may themselves contain
    `__`, so match by longest channel prefix instead of naive split("__", 1).
    """
    if meta.mode != "timeseries":
        return None
    names = feature_service.feature_names(meta, cfg)
    channels = list(meta.channels or [])
    exported = []
    for i in payload["feature_indices"]:
        name = names[i]
        ch = None
        for c in channels:
            if name == c or name.startswith(c + "__"):
                if ch is None or len(c) > len(ch):
                    ch = c
        if ch is None:
            raise _AppError(
                422, "FEATURE_NAME_MISMATCH",
                f"特征名 {name!r} 无法解析出通道，请重新计算特征矩阵",
            )
        f = name[len(ch) + 2:] if name.startswith(ch + "__") else ""
        exported.append((ch, f))
    return {
        "n": feature_service.effective_n(meta, cfg),
        "fs": meta.sampling_rate,
        "channels": channels,
        "n_channels": len(channels),
        "exported": exported,
        "freq_enabled": cfg.freq_enabled,
        "freq_bands": cfg.freq_bands,
    }


def _windows_for_chain(meta, payload: dict, matrix: dict, cfg) -> tuple[np.ndarray, np.ndarray]:
    """特征链自检数据：按 win_meta 取原始窗口 + 期望特征 X[:, feature_indices]。"""
    from .dataset_service import _files_json, load_file_data

    pdir = project_dir(meta.project_id)
    files = _files_json(pdir)
    win_file = matrix["win_file"]
    win_start = matrix["win_start"]
    n = feature_service.effective_n(meta, cfg)
    idx = payload["feature_indices"]
    X = matrix["X"][:, idx]
    ch_idx = {c: i for i, c in enumerate(meta.channels)}
    windows = np.empty((len(win_file), len(meta.channels) * n), dtype=np.float32)
    data_cache: dict[str, np.ndarray] = {}
    for r, (fid, s0) in enumerate(zip(win_file, win_start)):
        if fid not in data_cache:
            data_cache[fid] = load_file_data(pdir, fid)
        cols = files[fid]["cols"]
        for c, ch in enumerate(meta.channels):
            windows[r, c * n : (c + 1) * n] = data_cache[fid][s0 : s0 + n, cols.index(ch)]
    return windows, np.ascontiguousarray(X)


def export(pid: str) -> dict:
    meta = _svc.get(pid)
    require_stage(meta, "trained")
    bdir = project_dir(pid) / "training" / "best"
    if not (bdir / "model.joblib").exists():
        raise _AppError(422, "NO_BEST", "无最佳模型（训练失败或已取消）")

    payload = joblib.load(bdir / "model.joblib")
    matrix = feature_service.load_matrix(meta)
    cfg = feature_service.get_config(meta)
    split = feature_service.get_or_create_split(meta, seed=payload["seed"])
    date = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")

    # Windows 上 ctypes.CDLL 会 LoadLibrary("_selfcheck.so")，文件被锁，
    # rmtree 在 __exit__ 时无法删除并抛 NotADirectoryError/WinError 267。
    # ignore_cleanup_errors=True 让 tempdir 静默清理（孤儿目录由 OS 回收）。
    # Python 3.8 不支持 ignore_cleanup_errors，需要兼容处理
    if sys.version_info >= (3, 10):
        with tempfile.TemporaryDirectory(prefix="automl_export_",
                                         ignore_cleanup_errors=True) as td:
            workdir = Path(td) / "bundle"
            result = _export_inner(workdir, pid, meta, payload, matrix, cfg, split, date)
    else:
        td = tempfile.mkdtemp(prefix="automl_export_")
        try:
            workdir = Path(td) / "bundle"
            result = _export_inner(workdir, pid, meta, payload, matrix, cfg, split, date)
        finally:
            shutil.rmtree(td, ignore_errors=True)
    return result


def _export_inner(workdir: Path, pid: str, meta, payload, matrix, cfg, split, date: str) -> dict:
    """导出内部逻辑。"""
    info = generator.build_bundle(workdir, meta.name, payload,
                                  _feature_meta(meta, payload, matrix, cfg), date)
    symbol = info["name"]
    report: dict = {"ok": False, "errors": []}

    # ① 编译
    comp = selfcheck.step_compile(workdir)
    report["compile"] = {"ok": comp["ok"], "compiler": comp.get("compiler", "")}
    report["compiler"] = comp.get("compiler", "")
    if not comp["ok"]:
        report["errors"].append(f"编译失败: {comp.get('stderr', '')[:2000]}")
        return _finish(pid, meta, workdir, payload, report, info)

    # ② predict 一致性（train+test 全集，限导出特征子集——scaler 仅覆盖该子集）
    fidx = payload["feature_indices"]
    X_tr = matrix["X"][split["train_idx"]][:, fidx]
    X_te = matrix["X"][split["test_idx"]][:, fidx]
    pred = selfcheck.step_predict_consistency(workdir, symbol, payload, X_tr, X_te)
    report["predict_consistency"] = pred
    if not pred["ok"]:
        if pred.get("kind") == "regression_value":
            report["errors"].append(
                f"predict 一致性失败(回归): max_abs_diff={pred.get('max_abs_diff')}, "
                f"violations={pred.get('n_violations')}, {pred.get('error', '')}"
            )
        else:
            report["errors"].append(
                f"predict 一致性失败: id_match={pred.get('id_match_rate')}, {pred.get('error', '')}"
            )
        return _finish(pid, meta, workdir, payload, report, info)

    # ③ 特征链一致性（时序）
    if info["is_ts"]:
        windows, expected = _windows_for_chain(meta, payload, matrix, cfg)
        feat = selfcheck.step_feature_chain(workdir, symbol, windows, expected,
                                            windows.shape[1] // info["n_ch"], info["n_ch"])
        report["feature_consistency"] = feat
        if not feat["ok"]:
            report["errors"].append(
                f"特征链一致性失败: {feat.get('n_violations')} 处超阈, max={feat.get('max_violation')}"
            )
            return _finish(pid, meta, workdir, payload, report, info)
    else:
        report["feature_consistency"] = {"ok": True, "note": "表格工程无时序特征链"}

    # ④ 静态扫描
    scan = selfcheck.step_static_scan(workdir)
    report["static_scan"] = scan
    if not scan["ok"]:
        report["errors"].append(f"静态扫描命中禁词: {scan['hits']}")
        return _finish(pid, meta, workdir, payload, report, info)

    # ⑤ 符号映射校验（FR-10.4 / R-INT-4）：必含 algo_ml 符号映射 + nm 唯一性
    try:
        smap = selfcheck.step_symbol_map(workdir)
    except _AppError:
        raise
    except Exception as e:  # noqa: BLE001 - 防御非预期异常转 422
        raise _AppError(422, "ALGO_ML_NOT_FOUND", str(e))
    report["symbol_map"] = smap
    if not smap["ok"]:
        bits = []
        if smap["bad_refs"]:
            bits.append(f"bad_refs={smap['bad_refs'][:3]}")
        if smap["defined_leaks_nm"]:
            bits.append(f"defined_leaks_nm={smap['defined_leaks_nm'][:3]}")
        for ex in smap["examples"]:
            if not ex["ok"]:
                bits.append(f"example {ex['file']} compile fail: {ex['stderr'][:500]}")
        report["errors"].append(f"符号映射校验失败: {'; '.join(bits)}")
        return _finish(pid, meta, workdir, payload, report, info)

    report["ok"] = True
    return _finish(pid, meta, workdir, payload, report, info, cfg=cfg)


def _finish(pid, meta, workdir: Path, payload: dict, report: dict, info: dict, cfg=None) -> dict:
    """写 export_report.json + 打 zip 落盘 export/（自检失败时仅保留报告，不标 exported）。"""
    import sklearn

    report["sklearn_version"] = sklearn.__version__
    report["class_map"] = payload["labels"]
    report["code_size_bytes"] = info["code_size_bytes"]
    report["scaler_guarded"] = [
        i for i, g in enumerate(payload["scaler"].get("guarded", [])) if g
    ]
    report["data_hash"] = payload["data_hash"]
    report["exported_at"] = utc_now()

    # 评审 B5：资源预算量化（Flash / RAM 静态 / RAM 峰值 / 栈深）
    rb = info.get("resource_budget") or {}
    if rb:
        report["resource_budget"] = rb

    # v1.6: 透传符号映射字段（algo_ml_symbols / algo_ml_header / babyos_version）
    smap = report.get("symbol_map")
    if smap:
        report["algo_ml_symbols"] = smap.get("algo_ml_symbols", [])
        report["algo_ml_header_sha1"] = smap.get("algo_ml_header", {}).get("sha1")
        report["babyos_version"] = _git_describe()

    (workdir / "export_report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    exp_dir = project_dir(pid) / "export"
    shutil.rmtree(exp_dir, ignore_errors=True)
    exp_dir.mkdir(parents=True)
    zip_path = exp_dir / f"{info['name']}_bundle.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(workdir.rglob("*")):
            if p.is_file() and p.name != "_selfcheck.so" and "stub" not in p.parts:
                z.write(p, p.relative_to(workdir))
    atomic_write_json(exp_dir / "export_report.json", report)

    if report["ok"]:
        _svc.mark_exported(pid)
    elif report["errors"]:
        raise _AppError(422, "SELFCHECK_FAILED", "; ".join(report["errors"])[:4000])
    return report


def export_status(pid: str) -> dict:
    _svc.get(pid)
    exp = project_dir(pid) / "export"
    rep = read_json(exp / "export_report.json")
    zips = [p.name for p in exp.glob("*_bundle.zip")] if exp.is_dir() else []
    meta = _svc.get(pid)
    return {
        "exported": meta.stage == "exported",
        "export_stale": meta.export_stale,
        "report": rep,
        "zips": zips,
    }


def download_path(pid: str, filename: str) -> Path:
    _svc.get(pid)
    # BUG-1: Path traversal defense — reject directory separators and parent refs
    if ".." in filename or "/" in filename or "\\" in filename:
        raise _AppError(422, "INVALID_FILENAME", "文件名包含非法字符")
    p = (project_dir(pid) / "export" / filename).resolve()
    base = (project_dir(pid) / "export").resolve()
    if not str(p).startswith(str(base) + "/") and p != base:
        raise _AppError(422, "INVALID_PATH", "路径越界")
    if not p.exists() or not filename.endswith("_bundle.zip"):
        raise _AppError(404, "NO_EXPORT", "导出包不存在")
    return p
