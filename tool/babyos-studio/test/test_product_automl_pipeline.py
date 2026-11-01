#!/usr/bin/env python3
"""Product-grade AutoML pipeline tests (not UI shells).

Covers real service-layer AND FastAPI TestClient product paths using the
checked-in datasets under test/datasets/:

  Path A — service-layer table regression (regression.csv)
       create → import (raw y_float targets) → features → train → export C bundle
       SUCCESS path (regression export product bug fixed: N_CLASSES=1 + value
       consistency check).
  Path B — FastAPI TestClient HTTP API, table classification
       create → import → features → train → leaderboard/best/report
       → set_best → feature_importance → export → download → templates
  Path C — service-layer timeseries classification with freq_enabled features
       import (auto RLE) → freq feature config → train → export
       (exercises feat_extract + FFT C chain + feature_consistency)
  Path D — HTTP labels + segments product surface
       segments GET/POST/PATCH/DELETE; labels GET/POST/PATCH/DELETE;
       user-added label used in training/export; segment edit invalidates
       feature matrix; training cancel.
  Path E — HTTP timeseries export chain + project lifecycle
       ts export via TestClient; project copy; soft-delete + trash restore;
       archive export/import; dataset preview; file delete.
  Path F — service-layer table classification export
       (complements Path B: classification export via service layer)

Export assertions require REAL artifacts on disk and ok=True — diagnostic-only
report/zip existence on a failed export is NOT counted as product success.

Run (from studio root):
  PYTHONPATH=python python/.venv/bin/python test/test_product_automl_pipeline.py
  PYTHONPATH=python python/.venv/bin/python -m pytest test/test_product_automl_pipeline.py -v
"""
from __future__ import annotations

import io
import json
import os
import sys
import tempfile
import time
import zipfile
from pathlib import Path

import numpy as np

# Isolate project storage BEFORE importing app.* (config reads env at call time,
# but set early so create_app startup reconcile uses the temp root).
_DATA_ROOT = tempfile.mkdtemp(prefix="automl_prod_pipeline_")
os.environ["AUTOML_DATA_ROOT"] = _DATA_ROOT

_HERE = Path(__file__).resolve().parent
_STUDIO = _HERE.parent
_PY_ROOT = _STUDIO / "python"
for _p in (str(_PY_ROOT), str(_HERE)):
    if _p not in sys.path:
        sys.path.insert(0, _p)
os.chdir(_STUDIO)

from fastapi.testclient import TestClient  # noqa: E402

from app.main import create_app  # noqa: E402
from app.schemas import FeatureConfig, ProjectCreate, TrainConfig  # noqa: E402
from app.services import (  # noqa: E402
    dataset_service,
    export_service,
    feature_service,
    training_service,
)
from app.services.project_service import ProjectService  # noqa: E402
from app.deps import AppError  # noqa: E402

DATASETS = _STUDIO / "test" / "datasets"
CSV_REGRESSION = DATASETS / "regression.csv"
CSV_TABLE_CLS = DATASETS / "table_classification.csv"
CSV_TS_CLS = DATASETS / "ts_classification.csv"

PASS = 0
FAIL = 0
ERRORS: list = []


class MockUploadFile:
    """Minimal FastAPI UploadFile stand-in for the service layer."""

    def __init__(self, filename: str, content: bytes):
        self.filename = filename
        self.file = io.BytesIO(content)
        self.size = len(content)


def check(cond, msg: str) -> bool:
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {msg}")
        return True
    FAIL += 1
    ERRORS.append(msg)
    print(f"  [FAIL] {msg}")
    return False


def wait_training(pid: str, timeout_s: float = 180.0, poll_s: float = 0.5) -> dict:
    deadline = time.time() + timeout_s
    st = training_service.status(pid)
    while time.time() < deadline:
        st = training_service.status(pid)
        if st.get("status") not in ("running", "idle"):
            return st
        time.sleep(poll_s)
    return st


def wait_training_http(client: TestClient, pid: str, timeout_s: float = 180.0) -> dict:
    deadline = time.time() + timeout_s
    st = {"status": "running"}
    while time.time() < deadline:
        r = client.get(f"/api/projects/{pid}/training")
        st = r.json() if r.status_code == 200 else {"status": f"http_{r.status_code}"}
        if st.get("status") not in ("running", "idle"):
            return st
        time.sleep(0.4)
    return st


def assert_export_artifacts(pid: str, *, expect_ts: bool, via: str) -> dict:
    """Assert export produced REAL C bundle files + selfcheck report with ok=True."""
    exp_dir = Path(_DATA_ROOT) / "projects" / pid / "export"
    check(exp_dir.is_dir(), f"[{via}] export/ directory exists: {exp_dir}")

    report_path = exp_dir / "export_report.json"
    check(report_path.is_file(), f"[{via}] export_report.json exists")
    report = {}
    if report_path.is_file():
        report = json.loads(report_path.read_text(encoding="utf-8"))
        # Product success: ok must be True — diagnostic artifacts on failure
        # are NOT product success.
        check(report.get("ok") is True,
              f"[{via}] export_report.ok is True (errors={report.get('errors')})")
        for step in ("compile", "predict_consistency", "static_scan", "symbol_map"):
            step_info = report.get(step) or {}
            check(step_info.get("ok") is True,
                  f"[{via}] selfcheck {step}.ok is True (got {step_info})")
        fc = report.get("feature_consistency") or {}
        if expect_ts:
            check(fc.get("ok") is True,
                  f"[{via}] selfcheck feature_consistency.ok is True (got {fc})")
        else:
            check("ok" in fc,
                  f"[{via}] feature_consistency present for table export: {fc}")
        check(bool(report.get("compiler")),
              f"[{via}] report.compiler recorded: {report.get('compiler')!r}")
        check("sklearn_version" in report, f"[{via}] report.sklearn_version present")
        pc = report.get("predict_consistency") or {}
        if pc.get("kind") == "regression_value":
            check(pc.get("ok") is True,
                  f"[{via}] regression predict value consistency ok "
                  f"(max_abs_diff={pc.get('max_abs_diff')})")
        else:
            check(pc.get("id_match_rate") == 1.0,
                  f"[{via}] classification id_match_rate==1.0 "
                  f"(got {pc.get('id_match_rate')})")

    zips = sorted(p.name for p in exp_dir.glob("*_bundle.zip")) if exp_dir.is_dir() else []
    check(len(zips) >= 1, f"[{via}] bundle zip exists: {zips}")
    if not zips:
        return report

    zip_path = exp_dir / zips[0]
    with zipfile.ZipFile(zip_path) as zf:
        names = zf.namelist()
        algo_c = [n for n in names if n.startswith("algo_") and n.endswith(".c") and "_feat" not in n]
        algo_h = [n for n in names if n.startswith("algo_") and n.endswith(".h") and "_feat" not in n]
        check(len(algo_c) == 1 and len(algo_h) == 1,
              f"[{via}] zip has algo_*.c/.h: c={algo_c} h={algo_h}")
        required = ["Makefile.snippet", "b_config_snippet.h", "main.c", "README.md",
                    "section.txt", "export_report.json"]
        for req in required:
            check(req in names, f"[{via}] zip contains {req}")
        if expect_ts:
            check(any(n.endswith("_feat.h") for n in names),
                  f"[{via}] zip contains feat header shim: {[n for n in names if 'feat' in n]}")
        if algo_c:
            src = zf.read(algo_c[0]).decode("utf-8", errors="replace")
            check(len(src) > 200, f"[{via}] {algo_c[0]} non-trivial ({len(src)} bytes)")
            check("algo_ml.h" in src or "bAlgoMl" in src,
                  f"[{via}] {algo_c[0]} references algo_ml primitives")
            check("predict" in src, f"[{via}] {algo_c[0]} contains predict symbol")
            if expect_ts:
                check("feat_extract" in src,
                      f"[{via}] {algo_c[0]} contains feat_extract implementation")
        if algo_h:
            hdr = zf.read(algo_h[0]).decode("utf-8", errors="replace")
            check("predict" in hdr, f"[{via}] {algo_h[0]} declares predict")
    return report


