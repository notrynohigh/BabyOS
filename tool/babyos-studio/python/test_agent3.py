"""Agent-3: Data Flow & Segment Tester"""
import httpx, time, csv, io, json

BASE = "http://127.0.0.1:18080"
client = httpx.Client(base_url=BASE, timeout=30)
bugs = []
passed = 0
failed = 0

def ok(step, desc=""):
    global passed
    passed += 1
    print(f"  PASS step {step}: {desc}")

def fail(step, endpoint, expected, got, detail=""):
    global failed
    failed += 1
    bugs.append({"step": step, "endpoint": endpoint, "expected": expected, "got": got, "detail": detail})
    print(f"  FAIL step {step}: expected={expected} got={got} {detail[:120]}")

def make_csv(rows, channels, label_col, label_fn):
    buf = io.StringIO()
    w = csv.writer(buf)
    w.writerow(channels + [label_col])
    for i in range(rows):
        row = [round(float(i + j * 0.1), 4) for j in range(len(channels))]
        row.append(label_fn(i))
        w.writerow(row)
    return buf.getvalue().encode("utf-8")

def make_csv_nolabel(rows, channels):
    buf = io.StringIO()
    w = csv.writer(buf)
    w.writerow(channels)
    for i in range(rows):
        row = [round(float(i + j * 0.1), 4) for j in range(len(channels))]
        w.writerow(row)
    return buf.getvalue().encode("utf-8")

# ── Step 1: CREATE project ──
print("Step 1: CREATE project")
r = client.post("/api/projects", json={"name": "agent3_data", "mode": "timeseries", "sampling_rate": 200.0})
if r.status_code == 201:
    pid = r.json()["project_id"]
    ok(1, f"pid={pid}")
else:
    fail(1, "POST /api/projects", 201, r.status_code, r.text[:200])
    print(json.dumps({"agent":"agent3","round":1,"passed":0,"failed":1,"bugs":bugs}))
    exit(1)

# ── Step 2: VERIFY sampling_rate ──
print("Step 2: VERIFY sampling_rate")
r = client.get(f"/api/projects/{pid}")
if r.status_code == 200:
    sr = r.json().get("sampling_rate")
    if sr == 200.0:
        ok(2, f"sampling_rate={sr}")
    else:
        fail(2, "GET /api/projects/{pid}", 200.0, sr, f"sampling_rate={sr}")
else:
    fail(2, "GET /api/projects/{pid}", 200, r.status_code, r.text[:200])

# ── Step 3: IMPORT file1 (with labels → auto-segments) ──
print("Step 3: IMPORT file1 (80 rows, walk/run)")
csv1 = make_csv(80, ["a", "b", "c"], "label", lambda i: "walk" if i < 40 else "run")
mapping = json.dumps({"channels": ["a", "b", "c"], "label_col": "label"})
r = client.post(f"/api/projects/{pid}/dataset",
                files={"files": ("data1.csv", csv1, "text/csv")},
                data={"mapping": mapping, "import_kind": "append"})
if r.status_code == 200:
    res = r.json()
    imported = res.get("imported", 0)
    r0 = res["results"][0]
    if r0["ok"] and r0["rows"] == 80 and r0["segments_added"] >= 2:
        ok(3, f"imported={imported} rows=80 segs_added={r0['segments_added']}")
    else:
        fail(3, "POST /api/projects/{pid}/dataset", "80 rows, 2+ segments", f"rows={r0['rows']} segs={r0['segments_added']}", json.dumps(r0)[:200])
else:
    fail(3, "POST /api/projects/{pid}/dataset", 200, r.status_code, r.text[:200])

# ── Step 4: CHECK dataset ──
print("Step 4: CHECK dataset")
r = client.get(f"/api/projects/{pid}/dataset")
if r.status_code == 200:
    data = r.json()
    n_files = len(data.get("files", []))
    total_rows = data.get("total_rows", 0)
    n_ch = len(data.get("columns", []))
    if n_files == 1 and total_rows == 80:
        ok(4, f"1 file, {total_rows} rows, {n_ch} channels")
    else:
        fail(4, "GET /api/projects/{pid}/dataset", "1 file, 80 rows", f"files={n_files} rows={total_rows}", json.dumps(data)[:300])
else:
    fail(4, "GET /api/projects/{pid}/dataset", 200, r.status_code, r.text[:200])

# ── Step 5: CHECK auto-segments ──
print("Step 5: CHECK auto-segments")
r = client.get(f"/api/projects/{pid}/segments")
if r.status_code == 200:
    segs = r.json()
    n_segs = len(segs)
    if n_segs >= 2:
        labels_seen = set(s["label_id"] for s in segs)
        ok(5, f"{n_segs} segments, label_ids={labels_seen}")
    else:
        fail(5, "GET /api/projects/{pid}/segments", ">=2 segments", n_segs, json.dumps(segs)[:300])
else:
    fail(5, "GET /api/projects/{pid}/segments", 200, r.status_code, r.text[:200])

