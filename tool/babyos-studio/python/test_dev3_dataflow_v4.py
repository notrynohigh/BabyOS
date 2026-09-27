#!/usr/bin/env python3
"""Dev-3: Data Flow & Multi-file Tester for BabyOS AutoML backend - Round 4.

Data sizing math:
  window_len_s=0.3, sampling_rate=200Hz -> 60 samples/window
  step=30 samples
  windows_per_segment = floor((seg_rows - 60) / 30) + 1
  Need >=3 windows per segment for k=2 CV -> seg_rows >= 60 + 2*30 = 120

  file1: walk 120 rows + run 120 rows = 240 rows, 3+3=6 windows
  file2: sit 150 rows = 4 windows
  file3: walk 120 rows + run 120 rows = 240 rows, 3+3=6 windows
  total: 16 windows across 3 classes in 3 groups -> proper stratified CV
"""

import httpx
import time
import json
import sys
import traceback

BASE = "http://127.0.0.1:18080"
RESULTS = []
PROJECT_ID = None
CLIENT = None


def log(step, endpoint, expected, got, detail, passed):
    RESULTS.append({
        "step": step, "endpoint": endpoint,
        "expected": expected, "got": got,
        "detail": detail, "passed": passed
    })
    status = "PASS" if passed else "FAIL"
    print(f"  [{status}] Step {step}: {endpoint} -> {got} (expected {expected}) {detail}")


def file1_csv():
    """240 rows, 3 channels a,b,c + label. walk 0-119, run 120-239."""
    lines = ["a,b,c,label"]
    for i in range(120):
        lines.append(f"{i},{i*2},{i*3},walk")
    for i in range(120, 240):
        lines.append(f"{i},{i*2},{i*3},run")
    return "\n".join(lines)


def file2_csv():
    """150 rows, 3 channels a,b,c + label. All sit."""
    lines = ["a,b,c,label"]
    for i in range(150):
        lines.append(f"{i + 500},{(i+500)*2},{(i+500)*3},sit")
    return "\n".join(lines)


def file3_csv():
    """240 rows, 3 channels a,b,c + label. walk 0-119, run 120-239 (different offsets).
    Needed for >=3 groups so scoring CV has multiple classes per fold."""
    lines = ["a,b,c,label"]
    for i in range(120):
        lines.append(f"{i+800},{(i+800)*2},{(i+800)*3},walk")
    for i in range(120, 240):
        lines.append(f"{i+800},{(i+800)*2},{(i+800)*3},run")
    return "\n".join(lines)


def cleanup():
    """Force-delete the project if it still exists."""
    global PROJECT_ID, CLIENT
    if PROJECT_ID and CLIENT:
        try:
            r = CLIENT.delete(f"/api/projects/{PROJECT_ID}", params={"hard": "true"})
            print(f"  [CLEANUP] DELETE project {PROJECT_ID}: {r.status_code}")
        except Exception as e:
            print(f"  [CLEANUP] Error: {e}")


def label_all_segments(pid):
    """PATCH each segment to trigger stage transition to 'labeled'."""
    r = CLIENT.get(f"/api/projects/{pid}/segments")
    data = r.json()
    segs = data if isinstance(data, list) else data.get("segments", [])
    patched = 0
    for seg in segs:
        body = {
            "file_id": seg["file_id"],
            "start": seg["start"],
            "end": seg["end"],
            "label_id": seg["label_id"],
        }
        r2 = CLIENT.patch(f"/api/projects/{pid}/segments/{seg['id']}", json=body)
        if r2.status_code == 200:
            patched += 1
    print(f"    [INFO] Patched {patched}/{len(segs)} segments to 'labeled'")
    return patched > 0