def make_tiny_ts_csv(n_per_class: int = 600, n_classes: int = 4) -> bytes:
    """Synthetic ts CSV: n_classes classes × n_per_class samples @100Hz, 2 ch."""
    lines = ["timestamp,ch0,ch1,label"]
    for cls in range(n_classes):
        for i in range(n_per_class):
            t = (cls * n_per_class + i) * 0.01
            lines.append(
                f"{t:.2f},{0.1 * cls + 0.001 * i},{-0.05 * cls + 0.002 * i},{cls}"
            )
    return ("\n".join(lines) + "\n").encode("utf-8")


# ---------------------------------------------------------------------------
# Path A — service-layer table regression (product bug fixed → export SUCCESS)
# ---------------------------------------------------------------------------

def pipeline_service_regression() -> None:
    """Path A — real service layer, table regression on regression.csv."""
    print("\n=== Path A: service-layer table regression (regression.csv) ===")
    svc = ProjectService()
    body = ProjectCreate(name="prod_reg_svc", mode="table", task_type="regression")
    meta = svc.create(body)
    pid = meta.project_id
    check(meta.stage == "created", f"created project stage=created (got {meta.stage})")

    content = CSV_REGRESSION.read_bytes()
    check(len(content) > 0, f"regression.csv readable ({len(content)} bytes)")
    features = [f"feat_{i}" for i in range(6)]
    mapping = {"features": features, "label_col": "target"}
    uf = MockUploadFile("regression.csv", content)
    result = dataset_service.import_files(meta, [uf], mapping, import_kind="replace")
    check(result.get("imported", 0) > 0,
          f"import regression.csv imported={result.get('imported')}")

    meta = svc.get(pid)
    check(meta.task_type == "regression", f"task_type=regression (got {meta.task_type})")
    check(meta.stage in ("labeled", "data_imported"),
          f"stage advanced after import (got {meta.stage})")
    check(len(meta.channels) == 6, f"channels count=6 (got {meta.channels})")
    # Regression: labels = single target-column label (not per-unique-value)
    check(len(meta.labels) == 1,
          f"regression labels n=1 (target col) got={[(l.label_id, l.name) for l in meta.labels]}")

    cfg = feature_service.get_config(meta)
    check(cfg.feature_ids == [], "table mode feature_ids empty (all numeric cols)")
    info = feature_service.compute(meta, cfg)
    check(info.get("n_samples") == 100, f"feature matrix n_samples=100 (got {info.get('n_samples')})")
    check(info.get("n_features") == 6, f"feature matrix n_features=6 (got {info.get('n_features')})")

    # y_float must be the RAW continuous targets (not label_ids)
    m = feature_service.load_matrix(meta)
    check("y_float" in m, "matrix contains y_float")
    if "y_float" in m:
        yf = m["y_float"]
        check(yf.dtype == __import__("numpy").float64, f"y_float dtype float64 (got {yf.dtype})")
        check(float(yf.min()) < 0 < float(yf.max()) or float(yf.std()) > 0.1,
              f"y_float looks like raw targets (std={float(yf.std()):.3f}, "
              f"min={float(yf.min()):.3f}, max={float(yf.max()):.3f})")
        # raw targets from regression.csv are ~N(0, scale) — not integers 0..99
        frac_non_int = float((__import__("numpy").abs(yf - __import__("numpy").round(yf)) > 1e-6).mean())
        check(frac_non_int > 0.5,
              f"y_float mostly non-integer (raw targets, not label_ids): "
              f"frac_non_int={frac_non_int:.2f}")

    meta = svc.get(pid)
    check(meta.stage == "featured", f"stage=featured after compute (got {meta.stage})")

    tcfg = TrainConfig(
        k=2, n_iter=2, budget_s=15,
        metric="mse", task_type="regression",
        auto_feature_select=False, seed=42,
    )
    start = training_service.start(pid, tcfg)
    check(start.get("status") == "running", f"training started (got {start.get('status')})")
    st = wait_training(pid, timeout_s=120)
    check(st.get("status") == "done", f"training done (got {st.get('status')} err={st.get('error')})")

    lb = training_service.leaderboard(pid)
    check(lb.get("task_type") == "regression", f"leaderboard task_type=regression")
    check(len(lb.get("candidates") or []) > 0,
          f"leaderboard has candidates (n={len(lb.get('candidates') or [])})")

    report = training_service.get_report(pid)
    check(report.get("task_type") == "regression", "report task_type=regression")
    best = report.get("best_model") or {}
    check(bool(best.get("model_type")), f"best model_type present: {best.get('model_type')}")
    metrics_test = best.get("metrics_test") or {}
    check("mse" in metrics_test, f"best metrics_test contains mse: {list(metrics_test)}")
    # mse now computed on RAW targets — should be finite and order-of-magnitude sane
    mse = metrics_test.get("mse")
    check(mse is not None and mse == mse and mse < 100,
          f"regression mse on raw targets sane: mse={mse}")

    best_info = training_service.best_info(pid)
    check("model_type" in best_info, f"best_info has model_type: {best_info.get('model_type')}")

    # ---- Export: PRODUCT SUCCESS (regression export bug fixed) ----
    exp = None
    export_err = None
    try:
        exp = export_service.export(pid)
    except AppError as e:
        export_err = e
        print(f"  [INFO] regression export AppError: {getattr(e, 'detail', e)}")
    check(exp is not None,
          f"[svc-regression] export returned successfully (product path): "
          f"err={export_err}")
    if exp is not None:
        check(exp.get("ok") is True,
              f"[svc-regression] export ok=True (errors={exp.get('errors')})")
        meta = svc.get(pid)
        check(meta.stage == "exported", f"[svc-regression] stage=exported (got {meta.stage})")
        pc = exp.get("predict_consistency") or {}
        check(pc.get("kind") == "regression_value" and pc.get("ok") is True,
              f"[svc-regression] predict value consistency ok "
              f"(max_abs_diff={pc.get('max_abs_diff')})")
        # N_CLASSES baked as 1 for regression
        exp_dir = Path(_DATA_ROOT) / "projects" / pid / "export"
        zips = sorted(p.name for p in exp_dir.glob("*_bundle.zip"))
        check(len(zips) >= 1, f"[svc-regression] bundle zip exists: {zips}")
        if zips:
            with zipfile.ZipFile(exp_dir / zips[0]) as zf:
                hdrs = [n for n in zf.namelist() if n.startswith("algo_") and n.endswith(".h")]
                if hdrs:
                    hdr = zf.read(hdrs[0]).decode("utf-8", errors="replace")
                    check("N_CLASSES  (1)" in hdr or "N_CLASSES (1)" in hdr
                          or "N_CLASSES  (1)" in hdr.replace("  ", " "),
                          f"[svc-regression] header bakes N_CLASSES=1: "
                          f"{[ln.strip() for ln in hdr.splitlines() if 'N_CLASSES' in ln][:3]}")
        assert_export_artifacts(pid, expect_ts=False, via="svc-regression")
    else:
        check(False,
              f"[svc-regression] REGRESSION EXPORT FAILED (product bug not fixed): {export_err}")

    st_status = export_service.export_status(pid)
    check(st_status.get("exported") is True,
          f"[svc-regression] export_status.exported=True "
          f"(got {st_status.get('exported')})")
    check(len(st_status.get("zips") or []) >= 1,
          f"[svc-regression] export_status zips={st_status.get('zips')}")

    svc.delete(pid, hard=True)


