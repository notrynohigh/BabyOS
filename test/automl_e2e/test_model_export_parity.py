#!/usr/bin/env python3
"""TD2 — model export consistency (parity) tests.

Trains every exportable model_type on synthetic data, emits C via
``generator.build_bundle`` + ``model_cgen.extract_arrays`` / ``emit_model``,
compiles with gcc against the repo ``bos/algorithm`` primitives, runs
``algo_<name>_predict``, and compares against the estimator's own
``predict_proba`` / ``predict``.

Ground-truth alignment
----------------------
Generated C applies a **float32** baked scaler then float32 feature math.
To isolate *model export* parity from scaler rounding, the reference
predicts on the same f32-normalized features C uses internally::

    Xs_c = (X.astype(f32) - offset.astype(f32)) * inv_scale.astype(f32)

Pass criteria (contract):
- classification: max_abs_diff(proba) <= 1e-4  OR  argmax match rate == 100%
- regression:     max_abs_diff(pred)  <= 1e-4

Scope: table models only (feature_meta=None). Time-series feature-chain
parity is covered by other suites.

Usage:
    source tool/babyos-studio/python/.venv/bin/activate
    python test/automl_e2e/test_model_export_parity.py
    # exit 0 = all cases passed; exit 1 = at least one failure

Ownership: test-only file. Does not modify any source under bos/ or
tool/babyos-studio/python/app/.
"""
from __future__ import annotations

import ctypes
import os
import shutil
import subprocess
import sys
import tempfile
import traceback
import warnings
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, List, Optional, Tuple

import numpy as np

# ---------------------------------------------------------------------------
# Paths / environment
# ---------------------------------------------------------------------------

REPO_ROOT = Path("/home/yyds/code/BabyOS")
PY_ROOT = REPO_ROOT / "tool" / "babyos-studio" / "python"
TEST_DIR = REPO_ROOT / "test" / "automl_e2e"

# Prefer the project venv when present (caller may already have activated it).
_VENV_PY = PY_ROOT / ".venv" / "bin" / "python"
if _VENV_PY.is_file() and Path(sys.executable).resolve() != _VENV_PY.resolve():
    # Re-exec into the project venv so sklearn/xgb/lgbm match the contract.
    os.environ.setdefault("PYTHONPATH", str(PY_ROOT))
    os.execv(str(_VENV_PY), [str(_VENV_PY), str(Path(__file__).resolve()), *sys.argv[1:]])

if str(PY_ROOT) not in sys.path:
    sys.path.insert(0, str(PY_ROOT))
os.chdir(str(PY_ROOT))

warnings.filterwarnings("ignore")

from app.services.automl import make_estimator  # noqa: E402
from app.services.export import c_common, generator  # noqa: E402
from app.services.export.c_common import find_babyos_root  # noqa: E402
from app.services.scaler import Scaler  # noqa: E402

REPO = find_babyos_root()

PROBA_TOL = 1e-4
REG_TOL = 1e-4
N_EVAL = 200
N_FEAT = 6
SEED = 42


# ---------------------------------------------------------------------------
# Case registry — all 16 model_types + binary variants of special paths
# ---------------------------------------------------------------------------

@dataclass
class Case:
    model_type: str
    hp: dict
    task: str  # "classification" | "regression"
    nc: int  # number of classes (1 for regression)
    scaler_mode: str = "zscore"
    seed: int = SEED
    n: int = N_EVAL
    nf: int = N_FEAT
    tags: Tuple[str, ...] = ()
    name: str = ""

    def __post_init__(self) -> None:
        if not self.name:
            self.name = f"{self.model_type}:{self.task}:nc{self.nc}:{self.scaler_mode}"


