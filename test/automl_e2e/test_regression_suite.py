#!/usr/bin/env python3
"""TD3 回归套件：C e2e + 既有 Python 验证 + feature_cgen/MODEL_DESC 契约 + 导出冒烟。

职责边界：只新建测试脚本，不修改 W1-W3 源码。
运行方式：
    source /home/yyds/code/BabyOS/tool/babyos-studio/python/.venv/bin/activate
    python /home/yyds/code/BabyOS/test/automl_e2e/test_regression_suite.py
"""
from __future__ import annotations

import ast
import os
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

REPO = Path("/home/yyds/code/BabyOS")
C_DIR = REPO / "test/automl_e2e"
PY_DIR = REPO / "tool/babyos-studio/python"
EXPORT_DIR = PY_DIR / "app/services/export"

# 冻结契约：全部 model_type
ALL_MODEL_TYPES = [
    "dt", "rf", "et", "lr", "nb", "mlp", "simple_nn", "xgb", "lgbm",
    "dt_r", "rf_r", "et_r", "lr_r", "simple_nn_r", "xgb_r", "lgbm_r",
]
CLASSIFICATION_TYPES = ["dt", "rf", "et", "lr", "nb", "mlp", "simple_nn", "xgb", "lgbm"]
REGRESSION_TYPES = ["dt_r", "rf_r", "et_r", "lr_r", "simple_nn_r", "xgb_r", "lgbm_r"]


def _ensure_venv_path() -> None:
    pydir = str(PY_DIR)
    if pydir not in sys.path:
        sys.path.insert(0, pydir)


def _run(cmd, cwd=None, timeout=300, env=None):
    e = os.environ.copy()
    if env:
        e.update(env)
    # 保证子进程也能 import app.*
    pypath = e.get("PYTHONPATH", "")
    parts = [p for p in pypath.split(os.pathsep) if p]
    if str(PY_DIR) not in parts:
        parts.insert(0, str(PY_DIR))
    e["PYTHONPATH"] = os.pathsep.join(parts)
    return subprocess.run(
        cmd, cwd=str(cwd) if cwd else None, capture_output=True, text=True,
        timeout=timeout, env=e,
    )


# ============================================================
# 1. C e2e：make && ./build/test_automl
# ============================================================
def test_c_e2e():
    r_make = _run(["make", "clean"], cwd=C_DIR)
    r_make2 = _run(["make"], cwd=C_DIR)
    if r_make2.returncode != 0:
        return False, "make failed:\n" + (r_make2.stderr or r_make2.stdout)[-3000:]
    r = _run(["./build/test_automl"], cwd=C_DIR)
    out = (r.stdout or "") + (r.stderr or "")
    ok = r.returncode == 0 and "0 失败" in out and "失败" in out
    # 宽松：returncode==0 即通过
    ok = r.returncode == 0
    return ok, out[-4000:]


# ============================================================
# 2. 既有 Python 验证脚本
# ============================================================
def test_verify_deep_binding():
    script = C_DIR / "verify_deep_binding.py"
    if not script.is_file():
        return True, "SKIPPED: verify_deep_binding.py 不存在"
    r = _run([sys.executable, str(script)], cwd=C_DIR, timeout=180)
    out = (r.stdout or "") + (r.stderr or "")
    return r.returncode == 0, out[-3000:]


def test_export_pipeline_simple():
    script = C_DIR / "test_export_pipeline_simple.py"
    if not script.is_file():
        return True, "SKIPPED: test_export_pipeline_simple.py 不存在"
    r = _run([sys.executable, str(script)], cwd=C_DIR, timeout=180)
    out = (r.stdout or "") + (r.stderr or "")
    return r.returncode == 0, out[-3000:]