# ---------------------------------------------------------------------------
# Path B — HTTP TestClient table classification
# ---------------------------------------------------------------------------

def pipeline_http_table_classification() -> None:
    """Path B — FastAPI TestClient HTTP product path, table classification."""
    print("\n=== Path B: HTTP TestClient table classification (table_classification.csv) ===")
    app = create_app()
    client = TestClient(app)

    r = client.get("/api/version")
    check(r.status_code == 200, f"GET /api/version -> {r.status_code}")

    r = client.post("/api/projects", json={
        "name": "prod_tbl_cls_http",
        "mode": "table",
        "task_type": "classification",
    })
    check(r.status_code == 201, f"POST /api/projects -> {r.status_code} {r.text[:200]}")
    if r.status_code != 201:
        return
    pid = r.json()["project_id"]
    check(r.json().get("stage") == "created", "HTTP project stage=created")

    csv_bytes = CSV_TABLE_CLS.read_bytes()
    files = [("files", ("table_classification.csv", io.BytesIO(csv_bytes), "text/csv"))]
    mapping = {
        "features": [f"feat_{i}" for i in range(8)],
        "label_col": "label",
    }
    r = client.post(
        f"/api/projects/{pid}/dataset",
        files=files,
        data={"mapping": json.dumps(mapping), "import_kind": "replace"},
    )
    check(r.status_code == 200, f"POST dataset import -> {r.status_code} {r.text[:200]}")
    if r.status_code == 200:
        check(r.json().get("imported", 0) > 0, f"HTTP import imported={r.json().get('imported')}")

    r = client.get(f"/api/projects/{pid}")
    check(r.status_code == 200, f"GET project after import -> {r.status_code}")
    meta = r.json()
    check(meta.get("task_type") == "classification", f"HTTP meta task_type={meta.get('task_type')}")
    check(len(meta.get("channels") or []) == 8,
          f"HTTP channels=8 (got {meta.get('channels')})")
    check(len(meta.get("labels") or []) > 0,
          f"HTTP labels auto-created: n={len(meta.get('labels') or [])}")

    # Strengthened dataset info: table mode → total_rows + n_segments (real keys)
    r = client.get(f"/api/projects/{pid}/dataset")
    check(r.status_code == 200, f"GET dataset info -> {r.status_code}")
    if r.status_code == 200:
        info = r.json()
        check(info.get("total_rows", 0) > 0,
              f"dataset total_rows>0 (got {info.get('total_rows')})")
        check(info.get("n_segments", 0) > 0,
              f"dataset n_segments>0 for table mode (got {info.get('n_segments')})")
        check(len(info.get("files") or []) >= 1,
              f"dataset files listed n={len(info.get('files') or [])}")

    r = client.get(f"/api/projects/{pid}/features/config")
    check(r.status_code == 200, f"GET features/config -> {r.status_code}")

    r = client.post(f"/api/projects/{pid}/features/compute")
    check(r.status_code == 200, f"POST features/compute -> {r.status_code} {r.text[:200]}")
    if r.status_code == 200:
        cinfo = r.json()
        check(cinfo.get("n_samples") == 100,
              f"HTTP feature n_samples=100 (got {cinfo.get('n_samples')})")
        check(cinfo.get("n_features") == 8,
              f"HTTP feature n_features=8 (got {cinfo.get('n_features')})")

    r = client.get(f"/api/projects/{pid}/features/matrix")
    check(r.status_code == 200, f"GET features/matrix -> {r.status_code}")

    r = client.get(f"/api/projects/{pid}/features/scoring")
    check(r.status_code == 200, f"GET features/scoring -> {r.status_code}")
    if r.status_code == 200:
        check(len(r.json().get("ranking") or []) > 0, "scoring ranking non-empty")

    tcfg = {
        "k": 2, "n_iter": 3, "budget_s": 20,
        "metric": "f1_macro", "task_type": "classification",
        "auto_feature_select": False, "seed": 42,
    }
    r = client.post(f"/api/projects/{pid}/training", json=tcfg)
    check(r.status_code == 200, f"POST training -> {r.status_code} {r.text[:200]}")

    st = wait_training_http(client, pid, timeout_s=120)
    check(st.get("status") == "done", f"HTTP training done (got {st})")

    r = client.get(f"/api/projects/{pid}/training/leaderboard")
    check(r.status_code == 200, f"GET leaderboard -> {r.status_code}")
    cands = []
    if r.status_code == 200:
        cands = r.json().get("candidates") or []
        check(len(cands) > 0, f"HTTP leaderboard candidates n={len(cands)}")
        if cands:
            check(cands[0].get("cv_mean") is not None,
                  f"HTTP best cv_mean={cands[0].get('cv_mean')}")

    r = client.get(f"/api/projects/{pid}/training/best")
    check(r.status_code == 200, f"GET training/best -> {r.status_code}")
    if r.status_code == 200:
        check(bool(r.json().get("model_type")), f"HTTP best model_type={r.json().get('model_type')}")

    # set_best: switch to a different candidate
    if len(cands) >= 2:
        alt_id = cands[1].get("cand_id", 1)
        r = client.post(f"/api/projects/{pid}/training/set_best", json={"cand_id": alt_id})
        check(r.status_code == 200, f"POST set_best cand_id={alt_id} -> {r.status_code} {r.text[:200]}")
        if r.status_code == 200:
            check(r.json().get("cand_id") == alt_id,
                  f"set_best returned cand_id={r.json().get('cand_id')} (want {alt_id})")
        r = client.get(f"/api/projects/{pid}/training/best")
        if r.status_code == 200:
            check(r.json().get("cand_id") == alt_id,
                  f"best_info.cand_id after set_best={r.json().get('cand_id')} (want {alt_id})")

    # feature_importance
    r = client.get(f"/api/projects/{pid}/training/feature_importance")
    check(r.status_code == 200, f"GET feature_importance -> {r.status_code}")
    if r.status_code == 200:
        ranking = r.json().get("ranking") or []
        check(len(ranking) > 0, f"feature_importance ranking n={len(ranking)}")
        if ranking:
            check("feature" in ranking[0] and "importance" in ranking[0],
                  f"ranking entry has feature/importance: {list(ranking[0])}")

    r = client.get(f"/api/projects/{pid}/training/report")
    check(r.status_code == 200, f"GET training/report -> {r.status_code}")
    if r.status_code == 200:
        rep = r.json()
        check(rep.get("task_type") == "classification", "HTTP report task_type=classification")
        bm = rep.get("best_model") or {}
        mt = bm.get("metrics_test") or {}
        check("accuracy" in mt, f"HTTP report best metrics has accuracy: {list(mt)}")
        # meaningful quality metric: confusion_matrix structure
        cm = mt.get("confusion_matrix")
        check(isinstance(cm, list) and len(cm) > 0 and isinstance(cm[0], list),
              f"HTTP report confusion_matrix present: type={type(cm).__name__}")

    r = client.post(f"/api/projects/{pid}/export")
    check(r.status_code == 200, f"POST export -> {r.status_code} {r.text[:300]}")
    if r.status_code == 200:
        check(r.json().get("ok") is True,
              f"HTTP export ok=True (errors={r.json().get('errors')})")
        assert_export_artifacts(pid, expect_ts=False, via="http-table-cls")
    else:
        check(False, f"HTTP export failed (product path): {r.text[:300]}")

    r = client.get(f"/api/projects/{pid}/export")
    check(r.status_code == 200, f"GET export status -> {r.status_code}")
    zips = []
    if r.status_code == 200:
        body = r.json()
        check(body.get("exported") is True, f"HTTP export_status.exported={body.get('exported')}")
        zips = body.get("zips") or []
        check(len(zips) >= 1, f"HTTP export zips={zips}")

    # Export download endpoint
    if zips:
        r = client.get(f"/api/projects/{pid}/export/download/{zips[0]}")
        check(r.status_code == 200, f"GET export/download -> {r.status_code}")
        check(r.content[:2] == b"PK", f"download is zip (PK magic, got {r.content[:4]!r})")
    r = client.get(f"/api/projects/{pid}/export/download/no_such.zip")
    check(r.status_code in (404, 422),
          f"download missing file -> {r.status_code} (expect 404/422)")

    r = client.get(f"/api/projects/{pid}/export/dir")
    check(r.status_code == 200, f"GET export/dir -> {r.status_code}")

    # Templates API
    for kind in ("table", "timeseries"):
        r = client.get(f"/api/templates/{kind}")
        check(r.status_code == 200, f"GET /api/templates/{kind} -> {r.status_code}")
        check(r.content[:2] == b"PK", f"template {kind} is zip")
    r = client.get("/api/templates/bogus")
    check(r.status_code == 404, f"GET /api/templates/bogus -> {r.status_code} (expect 404)")

    r = client.delete(f"/api/projects/{pid}", params={"hard": "true"})
    check(r.status_code == 204, f"DELETE project hard -> {r.status_code}")


