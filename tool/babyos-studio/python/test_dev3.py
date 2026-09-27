#!/usr/bin/env python3
"""Dev-3: Data Flow & Multi-file Tester for AutoML backend.

NOTE on test data design:
- window_len_s=0.3 at sampling_rate=200 Hz => n_per_window=60 samples.
- Segments must be >= 60 rows to produce at least 1 window each.
- file1 uses 120 rows: walk 0-59 (60 rows), run 60-119 (60 rows).
- file2 uses 80 rows: sit 0-79 (80 rows).
- This produces 3 segments => enough windows for scoring & training.
"""

import httpx
import time
import json
import io
import csv

BASE = "http://127.0.0.1:18080"
client = httpx.Client(base_url=BASE, timeout=30)

results = {"agent": "dev3", "round": 1, "passed": 0, "failed": 0, "bugs": []}

def report(step, endpoint, expected, got, detail=""):
    status = "PASS" if got == expected else "FAIL"
    if status == "PASS":
        results["passed"] += 1
        print(f"  Step {step}: PASS ({endpoint})")
    else:
        results["failed"] += 1
        results["bugs"].append({
            "step": step,
            "endpoint": endpoint,
            "expected": expected,
            "got": got,
            "detail": detail,
        })
        print(f"  Step {step}: FAIL ({endpoint}) expected={expected} got={got} detail={detail}")


# ---- Step 1: Create project ----
print("\n=== Step 1: POST /api/projects ===")
payload = {"name": "dev3_data", "mode": "timeseries", "sampling_rate": 200.0}
r = client.post("/api/projects", json=payload)
report(1, "POST /api/projects", 201, r.status_code, r.text[:300])
proj_resp = r.json()
project_id = proj_resp.get("project_id") or proj_resp.get("id") or proj_resp.get("pid")
print(f"  project_id = {project_id}")

# ---- Step 2: Verify sampling_rate ----
print("\n=== Step 2: GET /api/projects/{pid} ===")
r = client.get(f"/api/projects/{project_id}")
report(2, "GET /api/projects/{pid}", 200, r.status_code, r.text[:300])
proj = r.json()
sr = proj.get("sampling_rate")
report(2.1, "sampling_rate==200.0", 200.0, sr, f"got {sr}")

# ---- Step 3: Import file1 (3 channels a,b,c + label) ----
# 120 rows: walk (0-59), run (60-119). Each segment = 60 rows.
print("\n=== Step 3: POST /api/projects/{pid}/dataset (file1) ===")
buf = io.StringIO()
writer = csv.writer(buf)
writer.writerow(["a", "b", "c", "label"])
for i in range(120):
    row = [round(0.1 * i + 0.01 * ci, 4) for ci in range(3)]
    row.append("walk" if i < 60 else "run")
    writer.writerow(row)
csv1 = buf.getvalue().encode("utf-8")

mapping = json.dumps({"channels": ["a", "b", "c"], "label_col": "label"})
files1 = {"files": ("file1.csv", csv1, "text/csv")}
r = client.post(
    f"/api/projects/{project_id}/dataset",
    files=files1,
    data={"mapping": mapping, "import_kind": "append"},
)
report(3, "POST /api/projects/{pid}/dataset (file1)", 200, r.status_code, r.text[:300])

# ---- Step 4: Dataset check (1 file, 120 rows) ----
print("\n=== Step 4: GET /api/projects/{pid}/dataset ===")
r = client.get(f"/api/projects/{project_id}/dataset")
report(4, "GET /api/projects/{pid}/dataset", 200, r.status_code, r.text[:500])
ds = r.json()
file_count = len(ds.get("files", []))
total_rows = ds.get("total_rows")
report(4.1, "files==1", 1, file_count, f"got {file_count}")
report(4.2, "rows==120", 120, total_rows, f"got {total_rows}")

# ---- Step 5: Segments check (2 segments: walk + run) ----
print("\n=== Step 5: GET /api/projects/{pid}/segments ===")
r = client.get(f"/api/projects/{project_id}/segments")
report(5, "GET /api/projects/{pid}/segments", 200, r.status_code, r.text[:500])
segs = r.json()
seg_list = segs if isinstance(segs, list) else segs.get("segments", segs.get("data", []))
report(5.1, "segments==2", 2, len(seg_list), f"got {len(seg_list)}")

# ---- Step 6: Import file2 (3 channels, 80 rows, label=sit for all, append) ----
print("\n=== Step 6: POST /api/projects/{pid}/dataset (file2, append) ===")
buf2 = io.StringIO()
writer2 = csv.writer(buf2)
writer2.writerow(["a", "b", "c", "label"])
for i in range(80):
    row = [round(0.2 * i + 0.01 * ci, 4) for ci in range(3)]
    row.append("sit")
    writer2.writerow(row)