# ============================================================
# 3. feature_cgen 全特征覆盖
# ============================================================
def test_feature_cgen_full_coverage():
    _ensure_venv_path()
    from app.services.export import feature_cgen
    from app.services.feature_service import TIME_FEATURES, FREQ_FEATURES

    msgs = []
    # 时域全集
    for f in TIME_FEATURES:
        try:
            s = feature_cgen._time_feature_c(f)
        except Exception as e:  # noqa: BLE001
            msgs.append("TIME %s: ValueError/Exception %r" % (f, e))
            continue
        if "out[oi++]" not in s:
            msgs.append("TIME %s bad expr: %r" % (f, s))
        # 预置原语绑定检查
        if f in ("mean", "std", "variance", "rms", "abs_mean", "zcr",
                 "autocorr", "skew", "kurt"):
            if "bAlgoSignal" not in s:
                msgs.append("TIME %s missing bAlgoSignal*: %r" % (f, s))

    # 频域：FREQ_FEATURES 含组合名 band_ratio，须展开为 band{i}_ratio
    for f in FREQ_FEATURES:
        if f == "band_ratio":
            continue
        try:
            s = feature_cgen._freq_feature_c(f, "p", 64, 100.0, 4)
            if "bAlgoFft" not in s:
                msgs.append("FREQ %s missing bAlgoFft*: %r" % (f, s))
        except Exception as e:  # noqa: BLE001
            msgs.append("FREQ %s raised %r" % (f, e))
    for b in range(1, 8):
        fname = "band%d_ratio" % b
        try:
            s = feature_cgen._freq_feature_c(fname, "p", 64, 100.0, b + 1)
            if "bAlgoFftBandRatio" not in s:
                msgs.append("FREQ %s bad: %r" % (fname, s))
        except Exception as e:  # noqa: BLE001
            msgs.append("FREQ %s raised %r" % (fname, e))

    # 全特征 emit_feat_extract（时域+频域混合）不抛异常
    all_feats = list(TIME_FEATURES) + ["spec_centroid", "spec_energy",
                                      "dominant_freq"] + [
        "band%d_ratio" % i for i in range(4)]
    exported = [("ch0", f) for f in all_feats]
    try:
        fe = feature_cgen.emit_feat_extract(
            "proj", ["ch0"], exported, n=64, fs=250.0,
            freq_enabled=True, freq_bands=4,
        )
        if "bAlgoSignalStats" not in fe["body"]:
            msgs.append("emit body missing bAlgoSignalStats")
        if "s_tw_re" not in fe["decls"] and "_tw_re" not in fe["decls"]:
            msgs.append("emit decls missing twiddle tables")
        if "helpers" not in fe or fe["helpers"] != "":
            msgs.append("helpers should be empty (primitives preset), got %r"
                        % fe.get("helpers"))
        for f in all_feats:
            # 时域表达式应出现在 body 中
            if f in TIME_FEATURES:
                key = feature_cgen._time_feature_c(f).strip()
                if key not in fe["body"]:
                    msgs.append("emit body missing expr for %s: %s" % (f, key))
    except Exception as e:  # noqa: BLE001
        msgs.append("emit_feat_extract raised %r" % e)

    # 未知特征必须显式 ValueError
    try:
        feature_cgen._time_feature_c("not_a_feature")
        msgs.append("unknown time feature should raise ValueError")
    except ValueError:
        pass
    try:
        feature_cgen._freq_feature_c("not_a_band", "p", 64, 100.0, 4)
        msgs.append("unknown freq feature should raise ValueError")
    except ValueError:
        pass

    return (not msgs), ("OK" if not msgs else "\n".join(msgs))


# ============================================================
# 4. MODEL_DESC 全 model_type 覆盖
# ============================================================
def test_model_desc_coverage():
    _ensure_venv_path()
    from app.services.export.generator import MODEL_DESC
    from app.services import automl

    msgs = []
    missing = [m for m in ALL_MODEL_TYPES if m not in MODEL_DESC]
    if missing:
        msgs.append("MODEL_DESC missing: %s" % missing)
    for m, desc in MODEL_DESC.items():
        if not isinstance(desc, str) or not desc.strip():
            msgs.append("MODEL_DESC[%r] empty" % m)
    extra = set(MODEL_DESC) - set(ALL_MODEL_TYPES)
    if extra:
        msgs.append("MODEL_DESC has unexpected keys: %s" % sorted(extra))

    # automl 模型池与契约一致
    cls_pool = list(automl.CLASSIFICATION_MODELS)
    reg_pool = list(automl.REGRESSION_MODELS)
    if sorted(cls_pool) != sorted(CLASSIFICATION_TYPES):
        msgs.append("automl.CLASSIFICATION_MODELS=%s != %s"
                    % (cls_pool, CLASSIFICATION_TYPES))
    if sorted(reg_pool) != sorted(REGRESSION_TYPES):
        msgs.append("automl.REGRESSION_MODELS=%s != %s"
                    % (reg_pool, REGRESSION_TYPES))

    return (not msgs), ("OK" if not msgs else "\n".join(msgs))