# ---------------------------------------------------------------------------
# Path C — service-layer timeseries with freq_enabled features
# ---------------------------------------------------------------------------

def pipeline_service_timeseries_freq() -> None:
    """Path C — service layer, timeseries + freq features + export."""
    print("\n=== Path C: service-layer timeseries + freq features (ts_classification.csv) ===")
    svc = ProjectService()
    body = ProjectCreate(name="prod_ts_freq_svc", mode="timeseries", task_type="classification")
    meta = svc.create(body)
    pid = meta.project_id

    content = CSV_TS_CLS.read_bytes()
    check(len(content) > 0, f"ts_classification.csv readable ({len(content)} bytes)")
    mapping = {
        "channels": ["ch0", "ch1", "ch2", "ch3", "ch4", "ch5"],
        "ts_col": "timestamp",
        "label_col": "label",
    }
    uf = MockUploadFile("ts_classification.csv", content)
    result = dataset_service.import_files(meta, [uf], mapping, import_kind="replace")
    check(result.get("imported", 0) > 0,
          f"import ts csv imported={result.get('imported')}")

    meta = svc.get(pid)
    check(meta.mode == "timeseries", f"mode=timeseries (got {meta.mode})")
    check(meta.sampling_rate > 0, f"sampling_rate inferred >0 (got {meta.sampling_rate})")
    check(len(meta.channels) == 6, f"ts channels=6 (got {meta.channels})")
    check(len(meta.labels) >= 2, f"ts labels auto-created n={len(meta.labels)}")

    info = dataset_service.dataset_info(meta)
    check(info.get("n_segments", 0) > 0,
          f"ts segments auto-created n={info.get('n_segments')}")

    # Frequency-domain features: n_per_window must be power of 2
    fcfg = FeatureConfig(
        window_len_s=0.64,
        n_per_window=64,
        step=64,
        feature_ids=["mean", "std", "spec_centroid", "dominant_freq", "band_ratio"],
        channel_features={
            "ch0": ["mean", "std", "spec_centroid", "dominant_freq", "band_ratio"],
            "ch1": ["mean", "std", "spec_centroid", "dominant_freq", "band_ratio"],
        },
        freq_enabled=True,
        freq_bands=4,
        norm="zscore",
    )
    saved = feature_service.put_config(meta, fcfg)
    check(saved.freq_enabled is True, f"freq feature config saved freq_enabled={saved.freq_enabled}")

    meta = svc.get(pid)
    cinfo = feature_service.compute(meta, saved)
    check(cinfo.get("n_samples", 0) > 0,
          f"ts freq feature n_samples>0 (got {cinfo.get('n_samples')})")
    # Product contract: band_ratio expands to band{0..freq_bands-1}_ratio per channel.
    # channel_features ch0/ch1 = [mean, std, spec_centroid, dominant_freq, band_ratio]
    #   → 4 named + 4 band ratios = 8 effective features × 2 channels = 16.
    check(cinfo.get("n_features") == 16,
          f"ts freq feature n_features=16 (band_ratio→4 bands × 2ch) "
          f"(got {cinfo.get('n_features')})")

    tcfg = TrainConfig(
        k=2, n_iter=2, budget_s=20,
        metric="f1_macro", task_type="classification",
        auto_feature_select=False, seed=42,
    )
    start = training_service.start(pid, tcfg)
    check(start.get("status") == "running", f"ts freq training started (got {start.get('status')})")
    st = wait_training(pid, timeout_s=150)
    check(st.get("status") == "done",
          f"ts freq training done (got {st.get('status')} err={st.get('error')})")

    report = training_service.get_report(pid)
    best = report.get("best_model") or {}
    check(bool(best.get("model_type")), f"ts freq best model_type={best.get('model_type')}")
    mt = best.get("metrics_test") or {}
    check("accuracy" in mt or "f1_macro" in mt,
          f"ts freq best metrics keys={list(mt)}")

    try:
        exp = export_service.export(pid)
    except AppError as e:
        check(False, f"ts freq export AppError: {getattr(e, 'detail', e)}")
        exp = None
    check(exp is not None and exp.get("ok") is True,
          f"ts freq export ok=True (errors={exp.get('errors') if exp else 'export=None'})")
    if exp is not None:
        fc = exp.get("feature_consistency") or {}
        check(fc.get("ok") is True, f"ts freq feature_consistency ok (got {fc})")
        # FFT C chain should be present (freq features exported)
        exp_dir = Path(_DATA_ROOT) / "projects" / pid / "export"
        zips = sorted(p.name for p in exp_dir.glob("*_bundle.zip"))
        if zips:
            with zipfile.ZipFile(exp_dir / zips[0]) as zf:
                cs = [n for n in zf.namelist() if n.endswith(".c") and "algo_" in n and "_feat" not in n]
                if cs:
                    src = zf.read(cs[0]).decode("utf-8", errors="replace")
                    check("algo_fft" in src or "fft" in src.lower(),
                          f"freq export C references FFT")
        assert_export_artifacts(pid, expect_ts=True, via="svc-ts-freq")

    svc.delete(pid, hard=True)


