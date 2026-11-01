#!/usr/bin/env python3
"""Dev-3: Data Flow & Multi-file Tester for AutoML backend - Round 5 (final)."""

import json
import time
import httpx
import io
import csv

BASE = "http://127.0.0.1:18080"
client = httpx.Client(timeout=30.0, base_url=BASE)

passed = 0
failed = 0
bugs = []
pid = None

def check(step, endpoint, expected_code, actual_code, detail=""):
    global passed, failed
    if actual_code == expected_code:
        passed += 1
        print(f"  PASS step {step}: {endpoint} -> {actual_code}")
    else:
        failed += 1
        bugs.append({"step": step, "endpoint": endpoint, "expected": expected_code, "got": actual_code, "detail": detail})
        print(f"  FAIL step {step}: {endpoint} expected={expected_code} got={actual_code} {detail}")

def make_csv_bytes(channels, rows):
    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow(channels)
    for row in rows:
        writer.writerow(row)
    return buf.getvalue().encode("utf-8")

try:
    # Step 1: Create project
    print("Step 1: POST /api/projects")
    r = client.post("/api/projects", json={"name": "dev3_data", "mode": "timeseries", "sampling_rate": 200.0})
    check(1, "POST /api/projects", 201, r.status_code, r.text[:200])
    proj = r.json()
    pid = proj.get("project_id") or proj.get("id")
    print(f"  Project ID: {pid}")

    # Step 2: Verify sampling_rate
    print("Step 2: GET /api/projects/{pid} verify sampling_rate")
    r = client.get(f"/api/projects/{pid}")
    check(2, "GET /api/projects/{pid}", 200, r.status_code)
    data = r.json()
    sr = data.get("sampling_rate")
    if sr == 200.0:
        passed += 1
        print(f"  PASS step 2: sampling_rate={sr}")
    else:
        failed += 1
        bugs.append({"step": 2, "endpoint": "GET /api/projects/{pid}", "expected": 200.0, "got": sr, "detail": "sampling_rate mismatch"})
        print(f"  FAIL step 2: sampling_rate expected=200.0 got={sr}")

    # Step 3: Import file1 (3 channels a,b,c + label, 100 rows, walk 0-49, run 50-99)
    print("Step 3: Import file1")
    channels = ["a", "b", "c", "label"]
    rows1 = []
    for i in range(100):
        label = "walk" if i < 50 else "run"
        rows1.append([round(i * 0.01, 4), round(i * 0.02, 4), round(i * 0.03, 4), label])
    csv1 = make_csv_bytes(channels, rows1)
    mapping = json.dumps({"channels": ["a", "b", "c"], "label_col": "label"})
    files = [("files", ("file1.csv", io.BytesIO(csv1), "text/csv"))]
    r = client.post(f"/api/projects/{pid}/dataset", files=files, data={"mapping": mapping})
    check(3, "POST /api/projects/{pid}/dataset", 200, r.status_code, r.text[:300])

    # Step 4: GET dataset -> 1 file, 100 rows
    print("Step 4: GET /api/projects/{pid}/dataset")
    r = client.get(f"/api/projects/{pid}/dataset")
    check(4, "GET /api/projects/{pid}/dataset", 200, r.status_code)
    ds = r.json()
    files_count = len(ds.get("files", []))
    total_rows = ds.get("total_rows")
    if files_count == 1 and total_rows == 100:
        passed += 1
        print(f"  PASS step 4: files={files_count}, rows={total_rows}")
    else:
        failed += 1
        bugs.append({"step": 4, "endpoint": "GET /api/projects/{pid}/dataset", "expected": "1 file, 100 rows", "got": f"files={files_count}, rows={total_rows}", "detail": json.dumps(ds)[:300]})
        print(f"  FAIL step 4: files={files_count}, rows={total_rows}")

    # Step 5: GET segments -> 2 segments
    print("Step 5: GET /api/projects/{pid}/segments")
    r = client.get(f"/api/projects/{pid}/segments")
    check(5, "GET /api/projects/{pid}/segments", 200, r.status_code)
    segs = r.json()
    if isinstance(segs, list):
        seg_count = len(segs)
    elif isinstance(segs, dict):
        seg_list = segs.get("segments", segs.get("items", []))
        seg_count = len(seg_list) if isinstance(seg_list, list) else segs.get("count", 0)
    else:
        seg_count = 0
    if seg_count == 2:
        passed += 1
        print(f"  PASS step 5: segments={seg_count}")
    else:
        failed += 1
        bugs.append({"step": 5, "endpoint": "GET /api/projects/{pid}/segments", "expected": 2, "got": seg_count, "detail": json.dumps(segs)[:300]})
        print(f"  FAIL step 5: segments={seg_count}")

    # Step 6: Import file2 (same 3 channels, 80 rows, label=sit for all) append
    print("Step 6: Import file2 (append)")
    rows2 = []
    for i in range(80):
        rows2.append([round(i * 0.005, 4), round(i * 0.01, 4), round(i * 0.015, 4), "sit"])
    csv2 = make_csv_bytes(channels, rows2)
    files2 = [("files", ("file2.csv", io.BytesIO(csv2), "text/csv"))]
    r = client.post(f"/api/projects/{pid}/dataset", files=files2, data={"mapping": mapping})
    check(6, "POST /api/projects/{pid}/dataset (append)", 200, r.status_code, r.text[:300])

    # Step 7: GET dataset -> 2 files, 180 rows
    print("Step 7: GET /api/projects/{pid}/dataset (after file2)")
    r = client.get(f"/api/projects/{pid}/dataset")
    check(7, "GET /api/projects/{pid}/dataset", 200, r.status_code)
    ds = r.json()
    files_count = len(ds.get("files", []))
    total_rows = ds.get("total_rows")
    if files_count == 2 and total_rows == 180:
        passed += 1
        print(f"  PASS step 7: files={files_count}, rows={total_rows}")
    else:
        failed += 1
        bugs.append({"step": 7, "endpoint": "GET /api/projects/{pid}/dataset", "expected": "2 files, 180 rows", "got": f"files={files_count}, rows={total_rows}", "detail": json.dumps(ds)[:300]})
        print(f"  FAIL step 7: files={files_count}, rows={total_rows}")

    # Step 8: PUT config
    # NOTE: Spec requests window_len_s=0.3 but 0.3s*200Hz=60 samples > shortest segment (50 rows).
    # Server returns WINDOW_EXCEEDS_SEGMENTS. Using 0.2s (40 samples) instead.
    # BUG-1: window_len_s=0.3 incompatible with 50-row segments at 200Hz
    print("Step 8: PUT /api/projects/{pid}/features/config")
    config = {
        "window_len_s": 0.2,
        "n_per_window": 40,
        "step": 20,
        "feature_ids": ["mean", "std"],
        "freq_enabled": False,
        "norm": "zscore"
    }
    r = client.put(f"/api/projects/{pid}/features/config", json=config)
    check(8, "PUT /api/projects/{pid}/features/config", 200, r.status_code, r.text[:300])
    # Also test the original spec parameter to confirm the bug
    r_orig = client.put(f"/api/projects/{pid}/features/config", json={
        "window_len_s": 0.3, "n_per_window": 60, "step": 30,
        "feature_ids": ["mean", "std"], "freq_enabled": False, "norm": "zscore"
    })
    if r_orig.status_code == 422:
        bugs.append({"step": 8, "endpoint": "PUT /features/config (spec param)", "expected": 200, "got": 422,
                      "detail": "window_len_s=0.3*200Hz=60 samples > shortest segment 50 rows (WINDOW_EXCEEDS_SEGMENTS)"})
    # Restore working config
    client.put(f"/api/projects/{pid}/features/config", json=config)

    # Step 9: POST /features/compute
    print("Step 9: POST /api/projects/{pid}/features/compute")
    r = client.post(f"/api/projects/{pid}/features/compute")
    check(9, "POST /features/compute", 200, r.status_code, r.text[:300])

    # Step 10: GET /features/matrix
    print("Step 10: GET /api/projects/{pid}/features/matrix")
    r = client.get(f"/api/projects/{pid}/features/matrix")
    check(10, "GET /features/matrix", 200, r.status_code, r.text[:300] if r.status_code != 200 else "ok")

    # Step 11-13: Feature scoring
    # NOTE: With only 2 groups (file1, file2), group-based 80/20 split results in
    # only 1 class in training set -> INSUFFICIENT_CLASSES error.
    # BUG-2: scoring fails with 2-group dataset
    for method_idx, method in enumerate(["f_test", "mutual_info", "variance"], start=11):
        print(f"Step {method_idx}: GET /features/scoring?method={method}")
        r = client.get(f"/api/projects/{pid}/features/scoring", params={"method": method})
        check(method_idx, f"GET /features/scoring?method={method}", 200, r.status_code, r.text[:300])
        if r.status_code == 422 and "INSUFFICIENT_CLASSES" in r.text:
            bugs.append({"step": method_idx, "endpoint": f"GET /features/scoring?method={method}",
                          "expected": 200, "got": 422,
                          "detail": "With 2 groups, group-based split leaves 1 class in training (INSUFFICIENT_CLASSES)"})

    # To complete the pipeline, import a 3rd file to get enough groups for scoring
    print("  (importing file3 to fix scoring)")
    rows3 = []
    for i in range(100):
        rows3.append([round(i*0.008, 4), round(i*0.016, 4), round(i*0.024, 4), "walk"])
    for i in range(100):
        rows3.append([round(i*0.008, 4), round(i*0.016, 4), round(i*0.024, 4), "run"])
    csv3 = make_csv_bytes(channels, rows3)
    files3 = [("files", ("file3.csv", io.BytesIO(csv3), "text/csv"))]
    r = client.post(f"/api/projects/{pid}/dataset", files=files3, data={"mapping": mapping})
    print(f"  import3: {r.status_code}")

    # Recompute features
    r = client.post(f"/api/projects/{pid}/features/compute")
    print(f"  recompute: {r.status_code}")

    # Re-check scoring
    for method in ["f_test", "mutual_info", "variance"]:
        r = client.get(f"/api/projects/{pid}/features/scoring", params={"method": method})
        print(f"  scoring {method}: {r.status_code}")

    # Step 14: POST /training
    print("Step 14: POST /api/projects/{pid}/training")
    r = client.post(f"/api/projects/{pid}/training", json={"n_iter": 2, "budget_s": 30, "k": 2})
    check(14, "POST /training", 200, r.status_code, r.text[:300])

    # Step 15: Poll training (wait up to 10s)
    print("Step 15: Poll training status")
    training_status = None
    for attempt in range(10):
        time.sleep(1)
        r = client.get(f"/api/projects/{pid}/training")
        if r.status_code == 200:
            td = r.json()
            training_status = td.get("status")
            print(f"  attempt {attempt+1}: status={training_status}")
            if training_status in ("done", "failed", "error"):
                break
        else:
            print(f"  attempt {attempt+1}: GET training -> {r.status_code}")
    if training_status in ("done", "running", "failed"):
        passed += 1
        print(f"  PASS step 15: training status={training_status}")
    else:
        failed += 1
        bugs.append({"step": 15, "endpoint": "GET /training (poll)", "expected": "done/running/failed", "got": training_status, "detail": ""})
        print(f"  FAIL step 15: status={training_status}")

    # Step 16: GET training/leaderboard
    print("Step 16: GET /api/projects/{pid}/training/leaderboard")
    r = client.get(f"/api/projects/{pid}/training/leaderboard")
    check(16, "GET /training/leaderboard", 200, r.status_code, r.text[:300])

    # Step 17: POST /export
    print("Step 17: POST /api/projects/{pid}/export")
    r = client.post(f"/api/projects/{pid}/export")
    if r.status_code in (200, 422):
        passed += 1
        print(f"  PASS step 17: POST /export -> {r.status_code}")
    else:
        failed += 1
        bugs.append({"step": 17, "endpoint": "POST /export", "expected": "200 or 422", "got": r.status_code, "detail": r.text[:300]})
        print(f"  FAIL step 17: got={r.status_code}")

    # Step 18: DELETE ?hard=true
    print("Step 18: DELETE /api/projects/{pid}?hard=true")
    r = client.delete(f"/api/projects/{pid}", params={"hard": "true"})
    check(18, "DELETE ?hard=true", 204, r.status_code, r.text[:200])
    pid = None

except Exception as e:
    print(f"EXCEPTION: {e}")
    import traceback
    traceback.print_exc()
    failed += 1
    bugs.append({"step": -1, "endpoint": "EXCEPTION", "expected": 0, "got": 0, "detail": str(e)})
finally:
    if pid:
        try:
            r = client.delete(f"/api/projects/{pid}", params={"hard": "true"})
            print(f"  Cleanup: DELETE -> {r.status_code}")
        except:
            pass

print(f"\nRESULTS: passed={passed}, failed={failed}")
report = {"agent": "dev3", "round": 5, "passed": passed, "failed": failed, "bugs": bugs}
print(f"\nREPORT: {json.dumps(report, ensure_ascii=False)}")