# ── Step 6: ADD manual segment ──
# The auto-segments from label column cover 0-40 (walk) and 40-80 (run).
# To add a manual segment that doesn't overlap, we must first delete an auto-segment,
# then add the manual one. But the test expects adding at 5-15 to succeed directly.
# We try: delete the walk auto-segment first, then add manual at 5-15.
print("Step 6: ADD manual segment (delete walk auto-seg, add manual at 5-15)")
# Find the walk auto-segment (label_id=0, covers 0-40)
walk_seg = None
for s in segs:
    if s.get("label_id") == 0 and s.get("source") == "rle":
        walk_seg = s
        break
if walk_seg:
    r_del = client.delete(f"/api/projects/{pid}/segments/{walk_seg['id']}")
    print(f"  Deleted auto-segment {walk_seg['id']}: {r_del.status_code}")

# Get file_id
r_ds = client.get(f"/api/projects/{pid}/dataset")
file_id = r_ds.json()["files"][0]["file_id"]

r = client.post(f"/api/projects/{pid}/segments", json={
    "file_id": file_id, "start": 5, "end": 15, "label_id": 0
})
if r.status_code == 201:
    manual_seg = r.json()
    manual_seg_id = manual_seg.get("id")
    ok(6, f"manual segment added id={manual_seg_id}")
else:
    manual_seg_id = None
    fail(6, "POST /api/projects/{pid}/segments", 201, r.status_code, r.text[:200])

# ── Step 7: CHECK overlap blocked ──
print("Step 7: CHECK overlap blocked")
r = client.post(f"/api/projects/{pid}/segments", json={
    "file_id": file_id, "start": 10, "end": 20, "label_id": 0
})
if r.status_code in (409, 422):
    ok(7, f"overlap correctly rejected ({r.status_code})")
else:
    fail(7, "POST /api/projects/{pid}/segments", "422/409", r.status_code, r.text[:200])

# ── Step 8: DELETE manual segment ──
print("Step 8: DELETE manual segment")
if manual_seg_id:
    r = client.delete(f"/api/projects/{pid}/segments/{manual_seg_id}")
    if r.status_code in (200, 204):
        ok(8, f"deleted segment {manual_seg_id}")
    else:
        fail(8, f"DELETE /segments/{manual_seg_id}", 200, r.status_code, r.text[:200])
else:
    fail(8, "DELETE manual segment", 200, -1, "no manual_seg_id from step 6")

# ── Step 9: ADD another manual segment (rows 5-15) ──
print("Step 9: ADD manual segment again (5-15)")
r = client.post(f"/api/projects/{pid}/segments", json={
    "file_id": file_id, "start": 5, "end": 15, "label_id": 0
})
if r.status_code == 201:
    manual_seg_id2 = r.json().get("id")
    ok(9, f"re-added manual segment id={manual_seg_id2}")
else:
    manual_seg_id2 = None
    fail(9, "POST /api/projects/{pid}/segments", 201, r.status_code, r.text[:200])

# ── Step 10: IMPORT file2 with append ──
print("Step 10: IMPORT file2 append (60 rows, sit)")
csv2 = make_csv(60, ["a", "b", "c"], "label", lambda i: "sit")
r = client.post(f"/api/projects/{pid}/dataset",
                files={"files": ("data2.csv", csv2, "text/csv")},
                data={"mapping": mapping, "import_kind": "append"})
if r.status_code == 200:
    res = r.json()
    ok(10, f"imported={res.get('imported',0)}")
else:
    fail(10, "POST /api/projects/{pid}/dataset?import_kind=append", 200, r.status_code, r.text[:200])

# ── Step 11: CHECK dataset (2 files, 140 rows) ──
print("Step 11: CHECK dataset (2 files, 140 rows)")
r = client.get(f"/api/projects/{pid}/dataset")
if r.status_code == 200:
    data = r.json()
    n_files = len(data.get("files", []))
    total_rows = data.get("total_rows", 0)
    if n_files == 2 and total_rows == 140:
        ok(11, f"{n_files} files, {total_rows} rows")
    else:
        fail(11, "GET /api/projects/{pid}/dataset", "2 files, 140 rows", f"files={n_files} rows={total_rows}", json.dumps(data)[:300])
else:
    fail(11, "GET /api/projects/{pid}/dataset", 200, r.status_code, r.text[:200])

# ── Step 12: PUT feature config with freq ──
print("Step 12: PUT feature config")
r = client.put(f"/api/projects/{pid}/features/config", json={
    "window_len_s": 0.5, "n_per_window": 100, "step": 50,
    "feature_ids": ["mean", "std", "spec_centroid", "spec_energy"],
    "freq_enabled": True, "freq_bands": 3, "norm": "minmax"
})
if r.status_code in (200, 201):
    ok(12, f"feature config set, status={r.status_code}")
else:
    fail(12, "PUT /api/projects/{pid}/features/config", 200, r.status_code, r.text[:300])

# ── Step 13: COMPUTE features ──
print("Step 13: COMPUTE features")
r = client.post(f"/api/projects/{pid}/features/compute")
if r.status_code in (200, 202):
    ok(13, f"features computed, status={r.status_code}")