# ---------------------------------------------------------------------------
# Path D — HTTP labels + segments product surface
# ---------------------------------------------------------------------------

def pipeline_http_labels_segments() -> None:
    """Path D — HTTP labels/segments CRUD + config preservation + training/export + cancel.

    Product contracts exercised:
    - segments GET/POST/PATCH/DELETE (RLE replace, overlap 422, manual source)
    - labels GET/POST/PATCH/DELETE (dup 422, unused 204, in-use 409)
    - user-added label participates in training AND export class_map
    - segment edit invalidates feature matrix + rolls stage back, but PRESERVES
      features/config.json (BUG-7: cascade must not wipe user feature config)
    - training cancel: no-active 404; active → cancelled (or done race)
    """
    print("\n=== Path D: HTTP labels + segments product surface ===")
    app = create_app()
    client = TestClient(app)

    r = client.post("/api/projects", json={
        "name": "prod_ts_labels_http",
        "mode": "timeseries",
        "task_type": "classification",
        "sampling_rate": 100.0,
    })
    check(r.status_code == 201, f"POST ts project -> {r.status_code}")
    if r.status_code != 201:
        return
    pid = r.json()["project_id"]

    # 4 classes × 800 samples: every class keeps ≥5 windows after segment surgery
    csv_bytes = make_tiny_ts_csv(n_per_class=800, n_classes=4)
    files = [("files", ("tiny_ts.csv", io.BytesIO(csv_bytes), "text/csv"))]
    mapping = {"channels": ["ch0", "ch1"], "ts_col": "timestamp", "label_col": "label"}
    r = client.post(
        f"/api/projects/{pid}/dataset",
        files=files,
        data={"mapping": json.dumps(mapping), "import_kind": "replace"},
    )
    check(r.status_code == 200, f"POST tiny ts import -> {r.status_code} {r.text[:200]}")
    if r.status_code != 200:
        return

    # ---- labels: GET / POST / PATCH / DELETE ----
    r = client.get(f"/api/projects/{pid}/labels")
    check(r.status_code == 200, f"GET labels -> {r.status_code}")
    labels = r.json() if r.status_code == 200 else []
    check(len(labels) >= 2, f"HTTP labels listed n={len(labels)}")

    target_lid = labels[0]["label_id"] if labels else 0
    r = client.patch(
        f"/api/projects/{pid}/labels/{target_lid}",
        json={"name": "class_renamed"},
    )
    check(r.status_code == 200, f"PATCH labels rename -> {r.status_code} {r.text[:200]}")
    if r.status_code == 200:
        check(r.json().get("name") == "class_renamed",
              f"renamed label name={r.json().get('name')}")

    r = client.post(f"/api/projects/{pid}/labels", json={"name": "extra_user"})
    check(r.status_code == 201, f"POST labels add -> {r.status_code}")
    extra_lid = None
    if r.status_code == 201:
        extra_lid = r.json().get("label_id")
        check(extra_lid is not None, f"user label id={extra_lid}")

    r = client.post(f"/api/projects/{pid}/labels", json={"name": "extra_user"})
    check(r.status_code == 422, f"POST labels dup -> {r.status_code} (expect 422)")

    # unused label delete → 204
    r = client.post(f"/api/projects/{pid}/labels", json={"name": "temp_unused"})
    temp_lid = r.json().get("label_id") if r.status_code == 201 else None
    if temp_lid is not None:
        r = client.delete(f"/api/projects/{pid}/labels/{temp_lid}")
        check(r.status_code == 204, f"DELETE unused label -> {r.status_code} (expect 204)")

    # ---- segments: GET list (RLE from import) ----
    r = client.get(f"/api/projects/{pid}/segments")
    check(r.status_code == 200, f"GET segments -> {r.status_code}")
    segs = r.json() if r.status_code == 200 else []
    check(len(segs) > 0, f"segments listed n={len(segs)} (RLE from import)")
    rle_src = [s for s in segs if s.get("source") == "rle"]
    check(len(rle_src) > 0, f"RLE segments present n={len(rle_src)}")

    r = client.get(f"/api/projects/{pid}/dataset")
    dinfo = r.json() if r.status_code == 200 else {}
    flist = dinfo.get("files") or []
    check(len(flist) >= 1, f"dataset files n={len(flist)}")
    fid = flist[0]["file_id"] if flist else None
    check(fid is not None, f"file_id available: {fid}")

    # ---- segments: DELETE one RLE + POST manual segs in freed range ----
    # Surgery keeps every class ≥5 windows:
    #   classes 0-2 keep full 800-sample RLE (≈31 windows each @ n=26 step=25)
    #   class-3 RLE replaced by two 400-sample manual segs (≈15 windows each):
    #     first half → extra_user (user label used in training/export)
    #     second half → original class-3 label_id
    manual_seg = None
    if rle_src and fid and extra_lid is not None:
        del_target = rle_src[-1]
        class3_lid = del_target["label_id"]
        freed_start, freed_end = del_target["start"], del_target["end"]
        mid = (freed_start + freed_end) // 2
        r = client.delete(f"/api/projects/{pid}/segments/{del_target['id']}")
        check(r.status_code == 204, f"DELETE segment (RLE) -> {r.status_code}")

        r = client.post(f"/api/projects/{pid}/segments", json={
            "file_id": fid, "start": freed_start, "end": mid, "label_id": extra_lid,
        })
        check(r.status_code == 201, f"POST segment (user label) -> {r.status_code} {r.text[:200]}")
        if r.status_code == 201:
            manual_seg = r.json()
            check(manual_seg.get("source") == "manual",
                  f"manual seg source={manual_seg.get('source')}")

        r = client.post(f"/api/projects/{pid}/segments", json={
            "file_id": fid, "start": mid, "end": freed_end, "label_id": class3_lid,
        })
        check(r.status_code == 201,
              f"POST segment (class3 half) -> {r.status_code} {r.text[:200]}")

        r = client.post(f"/api/projects/{pid}/segments", json={
            "file_id": fid, "start": freed_start, "end": mid, "label_id": extra_lid,
        })
        check(r.status_code == 422, f"POST segment overlap -> {r.status_code} (expect 422)")

    # ---- segments: PATCH (same values — exercises update endpoint) ----
    # Keep extra_user on its segment so it participates in training/export.
    if manual_seg:
        r = client.patch(
            f"/api/projects/{pid}/segments/{manual_seg['id']}",
            json={"file_id": fid, "start": manual_seg["start"],
                  "end": manual_seg["end"], "label_id": manual_seg["label_id"]},
        )
        check(r.status_code == 200, f"PATCH segment (same values) -> {r.status_code} {r.text[:200]}")
        if r.status_code == 200:
            check(r.json().get("label_id") == manual_seg["label_id"],
                  f"patched seg label_id={r.json().get('label_id')} (want {manual_seg['label_id']})")

    # ---- feature config + compute ----
    fcfg = {
        "window_len_s": 0.25,
        "n_per_window": 25,
        "step": 25,
        "feature_ids": ["mean", "std"],
        "channel_features": {"ch0": ["mean", "std"], "ch1": ["mean", "std"]},
        "freq_enabled": False,
        "norm": "zscore",
    }
    r = client.put(f"/api/projects/{pid}/features/config", json=fcfg)
    check(r.status_code == 200, f"PUT features/config -> {r.status_code}")
    if r.status_code == 200:
        body = r.json()
        check(body.get("n_per_window") == 25 and body.get("step") == 25,
              f"PUT config echo n_per_window=25 step=25 (got {body.get('n_per_window')}/{body.get('step')})")

    r = client.post(f"/api/projects/{pid}/features/compute")
    check(r.status_code == 200, f"POST features/compute (before seg edit) -> {r.status_code}")
    n_before = r.json().get("n_samples") if r.status_code == 200 else None
    check(n_before and n_before > 0, f"features computed n_samples={n_before}")

    # every class must have ≥5 windows (CLASS_TOO_FEW precondition)
    try:
        from app.services import feature_service as _fs
        from app.services.project_service import ProjectService as _PS
        _meta = _PS().get(pid)
        _m = _fs.load_matrix(_meta)
        _counts = np.bincount(np.asarray(_m["y"]), minlength=len(_meta.labels))
        check(int(_counts.min()) >= 5,
              f"every class ≥5 windows before training: counts={_counts.tolist()} "
              f"labels={[(l.label_id, l.name) for l in _meta.labels]}")
    except Exception as e:  # noqa: BLE001
        check(False, f"window-count audit failed: {type(e).__name__}: {e}")

    # ---- segment edit effects: matrix invalidated, stage rolled back, CONFIG PRESERVED ----
    if segs:
        # PATCH class-2 RLE (untouched by surgery) with identical values → cascade invalidation
        victim = next((s for s in segs if s.get("source") == "rle" and s.get("label_id") != extra_lid), segs[0])
        r = client.patch(
            f"/api/projects/{pid}/segments/{victim['id']}",
            json={"file_id": victim["file_id"], "start": victim["start"],
                  "end": victim["end"], "label_id": victim["label_id"]},
        )
        check(r.status_code == 200, f"PATCH segment (invalidate trigger) -> {r.status_code}")

    r = client.get(f"/api/projects/{pid}/features/matrix")
    check(r.status_code == 404,
          f"features/matrix invalidated after segment edit -> {r.status_code} (expect 404)")
    r = client.get(f"/api/projects/{pid}")
    if r.status_code == 200:
        stage_after_seg = r.json().get("stage")
        check(stage_after_seg in ("labeled", "data_imported"),
              f"stage rolled back after segment edit: {stage_after_seg}")

    # BUG-7 product assertion: user feature config must survive cascade invalidation
    r = client.get(f"/api/projects/{pid}/features/config")
    check(r.status_code == 200, f"GET features/config after seg edit -> {r.status_code}")
    if r.status_code == 200:
        cfg_after = r.json()
        check(cfg_after.get("n_per_window") == 25,
              f"config survives seg edit: n_per_window={cfg_after.get('n_per_window')} (want 25, "
              f"512 means BUG-7 regression)")
        check(cfg_after.get("window_len_s") == 0.25,
              f"config survives seg edit: window_len_s={cfg_after.get('window_len_s')} (want 0.25)")
        check(cfg_after.get("feature_ids") == ["mean", "std"],
              f"config survives seg edit: feature_ids={cfg_after.get('feature_ids')}")

    # recompute must reuse preserved config → same n_samples as before edit
    r = client.post(f"/api/projects/{pid}/features/compute")
    check(r.status_code == 200, f"POST features/compute (after seg edit) -> {r.status_code}")
    if r.status_code == 200:
        n_after = r.json().get("n_samples")
        check(n_after == n_before,
              f"recomputed n_samples equals pre-edit (config preserved): {n_after} == {n_before}")

    # ---- labels DELETE in-use guard (extra_user referenced by manual seg) ----
    if extra_lid is not None:
        r = client.delete(f"/api/projects/{pid}/labels/{extra_lid}")
        check(r.status_code == 409,
              f"DELETE in-use label extra_user -> {r.status_code} (expect 409)")

    # rename second auto label before training → appears in export class_map
    if len(labels) >= 2:
        r = client.patch(
            f"/api/projects/{pid}/labels/{labels[1]['label_id']}",
            json={"name": "class_second"},
        )
        check(r.status_code == 200, f"PATCH label2 rename -> {r.status_code}")

    # ---- training: uses renamed labels + user-added label segments ----
    tcfg = {
        "k": 2, "n_iter": 3, "budget_s": 30,
        "metric": "f1_macro", "task_type": "classification",
        "auto_feature_select": False, "seed": 42,
    }
    r = client.post(f"/api/projects/{pid}/training", json=tcfg)
    check(r.status_code == 200, f"POST training (labels surface) -> {r.status_code}")
    st = wait_training_http(client, pid, timeout_s=120)
    check(st.get("status") == "done",
          f"labels-surface training done (got {st.get('status')} err={st.get('error')})")

    r = client.post(f"/api/projects/{pid}/export")
    check(r.status_code == 200, f"POST export (labels surface) -> {r.status_code} {r.text[:200]}")
    if r.status_code == 200:
        check(r.json().get("ok") is True,
              f"labels-surface export ok=True (errors={r.json().get('errors')})")
        exp_dir = Path(_DATA_ROOT) / "projects" / pid / "export"
        rep_path = exp_dir / "export_report.json"
        if rep_path.is_file():
            rep = json.loads(rep_path.read_text(encoding="utf-8"))
            cmap = rep.get("class_map") or []
            names = [c.get("name") for c in cmap]
            check("class_renamed" in names,
                  f"export class_map contains renamed label: {names}")
            check("class_second" in names,
                  f"export class_map contains second renamed label: {names}")
            check("extra_user" in names,
                  f"export class_map contains user-added label: {names}")
        assert_export_artifacts(pid, expect_ts=True, via="labels-surface")
    else:
        check(False, f"labels-surface export failed: {r.text[:200]}")

    # ---- training cancel ----
    r = client.post(f"/api/projects/{pid}/training/cancel")
    check(r.status_code == 404,
          f"cancel with no active training -> {r.status_code} (expect 404)")

    # longer run so cancel can actually hit mid-flight
    tcfg2 = {
        "k": 5, "n_iter": 400, "budget_s": 120,
        "metric": "f1_macro", "task_type": "classification",
        "auto_feature_select": False, "seed": 7,
    }
    r = client.post(f"/api/projects/{pid}/training", json=tcfg2)
    check(r.status_code == 200, f"POST training (for cancel) -> {r.status_code}")
    seen_running = False
    for _ in range(150):
        st = client.get(f"/api/projects/{pid}/training")
        body = st.json() if st.status_code == 200 else {}
        if body.get("status") == "running":
            seen_running = True
            break
        if body.get("status") in ("done", "failed", "cancelled"):
            break
        time.sleep(0.02)
    r = client.post(f"/api/projects/{pid}/training/cancel")
    if seen_running:
        check(r.status_code == 200,
              f"cancel hit active training -> {r.status_code} {r.text[:200]}")
    else:
        check(r.status_code in (200, 404),
              f"cancel after fast finish -> {r.status_code} (200/404 race acceptable)")
        print("  [INFO] training finished before cancel poll (race)")
    st = wait_training_http(client, pid, timeout_s=120)
    check(st.get("status") in ("cancelled", "done"),
          f"training after cancel -> {st.get('status')}")
    if st.get("status") == "cancelled":
        print("  [INFO] training cancelled as expected")
    else:
        print("  [INFO] training finished before cancel took effect (race, acceptable)")

    r = client.delete(f"/api/projects/{pid}", params={"hard": "true"})
    check(r.status_code == 204, f"DELETE tiny ts project -> {r.status_code}")

    # ---- config survives replace re-import (BUG-7 dataset_service fix) ----
    r = client.post("/api/projects", json={
        "name": "prod_cfg_survive",
        "mode": "timeseries",
        "task_type": "classification",
        "sampling_rate": 100.0,
    })
    if r.status_code == 201:
        pid2 = r.json()["project_id"]
        files2 = [("files", ("cfg_survive.csv", io.BytesIO(csv_bytes), "text/csv"))]
        r = client.post(
            f"/api/projects/{pid2}/dataset",
            files=files2,
            data={"mapping": json.dumps(mapping), "import_kind": "replace"},
        )
        check(r.status_code == 200, f"cfg-survive import -> {r.status_code}")
        r = client.put(f"/api/projects/{pid2}/features/config", json=fcfg)
        check(r.status_code == 200, f"cfg-survive PUT config -> {r.status_code}")
        # re-import replace → config.json must survive
        files2b = [("files", ("cfg_survive2.csv", io.BytesIO(csv_bytes), "text/csv"))]
        r = client.post(
            f"/api/projects/{pid2}/dataset",
            files=files2b,
            data={"mapping": json.dumps(mapping), "import_kind": "replace"},
        )
        check(r.status_code == 200, f"cfg-survive re-import -> {r.status_code}")
        r = client.get(f"/api/projects/{pid2}/features/config")
        if r.status_code == 200:
            cfg2 = r.json()
            check(cfg2.get("n_per_window") == 25,
                  f"config survives replace re-import: n_per_window={cfg2.get('n_per_window')} "
                  f"(want 25, 512 means BUG-7 regression in dataset_service)")
        else:
            check(False, f"GET config after re-import -> {r.status_code}")
        client.delete(f"/api/projects/{pid2}", params={"hard": "true"})


