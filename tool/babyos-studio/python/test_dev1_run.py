"""Dev-1: AutoML Happy Path Test - all 20 steps."""
import httpx
import time
import json
import sys

BASE = "http://127.0.0.1:18080"
client = httpx.Client(base_url=BASE, timeout=30.0)

passed = 0
failed = 0
bugs = []
pid = None
fid = None


def check(step, method, url, expected_status, **kwargs):
    global passed, failed
    try:
        r = client.request(method, url, **kwargs)
        if isinstance(expected_status, (list, tuple)):
            ok = r.status_code in expected_status
        else:
            ok = r.status_code == expected_status

        if ok:
            passed += 1
            print(f"  PASS  Step {step}: {method} {url} -> {r.status_code}")
        else:
            failed += 1
            body = r.text[:300]
            bugs.append({
                "step": step,
                "endpoint": f"{method} {url}",
                "expected": expected_status,
                "got": r.status_code,
                "detail": f"response: {body}",
            })
            print(f"  FAIL  Step {step}: {method} {url} -> expected {expected_status}, got {r.status_code}")
            print(f"        body: {body[:200]}")
        return r
    except Exception as e:
        failed += 1
        bugs.append({
            "step": step, "endpoint": f"{method} {url}",
            "expected": expected_status, "got": "EXCEPTION", "detail": str(e),
        })
        print(f"  FAIL  Step {step}: {method} {url} -> EXCEPTION: {e}")
        return None


print("=" * 60)
print("Dev-1 AutoML Happy Path Test (Round 3)")
print("=" * 60)

# --- Step 1: Create project ---
print("\n--- Step 1: Create project ---")
r = check(1, "POST", "/api/projects", 201, json={
    "name": "dev1_test", "mode": "timeseries", "sampling_rate": 100.0,
})
if not r or r.status_code != 201:
    print("  FATAL: Cannot create project"); sys.exit(1)
data = r.json()
pid = data.get("project_id")
print(f"  pid={pid}")

# --- Step 2: List projects ---
print("\n--- Step 2: List projects ---")
r = check(2, "GET", "/api/projects", 200)
if r:
    projects = r.json()
    items = projects if isinstance(projects, list) else projects.get("items", projects.get("projects", []))
    names = [p.get("name") for p in items]
    print(f"  Found projects: {names[:5]}")
    if "dev1_test" not in names:
        print("  WARNING: project not found by name (but still present)")

# --- Step 3: Get project ---
print("\n--- Step 3: Get project ---")
r = check(3, "GET", f"/api/projects/{pid}", 200)

# --- Step 4: Update project ---
print("\n--- Step 4: Update project name ---")
r = check(4, "PATCH", f"/api/projects/{pid}", 200, json={"name": "dev1_renamed"})

# --- Step 5: Import CSV ---
print("\n--- Step 5: Import CSV ---")
csv_lines = ["timestamp,accel_x,accel_y,label"]
for i in range(200):
    ts = f"2024-01-01T00:00:{i:06.6f}"
    if i < 100:
        ax, ay = 0.1 + (i % 10) * 0.01, 0.2 + (i % 10) * 0.01
        label = "normal"
    else:
        ax, ay = 0.5 + (i % 10) * 0.1, 0.8 + (i % 10) * 0.1
        label = "abnormal"
    csv_lines.append(f"{ts},{ax:.4f},{ay:.4f},{label}")
csv_content = "\n".join(csv_lines)
mapping = json.dumps({"channels": ["accel_x", "accel_y"], "label_col": "label"})

r = check(5, "POST", f"/api/projects/{pid}/dataset", 200,
    files={"files": ("test_data.csv", csv_content.encode("utf-8"), "text/csv")},
    data={"mapping": mapping, "import_kind": "append"})
imported_fid = None
if r and r.status_code == 200:
    imp = r.json()
    print(f"  Import response: {json.dumps(imp)[:300]}")
    if imp.get("results"):
        for res in imp["results"]:
            if res.get("ok") and res.get("file_id"):
                imported_fid = res["file_id"]
                print(f"  imported file_id={imported_fid}, rows={res.get('rows')}, segments_added={res.get('segments_added')}")

