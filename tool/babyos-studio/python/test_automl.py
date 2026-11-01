#!/usr/bin/env python3
"""AutoML Happy Path Test -- Dev-1 agent, round 4."""
import io, csv, time, json, sys
import httpx

BASE = "http://127.0.0.1:18080"
PASSED = 0
FAILED = 0
BUGS = []

def check(step, endpoint, expected_code, resp, detail_check=None):
    global PASSED, FAILED, BUGS
    ok = True
    msg = ""
    if resp.status_code != expected_code:
        ok = False
        msg = f"expected {expected_code}, got {resp.status_code}"
    elif detail_check:
        ok, msg = detail_check(resp)
    if ok:
        PASSED += 1
        print(f"  Step {step}: PASS ({resp.status_code})")
    else:
        FAILED += 1
        bug = {"step": step, "endpoint": endpoint, "expected": expected_code, "got": resp.status_code, "detail": msg}
        BUGS.append(bug)
        print(f"  Step {step}: FAIL - {endpoint} - {msg}")
        print(f"    body: {resp.text[:300]}")
    return ok

def main():
    global PASSED, FAILED, BUGS
    client = httpx.Client(base_url=BASE, timeout=30)

    # ---- Step 1: POST /api/projects ----
    print("Step 1: Create project")
    r = client.post("/api/projects", json={
        "name": "dev1_test", "mode": "timeseries", "sampling_rate": 100.0
    })
    check(1, "POST /api/projects", 201, r)
    pid = r.json().get("project_id")
    print(f"  project_id = {pid}")

    # ---- Step 2: GET /api/projects ----
    print("Step 2: List projects")
    r = client.get("/api/projects")
    def check_list(resp):
        data = resp.json()
        ids = [p["project_id"] for p in data]
        if pid in ids:
            return True, ""
        return False, f"project {pid} not in list"
    check(2, "GET /api/projects", 200, r, check_list)

    # ---- Step 3: GET /api/projects/{pid} ----
    print("Step 3: Get project")
    r = client.get(f"/api/projects/{pid}")
    check(3, f"GET /api/projects/{pid}", 200, r)

    # ---- Step 4: PATCH /api/projects/{pid} ----
    print("Step 4: Patch project name")
    r = client.patch(f"/api/projects/{pid}", json={"name": "dev1_renamed"})
    def check_patch(resp):
        if resp.json().get("name") == "dev1_renamed":
            return True, ""
        return False, f"name not updated: {resp.json().get('name')}"
    check(4, f"PATCH /api/projects/{pid}", 200, r, check_patch)

    # ---- Step 5: Import CSV ----
    print("Step 5: Import CSV (200 rows)")
    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow(["timestamp", "accel_x", "accel_y", "label"])
    for i in range(100):
        writer.writerow([i, 0.1 * (i % 10), 0.05 * (i % 10), 0])
    for i in range(100, 200):
        writer.writerow([i, 0.5 * ((i - 100) % 10), -0.3 * ((i - 100) % 10), 1])
    csv_bytes = buf.getvalue().encode("utf-8")

    files = [("files", ("test_data.csv", io.BytesIO(csv_bytes), "text/csv"))]
    mapping = json.dumps({
        "channels": ["accel_x", "accel_y"],
        "label_col": "label",
        "timestamp_col": "timestamp"
    })
    data = {"mapping": mapping, "import_kind": "append"}
    r = client.post(f"/api/projects/{pid}/dataset", files=files, data=data)
    check(5, f"POST /api/projects/{pid}/dataset", 200, r)

    # ---- Step 6: GET /api/projects/{pid}/segments ----
    print("Step 6: Get segments")
    r = client.get(f"/api/projects/{pid}/segments")
    def check_segments(resp):
        segs = resp.json()
        if len(segs) == 2:
            return True, ""
        return False, f"expected 2 segments, got {len(segs)}: {resp.text[:200]}"
    check(6, f"GET /api/projects/{pid}/segments", 200, r, check_segments)

    # ---- Step 7: GET /api/projects/{pid}/labels ----
    print("Step 7: Get labels")
    r = client.get(f"/api/projects/{pid}/labels")
    def check_labels(resp):
        labels = resp.json()
        if len(labels) >= 2:
            return True, ""
        return False, f"expected >=2 labels, got {len(labels)}: {resp.text[:200]}"
    check(7, f"GET /api/projects/{pid}/labels", 200, r, check_labels)

    # ---- Step 8: GET /api/projects/{pid}/dataset ----
    print("Step 8: Dataset info")
    r = client.get(f"/api/projects/{pid}/dataset")
    def check_dataset(resp):
        d = resp.json()
        files_list = d.get("files", [])
        n_rows = d.get("total_rows", d.get("n_rows", 0))
        if len(files_list) >= 1 and n_rows >= 200:
            return True, ""
        return False, f"expected >=1 file, >=200 rows; got {len(files_list)} files, {n_rows} rows: {resp.text[:300]}"
    check(8, f"GET /api/projects/{pid}/dataset", 200, r, check_dataset)

    dataset_info = r.json()
    fid = dataset_info["files"][0]["file_id"] if dataset_info.get("files") else None
    print(f"  file_id = {fid}")

    # ---- Step 9: GET file data ----
    print("Step 9: Get file data (limit=5)")
    r = client.get(f"/api/projects/{pid}/dataset/file/{fid}/data", params={"limit": 5})
    def check_file_data(resp):
        d = resp.json()
        data_rows = d.get("data", [])
        if len(data_rows) == 5:
            return True, ""
        return False, f"expected 5 data rows, got {len(data_rows)}: {resp.text[:200]}"
    check(9, f"GET /api/projects/{pid}/dataset/file/{fid}/data", 200, r, check_file_data)

    # ---- Step 10: PUT features config ----
    print("Step 10: Put feature config")
    r = client.put(f"/api/projects/{pid}/features/config", json={
        "window_len_s": 0.2,
        "n_per_window": 20,
        "step": 10,
        "feature_ids": ["mean", "std", "rms", "ptp", "zcr"],
        "freq_enabled": False,
        "norm": "zscore"
    })
    check(10, f"PUT /api/projects/{pid}/features/config", 200, r)

    # ---- Step 11: POST features compute ----
    print("Step 11: Compute features")
    r = client.post(f"/api/projects/{pid}/features/compute")
    check(11, f"POST /api/projects/{pid}/features/compute", 200, r)

    # ---- Step 12: GET features matrix ----
    print("Step 12: Get feature matrix")
    r = client.get(f"/api/projects/{pid}/features/matrix")
    check(12, f"GET /api/projects/{pid}/features/matrix", 200, r)

    # ---- Step 13: GET features scoring ----
    print("Step 13: Get feature scoring")
    r = client.get(f"/api/projects/{pid}/features/scoring")
    check(13, f"GET /api/projects/{pid}/features/scoring", 200, r)

    # ---- Step 14: POST training ----
    print("Step 14: Start training")
    r = client.post(f"/api/projects/{pid}/training", json={
        "n_iter": 2,
        "budget_s": 30,
        "k": 2
    })
    check(14, f"POST /api/projects/{pid}/training", 200, r)

    # ---- Step 15: Wait and poll training status ----
    print("Step 15: Poll training status (wait 10s)")
    time.sleep(10)
    r = client.get(f"/api/projects/{pid}/training")
    def check_training(resp):
        st = resp.json().get("status", "")
        if st in ("running", "done", "failed"):
            return True, ""
        return False, f"unexpected status '{st}': {resp.text[:200]}"
    check(15, f"GET /api/projects/{pid}/training", 200, r, check_training)
    training_status = r.json().get("status")
    print(f"  training status = {training_status}")

    # If still running, wait more (up to 60s total)
    if training_status == "running":
        print("  Training still running, waiting up to 60 more seconds...")
        for _ in range(12):
            time.sleep(5)
            r = client.get(f"/api/projects/{pid}/training")
            training_status = r.json().get("status")
            print(f"  status = {training_status}")
            if training_status in ("done", "failed"):
                break

    # ---- Step 16: GET leaderboard ----
    print("Step 16: Get leaderboard")
    r = client.get(f"/api/projects/{pid}/training/leaderboard")
    check(16, f"GET /api/projects/{pid}/training/leaderboard", 200, r)

    # ---- Step 17: GET export status ----
    print("Step 17: Get export status")
    r = client.get(f"/api/projects/{pid}/export")
    check(17, f"GET /api/projects/{pid}/export", 200, r)

    # ---- Step 18: POST export ----
    print("Step 18: Trigger export")
    r = client.post(f"/api/projects/{pid}/export")
    if r.status_code in (200, 422):
        PASSED += 1
        print(f"  Step 18: PASS ({r.status_code})")
    else:
        FAILED += 1
        bug = {"step": 18, "endpoint": "POST export", "expected": "200 or 422", "got": r.status_code, "detail": r.text[:300]}
        BUGS.append(bug)
        print(f"  Step 18: FAIL - got {r.status_code}: {r.text[:200]}")

    # ---- Step 19: GET /api/templates/timeseries ----
    print("Step 19: Get template")
    r = client.get("/api/templates/timeseries")
    check(19, "GET /api/templates/timeseries", 200, r)

    # ---- Step 20: DELETE /api/projects/{pid}?hard=true ----
    print("Step 20: Hard delete project")
    r = client.delete(f"/api/projects/{pid}", params={"hard": "true"})
    check(20, f"DELETE /api/projects/{pid}?hard=true", 204, r)

    client.close()

    # ---- REPORT ----
    report = {
        "agent": "dev1",
        "round": 4,
        "passed": PASSED,
        "failed": FAILED,
        "bugs": BUGS
    }
    print("\n" + "=" * 60)
    print(f"REPORT: {json.dumps(report, ensure_ascii=False)}")
    return 0 if FAILED == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