# ---------------------------------------------------------------------------
# Path E — HTTP timeseries export + project lifecycle
# ---------------------------------------------------------------------------

def pipeline_http_ts_export_and_lifecycle() -> None:
    """Path E — HTTP ts export chain + copy + trash + archive + preview + file delete."""
    print("\n=== Path E: HTTP timeseries export + project lifecycle ===")
    app = create_app()
    client = TestClient(app)

    # ---- HTTP timeseries export ----
    r = client.post("/api/projects", json={
        "name": "prod_ts_http_export",
        "mode": "timeseries",
        "task_type": "classification",
        "sampling_rate": 100.0,
    })
    check(r.status_code == 201, f"POST ts project -> {r.status_code}")
    if r.status_code != 201:
        return
    pid = r.json()["project_id"]

    csv_bytes = make_tiny_ts_csv(n_per_class=400, n_classes=3)
    files = [("files", ("ts_export.csv", io.BytesIO(csv_bytes), "text/csv"))]
    mapping = {"channels": ["ch0", "ch1"], "ts_col": "timestamp", "label_col": "label"}
    r = client.post(
        f"/api/projects/{pid}/dataset",
        files=files,
        data={"mapping": json.dumps(mapping), "import_kind": "replace"},
    )
    check(r.status_code == 200, f"POST ts import -> {r.status_code}")

    fcfg = {
        "window_len_s": 0.5,
        "n_per_window": 50,
        "step": 50,
        "feature_ids": ["mean", "std", "rms", "ptp"],
        "channel_features": {
            "ch0": ["mean", "std", "rms", "ptp"],
            "ch1": ["mean", "std", "rms", "ptp"],
        },
        "freq_enabled": False,
        "norm": "zscore",
    }
    r = client.put(f"/api/projects/{pid}/features/config", json=fcfg)
    check(r.status_code == 200, f"PUT features/config -> {r.status_code}")
    r = client.post(f"/api/projects/{pid}/features/compute")
    check(r.status_code == 200, f"POST features/compute -> {r.status_code}")

    tcfg = {
        "k": 2, "n_iter": 2, "budget_s": 20,
        "metric": "f1_macro", "task_type": "classification",
        "auto_feature_select": False, "seed": 42,
    }
    r = client.post(f"/api/projects/{pid}/training", json=tcfg)
    check(r.status_code == 200, f"POST training -> {r.status_code}")
    st = wait_training_http(client, pid, timeout_s=120)
    check(st.get("status") == "done", f"ts HTTP training done (got {st})")

    r = client.post(f"/api/projects/{pid}/export")
    check(r.status_code == 200, f"POST ts export -> {r.status_code} {r.text[:300]}")
    if r.status_code == 200:
        check(r.json().get("ok") is True,
              f"ts HTTP export ok=True (errors={r.json().get('errors')})")
        assert_export_artifacts(pid, expect_ts=True, via="http-ts-export")
    else:
        check(False, f"ts HTTP export failed: {r.text[:300]}")

    # download the ts export zip
    r = client.get(f"/api/projects/{pid}/export")
    zips = r.json().get("zips") if r.status_code == 200 else []
    if zips:
        r = client.get(f"/api/projects/{pid}/export/download/{zips[0]}")
        check(r.status_code == 200 and r.content[:2] == b"PK",
              f"ts export download ok (status={r.status_code}, magic={r.content[:2]!r})")

    # ---- project copy ----
    r = client.post(f"/api/projects/{pid}/copy")
    check(r.status_code == 201, f"POST project copy -> {r.status_code} {r.text[:200]}")
    copy_pid = None
    if r.status_code == 201:
        copy_pid = r.json().get("project_id")
        check(copy_pid and copy_pid != pid, f"copy has new project_id: {copy_pid}")
        check("副本" in (r.json().get("name") or ""),
              f"copy name has 副本: {r.json().get('name')}")
        # copy should carry dataset
        r2 = client.get(f"/api/projects/{copy_pid}/dataset")
        if r2.status_code == 200:
            check(r2.json().get("total_rows", 0) > 0,
                  f"copy dataset total_rows={r2.json().get('total_rows')}")

    # ---- soft delete + trash restore ----
    r = client.delete(f"/api/projects/{pid}")  # soft by default
    check(r.status_code == 204, f"DELETE project (soft) -> {r.status_code}")
    r = client.get("/api/projects")
    if r.status_code == 200:
        pids = [p.get("project_id") for p in r.json()]
        check(pid not in pids, f"soft-deleted project not in list")
    r = client.get("/api/projects/trash")
    check(r.status_code == 200, f"GET trash -> {r.status_code}")
    trash_id = None
    if r.status_code == 200:
        for t in r.json():
            if t.get("project_id") == pid or (t.get("trash_id") or "").startswith(pid):
                trash_id = t.get("trash_id")
                break
        check(trash_id is not None, f"trash entry found: trash_id={trash_id}")
    if trash_id:
        r = client.post(f"/api/projects/trash/{trash_id}/restore")
        check(r.status_code == 200, f"POST trash restore -> {r.status_code} {r.text[:200]}")
        if r.status_code == 200:
            check(r.json().get("project_id") == pid,
                  f"restored project_id={r.json().get('project_id')} (want {pid})")

    # ---- archive export / import ----
    r = client.post(f"/api/projects/{pid}/archive")
    check(r.status_code == 200, f"POST archive -> {r.status_code}")
    archive_bytes = b""
    if r.status_code == 200:
        archive_bytes = r.content
        check(archive_bytes[:2] == b"PK", f"archive is zip (PK magic)")
    if archive_bytes:
        r = client.post(
            "/api/projects/import",
            files=[("file", ("project.bosml", io.BytesIO(archive_bytes), "application/zip"))],
        )
        check(r.status_code == 201, f"POST import archive -> {r.status_code} {r.text[:200]}")
        if r.status_code == 201:
            imported_meta = r.json().get("meta") or {}
            imported_pid = imported_meta.get("project_id")
            check(imported_pid and imported_pid != pid,
                  f"imported project new id: {imported_pid}")
            # cleanup imported copy
            if imported_pid:
                client.delete(f"/api/projects/{imported_pid}", params={"hard": "true"})

    # ---- dataset preview ----
    r = client.post(
        f"/api/projects/{pid}/dataset/preview",
        files=[("file", ("preview.csv", io.BytesIO(csv_bytes), "text/csv"))],
    )
    check(r.status_code == 200, f"POST dataset/preview -> {r.status_code}")
    if r.status_code == 200:
        pv = r.json()
        check("ch0" in (pv.get("columns") or []),
              f"preview columns include ch0: {pv.get('columns')}")
        check(len(pv.get("rows") or []) > 0, f"preview rows n={len(pv.get('rows') or [])}")
        check((pv.get("inferred") or {}).get("ch0") == "numeric",
              f"preview inferred ch0=numeric: {(pv.get('inferred') or {}).get('ch0')}")

    # ---- file delete ----
    r = client.get(f"/api/projects/{pid}/dataset")
    flist = r.json().get("files") if r.status_code == 200 else []
    if flist:
        fid = flist[0]["file_id"]
        r = client.delete(f"/api/projects/{pid}/dataset/file/{fid}")
        check(r.status_code == 200, f"DELETE dataset/file -> {r.status_code} {r.text[:200]}")
        r = client.get(f"/api/projects/{pid}/dataset")
        if r.status_code == 200:
            check(len(r.json().get("files") or []) == 0,
                  f"files empty after delete: {r.json().get('files')}")
            check(r.json().get("total_rows", -1) == 0,
                  f"total_rows=0 after delete: {r.json().get('total_rows')}")

    # cleanup
    r = client.delete(f"/api/projects/{pid}", params={"hard": "true"})
    check(r.status_code == 204, f"DELETE ts export project hard -> {r.status_code}")
    if copy_pid:
        client.delete(f"/api/projects/{copy_pid}", params={"hard": "true"})