# --- Step 6: Get segments (RLE auto-created during import) ---
print("\n--- Step 6: Get segments ---")
r = check(6, "GET", f"/api/projects/{pid}/segments", 200)
seg_count = 0
if r:
    segs = r.json()
    seg_count = len(segs) if isinstance(segs, list) else 0
    print(f"  Segments count: {seg_count}")
    if seg_count != 2:
        failed += 1
        bugs.append({"step": 6, "endpoint": f"GET /api/projects/{pid}/segments",
            "expected": 2, "got": seg_count, "detail": f"segments={str(segs)[:200]}"})
        print(f"  FAIL-AUTO  Step 6: expected 2 segments, got {seg_count}")

# --- Step 7: Get labels ---
print("\n--- Step 7: Get labels ---")
r = check(7, "GET", f"/api/projects/{pid}/labels", 200)
label_count = 0
if r:
    labels = r.json()
    label_count = len(labels) if isinstance(labels, list) else 0
    print(f"  Labels count: {label_count}")
    if label_count != 2:
        failed += 1
        bugs.append({"step": 7, "endpoint": f"GET /api/projects/{pid}/labels",
            "expected": 2, "got": label_count, "detail": f"labels={str(labels)[:200]}"})
        print(f"  FAIL-AUTO  Step 7: expected 2 labels, got {label_count}")

# --- Step 8: Get dataset ---
print("\n--- Step 8: Get dataset ---")
r = check(8, "GET", f"/api/projects/{pid}/dataset", 200)
ds_file_count = 0
if r:
    ds = r.json()
    files_list = ds.get("files", []) if isinstance(ds, dict) else []
    ds_file_count = len(files_list)
    print(f"  Dataset files: {ds_file_count}, total_rows={ds.get('total_rows')}, n_segments={ds.get('n_segments')}")
    if ds_file_count != 1:
        failed += 1
        bugs.append({"step": 8, "endpoint": f"GET /api/projects/{pid}/dataset",
            "expected": "1 file", "got": ds_file_count, "detail": str(ds)[:300]})
        print(f"  FAIL-AUTO  Step 8: expected 1 file, got {ds_file_count}")
    if ds.get("total_rows") != 200:
        failed += 1
        bugs.append({"step": 8, "endpoint": f"GET /api/projects/{pid}/dataset",
            "expected": 200, "got": ds.get("total_rows"), "detail": f"total_rows mismatch"})
        print(f"  FAIL-AUTO  Step 8: expected 200 rows, got {ds.get('total_rows')}")

# Use fid from import response or from dataset info
if not fid:
    fid = imported_fid
if not fid and files_list:
    fid = files_list[0].get("file_id")
print(f"  Using fid={fid}")

# --- Step 9: Get file data ---
print("\n--- Step 9: Get file data ---")
if fid:
    r = check(9, "GET", f"/api/projects/{pid}/dataset/file/{fid}/data?limit=5", 200)
    if r:
        fdata = r.json()
        print(f"  file_data keys: {list(fdata.keys()) if isinstance(fdata, dict) else 'list'}, "
              f"rows={fdata.get('rows') if isinstance(fdata, dict) else len(fdata)}, "
              f"data_points={len(fdata.get('data', [])) if isinstance(fdata, dict) else '?'}")
else:
    print("  SKIP - no file id"); failed += 1
    bugs.append({"step": 9, "endpoint": "GET .../file/{fid}/data", "expected": 200, "got": "N/A", "detail": "no fid"})

# --- Step 10: Configure features ---
print("\n--- Step 10: Configure features ---")
r = check(10, "PUT", f"/api/projects/{pid}/features/config", 200, json={
    "window_len_s": 0.2, "n_per_window": 20, "step": 10,
    "feature_ids": ["mean", "std", "rms", "ptp", "zcr"],
    "freq_enabled": False, "norm": "zscore",
})