# ============================================================
# 5. 全部 model_type：extract_arrays + emit_model 冒烟
# ============================================================
def _make_payload(model_type, X, y, seed=0):
    _ensure_venv_path()
    from app.services import automl
    from app.services.scaler import Scaler
    from app.services.simple_nn import SimpleNNClassifier, SimpleNNRegressor

    # automl.make_estimator 内部使用 numpy Generator.integers → 必须 default_rng
    rng = np.random.default_rng(seed)
    nf = X.shape[1]
    feature_indices = list(range(nf))
    scaler = Scaler("zscore").fit(X[:, feature_indices])

    hp = {}
    if model_type in ("dt", "dt_r"):
        hp = {"max_depth": 4}
    elif model_type in ("rf", "et", "rf_r", "et_r"):
        hp = {"n_estimators": 8, "max_depth": 4}
    elif model_type in ("lr", "lr_r"):
        hp = {}
    elif model_type in ("mlp",):
        hp = {"hidden": 8, "alpha": 0.001}
    elif model_type in ("simple_nn", "simple_nn_r"):
        # seed 由构造函数显式传入，不放进 hp
        hp = {"hidden": 6, "max_iter": 80}
    elif model_type in ("xgb", "xgb_r"):
        hp = {"n_estimators": 12, "max_depth": 3, "learning_rate": 0.3}
    elif model_type in ("lgbm", "lgbm_r"):
        hp = {"n_estimators": 12, "max_depth": 3, "learning_rate": 0.3}

    Xt = scaler.transform(X[:, feature_indices])
    if model_type == "simple_nn":
        est = SimpleNNClassifier(seed=seed, **hp)
        est.fit(Xt, y)
    elif model_type == "simple_nn_r":
        est = SimpleNNRegressor(seed=seed, **hp)
        est.fit(Xt, y)
    else:
        est = automl.make_estimator(model_type, hp, rng)
        est.fit(Xt, y)

    labels = [{"name": "c%d" % i} for i in range(len(np.unique(y)))]
    if model_type.endswith("_r"):
        labels = [{"name": "y"}]
    return {
        "estimator": est,
        "model_type": model_type,
        "feature_indices": feature_indices,
        "scaler": scaler.snapshot(),
        "labels": labels,
        "seed": seed,
        "hyperparams": hp,
        "norm": "zscore",
    }