# ---------------------------------------------------------------------------
# Path F — service-layer table classification export
# ---------------------------------------------------------------------------

def pipeline_service_table_classification_export() -> None:
    """Path F — service-layer table classification export (complements Path B)."""
    print("\n=== Path F: service-layer table classification export ===")
    svc = ProjectService()
    body = ProjectCreate(name="prod_tbl_cls_svc", mode="table", task_type="classification")
    meta = svc.create(body)
    pid = meta.project_id

    content = CSV_TABLE_CLS.read_bytes()
    features = [f"feat_{i}" for i in range(8)]
    mapping = {"features": features, "label_col": "label"}
    uf = MockUploadFile("table_classification.csv", content)
    result = dataset_service.import_files(meta, [uf], mapping, import_kind="replace")
    check(result.get("imported", 0) > 0,
          f"import table cls imported={result.get('imported')}")

    meta = svc.get(pid)
    cfg = feature_service.get_config(meta)
    info = feature_service.compute(meta, cfg)
    check(info.get("n_samples") == 100, f"n_samples=100 (got {info.get('n_samples')})")

    tcfg = TrainConfig(
        k=2, n_iter=2, budget_s=15,
        metric="f1_macro", task_type="classification",
        auto_feature_select=False, seed=42,
    )
    training_service.start(pid, tcfg)
    st = wait_training(pid, timeout_s=120)
    check(st.get("status") == "done", f"svc tbl-cls training done (got {st})")

    try:
        exp = export_service.export(pid)
    except AppError as e:
        check(False, f"svc tbl-cls export AppError: {getattr(e, 'detail', e)}")
        exp = None
    check(exp is not None and exp.get("ok") is True,
          f"svc tbl-cls export ok=True (errors={exp.get('errors') if exp else 'None'})")
    if exp is not None:
        pc = exp.get("predict_consistency") or {}
        check(pc.get("id_match_rate") == 1.0,
              f"svc tbl-cls id_match_rate=1.0 (got {pc.get('id_match_rate')})")
        assert_export_artifacts(pid, expect_ts=False, via="svc-tbl-cls")
        meta = svc.get(pid)
        check(meta.stage == "exported", f"svc tbl-cls stage=exported (got {meta.stage})")

    svc.delete(pid, hard=True)