csv2 = buf2.getvalue().encode("utf-8")

files2 = {"files": ("file2.csv", csv2, "text/csv")}
r = client.post(
    f"/api/projects/{project_id}/dataset",
    files=files2,
    data={"mapping": mapping, "import_kind": "append"},
)
report(6, "POST /api/projects/{pid}/dataset (file2 append)", 200, r.status_code, r.text[:300])

# ---- Step 7: Dataset check (2 files, 200 rows) ----
print("\n=== Step 7: GET /api/projects/{pid}/dataset (after file2) ===")
r = client.get(f"/api/projects/{project_id}/dataset")
report(7, "GET /api/projects/{pid}/dataset", 200, r.status_code, r.text[:500])
ds = r.json()
file_count = len(ds.get("files", []))
total_rows = ds.get("total_rows")
report(7.1, "files==2", 2, file_count, f"got {file_count}")
report(7.2, "rows==200", 200, total_rows, f"got {total_rows}")

# ---- Step 8: PUT features/config ----
print("\n=== Step 8: PUT /api/projects/{pid}/features/config ===")
config_payload = {
    "window_len_s": 0.3,
    "n_per_window": 60,
    "step": 30,
    "feature_ids": ["mean", "std"],
    "freq_enabled": False,
    "norm": "zscore",
}
r = client.put(f"/api/projects/{project_id}/features/config", json=config_payload)
report(8, "PUT /api/projects/{pid}/features/config", 200, r.status_code, r.text[:300])

# ---- Step 9: POST /features/compute ----
print("\n=== Step 9: POST /api/projects/{pid}/features/compute ===")
r = client.post(f"/api/projects/{project_id}/features/compute")
report(9, "POST /features/compute", 200, r.status_code, r.text[:300])

# ---- Step 10: GET /features/matrix ----
print("\n=== Step 10: GET /api/projects/{pid}/features/matrix ===")
r = client.get(f"/api/projects/{project_id}/features/matrix")
report(10, "GET /features/matrix", 200, r.status_code, r.text[:500])

# ---- Steps 11-13: Feature scoring ----
for idx, method in enumerate(["f_test", "mutual_info", "variance"]):
    step = 11 + idx
    print(f"\n=== Step {step}: GET /features/scoring?method={method} ===")
    r = client.get(f"/api/projects/{project_id}/features/scoring", params={"method": method})
    report(step, f"GET /features/scoring?method={method}", 200, r.status_code, r.text[:300])

# ---- Step 14: POST /training ----
print("\n=== Step 14: POST /api/projects/{pid}/training ===")
training_payload = {"n_iter": 2, "budget_s": 30, "k": 2}
r = client.post(f"/api/projects/{project_id}/training", json=training_payload)
report(14, "POST /training", 200, r.status_code, r.text[:300])

# ---- Step 15: Poll training ----
print("\n=== Step 15: Poll training status ===")
start = time.time()
max_wait = 15
final_status = "unknown"
for _ in range(8):
    time.sleep(2)
    r = client.get(f"/api/projects/{project_id}/training")
    if r.status_code == 200:
        td = r.json()
        status = td.get("status", td.get("state", "unknown"))
        print(f"  [{time.time()-start:.0f}s] status={status}")
        if status in ("done", "completed", "failed", "error"):
            final_status = status
            break
    else:
        print(f"  [{time.time()-start:.0f}s] HTTP {r.status_code}: {r.text[:200]}")

report(15, "GET /training (poll)", 200, r.status_code, f"final_status={final_status}")

# ---- Step 16: GET /training/leaderboard ----
print("\n=== Step 16: GET /api/projects/{pid}/training/leaderboard ===")
r = client.get(f"/api/projects/{project_id}/training/leaderboard")
report(16, "GET /training/leaderboard", 200, r.status_code, r.text[:500])

# ---- Step 17: POST /export ----
print("\n=== Step 17: POST /api/projects/{pid}/export ===")
r = client.post(f"/api/projects/{project_id}/export")
export_ok = r.status_code in (200, 201)
report(17, "POST /export", 200, r.status_code, r.text[:300])
if r.status_code == 422:
    r2 = client.post(f"/api/projects/{project_id}/export", json={"format": "c"})
    export_ok = r2.status_code in (200, 201)
    report(17.1, "POST /export (with body)", 200, r2.status_code, r2.text[:300])

# ---- Step 18: DELETE ----
print("\n=== Step 18: DELETE /api/projects/{pid}?hard=true ===")
r = client.delete(f"/api/projects/{project_id}", params={"hard": "true"})
report(18, "DELETE /api/projects/{pid}?hard=true", 204, r.status_code, r.text[:300])

# ---- REPORT ----
print("\n" + "=" * 60)
print("REPORT:")
print(json.dumps(results, indent=2, ensure_ascii=False))