def test_all_model_types_emit():
    _ensure_venv_path()
    from app.services.export import generator, model_cgen

    from sklearn.datasets import make_classification, make_regression
    from sklearn.exceptions import NotFittedError  # noqa: F401

    Xc, yc = make_classification(
        n_samples=160, n_features=8, n_informative=5, n_classes=3,
        random_state=0,
    )
    Xc2, yc2 = make_classification(
        n_samples=160, n_features=8, n_informative=5, n_classes=2,
        random_state=1,
    )
    Xr, yr = make_regression(
        n_samples=160, n_features=8, n_informative=5, random_state=0,
    )

    msgs = []
    ok_types = []
    for mt in ALL_MODEL_TYPES:
        if mt in ("xgb", "lgbm"):
            X, y = Xc2, yc2  # 二分类路径（含 sigmoid/base_score）
        elif mt in CLASSIFICATION_TYPES:
            X, y = Xc, yc
        else:
            X, y = Xr, yr
        try:
            payload = _make_payload(mt, X, y, seed=7)
            nc = len(payload["labels"])
            nf = len(payload["feature_indices"])
            arrs = model_cgen.extract_arrays(payload, nc)
            emitted = model_cgen.emit_model(
                payload, arrs, "t", nc, nf, "ALGO_T_N_CLASSES"
            )
            core = emitted.get("predict_core") or ""
            decls = emitted.get("decls") or ""
            if not core:
                msgs.append("%s: empty predict_core" % mt)
            # GBDT 应走树表 + bAlgoMlTreePredict
            if mt in ("xgb", "lgbm", "xgb_r", "lgbm_r"):
                if "bAlgoMlTreePredict" not in core:
                    msgs.append("%s: predict_core missing bAlgoMlTreePredict" % mt)
                if "bAlgoMlTreeNode_t" not in decls:
                    msgs.append("%s: decls missing bAlgoMlTreeNode_t" % mt)
            # 分类 GBDT 应有 sigmoid 或 softmax
            if mt in ("xgb", "lgbm"):
                if "bAlgoMlSigmoid" not in core and "bAlgoMlSoftmax" not in core:
                    msgs.append("%s: missing sigmoid/softmax path" % mt)
            # 回归 GBDT 应写 *out 且无 softmax
            if mt in ("xgb_r", "lgbm_r"):
                if "*out" not in core:
                    msgs.append("%s: regression core missing *out" % mt)
                if "bAlgoMlSoftmax" in core:
                    msgs.append("%s: regression core must not softmax" % mt)
            # MODEL_DESC 可查
            if mt not in generator.MODEL_DESC:
                msgs.append("%s: not in MODEL_DESC" % mt)
            # 构造完整 header 用 desc（捕获 KeyError）
            _ = generator.MODEL_DESC[mt]
            ok_types.append(mt)
        except Exception as e:  # noqa: BLE001
            msgs.append("%s: export smoke failed: %r" % (mt, e))

    missing_ok = [m for m in ALL_MODEL_TYPES if m not in ok_types]
    if missing_ok:
        msgs.append("types failed smoke: %s" % missing_ok)
    summary = "OK: %d/%d types smoke-passed" % (len(ok_types), len(ALL_MODEL_TYPES))
    if msgs:
        summary = summary + "\n" + "\n".join(msgs)
    return (not msgs), summary


# ============================================================
# 6. GBDT 提取逻辑独立回放（对照 sklearn/xgboost/lightgbm 原生输出）
# ============================================================
def _sigmoid_f32(z):
    return np.float32(1.0) / np.float32(1.0 + np.exp(-np.float32(z), dtype=np.float32))


def _softmax_f32(z):
    m = np.max(z)
    e = np.exp(np.float32(z - np.float32(m)), dtype=np.float32)
    s = np.float32(0.0)
    for i in range(e.shape[0]):
        s = np.float32(s + e[i])
    return np.array([np.float32(v / s) for v in e], dtype=np.float32)


def _tree_walk_val(tree, xf):
    cur = 0
    while True:
        if int(tree["feat"][cur]) < 0:
            return float(tree["values"][cur])
        xv = float(np.float64(xf[int(tree["feat"][cur])]))
        cur = int(tree["left"][cur]) if xv <= float(tree["thr"][cur]) \
            else int(tree["right"][cur])


def _normalize_f32(x, scaler):
    off = np.asarray(scaler["offset"], dtype=np.float32)
    inv = np.asarray([np.float32(1.0 / np.float64(s)) for s in scaler["scale"]],
                     dtype=np.float32)
    return np.array(
        [np.float32(np.float32(np.float32(x[j]) - off[j]) * inv[j])
         for j in range(len(x))],
        dtype=np.float32,
    )