# ---------------------------------------------------------------------------

def _run_all_paths() -> None:
    pipeline_service_regression()
    pipeline_http_table_classification()
    pipeline_service_timeseries_freq()
    pipeline_http_labels_segments()
    pipeline_http_ts_export_and_lifecycle()
    pipeline_service_table_classification_export()


def main() -> int:
    print(f"AUTOML_DATA_ROOT={_DATA_ROOT}")
    print(f"datasets: {DATASETS}")
    for p in (CSV_REGRESSION, CSV_TABLE_CLS, CSV_TS_CLS):
        print(f"  {p.name}: exists={p.is_file()} size={p.stat().st_size if p.is_file() else 0}")

    t0 = time.time()
    try:
        _run_all_paths()
    except Exception as e:  # noqa: BLE001 — surface unexpected crashes as test failures
        import traceback
        traceback.print_exc()
        check(False, f"unhandled exception: {type(e).__name__}: {e}")
    elapsed = time.time() - t0

    print("\n" + "=" * 60)
    print(f"Product AutoML pipeline: PASS={PASS} FAIL={FAIL} elapsed={elapsed:.1f}s")
    if ERRORS:
        print("Failures:")
        for e in ERRORS:
            print(f"  - {e}")
    print("=" * 60)
    return 0 if FAIL == 0 else 1


# ---- pytest entry (thin wrappers; script mode uses main()) ----

def test_product_automl_pipeline_all_paths():
    """Run all product paths once. All paths must pass (product-grade)."""
    rc = main()
    assert rc == 0, (
        f"product AutoML pipeline had failures: PASS={PASS} FAIL={FAIL} errors={ERRORS}"
    )


if __name__ == "__main__":
    sys.exit(main())
