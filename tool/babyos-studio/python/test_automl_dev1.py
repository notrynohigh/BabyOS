#!/usr/bin/env python3
"""AutoML Happy Path Test - Dev1 (Round 5)"""
import httpx
import time
import json
import csv
import io
import sys

BASE = "http://127.0.0.1:18080"
client = httpx.Client(base_url=BASE, timeout=30.0)
pid = None
fid = None
bugs = []
passed = 0
failed = 0
ROUND = 5

def check(step_n, endpoint, expected, resp, extra_check=None):
    global passed, failed
    got = resp.status_code
    if extra_check:
        ok_flag, detail = extra_check(resp)
    else:
        ok_flag = (got == expected)
        detail = ""
    if ok_flag:
        passed += 1
        print(f"  PASS: {endpoint} -> {got}")
    else:
        failed += 1
        bugs.append({"step": step_n, "endpoint": endpoint, "expected": expected, "got": got, "detail": detail or resp.text[:200]})
        print(f"  FAIL: {endpoint} -> expected {expected}, got {got}. {detail or resp.text[:200]}")

try:
    # === Step 1: Create project ===
    print("Step 1: POST /api/projects")
    r = client.post("/api/projects", json={"name": "dev1_test", "mode": "timeseries", "sampling_rate": 100.0})
    if r.status_code == 201:
        body = r.json()
        pid = body.get("project_id") or body.get("id")
    check(1, "POST /api/projects", 201, r)
    print(f"  project_id: {pid}")

    if not pid:
        print("FATAL: no project_id, cannot continue")
        sys.exit(1)

    # === Step 2: List projects ===
    print("Step 2: GET /api/projects")
    r = client.get("/api/projects")
    def check_list(resp):
        data = resp.json()
        items = data if isinstance(data, list) else data.get("items", data.get("projects", []))
        found = False
        for p in items:
            if (p.get("project_id") or p.get("id")) == pid:
                found = True
                break
        if found:
            return True, ""
        names = [p.get("name", "?") for p in items[:10]]
        return False, f"pid {pid} not found in projects (names={names})"
    check(2, "GET /api/projects", 200, r, check_list)

    # === Step 3: Get project ===
    print(f"Step 3: GET /api/projects/{pid}")
    r = client.get(f"/api/projects/{pid}")
    check(3, f"GET /api/projects/{{pid}}", 200, r)

    # === Step 4: Rename project ===
    print(f"Step 4: PATCH /api/projects/{pid}")
    r = client.patch(f"/api/projects/{pid}", json={"name": "dev1_renamed"})
    check(4, f"PATCH /api/projects/{{pid}}", 200, r)

    # === Step 5: Import CSV ===
    print("Step 5: Import CSV (200 rows: normal 0-99, abnormal 100-199)")
    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow(["timestamp", "accel_x", "accel_y", "label"])
    for i in range(200):
        ts = i * 10  # ms, 100Hz sampling
        if i < 100:
            ax = round(0.1 * (i % 10), 4)
            ay = round(0.05 * (i % 10), 4)
            label = 0
        else:
            ax = round(0.5 * ((i - 100) % 10), 4)
            ay = round(-0.3 * ((i - 100) % 10), 4)
            label = 1
        writer.writerow([ts, ax, ay, label])
    csv_bytes = buf.getvalue().encode("utf-8")

    files = [("files", ("test_data.csv", io.BytesIO(csv_bytes), "text/csv"))]
    mapping = json.dumps({
        "channels": ["accel_x", "accel_y"],
        "label_col": "label",
        "timestamp_col": "timestamp"
    })
    data = {"mapping": mapping, "import_kind": "append"}
    r = client.post(f"/api/projects/{pid}/dataset", files=files, data=data)
    check(5, f"POST /api/projects/{{pid}}/dataset", 200, r)

    # === Step 6: Get segments ===
    print(f"Step 6: GET /api/projects/{pid}/segments")
    r = client.get(f"/api/projects/{pid}/segments")
    def check_segments(resp):
        data = resp.json()
        items = data if isinstance(data, list) else data.get("items", data.get("segments", []))
        count = len(items)
        if count == 2:
            return True, ""
        return False, f"expected 2 segments, got {count}; body={str(data)[:200]}"
    check(6, f"GET /api/projects/{{pid}}/segments", 200, r, check_segments)

    # === Step 7: Get labels ===
    print(f"Step 7: GET /api/projects/{pid}/labels")
    r = client.get(f"/api/projects/{pid}/labels")
    def check_labels(resp):
        data = resp.json()
        items = data if isinstance(data, list) else data.get("items", data.get("labels", []))
        count = len(items)
        if count == 2:
            return True, ""
        return False, f"expected 2 labels, got {count}; body={str(data)[:200]}"
    check(7, f"GET /api/projects/{{pid}}/labels", 200, r, check_labels)

    # === Step 8: Get dataset ===
    print(f"Step 8: GET /api/projects/{pid}/dataset")
    r = client.get(f"/api/projects/{pid}/dataset")
    def check_dataset(resp):
        global fid
        data = resp.json()
        files_list = data.get("files", [])
        total = data.get("total_rows", 0)
        if files_list:
            fid = files_list[0].get("id") or files_list[0].get("file_id")
        if len(files_list) >= 1 and total == 200:
            return True, ""
        return False, f"expected 1 file 200 rows, got {len(files_list)} files {total} rows; body={str(data)[:200]}"
    check(8, f"GET /api/projects/{{pid}}/dataset", 200, r, check_dataset)
    print(f"  file_id: {fid}")

    # === Step 9: Get dataset file data ===
    if fid:
        print(f"Step 9: GET /api/projects/{pid}/dataset/file/{fid}/data?limit=5")
        r = client.get(f"/api/projects/{pid}/dataset/file/{fid}/data", params={"limit": 5})
        def check_file_data(resp):
            d = resp.json()
            data_rows = d.get("data", [])
            if len(data_rows) <= 5:
                return True, ""
            return False, f"expected <=5 data rows, got {len(data_rows)}"
        check(9, f"GET .../file/{{fid}}/data?limit=5", 200, r, check_file_data)
    else:
        print("Step 9: SKIPPED (no file_id)")
        failed += 1
        bugs.append({"step": 9, "endpoint": "GET .../file/{fid}/data?limit=5", "expected": 200, "got": "skipped", "detail": "no file_id from step 8"})

    # === Step 10: PUT features config ===
    print(f"Step 10: PUT /api/projects/{pid}/features/config")
    r = client.put(f"/api/projects/{pid}/features/config", json={
        "window_len_s": 0.2,
        "n_per_window": 20,
        "step": 10,
        "feature_ids": ["mean", "std", "rms", "ptp", "zcr"],
        "freq_enabled": False,
        "norm": "zscore"
    })
    check(10, f"PUT /api/projects/{{pid}}/features/config", 200, r)

    # === Step 11: Compute features ===
    print(f"Step 11: POST /api/projects/{pid}/features/compute")
    r = client.post(f"/api/projects/{pid}/features/compute")
    check(11, f"POST /api/projects/{{pid}}/features/compute", 200, r)

    # === Step 12: Get features matrix ===
    print(f"Step 12: GET /api/projects/{pid}/features/matrix")
    r = client.get(f"/api/projects/{pid}/features/matrix")
    check(12, f"GET /api/projects/{{pid}}/features/matrix", 200, r)

    # === Step 13: Get features scoring ===
    print(f"Step 13: GET /api/projects/{pid}/features/scoring")
    r = client.get(f"/api/projects/{pid}/features/scoring")
    check(13, f"GET /api/projects/{{pid}}/features/scoring", 200, r)

    # === Step 14: Start training ===
    print(f"Step 14: POST /api/projects/{pid}/training")
    r = client.post(f"/api/projects/{pid}/training", json={
        "n_iter": 2,
        "budget_s": 30,
        "k": 2
    })
    check(14, f"POST /api/projects/{{pid}}/training", 200, r)

    # === Step 15: Wait and check training status ===
    print("Step 15: Wait 10s, GET training status")
    time.sleep(10)
    r = client.get(f"/api/projects/{pid}/training")
    def check_training(resp):
        data = resp.json()
        status = data.get("status") or data.get("state")
        if status in ("running", "done", "failed"):
            return True, ""
        return False, f"unexpected status: {status}; body={str(data)[:200]}"
    check(15, f"GET /api/projects/{{pid}}/training", 200, r, check_training)

    # === Step 16: Leaderboard ===
    print(f"Step 16: GET /api/projects/{pid}/training/leaderboard")
    r = client.get(f"/api/projects/{pid}/training/leaderboard")
    check(16, f"GET /api/projects/{{pid}}/training/leaderboard", 200, r)

    # === Step 17: GET export ===
    print(f"Step 17: GET /api/projects/{pid}/export")
    r = client.get(f"/api/projects/{pid}/export")
    check(17, f"GET /api/projects/{{pid}}/export", 200, r)

    # === Step 18: POST export ===
    print(f"Step 18: POST /api/projects/{pid}/export")
    r = client.post(f"/api/projects/{pid}/export")
    def check_export(resp):
        if resp.status_code in (200, 422):
            return True, ""
        return False, f"expected 200 or 422, got {resp.status_code}"
    check(18, f"POST /api/projects/{{pid}}/export", 200, r, check_export)

    # === Step 19: GET templates ===
    print("Step 19: GET /api/templates/timeseries")
    r = client.get("/api/templates/timeseries")
    check(19, "GET /api/templates/timeseries", 200, r)

    # === Step 20: DELETE project ===
    print(f"Step 20: DELETE /api/projects/{pid}?hard=true")
    r = client.delete(f"/api/projects/{pid}", params={"hard": "true"})
    check(20, f"DELETE /api/projects/{{pid}}?hard=true", 204, r)

except Exception as e:
    import traceback
    traceback.print_exc()
    failed += 1
    bugs.append({"step": -1, "endpoint": "exception", "expected": "N/A", "got": "error", "detail": str(e)})
    # Cleanup attempt
    if pid:
        try:
            print(f"\nCleanup: deleting project {pid}")
            client.delete(f"/api/projects/{pid}", params={"hard": "true"})
        except:
            pass

# Final report
print("\n" + "=" * 60)
report = {"agent": "dev1", "round": ROUND, "passed": passed, "failed": failed, "bugs": bugs}
print("REPORT:")
print(json.dumps(report, indent=2))