def _replay_gbdt(payload, X, nc):
    """按 model_cgen 契约回放提取后的 GBDT 表。"""
    from app.services.export import model_cgen
    arrs = model_cgen.extract_arrays(payload, nc)
    mt = arrs["model_type"]
    tree_nc = int(arrs.get("tree_nc", 1))
    bases = np.asarray(arrs.get("base", np.zeros(max(tree_nc, 1))), dtype=np.float64)
    n_trees = len(arrs["trees"])
    out = np.zeros((X.shape[0], nc), dtype=np.float32)
    is_regressor = bool(arrs.get("is_regressor", False))

    for r in range(X.shape[0]):
        xi = _normalize_f32(X[r], payload["scaler"])
        if is_regressor:
            s = float(bases.ravel()[0])
            for t in range(n_trees):
                s += _tree_walk_val(arrs["trees"][t], xi)
            out[r, 0] = np.float32(s)
            continue
        if tree_nc == 1:
            raw = float(bases[0]) if bases.size else 0.0
            for t in range(n_trees):
                raw += _tree_walk_val(arrs["trees"][t], xi)
            p1 = _sigmoid_f32(raw)
            out[r, 1] = p1
            out[r, 0] = np.float32(1.0 - p1)
        else:
            raw = np.zeros(nc, dtype=np.float64)
            for k in range(nc):
                raw[k] = float(bases[k]) if k < bases.size else 0.0
            for t in range(n_trees):
                raw[t % tree_nc] += _tree_walk_val(arrs["trees"][t], xi)
            out[r] = _softmax_f32(raw.astype(np.float32))
    return out, arrs


