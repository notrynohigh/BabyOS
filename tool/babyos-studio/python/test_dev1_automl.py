#!/usr/bin/env python3
"""Dev-1 AutoML Happy Path Tester - 20 steps."""

import httpx
import time
import json

BASE = "http://127.0.0.1:18080"
PASSED = 0
FAILED = 0
BUGS = []

def check(step, method, endpoint, expected, resp, detail_extra=""):
    global PASSED, FAILED
    status = resp.status_code
    if isinstance(expected, list):
        ok = status in expected
    else:
        ok = (status == expected)
    if ok:
        PASSED += 1
        print(f"  Step {step}: PASS ({status})")
    else:
        FAILED += 1
        msg = f"step={step} endpoint={method} {endpoint} expected={expected} got={status}"
        if detail_extra:
            msg += f" detail={detail_extra}"
        BUGS.append(msg)
        print(f"  Step {step}: FAIL ({status} != {expected}) {detail_extra}")
    return ok

def main():
    global PASSED, FAILED
    client = httpx.Client(base_url=BASE, timeout=30.0)

    # Step 1: POST /api/projects
    print("Step 1: Create project")
    r = client.post("/api/projects", json={"name":"dev1_test","mode":"timeseries","sampling_rate":100.0})
    check(1, "POST", "/api/projects", 201, r)
    proj = r.json()
    pid = proj.get("project_id") or proj.get("id")
    if not pid:
        print(f"  Could not find project id in response: {proj}")
        FAILED += 1
        BUGS.append("step=1 no project id in response")
        return
    print(f"  Project ID: {pid}")

    # Step 2: GET /api/projects
    print("Step 2: List projects")
    r = client.get("/api/projects")
    check(2, "GET", "/api/projects", 200, r)
    projects = r.json()
    found = False
    items = projects if isinstance(projects, list) else projects.get("items", projects.get("projects", []))
    for p in items:
        if p.get("project_id") == pid or p.get("id") == pid:
            found = True
            break
    if not found:
        print(f"  Note: project not found in list, but got 200")

    # Step 3: GET /api/projects/{pid}
    print("Step 3: Get project")
    r = client.get(f"/api/projects/{pid}")
    check(3, "GET", f"/api/projects/{pid}", 200, r)

    # Step 4: PATCH /api/projects/{pid}
    print("Step 4: Rename project")
    r = client.patch(f"/api/projects/{pid}", json={"name":"dev1_renamed"})
    check(4, "PATCH", f"/api/projects/{pid}", 200, r)

    # Step 5: Import CSV
    print("Step 5: Import CSV")
    csv_lines = ["timestamp,accel_x,accel_y,label"]
    for i in range(200):
        label = 0 if i < 100 else 1
        csv_lines.append(f"{i},{i*0.1},{i*0.2},{label}")
    csv_content = "\n".join(csv_lines)
    mapping = json.dumps({
        "timestamp_col": "timestamp",
        "channels": ["accel_x", "accel_y"],
        "label_col": "label"
    })
    files = [("files", ("test_data.csv", csv_content.encode(), "text/csv"))]
    data = {"mapping": mapping, "import_kind": "append"}
    r = client.post(f"/api/projects/{pid}/dataset", files=files, data=data)
    check(5, "POST", f"/api/projects/{pid}/dataset", 200, r, f"body={r.text[:300]}")

    # Step 6: GET segments
    print("Step 6: Get segments")
    r = client.get(f"/api/projects/{pid}/segments")
    ok = check(6, "GET", f"/api/projects/{pid}/segments", 200, r)
    if ok:
        segs = r.json()
        seg_list = segs if isinstance(segs, list) else segs.get("segments", segs.get("items", []))
        print(f"  Segments count: {len(seg_list)}")

    # Step 7: GET labels
    print("Step 7: Get labels")
    r = client.get(f"/api/projects/{pid}/labels")
    ok = check(7, "GET", f"/api/projects/{pid}/labels", 200, r)
    if ok:
        labels = r.json()
        lbl_list = labels if isinstance(labels, list) else labels.get("labels", labels.get("items", []))
        print(f"  Labels count: {len(lbl_list)}")

    # Step 8: GET dataset
    print("Step 8: Get dataset")
    r = client.get(f"/api/projects/{pid}/dataset")
    ok = check(8, "GET", f"/api/projects/{pid}/dataset", 200, r)
    if ok:
        ds = r.json()
        print(f"  Dataset: files={len(ds.get('files',[]))} rows={ds.get('total_rows',0)} segments={ds.get('n_segments',0)}")

    # Step 9: GET dataset file data
    print("Step 9: Get dataset file data")
    ds_data = r.json()
    fid = None
    files_list = ds_data.get("files", []) if isinstance(ds_data, dict) else []
    if files_list:
        fid = files_list[0].get("file_id")
    if fid:
        r = client.get(f"/api/projects/{pid}/dataset/file/{fid}/data", params={"limit": 5})
        check(9, "GET", f"/api/projects/{pid}/dataset/file/{fid}/data", 200, r)
    else:
        check(9, "GET", "dataset/file/data", 200, httpx.Response(404), "could not find file id")

    # Step 10: PUT features/config
    print("Step 10: Put features config")
    feat_config = {
        "window_len_s": 0.2,
        "n_per_window": 20,
        "step": 10,
        "feature_ids": ["mean","std","rms","ptp","zcr"],
        "freq_enabled": False,
        "norm": "zscore"
    }
    r = client.put(f"/api/projects/{pid}/features/config", json=feat_config)
    check(10, "PUT", f"/api/projects/{pid}/features/config", 200, r, f"body={r.text[:200]}")

    # Step 11: POST features/compute
    print("Step 11: Compute features")
    r = client.post(f"/api/projects/{pid}/features/compute")
    if r.status_code != 200:
        r2 = client.post(f"/api/projects/{pid}/features/compute", json={})
        if r2.status_code == 200:
            r = r2
    check(11, "POST", f"/api/projects/{pid}/features/compute", 200, r, f"body={r.text[:300]}")

    # Step 12: GET features/matrix
    print("Step 12: Get features matrix")
    r = client.get(f"/api/projects/{pid}/features/matrix")
    check(12, "GET", f"/api/projects/{pid}/features/matrix", 200, r, f"body={str(r.text)[:200]}")

    # Step 13: GET features/scoring
    print("Step 13: Get features scoring")
    r = client.get(f"/api/projects/{pid}/features/scoring")
    check(13, "GET", f"/api/projects/{pid}/features/scoring", 200, r, f"body={str(r.text)[:200]}")

    # Step 14: POST training
    print("Step 14: Start training")
    r = client.post(f"/api/projects/{pid}/training", json={"n_iter":2,"budget_s":30,"k":2})
    check(14, "POST", f"/api/projects/{pid}/training", 200, r, f"body={r.text[:300]}")

    # Step 15: Wait and check training status
    print("Step 15: Wait 10s, check training status")
    time.sleep(10)
    r = client.get(f"/api/projects/{pid}/training")
    ok = check(15, "GET", f"/api/projects/{pid}/training", 200, r)
    if ok:
        tdata = r.json()
        status = tdata.get("status") if isinstance(tdata, dict) else None
        print(f"  Training status: {status}")
        if status not in ("running", "done", "failed"):
            FAILED += 1
            BUGS.append(f"step=15 training status={status} not in (running,done,failed)")
            print(f"  Step 15: FAIL - status '{status}' not in expected set")

    # Step 16: GET leaderboard
    print("Step 16: Get leaderboard")
    r = client.get(f"/api/projects/{pid}/training/leaderboard")
    check(16, "GET", f"/api/projects/{pid}/training/leaderboard", 200, r, f"body={str(r.text)[:200]}")

    # Step 17: GET export
    print("Step 17: Get export info")
    r = client.get(f"/api/projects/{pid}/export")
    check(17, "GET", f"/api/projects/{pid}/export", 200, r, f"body={str(r.text)[:200]}")

    # Step 18: POST export
    print("Step 18: POST export")
    r = client.post(f"/api/projects/{pid}/export")
    check(18, "POST", f"/api/projects/{pid}/export", [200, 422], r, f"body={r.text[:200]}")

    # Step 19: GET templates
    print("Step 19: Get timeseries template")
    r = client.get("/api/templates/timeseries")
    check(19, "GET", "/api/templates/timeseries", 200, r, f"body={str(r.text)[:200]}")

    # Step 20: DELETE project
    print("Step 20: Delete project")
    r = client.delete(f"/api/projects/{pid}", params={"hard": "true"})
    check(20, "DELETE", f"/api/projects/{pid}?hard=true", 204, r)

    # Report
    report = {
        "agent": "dev1",
        "round": 5,
        "passed": PASSED,
        "failed": FAILED,
        "bugs": BUGS
    }
    print("\n" + "="*60)
    print("REPORT:")
    print(json.dumps(report, indent=2))

if __name__ == "__main__":
    main()