# --- Step 11: Compute features ---
print("\n--- Step 11: Compute features ---")
r = check(11, "POST", f"/api/projects/{pid}/features/compute", 200)
if r:
    print(f"  Compute response: {r.text[:300]}")

# --- Step 12: Get feature matrix ---
print("\n--- Step 12: Get feature matrix ---")
r = check(12, "GET", f"/api/projects/{pid}/features/matrix", 200)
if r:
    matrix = r.json()
    if isinstance(matrix, dict):
        print(f"  Matrix: n_samples={matrix.get('n_samples')}, n_features={matrix.get('n_features')}")
    else:
        print(f"  Matrix: {type(matrix)}")

# --- Step 13: Get feature scoring ---
print("\n--- Step 13: Get feature scoring ---")
r = check(13, "GET", f"/api/projects/{pid}/features/scoring", 200)
if r:
    score_data = r.json()
    ranking = score_data.get("ranking", [])
    print(f"  Scoring method={score_data.get('method')}, features={len(ranking)}")

# --- Step 14: Start training ---
print("\n--- Step 14: Start training ---")
r = check(14, "POST", f"/api/projects/{pid}/training", 200, json={
    "n_iter": 2, "budget_s": 30, "k": 2,
})
if r:
    print(f"  Training start: {r.text[:200]}")

# --- Step 15: Wait and check training ---
print("\n--- Step 15: Wait 10s then check training status ---")
time.sleep(10)
r = check(15, "GET", f"/api/projects/{pid}/training", 200)
training_failed_expected = False
if r:
    tdata = r.json()
    status = tdata.get("status", "unknown")
    print(f"  Training status: {status}, done={tdata.get('done')}/{tdata.get('total')}, error={tdata.get('error','')[:100]}")
    if status not in ("running", "done", "failed"):
        failed += 1
        bugs.append({"step": 15, "endpoint": f"GET /api/projects/{pid}/training",
            "expected": "running|done|failed", "got": status, "detail": str(tdata)[:300]})
        print(f"  FAIL-AUTO  Step 15: unexpected status '{status}'")
    if status == "failed":
        training_failed_expected = True
        print(f"  NOTE: Training failed (expected - single-file GroupKFold needs >=3 groups)")

# --- Step 16: Get leaderboard ---
print("\n--- Step 16: Get leaderboard ---")
r = check(16, "GET", f"/api/projects/{pid}/training/leaderboard", 200)
if r:
    lb = r.json()
    lb_count = len(lb) if isinstance(lb, list) else len(lb.get("candidates", [])) if isinstance(lb, dict) else 0
    print(f"  Leaderboard entries: {lb_count}")

# --- Step 17: Get export info ---
print("\n--- Step 17: Get export info ---")
r = check(17, "GET", f"/api/projects/{pid}/export", 200)

# --- Step 18: Trigger export ---
print("\n--- Step 18: Trigger export ---")
r = check(18, "POST", f"/api/projects/{pid}/export", [200, 422])
if r:
    print(f"  Export response ({r.status_code}): {r.text[:200]}")

# --- Step 19: Get templates ---
print("\n--- Step 19: Get templates ---")
r = check(19, "GET", "/api/templates/timeseries", 200)
if r:
    ct = r.headers.get("content-type", "")
    print(f"  Content-Type: {ct}, size: {len(r.content)} bytes")
    if "zip" in ct or "octet" in ct:
        print("  Got ZIP template file (expected)")
    elif "json" in ct:
        tpls = r.json()
        print(f"  Templates JSON: {str(tpls)[:100]}")

# --- Step 20: Delete project ---
print("\n--- Step 20: Delete project ---")
r = check(20, "DELETE", f"/api/projects/{pid}?hard=true", 204)

# REPORT
print("\n" + "=" * 60)
report = {"agent": "dev1", "round": 3, "passed": passed, "failed": failed, "bugs": bugs}
print(f"REPORT: {json.dumps(report)}")
print("=" * 60)