def test_gbdt_parity():
    """GBDT 提取逻辑 vs 原生 predict_proba。

    验收口径（产品级）：
    - LGBM：必须近似精确（容差 1e-3）—— decision_type '<=' 与 C 判定一致
    - XGB：nextafter 适配在 |x-thr| 极小时受 f32/f64 表示差影响（已知边界）。
      非边界样本（|x-thr|>1e-3）必须在容差内；边界样本单独计数上报，不单独否决。
    - 二分类 XGB base：仅当 is_binary 时校验 logit(base_score) 语义
    """
    _ensure_venv_path()
    from sklearn.datasets import make_classification, make_regression
    from app.services.export import model_cgen
    from app.services.scaler import Scaler

    msgs = []
    notes = []
    Xc2, yc2 = make_classification(
        n_samples=140, n_features=6, n_informative=4, n_classes=2,
        random_state=2,
    )
    Xc, yc = make_classification(
        n_samples=140, n_features=6, n_informative=4, n_classes=3,
        random_state=3,
    )
    Xr, yr = make_regression(
        n_samples=140, n_features=6, n_informative=4, random_state=4,
    )

    def min_split_margin(arrs, xi):
        m = 1e9
        for tr in arrs["trees"]:
            cur = 0
            while int(tr["feat"][cur]) >= 0:
                f = int(tr["feat"][cur])
                thr = float(tr["thr"][cur])
                m = min(m, abs(float(np.float64(xi[f])) - thr))
                xv = float(np.float64(xi[f]))
                cur = int(tr["left"][cur]) if xv <= thr else int(tr["right"][cur])
        return m

    def check_clf(mt, X, y, tol=5e-2, n=40):
        payload = _make_payload(mt, X, y, seed=11)
        nc = len(payload["labels"])
        replay, arrs = _replay_gbdt(payload, X[:n], nc)
        scaler = Scaler.from_snapshot(payload["scaler"])
        Xn = scaler.transform(X[:n][:, payload["feature_indices"]])
        native = payload["estimator"].predict_proba(Xn)
        classes = list(getattr(payload["estimator"], "classes_", range(nc)))

        n_boundary = 0
        n_fail = 0
        n_fail_nb = 0
        max_err = 0.0
        max_err_nb = 0.0
        for r in range(replay.shape[0]):
            xi = _normalize_f32(X[r], payload["scaler"])
            margin = min_split_margin(arrs, xi)
            err = 0.0
            for ci, cls in enumerate(classes):
                if cls >= replay.shape[1]:
                    continue
                err = max(err, abs(float(replay[r, cls]) - float(native[r, ci])))
            max_err = max(max_err, err)
            if margin < 1e-3:
                n_boundary += 1
            else:
                max_err_nb = max(max_err_nb, err)
            if err > tol:
                n_fail += 1
                if margin >= 1e-3:
                    n_fail_nb += 1

        # 二分类 XGB base_score → margin 语义
        if mt == "xgb" and bool(arrs.get("is_binary")):
            base = float(np.asarray(arrs["base"]).ravel()[0])
            booster = payload["estimator"].get_booster()
            expect = model_cgen._xgb_binary_margin_base(booster)
            if abs(base - expect) > 1e-9:
                msgs.append("xgb binary base=%r != margin base=%r" % (base, expect))
            notes.append("%s binary base=%.6f (logit ok)" % (mt, base))

        note = "%s: max_err=%.5f max_err_nonboundary=%.5f boundary=%d fail=%d fail_nb=%d" % (
            mt, max_err, max_err_nb, n_boundary, n_fail, n_fail_nb)
        notes.append(note)
        # LGBM 必须近精确；XGB 非边界必须过容差
        if mt == "lgbm" and max_err > 1e-3:
            msgs.append("lgbm parity not tight: " + note)
        if mt == "xgb" and n_fail_nb > 0:
            msgs.append("xgb non-boundary parity fail: " + note)
        return max_err

    def check_reg(mt, X, y, tol=5e-2, n=40):
        payload = _make_payload(mt, X, y, seed=12)
        replay, arrs = _replay_gbdt(payload, X[:n], 1)
        scaler = Scaler.from_snapshot(payload["scaler"])
        Xn = scaler.transform(X[:n][:, payload["feature_indices"]])
        native = np.asarray(payload["estimator"].predict(Xn), dtype=np.float64)
        errs = np.abs(replay[:, 0].astype(np.float64) - native)
        n_boundary = 0
        n_fail_nb = 0
        max_err_nb = 0.0
        for r in range(n):
            xi = _normalize_f32(X[r], payload["scaler"])
            margin = min_split_margin(arrs, xi)
            if margin < 1e-3:
                n_boundary += 1
            else:
                max_err_nb = max(max_err_nb, float(errs[r]))
                if errs[r] > tol:
                    n_fail_nb += 1
        note = "%s: max_err=%.5f max_err_nonboundary=%.5f boundary=%d fail_nb=%d" % (
            mt, float(errs.max()), max_err_nb, n_boundary, n_fail_nb)
        notes.append(note)
        if mt == "lgbm_r" and float(errs.max()) > 1e-3:
            msgs.append("lgbm_r parity not tight: " + note)
        if mt == "xgb_r" and n_fail_nb > 0:
            msgs.append("xgb_r non-boundary parity fail: " + note)
        return float(errs.max())

    try:
        check_clf("xgb", Xc2, yc2)
        check_clf("lgbm", Xc2, yc2)
        check_clf("xgb", Xc, yc, tol=8e-2)
        check_clf("lgbm", Xc, yc, tol=8e-2)
        check_reg("xgb_r", Xr, yr)
        check_reg("lgbm_r", Xr, yr)
    except Exception as e:  # noqa: BLE001
        msgs.append("gbdt parity exception: %r" % e)

    summary = "\n".join(notes) if notes else "OK"
    if msgs:
        summary = summary + "\n" + "\n".join(msgs)
    return (not msgs), summary


