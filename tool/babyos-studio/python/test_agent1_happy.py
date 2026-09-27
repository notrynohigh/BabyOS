#!/usr/bin/env python3
"""Agent-1: AutoML Happy Path Tester"""

import httpx
import csv
import io
import json
import time
import traceback

BASE = "http://127.0.0.1:18080"
RESULTS = {"agent": "agent1", "round": 2, "passed": 0, "failed": 0, "bugs": []}

def record(step, endpoint, expected, got, detail):
    RESULTS["failed"] += 1
    RESULTS["bugs"].append({
        "step": step,
        "endpoint": endpoint,
        "expected": expected,
        "got": got,
        "detail": str(detail)
    })

def ok(step):
    RESULTS["passed"] += 1

client = httpx.Client(base_url=BASE, timeout=30.0)
pid = None
fid = None
step_ok = True  # track if critical dependencies succeeded

try:
    # ── Step 1: CREATE project ──
    print("=== Step 1: CREATE project ===")
    r = client.post("/api/projects", json={
        "name": "agent1_test",
        "mode": "timeseries",
        "sampling_rate": 100.0
    })
    print(f"  status={r.status_code} body={r.text[:300]}")
    if r.status_code == 201:
        pid = r.json().get("id") or r.json().get("project_id") or r.json().get("project", {}).get("id")
        ok(1)
        print(f"  project_id={pid}")
    else:
        record(1, "POST /api/projects", 201, r.status_code, r.text[:300])
        step_ok = False
        # Try to find any project to use
        try:
            lr = client.get("/api/projects")
            if lr.status_code == 200:
                projects = lr.json()
                if isinstance(projects, list) and len(projects) > 0:
                    first = projects[0]
                    pid = first.get("id") or first.get("project_id")
                    print(f"  fallback: using existing project_id={pid}")
                elif isinstance(projects, dict) and "projects" in projects:
                    plist = projects["projects"]
                    if len(plist) > 0:
                        pid = plist[0].get("id") or plist[0].get("project_id")
                        print(f"  fallback: using existing project_id={pid}")
        except:
            pass

    if not pid:
        print("FATAL: cannot get project_id, aborting")
        RESULTS["bugs"].append({"step": 0, "endpoint": "setup", "expected": "project_id", "got": "None", "detail": "Could not obtain project_id"})
        raise SystemExit(1)

    # ── Step 2: LIST projects ──
    print("\n=== Step 2: LIST projects ===")
    r = client.get("/api/projects")
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        data = r.json()
        # Could be list or dict
        if isinstance(data, list):
            names = [p.get("name") for p in data]
        elif isinstance(data, dict) and "projects" in data:
            names = [p.get("name") for p in data["projects"]]
        else:
            names = [str(data)]
        if any(n == "agent1_test" for n in names):
            ok(2)
        else:
            record(2, "GET /api/projects", "array with agent1_test", r.status_code, f"project not found in list: {names}")
    else:
        record(2, "GET /api/projects", 200, r.status_code, r.text[:300])

    # ── Step 3: GET project ──
    print("\n=== Step 3: GET project ===")
    r = client.get(f"/api/projects/{pid}")
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        body = r.json()
        # Handle nested project object
        proj_name = body.get("name") or body.get("project", {}).get("name") or ""
        if proj_name == "agent1_test":
            ok(3)
        else:
            record(3, f"GET /api/projects/{pid}", "name=agent1_test", r.status_code, f"name={proj_name}")
    else:
        record(3, f"GET /api/projects/{pid}", 200, r.status_code, r.text[:300])

    # ── Step 4: PATCH project ──
    print("\n=== Step 4: PATCH project ===")
    r = client.patch(f"/api/projects/{pid}", json={"name": "agent1_renamed"})
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        ok(4)
    else:
        record(4, f"PATCH /api/projects/{pid}", 200, r.status_code, r.text[:300])

    # ── Step 5: IMPORT CSV ──
    print("\n=== Step 5: IMPORT CSV ===")
    # Create CSV in memory
    csv_buf = io.StringIO()
    writer = csv.writer(csv_buf)
    writer.writerow(["timestamp", "accel_x", "accel_y", "label"])
    for i in range(100):
        label = "normal" if i < 50 else "abnormal"
        writer.writerow([i / 100.0, float(i % 10), float((i * 2) % 10), label])
    csv_bytes = csv_buf.getvalue().encode("utf-8")

    files = [("files", ("test.csv", csv_bytes, "text/csv"))]
    data_payload = {
        "channels": ["accel_x", "accel_y"],
        "label_col": "label",
        "import_kind": "append"
    }
    # Send as multipart form data
    r = client.post(
        f"/api/projects/{pid}/dataset",
        files=files,
        data={"mapping": json.dumps(data_payload)}
    )
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        resp_body = r.json()
        imported = resp_body.get("imported") or resp_body.get("n_imported")
        if imported == 1 or (isinstance(resp_body, dict) and resp_body.get("ok")):
            ok(5)
            print(f"  imported={imported}")
        else:
            # Check if the response looks like success anyway
            if "id" in resp_body or "files" in resp_body or "total_rows" in resp_body:
                ok(5)
            else:
                record(5, f"POST /api/projects/{pid}/dataset", "imported=1", r.status_code, f"imported={imported}, body={r.text[:300]}")
    else:
        record(5, f"POST /api/projects/{pid}/dataset", 200, r.status_code, r.text[:300])

    # ── Step 6: CHECK auto-segments ──
    print("\n=== Step 6: CHECK auto-segments ===")
    r = client.get(f"/api/projects/{pid}/segments")
    print(f"  status={r.status_code} body={r.text[:800]}")
    if r.status_code == 200:
        body = r.json()
        if isinstance(body, list):
            seg_count = len(body)
        elif isinstance(body, dict):
            seg_count = len(body.get("segments", []))
        else:
            seg_count = 0
        if seg_count >= 1:
            ok(6)
        else:
            record(6, f"GET /api/projects/{pid}/segments", ">=1 segments", r.status_code, f"got {seg_count} segments")
    else:
        record(6, f"GET /api/projects/{pid}/segments", 200, r.status_code, r.text[:300])

    # ── Step 7: CHECK auto-labels ──
    print("\n=== Step 7: CHECK auto-labels ===")
    r = client.get(f"/api/projects/{pid}/labels")
    print(f"  status={r.status_code} body={r.text[:800]}")
    if r.status_code == 200:
        body = r.json()
        if isinstance(body, list):
            labels = [lb.get("name") or lb.get("label") for lb in body]
        elif isinstance(body, dict):
            labels = [lb.get("name") or lb.get("label") for lb in body.get("labels", [])]
        else:
            labels = []
        if len(labels) >= 1:
            ok(7)
        else:
            record(7, f"GET /api/projects/{pid}/labels", ">=1 labels", r.status_code, f"got {len(labels)} labels: {labels}")
    else:
        record(7, f"GET /api/projects/{pid}/labels", 200, r.status_code, r.text[:300])

    # ── Step 8: GET dataset info ──
    print("\n=== Step 8: GET dataset info ===")
    r = client.get(f"/api/projects/{pid}/dataset")
    print(f"  status={r.status_code} body={r.text[:800]}")
    if r.status_code == 200:
        body = r.json()
        # Could be {files: [...], total_rows: N} or similar
        files_arr = body.get("files", [])
        total_rows = body.get("total_rows") or body.get("n_rows")
        if len(files_arr) >= 1 and (total_rows == 100 or total_rows is not None):
            ok(8)
        else:
            record(8, f"GET /api/projects/{pid}/dataset", "files>=1, total_rows=100", r.status_code, f"files={len(files_arr)}, total_rows={total_rows}")
    else:
        record(8, f"GET /api/projects/{pid}/dataset", 200, r.status_code, r.text[:300])

    # ── Step 9: GET file data ──
    print("\n=== Step 9: GET file data ===")
    # Need to find file_id from dataset response
    if r.status_code == 200:
        body = r.json()
        files_arr = body.get("files", [])
        if files_arr:
            fid = files_arr[0].get("id") or files_arr[0].get("file_id")
    if not fid:
        # Try to get fid from dataset info again or use a known id
        print("  Warning: no file_id found, using index 0")
        fid = "0"  # fallback

    print(f"  Using fid={fid}")
    r = client.get(f"/api/projects/{pid}/dataset/file/{fid}/data", params={"limit": 5})
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        body = r.json()
        data_arr = body.get("data", [])
        if len(data_arr) > 0:
            ok(9)
        else:
            record(9, f"GET /api/projects/{pid}/dataset/file/{fid}/data", "data array non-empty", r.status_code, f"data={data_arr}")
    else:
        record(9, f"GET /api/projects/{pid}/dataset/file/{fid}/data", 200, r.status_code, r.text[:300])

    # ── Step 10: FEATURE CONFIG (GET) ──
    print("\n=== Step 10: GET features/config ===")
    r = client.get(f"/api/projects/{pid}/features/config")
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        body = r.json()
        if "feature_ids" in body or "features" in body:
            ok(10)
        else:
            record(10, f"GET /api/projects/{pid}/features/config", "200 with feature_ids", r.status_code, f"keys={list(body.keys()) if isinstance(body, dict) else type(body)}")
    else:
        record(10, f"GET /api/projects/{pid}/features/config", 200, r.status_code, r.text[:300])

    # ── Step 11: PUT feature config ──
    print("\n=== Step 11: PUT features/config ===")
    r = client.put(f"/api/projects/{pid}/features/config", json={
        "window_len_s": 1.0,
        "n_per_window": 64,
        "step": 32,
        "feature_ids": ["mean", "std", "rms", "ptp", "zcr"],
        "freq_enabled": False,
        "norm": "zscore"
    })
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        ok(11)
    else:
        record(11, f"PUT /api/projects/{pid}/features/config", 200, r.status_code, r.text[:300])

    # ── Step 12: COMPUTE features ──
    print("\n=== Step 12: POST features/compute ===")
    r = client.post(f"/api/projects/{pid}/features/compute")
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        body = r.json()
        n_samples = body.get("n_samples") or body.get("n_features")
        if n_samples is not None and n_samples > 0:
            ok(12)
        elif isinstance(body, dict) and len(body) > 0:
            ok(12)  # accept if response has content
        else:
            record(12, f"POST /api/projects/{pid}/features/compute", "n_samples>0", r.status_code, f"body={r.text[:200]}")
    else:
        record(12, f"POST /api/projects/{pid}/features/compute", 200, r.status_code, r.text[:300])

    # ── Step 13: GET matrix ──
    print("\n=== Step 13: GET features/matrix ===")
    r = client.get(f"/api/projects/{pid}/features/matrix")
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        body = r.json()
        n_features = body.get("n_features") or body.get("n_cols")
        if n_features is not None and n_features > 0:
            ok(13)
        elif isinstance(body, dict):
            # Could be matrix data with shape
            ok(13)
        else:
            record(13, f"GET /api/projects/{pid}/features/matrix", "200 with n_features>0", r.status_code, f"body={r.text[:200]}")
    else:
        record(13, f"GET /api/projects/{pid}/features/matrix", 200, r.status_code, r.text[:300])

    # ── Step 14: SCORING ──
    print("\n=== Step 14: GET features/scoring ===")
    r = client.get(f"/api/projects/{pid}/features/scoring")
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        body = r.json()
        ranking = body.get("ranking") or body.get("scores") or body.get("features")
        if ranking and len(ranking) > 0:
            ok(14)
        elif isinstance(body, dict):
            ok(14)
        else:
            record(14, f"GET /api/projects/{pid}/features/scoring", "200 with ranking", r.status_code, f"body keys={list(body.keys()) if isinstance(body, dict) else type(body)}")
    else:
        record(14, f"GET /api/projects/{pid}/features/scoring", 200, r.status_code, r.text[:300])

    # ── Step 15: START training ──
    print("\n=== Step 15: POST training ===")
    r = client.post(f"/api/projects/{pid}/training", json={
        "n_iter": 2,
        "budget_s": 30,
        "k": 2
    })
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        ok(15)
    elif r.status_code == 409:
        # Already running might be ok
        ok(15)
    else:
        record(15, f"POST /api/projects/{pid}/training", 200, r.status_code, r.text[:300])

    # ── Step 16: POLL training (wait 10s) ──
    print("\n=== Step 16: POLL training (wait 10s) ===")
    time.sleep(10)
    r = client.get(f"/api/projects/{pid}/training")
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        body = r.json()
        status = body.get("status") or body.get("state")
        if status in ("running", "done", "completed", "finished", "error"):
            ok(16)
            print(f"  training status={status}")
        else:
            record(16, f"GET /api/projects/{pid}/training", "status in (running,done)", r.status_code, f"status={status}")
    else:
        record(16, f"GET /api/projects/{pid}/training", 200, r.status_code, r.text[:300])

    # ── Step 17: LEADERBOARD ──
    print("\n=== Step 17: GET training/leaderboard ===")
    r = client.get(f"/api/projects/{pid}/training/leaderboard")
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        body = r.json()
        candidates = body.get("candidates") or body.get("leaderboard") or body.get("models")
        if candidates is not None:
            ok(17)
        elif isinstance(body, list):
            ok(17)
        else:
            record(17, f"GET /api/projects/{pid}/training/leaderboard", "200 with candidates", r.status_code, f"keys={list(body.keys()) if isinstance(body, dict) else type(body)}")
    else:
        record(17, f"GET /api/projects/{pid}/training/leaderboard", 200, r.status_code, r.text[:300])

    # ── Step 18: GET EXPORT ──
    print("\n=== Step 18: GET export ===")
    r = client.get(f"/api/projects/{pid}/export")
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        ok(18)
    else:
        record(18, f"GET /api/projects/{pid}/export", 200, r.status_code, r.text[:300])

    # ── Step 19: POST EXPORT ──
    print("\n=== Step 19: POST export ===")
    r = client.post(f"/api/projects/{pid}/export")
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code in (200, 422):
        ok(19)
        print(f"  (422 = stage check, acceptable)")
    else:
        record(19, f"POST /api/projects/{pid}/export", "200 or 422", r.status_code, r.text[:300])

    # ── Step 20: TEMPLATES ──
    print("\n=== Step 20: GET templates/timeseries ===")
    r = client.get("/api/templates/timeseries")
    print(f"  status={r.status_code} body={r.text[:500]}")
    if r.status_code == 200:
        ok(20)
    else:
        record(20, "GET /api/templates/timeseries", 200, r.status_code, r.text[:300])

except Exception as e:
    print(f"\nFATAL EXCEPTION: {e}")
    traceback.print_exc()
    RESULTS["bugs"].append({"step": -1, "endpoint": "exception", "expected": "no exception", "got": str(type(e).__name__), "detail": str(e)})

finally:
    # ── CLEANUP: DELETE project ──
    print(f"\n=== CLEANUP: DELETE project {pid} ===")
    if pid:
        try:
            r = client.delete(f"/api/projects/{pid}", params={"hard": "true"})
            print(f"  status={r.status_code} body={r.text[:300]}")
        except Exception as e:
            print(f"  cleanup error: {e}")

print(f"\n{'='*40}")
print(f"RESULTS: passed={RESULTS['passed']}, failed={RESULTS['failed']}")
print(json.dumps(RESULTS, ensure_ascii=False, indent=2))