def run_tests():
    global PROJECT_ID, CLIENT
    CLIENT = httpx.Client(base_url=BASE, timeout=30.0)

    try:
        # ── Step 1: Create project ──
        step = 1
        try:
            r = CLIENT.post("/api/projects", json={
                "name": "dev3_data", "mode": "timeseries", "sampling_rate": 200.0
            })
            passed = r.status_code == 201
            detail = ""
            if passed:
                PROJECT_ID = r.json().get("project_id")
                detail = f"pid={PROJECT_ID}"
            else:
                detail = r.text[:300]
            log(step, "POST /api/projects", 201, r.status_code, detail, passed)
        except Exception as e:
            log(step, "POST /api/projects", 201, "ERR", str(e), False)
            print("FATAL: cannot create project"); return

        # ── Step 2: Verify sampling_rate=200.0 ──
        step = 2
        try:
            r = CLIENT.get(f"/api/projects/{PROJECT_ID}")
            data = r.json()
            sr = data.get("sampling_rate")
            passed = r.status_code == 200 and sr == 200.0
            log(step, "GET /api/projects/{pid}", 200, r.status_code,
                f"sampling_rate={sr}", passed)
        except Exception as e:
            log(step, "GET /api/projects/{pid}", 200, "ERR", str(e), False)

        # ── Step 3: Import file1 (replace) ──
        step = 3
        try:
            csv_bytes = file1_csv().encode()
            mapping = json.dumps({"channels": ["a", "b", "c"], "label_col": "label"})
            r = CLIENT.post(
                f"/api/projects/{PROJECT_ID}/dataset",
                files=[("files", ("file1.csv", csv_bytes, "text/csv"))],
                data={"mapping": mapping, "import_kind": "replace"},
            )
            passed = r.status_code == 200
            detail = r.text[:300]
            log(step, "POST /dataset (file1)", 200, r.status_code, detail, passed)
        except Exception as e:
            log(step, "POST /dataset (file1)", 200, "ERR", str(e), False)

        # ── Step 4: GET dataset -> 1 file, 240 rows ──
        step = 4
        try:
            r = CLIENT.get(f"/api/projects/{PROJECT_ID}/dataset")
            data = r.json()
            files = data.get("files", [])
            total_rows = data.get("total_rows", 0)
            n_files = len(files)
            passed = r.status_code == 200 and n_files == 1 and total_rows == 240
            log(step, "GET /dataset", 200, r.status_code,
                f"files={n_files}, rows={total_rows}", passed)
        except Exception as e:
            log(step, "GET /dataset", 200, "ERR", str(e), False)

        # ── Step 5: GET segments -> 2 segments (walk + run) ──
        step = 5
        try:
            r = CLIENT.get(f"/api/projects/{PROJECT_ID}/segments")
            data = r.json()
            segs = data if isinstance(data, list) else data.get("segments", [])
            n_segs = len(segs)
            passed = r.status_code == 200 and n_segs == 2
            detail = f"segments={n_segs}"
            if n_segs > 0:
                for s in segs:
                    detail += f" [{s.get('start', '?')}-{s.get('end', '?')}]"
            log(step, "GET /segments", 200, r.status_code, detail, passed)
        except Exception as e:
            log(step, "GET /segments", 200, "ERR", str(e), False)

        # Label segments to advance stage to 'labeled'
        print("    [INFO] Labeling segments to advance stage...")
        label_all_segments(PROJECT_ID)

        # ── Step 6: Import file2 (append) ──
        step = 6
        try:
            csv_bytes = file2_csv().encode()
            mapping = json.dumps({"channels": ["a", "b", "c"], "label_col": "label"})
            r = CLIENT.post(
                f"/api/projects/{PROJECT_ID}/dataset",
                files=[("files", ("file2.csv", csv_bytes, "text/csv"))],
                data={"mapping": mapping, "import_kind": "append"},
            )
            passed = r.status_code == 200
            detail = r.text[:300]
            log(step, "POST /dataset (file2 append)", 200, r.status_code, detail, passed)
        except Exception as e:
            log(step, "POST /dataset (file2 append)", 200, "ERR", str(e), False)

        # ── Step 7: GET dataset -> 2 files, 390 rows ──
        step = 7
        try:
            r = CLIENT.get(f"/api/projects/{PROJECT_ID}/dataset")
            data = r.json()
            files = data.get("files", [])
            total_rows = data.get("total_rows", 0)
            n_files = len(files)
            passed = r.status_code == 200 and n_files == 2 and total_rows == 390
            log(step, "GET /dataset (after append)", 200, r.status_code,
                f"files={n_files}, rows={total_rows}", passed)
        except Exception as e:
            log(step, "GET /dataset (after append)", 200, "ERR", str(e), False)

        # ── Setup: Import file3 (3rd group needed for scoring CV and training) ──
        try:
            csv_bytes = file3_csv().encode()
            mapping = json.dumps({"channels": ["a", "b", "c"], "label_col": "label"})
            r = CLIENT.post(
                f"/api/projects/{PROJECT_ID}/dataset",
                files=[("files", ("file3.csv", csv_bytes, "text/csv"))],
                data={"mapping": mapping, "import_kind": "append"},
            )
            print(f"    [INFO] Imported file3: {r.status_code}, rows={r.json().get('results',[{}])[0].get('rows','?')}")
            # Re-label all segments after 3rd file import
            label_all_segments(PROJECT_ID)
        except Exception as e:
            print(f"    [INFO] file3 import failed: {e}")

        # ── Step 8: PUT features/config ──
        step = 8
        try:
            config = {
                "window_len_s": 0.3,
                "n_per_window": 60,
                "step": 30,
                "feature_ids": ["mean", "std"],
                "freq_enabled": False,
                "norm": "zscore"
            }
            r = CLIENT.put(f"/api/projects/{PROJECT_ID}/features/config", json=config)
            passed = r.status_code == 200
            detail = r.text[:300]
            log(step, "PUT /features/config", 200, r.status_code, detail, passed)
        except Exception as e:
            log(step, "PUT /features/config", 200, "ERR", str(e), False)

        # ── Step 9: POST /features/compute ──
        step = 9
        try:
            r = CLIENT.post(f"/api/projects/{PROJECT_ID}/features/compute")
            passed = r.status_code == 200
            detail = ""
            if passed:
                data = r.json()
                detail = f"n_samples={data.get('n_samples','?')}, n_features={data.get('n_features','?')}"
            else:
                detail = r.text[:300]
            log(step, "POST /features/compute", 200, r.status_code, detail, passed)
        except Exception as e:
            log(step, "POST /features/compute", 200, "ERR", str(e), False)

        # ── Step 10: GET /features/matrix ──
        step = 10
        try:
            r = CLIENT.get(f"/api/projects/{PROJECT_ID}/features/matrix")
            passed = r.status_code == 200
            detail = ""
            if passed:
                data = r.json()
                detail = f"n_samples={data.get('n_samples','?')}, n_features={data.get('n_features','?')}"
            else:
                detail = r.text[:300]
            log(step, "GET /features/matrix", 200, r.status_code, detail, passed)
        except Exception as e:
            log(step, "GET /features/matrix", 200, "ERR", str(e), False)

        # ── Step 11: GET /features/scoring?method=f_test ──
        step = 11
        try:
            r = CLIENT.get(f"/api/projects/{PROJECT_ID}/features/scoring",
                           params={"method": "f_test"})
            passed = r.status_code == 200
            detail = ""
            if passed:
                data = r.json()
                ranking = data.get("ranking", [])
                detail = f"ranking_len={len(ranking)}"
            else:
                detail = r.text[:300]
            log(step, "GET /features/scoring?method=f_test", 200, r.status_code, detail, passed)
        except Exception as e:
            log(step, "GET /features/scoring?method=f_test", 200, "ERR", str(e), False)

        # ── Step 12: GET /features/scoring?method=mutual_info ──
        step = 12
        try:
            r = CLIENT.get(f"/api/projects/{PROJECT_ID}/features/scoring",
                           params={"method": "mutual_info"})
            passed = r.status_code == 200
            detail = ""
            if passed:
                data = r.json()
                ranking = data.get("ranking", [])
                detail = f"ranking_len={len(ranking)}"
            else:
                detail = r.text[:300]
            log(step, "GET /features/scoring?method=mutual_info", 200, r.status_code, detail, passed)
        except Exception as e:
            log(step, "GET /features/scoring?method=mutual_info", 200, "ERR", str(e), False)

        # ── Step 13: GET /features/scoring?method=variance ──
        step = 13
        try:
            r = CLIENT.get(f"/api/projects/{PROJECT_ID}/features/scoring",
                           params={"method": "variance"})
            passed = r.status_code == 200
            detail = ""
            if passed:
                data = r.json()
                ranking = data.get("ranking", [])
                detail = f"ranking_len={len(ranking)}"
            else:
                detail = r.text[:300]
            log(step, "GET /features/scoring?method=variance", 200, r.status_code, detail, passed)
        except Exception as e:
            log(step, "GET /features/scoring?method=variance", 200, "ERR", str(e), False)

        # ── Step 14: POST /training ──
        step = 14
        try:
            r = CLIENT.post(f"/api/projects/{PROJECT_ID}/training", json={
                "n_iter": 2, "budget_s": 30, "k": 2
            })
            passed = r.status_code == 200
            detail = ""
            if passed:
                data = r.json()
                detail = f"status={data.get('status')}"
            else:
                detail = r.text[:300]
            log(step, "POST /training", 200, r.status_code, detail, passed)
        except Exception as e:
            log(step, "POST /training", 200, "ERR", str(e), False)

        # ── Step 15: Wait 10s, poll training ──
        step = 15
        time.sleep(10)
        try:
            r = CLIENT.get(f"/api/projects/{PROJECT_ID}/training")
            data = r.json()
            status = data.get("status", "unknown")
            done = data.get("done", 0)
            total = data.get("total", 0)
            error = data.get("error", "")
            passed = r.status_code == 200 and status in ("done", "running", "failed", "cancelled", "interrupted")
            detail = f"status={status}, done={done}/{total}"
            if error:
                detail += f", error={error[:200]}"
            log(step, "GET /training (poll)", 200, r.status_code, detail, passed)
        except Exception as e:
            log(step, "GET /training (poll)", 200, "ERR", str(e), False)

        # ── Step 16: GET /training/leaderboard ──
        step = 16
        try:
            r = CLIENT.get(f"/api/projects/{PROJECT_ID}/training/leaderboard")
            passed = r.status_code == 200
            detail = ""
            if passed:
                data = r.json()
                n = len(data.get("candidates", []))
                detail = f"entries={n}"
            else:
                detail = r.text[:300]
            log(step, "GET /training/leaderboard", 200, r.status_code, detail, passed)
        except Exception as e:
            log(step, "GET /training/leaderboard", 200, "ERR", str(e), False)

        # ── Step 17: POST /export ──
        step = 17
        try:
            r = CLIENT.post(f"/api/projects/{PROJECT_ID}/export", timeout=120.0)
            passed = r.status_code in (200, 201, 422)
            detail = ""
            if not passed:
                detail = r.text[:500]
            else:
                try:
                    data = r.json()
                    ok = data.get("ok", False)
                    errors = data.get("errors", [])
                    detail = f"ok={ok}, errors={errors[:3]}" if errors else f"ok={ok}"
                except Exception:
                    detail = r.text[:200]
            log(step, "POST /export", "200|422", r.status_code, detail, passed)
        except Exception as e:
            log(step, "POST /export", "200|422", "ERR", str(e), False)

        # ── Step 18: DELETE ?hard=true ──
        step = 18
        try:
            r = CLIENT.delete(f"/api/projects/{PROJECT_ID}", params={"hard": "true"})
            passed = r.status_code == 204
            detail = r.text[:100] if r.text else ""
            log(step, "DELETE ?hard=true", 204, r.status_code, detail, passed)
            if passed:
                PROJECT_ID = None  # prevent double-delete in cleanup
        except Exception as e:
            log(step, "DELETE ?hard=true", 204, "ERR", str(e), False)

    except Exception as e:
        print(f"FATAL: {e}")
        traceback.print_exc()
    finally:
        cleanup()
        if CLIENT:
            CLIENT.close()

    passed = sum(1 for r in RESULTS if r["passed"])
    failed = sum(1 for r in RESULTS if not r["passed"])
    bugs = [{"step": r["step"], "endpoint": r["endpoint"],
             "expected": r["expected"], "got": r["got"],
             "detail": r["detail"]}
            for r in RESULTS if not r["passed"]]

    report = {
        "agent": "dev3",
        "round": 4,
        "passed": passed,
        "failed": failed,
        "bugs": bugs
    }
    print("\n" + json.dumps(report, indent=2))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(run_tests())