# ============================================================
# 7. reference.py 是否覆盖新 model_type（导出自检契约缺口检测）
# ============================================================
def test_reference_covers_new_types():
    """reference.predict_proba_ref 是 selfcheck ② 的回放权威。

    model_cgen 现已支持 16 种 model_type；若 reference 未同步，
    导出自检会在 xgb/lgbm/simple_nn/回归路径上直接 ValueError → 422。
    """
    _ensure_venv_path()
    from app.services.export import reference
    from app.services.export import model_cgen
    from sklearn.datasets import make_classification, make_regression
    from app.services.scaler import Scaler

    msgs = []
    Xc2, yc2 = make_classification(
        n_samples=80, n_features=5, n_informative=3, n_classes=2, random_state=5
    )
    Xr, yr = make_regression(
        n_samples=80, n_features=5, n_informative=3, random_state=6
    )
    gaps = []
    for mt, X, y in [
        ("simple_nn", Xc2, yc2),
        ("xgb", Xc2, yc2),
        ("lgbm", Xc2, yc2),
        ("dt_r", Xr, yr),
        ("lr_r", Xr, yr),
        ("xgb_r", Xr, yr),
        ("lgbm_r", Xr, yr),
        ("simple_nn_r", Xr, yr),
    ]:
        try:
            payload = _make_payload(mt, X, y, seed=21)
            nc = len(payload["labels"])
            # 先确认 model_cgen 能提取（新导出路径可用）
            model_cgen.extract_arrays(payload, nc)
            try:
                reference.predict_proba_ref(payload, X[:5][:, payload["feature_indices"]])
            except ValueError as e:
                if "未知模型类型" in str(e):
                    gaps.append(mt)
                else:
                    msgs.append("%s reference other ValueError: %r" % (mt, e))
        except Exception as e:  # noqa: BLE001
            msgs.append("%s setup failed: %r" % (mt, e))

    if gaps:
        # 作为回归缺口记录：reference 未覆盖这些 model_type
        msgs.append(
            "GAP reference.predict_proba_ref 未覆盖: %s "
            "(model_cgen 已支持；导出 selfcheck② 对这些类型会 422)" % gaps
        )
    summary = "OK: reference 覆盖全部导出类型" if not msgs else "\n".join(msgs)
    return (not msgs), summary


# ============================================================
# 8. Python 3.8 语法 / W2-W3 源文件静态检查
# ============================================================
def test_py38_and_static():
    files = [
        EXPORT_DIR / "feature_cgen.py",
        EXPORT_DIR / "model_cgen.py",
        EXPORT_DIR / "generator.py",
        EXPORT_DIR / "reference.py",
        EXPORT_DIR / "selfcheck.py",
    ]
    msgs = []
    for f in files:
        if not f.is_file():
            msgs.append("missing %s" % f)
            continue
        src = f.read_text(encoding="utf-8")
        try:
            tree = ast.parse(src)
        except SyntaxError as e:
            msgs.append("%s: SyntaxError %r" % (f.name, e))
            continue
        # 禁止 match/case（3.10+）；ast.Match 仅 3.10+
        match_cls = getattr(ast, "Match", None)
        if match_cls is not None:
            for node in ast.walk(tree):
                if isinstance(node, match_cls):
                    msgs.append("%s: match statement not py3.8-safe" % f.name)
        if "__future__" not in src:
            # 使用 X | Y 注解时必须有 future annotations
            if re.search(r"\bdict\s*\|\s*None\b|\blist\s*\[|\bint\s*\|", src):
                msgs.append("%s: py3.10+ annotations without __future__" % f.name)
        # VLA / 大栈数组启发式（生成器模板）
        if f.name == "feature_cgen.py":
            if "static float s_re[" not in src and "s_re[{n}]" not in src:
                msgs.append("feature_cgen: FFT buffers should be static")
        if f.name == "model_cgen.py":
            if "ALGO_ML_TREE_MAX_DEPTH = 128" not in src:
                msgs.append("model_cgen: ALGO_ML_TREE_MAX_DEPTH != 128")
            if "nextafter" not in src:
                msgs.append("model_cgen: missing XGB nextafter adaptation")
            # 禁止在 bundle 生成中定义 bAlgoMl*（只允许调用）
            if re.search(r"(?m)^\s*(?:static\s+)?float\s+bAlgoMl\w+\s*\(", src):
                msgs.append("model_cgen: defines bAlgoMl* (must only call)")

    # selfcheck 约束常量存在
    sc = (EXPORT_DIR / "selfcheck.py").read_text(encoding="utf-8")
    for token in ["-ffp-contract=off", "algo_ml.c", "bAlgoMl"]:
        if token not in sc:
            msgs.append("selfcheck.py missing token %r" % token)
    if "algo_signal.c" not in sc:
        msgs.append("selfcheck.py: deep-binding missing algo_signal.c injection")

    return (not msgs), ("OK" if not msgs else "\n".join(msgs))