def _all_cases() -> List[Case]:
    xgb_hp = {"n_estimators": 8, "max_depth": 3, "learning_rate": 0.1}
    lgbm_hp = {"n_estimators": 8, "max_depth": 3, "learning_rate": 0.1}
    rf_hp = {"n_estimators": 8, "max_depth": 4}
    tree_hp = {"max_depth": 4}
    mlp_hp = {"hidden": 8, "alpha": 0.001}
    nn_hp = {"hidden": 8, "max_iter": 300}

    cases: List[Case] = [
        # ---- classification: tree / linear / nb / mlp (multiclass + binary where path differs)
        Case("dt", tree_hp, "classification", 3, tags=("sklearn", "tree")),
        Case("dt", tree_hp, "classification", 2, tags=("sklearn", "tree", "binary")),
        Case("rf", rf_hp, "classification", 3, tags=("sklearn", "forest")),
        Case("et", rf_hp, "classification", 3, tags=("sklearn", "forest")),
        Case("lr", {"C": 1.0}, "classification", 2, tags=("sklearn", "linear", "binary")),
        Case("lr", {"C": 1.0}, "classification", 3, tags=("sklearn", "linear")),
        Case("nb", {}, "classification", 3, tags=("sklearn", "nb")),
        Case("mlp", mlp_hp, "classification", 2, tags=("sklearn", "mlp", "binary")),
        Case("mlp", mlp_hp, "classification", 3, tags=("sklearn", "mlp")),
        Case("simple_nn", nn_hp, "classification", 2, tags=("studio", "nn", "binary")),
        Case("simple_nn", nn_hp, "classification", 3, tags=("studio", "nn")),
        # ---- classification: GBDT (required: multiclass nc=3 + binary)
        Case("xgb", xgb_hp, "classification", 3, tags=("xgb", "gbdt", "multiclass")),
        Case("xgb", xgb_hp, "classification", 2, tags=("xgb", "gbdt", "binary")),
        Case("lgbm", lgbm_hp, "classification", 3, tags=("lgbm", "gbdt", "multiclass")),
        Case("lgbm", lgbm_hp, "classification", 2, tags=("lgbm", "gbdt", "binary")),
        # ---- regression: all 7 regressors (required: xgb_r / lgbm_r + sklearn family)
        Case("dt_r", tree_hp, "regression", 1, tags=("sklearn", "tree")),
        Case("rf_r", rf_hp, "regression", 1, tags=("sklearn", "forest")),
        Case("et_r", rf_hp, "regression", 1, tags=("sklearn", "forest")),
        Case("lr_r", {}, "regression", 1, tags=("sklearn", "linear")),
        Case("simple_nn_r", nn_hp, "regression", 1, tags=("studio", "nn")),
        Case("xgb_r", xgb_hp, "regression", 1, tags=("xgb", "gbdt")),
        Case("lgbm_r", lgbm_hp, "regression", 1, tags=("lgbm", "gbdt")),
        # ---- identity scaler: isolates model export from scaler baking
        Case("xgb", xgb_hp, "classification", 3, scaler_mode="none", tags=("xgb", "identity")),
        Case("lgbm", lgbm_hp, "classification", 3, scaler_mode="none", tags=("lgbm", "identity")),
        Case("xgb_r", xgb_hp, "regression", 1, scaler_mode="none", tags=("xgb", "identity")),
        Case("rf", rf_hp, "classification", 3, scaler_mode="none", tags=("sklearn", "identity")),
        Case("lr", {"C": 1.0}, "classification", 2, scaler_mode="none", tags=("sklearn", "identity")),
    ]
    return cases


# ---------------------------------------------------------------------------
# Synthetic data
# ---------------------------------------------------------------------------