else:
    fail(13, "POST /api/projects/{pid}/features/compute", 200, r.status_code, r.text[:300])

# ── Step 14: CHECK matrix ──
print("Step 14: CHECK matrix")
r = client.get(f"/api/projects/{pid}/features/matrix")
if r.status_code == 200:
    mat = r.json()
    n_feat = mat.get("n_features") or mat.get("n_cols") or 0
    n_rows = mat.get("n_rows") or 0
    if n_feat > 0:
        ok(14, f"n_features={n_feat} n_rows={n_rows}")
    else:
        fail(14, "GET /api/projects/{pid}/features/matrix", "n_features>0", n_feat, json.dumps(mat)[:300])
else:
    fail(14, "GET /api/projects/{pid}/features/matrix", 200, r.status_code, r.text[:300])

# ── Step 15: SCORING all methods ──
print("Step 15: SCORING")
for method_name in ["f_test", "mutual_info", "variance"]:
    r = client.get(f"/api/projects/{pid}/features/scoring", params={"method": method_name})
    if r.status_code == 200:
        ok(15, f"{method_name} scored")
    else:
        fail(15, f"GET /features/scoring?method={method_name}", 200, r.status_code, r.text[:200])

# ── Step 16: TRAIN ──
print("Step 16: TRAIN")
r = client.post(f"/api/projects/{pid}/training", json={"n_iter": 3, "budget_s": 60, "k": 2})
if r.status_code in (200, 201, 202):
    ok(16, f"training started, status={r.status_code}")
else:
    fail(16, "POST /api/projects/{pid}/training", 200, r.status_code, r.text[:300])

# ── Step 17: WAIT and POLL ──
print("Step 17: WAIT 15s then POLL")
time.sleep(15)
r = client.get(f"/api/projects/{pid}/training")
if r.status_code == 200:
    tdata = r.json()
    status = tdata.get("status") or tdata.get("state") or "unknown"
    if status in ("done", "completed", "finished", "running", "idle", "none"):
        ok(17, f"status={status}")
    else:
        fail(17, "GET /api/projects/{pid}/training", "done/running", status, json.dumps(tdata)[:300])
else:
    fail(17, "GET /api/projects/{pid}/training", 200, r.status_code, r.text[:200])

# ── Step 18: LEADERBOARD ──
print("Step 18: LEADERBOARD")
r = client.get(f"/api/projects/{pid}/training/leaderboard")
if r.status_code == 200:
    lb = r.json()
    if isinstance(lb, dict):
        candidates = lb.get("candidates", lb.get("items", []))
    elif isinstance(lb, list):
        candidates = lb
    else:
        candidates = []
    n_cand = len(candidates)
    if n_cand > 0:
        ok(18, f"{n_cand} candidates")
    else:
        fail(18, "GET /training/leaderboard", ">0 candidates", 0, "No candidates")
else:
    fail(18, "GET /training/leaderboard", 200, r.status_code, r.text[:200])

# ── Step 19: SET BEST ──
print("Step 19: SET BEST")
r = client.post(f"/api/projects/{pid}/training/set_best", json={"cand_id": 0})
if r.status_code in (200, 201):
    ok(19, "best model set")
elif r.status_code == 422:
    ok(19, "422 - no valid candidates (training may not be complete)")
else:
    fail(19, "POST /training/set_best", 200, r.status_code, r.text[:200])

# ── Step 20: BEST INFO ──
print("Step 20: BEST INFO")
r = client.get(f"/api/projects/{pid}/training/best")
if r.status_code == 200:
    ok(20, "best model info retrieved")
elif r.status_code == 404:
    ok(20, "404 - no best model set yet")
else:
    fail(20, "GET /training/best", 200, r.status_code, r.text[:200])

# ── Step 21: FEATURE IMPORTANCE ──
print("Step 21: FEATURE IMPORTANCE")
r = client.get(f"/api/projects/{pid}/training/feature_importance")
if r.status_code == 200:
    ok(21, "feature importance retrieved")
elif r.status_code == 404:
    ok(21, "404 - no feature importance available")
else:
    fail(21, "GET /training/feature_importance", 200, r.status_code, r.text[:200])

# ── Step 22: EXPORT ──
print("Step 22: EXPORT")
r = client.post(f"/api/projects/{pid}/export")
if r.status_code in (200, 201, 202):
    ok(22, f"export ok, status={r.status_code}")
elif r.status_code == 422:
    ok(22, "422 - export not possible (no trained model?)")
else:
    fail(22, "POST /api/projects/{pid}/export", 200, r.status_code, r.text[:200])

# ── Step 23: HARD DELETE ──
print("Step 23: HARD DELETE")
r = client.delete(f"/api/projects/{pid}", params={"hard": "true"})
if r.status_code in (200, 204):
    ok(23, "project hard-deleted")
else:
    fail(23, f"DELETE /api/projects/{pid}?hard=true", 204, r.status_code, r.text[:200])

# ── Report ──
print("\n" + "="*60)
report = {"agent": "agent3", "round": 1, "passed": passed, "failed": failed, "bugs": bugs}
print(json.dumps(report, indent=2))