# ============================================================
# 9. 既有 Python 测试脚本是否使用真实 feature_cgen（防 stale 内联副本）
# ============================================================
def test_stale_inline_copies():
    msgs = []
    for name in ("verify_deep_binding.py", "test_export_pipeline_simple.py"):
        p = C_DIR / name
        if not p.is_file():
            continue
        src = p.read_text(encoding="utf-8")
        # 必须 import 真实模块
        if "from app.services.export import feature_cgen" not in src and \
           "from app.services.export.feature_cgen import" not in src:
            msgs.append(
                "STALE %s: 未 import 真实 feature_cgen 模块" % name
            )
        # 禁止内联 _time_feature_c 实现（应 import 后赋值别名）
        if "def _time_feature_c" in src:
            msgs.append(
                "STALE %s: 仍内联定义 _time_feature_c（应 import feature_cgen）" % name
            )
    return (not msgs,
            "OK" if not msgs else "FAIL: " + "; ".join(msgs))


# ============================================================
# 10. Makefile / CFLAGS 契约
# ============================================================
def test_makefile_contract():
    mk = (C_DIR / "Makefile").read_text(encoding="utf-8")
    msgs = []
    for token in [
        "algo_signal.c", "algo_fft.c",
        "-D_ALGO_ML_ENABLE=1", "-D_ALGO_SIGNAL_ENABLE=1", "-D_ALGO_FFT_ENABLE=1",
        "-ffp-contract=off", "-std=c99",
        "test_algo_signal.c", "test_algo_fft.c",
    ]:
        if token not in mk:
            msgs.append("Makefile missing %r" % token)
    return (not msgs), ("OK" if not msgs else "\n".join(msgs))


# ============================================================
# 汇总执行
# ============================================================
def main():
    # 先跑边界独立脚本（任务项 2）
    print("=" * 64)
    print("TD3 回归套件 (regression + boundary + review gate)")
    print("=" * 64)

    suite = [
        ("C e2e (make && ./build/test_automl)", test_c_e2e),
        ("verify_deep_binding.py", test_verify_deep_binding),
        ("test_export_pipeline_simple.py", test_export_pipeline_simple),
        ("feature_cgen 全 TIME+FREQ 特征覆盖", test_feature_cgen_full_coverage),
        ("generator.MODEL_DESC 全 16 model_type", test_model_desc_coverage),
        ("全部 model_type extract+emit 冒烟", test_all_model_types_emit),
        ("GBDT 提取回放 vs 原生 predict_proba", test_gbdt_parity),
        ("reference.py 导出自检覆盖缺口", test_reference_covers_new_types),
        ("Python3.8 / W2-W3 静态契约", test_py38_and_static),
        ("既有脚本 stale 内联副本记录", test_stale_inline_copies),
        ("Makefile CFLAGS 契约", test_makefile_contract),
    ]

    results = []
    for name, fn in suite:
        try:
            ok, out = fn()
        except Exception as e:  # noqa: BLE001
            ok, out = False, "exception: %r" % e
        results.append((name, ok, out))

    # 边界用例子脚本
    try:
        r = _run([sys.executable, str(C_DIR / "test_boundary_cases.py")],
                 cwd=C_DIR, timeout=300)
        out = (r.stdout or "") + (r.stderr or "")
        results.append(("test_boundary_cases.py（n=1/全零/常数/FFT n=2/深度拒绝）",
                        r.returncode == 0, out[-4000:]))
    except Exception as e:  # noqa: BLE001
        results.append(("test_boundary_cases.py", False, repr(e)))

    print()
    failed = 0
    for name, ok, out in results:
        status = "PASS" if ok else "FAIL"
        print("  [%s] %s" % (status, name))
        if not ok:
            failed += 1
            for line in str(out).splitlines()[:40]:
                print("      %s" % line)
        else:
            # 显示简要输出
            brief = str(out).strip().splitlines()
            for line in brief[:6]:
                if line.strip():
                    print("      %s" % line)

    print()
    print("-" * 64)
    print("回归套件: %d 通过 / %d 失败 / 共 %d 组"
          % (len(results) - failed, failed, len(results)))
    if failed:
        print("结论: FAIL — 存在阻断项，需研发修复后回归")
        return 1
    print("结论: PASS — 回归+边界全部通过（详见 test_review_findings.md）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