def make_data(case: Case) -> Tuple[np.ndarray, np.ndarray, list]:
    rng = np.random.default_rng(case.seed)
    n, nf, nc = case.n, case.nf, case.nc
    if case.task == "regression":
        X = rng.normal(size=(n, nf))
        y = (
            2.0 * X[:, 0]
            - 1.5 * X[:, 1]
            + 0.5 * X[:, 2]
            + 0.2 * X[:, 3] * X[:, 4]
            + rng.normal(size=n) * 0.05
        )
        labels = [{"label_id": 0, "name": "target"}]
        return X, y, labels

    X = rng.normal(size=(n, nf))
    if nc == 2:
        y = (X[:, 0] + 0.5 * X[:, 1] + rng.normal(size=n) * 0.1 > 0).astype(int)
        labels = [
            {"label_id": 0, "name": "neg"},
            {"label_id": 1, "name": "pos"},
        ]
    else:
        # 3 well-separated clusters (avoid near-tie argmax flakiness)
        y = np.zeros(n, dtype=int)
        y[X[:, 0] > 0.5] = 1
        y[X[:, 1] > 0.5] = 2
        # force balanced support
        y[: n // 5] = 0
        y[n // 5 : 2 * n // 5] = 1
        y[2 * n // 5 : 3 * n // 5] = 2
        labels = [{"label_id": i, "name": f"c{i}"} for i in range(nc)]
    return X, y, labels


def f32_normalize(X: np.ndarray, scaler_snap: dict) -> np.ndarray:
    """Replicate C-side bAlgoMlNormalize baking: (x - off_f32) * inv_f32."""
    off = np.asarray(scaler_snap["offset"], dtype=np.float32)
    inv = np.array(
        [np.float32(1.0 / np.float64(s)) for s in scaler_snap["scale"]],
        dtype=np.float32,
    )
    X32 = np.asarray(X, dtype=np.float32)
    return ((X32 - off) * inv).astype(np.float64)


# ---------------------------------------------------------------------------
# Build / compile / predict
# ---------------------------------------------------------------------------

def build_payload(case: Case, X: np.ndarray, y: np.ndarray, labels: list) -> dict:
    sc = Scaler(case.scaler_mode).fit(X)
    Xs = sc.transform(X)
    est = make_estimator(case.model_type, case.hp, np.random.default_rng(case.seed))
    est.fit(Xs, y)
    return {
        "estimator": est,
        "model_type": case.model_type,
        "hyperparams": dict(case.hp),
        "feature_indices": list(range(case.nf)),
        "norm": case.scaler_mode,
        "scaler": sc.snapshot(),
        "labels": labels,
        "seed": case.seed,
        "data_hash": f"parity:{case.name}",
        "feature_names": [f"f{i}" for i in range(case.nf)],
        "task_type": "regression" if case.task == "regression" else "classification",
    }


def compile_bundle(workdir: Path, name: str) -> Path:
    """gcc -shared: generated algo_*.c + repo algo_ml/signal/fft + stub b_os.h."""
    stub = workdir / "stub"
    stub.mkdir(exist_ok=True)
    (stub / "b_os.h").write_text(c_common.STUB_B_OS_H, encoding="utf-8")

    algo_ml = REPO / "bos" / "algorithm" / "algo_ml.c"
    algo_signal = REPO / "bos" / "algorithm" / "algo_signal.c"
    algo_fft = REPO / "bos" / "algorithm" / "algo_fft.c"
    for p in (algo_ml, algo_signal, algo_fft):
        if not p.is_file():
            raise FileNotFoundError(f"missing repo primitive: {p}")

    so = workdir / "_parity.so"
    c_files = [
        str(workdir / f"algo_{name}.c"),
        str(algo_ml),
        str(algo_signal),
        str(algo_fft),
    ]
    cmd = [
        "gcc",
        "-std=c99",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-ffp-contract=off",
        "-shared",
        "-fPIC",
        "-O2",
        "-I", str(stub),
        "-I", str(workdir),
        "-I", str(REPO / "bos" / "algorithm"),
        "-I", str(REPO / "bos" / "algorithm" / "inc"),
        "-D_ALGO_ML_ENABLE=1",
        "-D_ALGO_SIGNAL_ENABLE=1",
        "-D_ALGO_FFT_ENABLE=1",
        "-lm",
        "-o", str(so),
    ] + c_files
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        raise RuntimeError(f"gcc failed ({r.returncode}):\n{r.stderr[-3000:]}")
    return so


def run_c_predict(so: Path, name: str, X: np.ndarray, nc: int) -> Tuple[np.ndarray, np.ndarray]:
    """Call algo_<name>_predict on each row. Returns (proba_or_value, class_id)."""
    n, nf = X.shape
    lib = ctypes.CDLL(str(so))
    try:
        fn = getattr(lib, f"algo_{name}_predict")
        fn.argtypes = [
            ctypes.POINTER(ctypes.c_float),
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_float),
        ]
        fn.restype = ctypes.c_int

        out = np.empty((n, nc), dtype=np.float64)
        ids = np.empty(n, dtype=np.int64)
        for i in range(n):
            xin = np.ascontiguousarray(X[i], dtype=np.float32)
            pout = np.empty(nc, dtype=np.float32)
            rid = fn(
                xin.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
                ctypes.c_uint32(nf),
                pout.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
            )
            if rid < 0:
                raise RuntimeError(f"sample {i}: C predict returned {rid}")
            ids[i] = rid
            out[i] = pout
        return out, ids
    finally:
        # Windows CDLL lock — not needed on Linux, kept for parity with selfcheck
        if hasattr(lib, "_handle") and sys.platform == "win32":
            try:
                ctypes.windll.kernel32.FreeLibrary(lib._handle)  # type: ignore[attr-defined]
            except Exception:
                pass


def reference_predict(payload: dict, Xs_c: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
    """sklearn / studio estimator ground truth on f32-aligned features."""
    est = payload["estimator"]
    if payload["task_type"] == "regression":
        pred = np.asarray(est.predict(Xs_c), dtype=np.float64).reshape(-1)
        return pred.reshape(-1, 1), np.zeros(len(pred), dtype=np.int64)
    proba = np.asarray(est.predict_proba(Xs_c), dtype=np.float64)
    ids = np.argmax(proba, axis=1).astype(np.int64)
    return proba, ids


def softmax_standard(z: np.ndarray) -> np.ndarray:
    z = np.asarray(z, dtype=np.float64)
    m = z.max(axis=1, keepdims=True)
    e = np.exp(z - m)
    return e / e.sum(axis=1, keepdims=True)


def simple_nn_standard_proba(est, Xs_c: np.ndarray) -> np.ndarray:
    """Standard softmax over W2_ logits — what the C export implements."""
    z1 = Xs_c @ est.W1_.T + est.b1_
    a1 = np.maximum(0.0, z1)
    z2 = a1 @ est.W2_.T + est.b2_
    return softmax_standard(z2)


# ---------------------------------------------------------------------------
# One full case
# ---------------------------------------------------------------------------

@dataclass
class CaseResult:
    case: Case
    ok: bool
    stage: str = "ok"
    max_diff: float = 0.0
    id_match: float = 1.0
    n: int = 0
    message: str = ""
    extra: dict = field(default_factory=dict)

    def line(self) -> str:
        status = "PASS" if self.ok else "FAIL"
        return (
            f"[{status}] {self.case.name:42s} "
            f"stage={self.stage:8s} max_diff={self.max_diff:.3e} "
            f"id_match={self.id_match:.3f} n={self.n} {self.message}"
        )


def run_case(case: Case) -> CaseResult:
    X, y, labels = make_data(case)
    try:
        payload = build_payload(case, X, y, labels)
    except Exception as e:
        return CaseResult(case, False, stage="train", message=str(e))

    nc = case.nc
    is_reg = case.task == "regression"

    # extract_arrays + emit must not raise (MODEL_DESC / dispatch contract)
    try:
        from app.services.export import model_cgen

        arrs = model_cgen.extract_arrays(payload, nc)
        emitted = model_cgen.emit_model(
            payload, arrs, "parity", nc, case.nf, "ALGO_PARITY_N_CLASSES"
        )
        if not emitted.get("predict_core"):
            return CaseResult(case, False, stage="emit", message="empty predict_core")
    except Exception as e:
        return CaseResult(case, False, stage="emit", message=f"{type(e).__name__}: {e}")

    with tempfile.TemporaryDirectory(prefix=f"parity_{case.model_type}_") as td:
        td_path = Path(td)
        try:
            info = generator.build_bundle(
                td_path, "parity", payload, None, "2026-10-02"
            )
        except Exception as e:
            return CaseResult(
                case, False, stage="bundle",
                message=f"{type(e).__name__}: {e}",
            )
        name = info["name"]
        try:
            so = compile_bundle(td_path, name)
        except Exception as e:
            return CaseResult(case, False, stage="compile", message=str(e)[:2000])

        # Ground truth on the exact f32-normalized features C uses
        scaler_snap = payload["scaler"]
        Xs_c = f32_normalize(X, scaler_snap)
        try:
            ref_out, ref_ids = reference_predict(payload, Xs_c)
        except Exception as e:
            return CaseResult(case, False, stage="reference", message=str(e))

        try:
            c_out, c_ids = run_c_predict(so, name, X, nc)
        except Exception as e:
            return CaseResult(case, False, stage="predict", message=str(e)[:2000])

    n = len(X)
    if is_reg:
        ref = ref_out.reshape(-1)
        got = c_out.reshape(-1)
        max_diff = float(np.max(np.abs(got - ref))) if n else 0.0
        id_match = 1.0  # regression has no class id
        ok = max_diff <= REG_TOL
        return CaseResult(
            case, ok, stage="ok" if ok else "parity",
            max_diff=max_diff, id_match=id_match, n=n,
            message="" if ok else f"regression tol {REG_TOL}",
            extra={"ref_sample": ref[:3].tolist(), "c_sample": got[:3].tolist()},
        )

    max_diff = float(np.max(np.abs(c_out - ref_out))) if n else 0.0
    id_match = float(np.mean(c_ids == ref_ids)) if n else 1.0
    ok = (max_diff <= PROBA_TOL) or (id_match == 1.0)

    extra = {}
    if not ok and case.model_type == "simple_nn" and case.nc == 2:
        # Diagnostic: export = standard softmax; estimator.predict_proba may swap
        est = payload["estimator"]
        try:
            Xs_c2 = f32_normalize(X, payload["scaler"])
            std = simple_nn_standard_proba(est, Xs_c2)
            py = np.asarray(est.predict_proba(Xs_c2), dtype=np.float64)
            extra["std_softmax_vs_c_maxdiff"] = float(np.max(np.abs(std - c_out)))
            extra["py_predict_proba_vs_c_maxdiff"] = float(np.max(np.abs(py - c_out)))
            extra["py_vs_std_maxdiff"] = float(np.max(np.abs(py - std)))
        except Exception as e:
            extra["diag_error"] = str(e)

    msg = ""
    if not ok:
        if id_match < 1.0 and max_diff > PROBA_TOL:
            msg = f"proba tol {PROBA_TOL} AND argmax mismatch (id_match={id_match:.3f})"
        elif max_diff > PROBA_TOL:
            msg = f"proba tol {PROBA_TOL}"
    return CaseResult(
        case, ok, stage="ok" if ok else "parity",
        max_diff=max_diff, id_match=id_match, n=n, message=msg, extra=extra,
    )


# ---------------------------------------------------------------------------
# Static contract checks (no compile)
# ---------------------------------------------------------------------------

def check_model_desc_complete() -> Tuple[bool, str]:
    from app.services.export.generator import MODEL_DESC
    from app.services.automl import CLASSIFICATION_MODELS, REGRESSION_MODELS

    required = list(CLASSIFICATION_MODELS) + list(REGRESSION_MODELS)
    missing = [m for m in required if m not in MODEL_DESC]
    if missing:
        return False, f"MODEL_DESC missing: {missing}"
    return True, f"MODEL_DESC covers all {len(required)} model_types"


def check_dispatch_all_types() -> Tuple[bool, str]:
    """extract_arrays + emit_model must accept every model_type (no KeyError/raise)."""
    from app.services.export import model_cgen

    # Minimal fake estimators are heavy; instead rely on real trained payloads
    # from the case list — this helper is invoked after training smoke if needed.
    return True, "dispatch covered by live cases"


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(argv: Optional[List[str]] = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    only = None
    if "--only" in argv:
        i = argv.index("--only")
        only = argv[i + 1] if i + 1 < len(argv) else None

    print("=" * 78)
    print("BabyOS AutoML — model export parity (TD2)")
    print(f"repo: {REPO}")
    print(f"python: {sys.executable}")
    print(f"numpy {np.__version__}", end="")
    try:
        import sklearn
        import xgboost
        import lightgbm

        print(
            f" | sklearn {sklearn.__version__}"
            f" | xgboost {xgboost.__version__}"
            f" | lightgbm {lightgbm.__version__}"
        )
    except Exception as e:
        print(f" | (version probe failed: {e})")
    print("=" * 78)

    ok_md, msg_md = check_model_desc_complete()
    print(f"[static] MODEL_DESC: {'OK' if ok_md else 'FAIL'} — {msg_md}")

    cases = _all_cases()
    if only:
        cases = [c for c in cases if only in c.model_type or only in c.name]
    print(f"[static] running {len(cases)} parity cases "
          f"(proba/reg tol={PROBA_TOL}, argmax fallback 100%)")
    print("-" * 78)

    results: List[CaseResult] = []
    for case in cases:
        try:
            res = run_case(case)
        except Exception:
            res = CaseResult(
                case, False, stage="exception",
                message=traceback.format_exc()[-500:],
            )
        results.append(res)
        print(res.line(), flush=True)

    n_pass = sum(1 for r in results if r.ok)
    n_fail = len(results) - n_pass
    covered_types = sorted({r.case.model_type for r in results})
    print("-" * 78)
    print(f"model_types covered ({len(covered_types)}): {', '.join(covered_types)}")
    print(f"RESULT: {n_pass}/{len(results)} passed, {n_fail} failed")

    if n_fail:
        print("\nFailures:")
        for r in results:
            if not r.ok:
                print(f"  - {r.case.name}: stage={r.stage} {r.message}")
                if r.extra:
                    print(f"      extra={r.extra}")

    # Hard gate: MODEL_DESC must be complete
    if not ok_md:
        return 1
    return 0 if n_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
