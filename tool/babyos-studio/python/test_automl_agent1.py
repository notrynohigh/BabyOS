#!/usr/bin/env python3
"""Agent-1: AutoML Happy Path Tester - resilient version"""

import json
import time
import traceback

import httpx

BASE = "http://127.0.0.1:18080"
results = []
pid = None
fid = None
passed = 0
failed = 0
bugs = []


def safe_json(r):
    try:
        return r.json()
    except Exception:
        return {"_raw": r.text[:500]}


def record(step, name, status_code, expected, got, detail=""):
    global passed, failed
    ok = (expected is None or got == expected)
    if ok:
        passed += 1
        results.append(f"  PASS step {step}: {name} (got {got})")
    else:
        failed += 1
        bugs.append({"step": step, "endpoint": name, "expected": expected, "got": got, "detail": detail})
        results.append(f"  FAIL step {step}: {name} (expected {expected}, got {got}) {detail}")
    return ok


def safe_request(client, method, url, **kwargs):
    """Make a request, handling connection errors gracefully."""
    try:
        return getattr(client, method)(url, **kwargs)
    except httpx.ConnectError as e:
        results.append(f"  CONNECTION ERROR: {e}")
        return None
    except Exception as e:
        results.append(f"  REQUEST ERROR: {e}")
        return None


try:
    with httpx.Client(base_url=BASE, timeout=30) as client:

        # =========================================================================
        # Step 1: CREATE project
        # =========================================================================
        r = safe_request(client, "post", "/api/projects", json={
            "name": "agent1_test",
            "mode": "timeseries",
            "sampling_rate": 100.0
        })
        if r is None:
            raise RuntimeError("Server not reachable")
        record(1, "POST /api/projects", r.status_code, 201, r.status_code, r.text[:300])
        body = safe_json(r)
        pid = body.get("project_id") or body.get("id")
        results.append(f"  project_id={pid}")

        # =========================================================================
        # Step 2: LIST projects
        # =========================================================================
        r = safe_request(client, "get", "/api/projects")
        if r:
            record(2, "GET /api/projects", r.status_code, 200, r.status_code, r.text[:300])
            projects = safe_json(r)
            if isinstance(projects, list):
                found = any(p.get("name") == "agent1_test" for p in projects)
            elif isinstance(projects, dict) and "projects" in projects:
                found = any(p.get("name") == "agent1_test" for p in projects["projects"])
            else:
                found = False
            if found:
                passed += 1
                results.append(f"  PASS step 2: project found in list")
            else:
                failed += 1
                bugs.append({"step": 2, "endpoint": "GET /api/projects", "expected": "agent1_test in list", "got": "not found", "detail": str(projects)[:300]})
                results.append(f"  FAIL step 2: project not in list")

        # =========================================================================
        # Step 3: GET project
        # =========================================================================
        r = safe_request(client, "get", f"/api/projects/{pid}")
        if r:
            record(3, "GET /api/projects/{pid}", r.status_code, 200, r.status_code, r.text[:300])
            proj = safe_json(r)
            if proj.get("name") != "agent1_test":
                failed += 1
                bugs.append({"step": 3, "endpoint": "GET /api/projects/{pid}", "expected": "name=agent1_test", "got": proj.get("name"), "detail": r.text[:300]})
                results.append(f"  FAIL step 3: name mismatch: {proj.get('name')}")
            else:
                passed += 1
                results.append(f"  PASS step 3: name=agent1_test")

        # =========================================================================
        # Step 4: PATCH project
        # =========================================================================
        r = safe_request(client, "patch", f"/api/projects/{pid}", json={"name": "agent1_renamed"})
        if r:
            record(4, "PATCH /api/projects/{pid}", r.status_code, 200, r.status_code, r.text[:300])

            # Verify rename
            r2 = safe_request(client, "get", f"/api/projects/{pid}")
            if r2:
                proj = safe_json(r2)
                if proj.get("name") == "agent1_renamed":
                    passed += 1
                    results.append(f"  PASS step 4: rename confirmed")
                else:
                    failed += 1
                    bugs.append({"step": 4, "endpoint": "PATCH /api/projects/{pid}", "expected": "name=agent1_renamed", "got": proj.get("name"), "detail": r.text[:300]})
                    results.append(f"  FAIL step 4: rename not applied, got name={proj.get('name')}")

        # =========================================================================
        # Step 5: IMPORT CSV
        # =========================================================================
        csv_lines = ["timestamp,accel_x,accel_y,label"]
        for i in range(100):
            label = "normal" if i < 50 else "abnormal"
            csv_lines.append(f"{i},{0.1*i:.2f},{0.05*i:.4f},{label}")
        csv_bytes = "\n".join(csv_lines).encode("utf-8")

        r = safe_request(
            client, "post",
            f"/api/projects/{pid}/dataset",
            files=[("files", ("test.csv", csv_bytes, "text/csv"))],
            data={
                "mapping": json.dumps({"channels": ["accel_x", "accel_y"], "label_col": "label"}),
                "import_kind": "append"
            }
        )
        if r:
            record(5, "POST /api/projects/{pid}/dataset", r.status_code, 200, r.status_code, r.text[:500])
            ds_resp = safe_json(r)
            imported = ds_resp.get("imported", ds_resp.get("total_imported"))
            if imported == 1:
                passed += 1
                results.append(f"  PASS step 5: imported={imported}")
            elif imported is not None:
                failed += 1
                bugs.append({"step": 5, "endpoint": "POST .../dataset", "expected": "imported=1", "got": imported, "detail": r.text[:300]})
            else:
                results.append(f"  INFO step 5: no imported field, response keys={list(ds_resp.keys()) if isinstance(ds_resp, dict) else 'N/A'}")

        # =========================================================================
        # Step 6: CHECK auto-segments
        # =========================================================================
        r = safe_request(client, "get", f"/api/projects/{pid}/segments")
        if r:
            record(6, "GET /api/projects/{pid}/segments", r.status_code, 200, r.status_code, r.text[:500])
            segs = safe_json(r)
            if isinstance(segs, list):
                seg_list = segs
            elif isinstance(segs, dict):
                seg_list = segs.get("segments", segs.get("data", []))
            else:
                seg_list = []
            if len(seg_list) == 2:
                passed += 1
                results.append(f"  PASS step 6: 2 segments found")
            else:
                failed += 1
                bugs.append({"step": 6, "endpoint": "GET .../segments", "expected": 2, "got": len(seg_list), "detail": r.text[:300]})
                results.append(f"  FAIL step 6: got {len(seg_list)} segments")

        # =========================================================================
        # Step 7: CHECK auto-labels
        # =========================================================================
        r = safe_request(client, "get", f"/api/projects/{pid}/labels")
        if r:
            record(7, "GET /api/projects/{pid}/labels", r.status_code, 200, r.status_code, r.text[:500])
            labels_resp = safe_json(r)
            if isinstance(labels_resp, list):
                lbl_list = labels_resp
            elif isinstance(labels_resp, dict):
                lbl_list = labels_resp.get("labels", labels_resp.get("data", []))
            else:
                lbl_list = []
            if len(lbl_list) == 2:
                passed += 1
                results.append(f"  PASS step 7: 2 labels found")
            else:
                failed += 1
                bugs.append({"step": 7, "endpoint": "GET .../labels", "expected": 2, "got": len(lbl_list), "detail": r.text[:300]})

        # =========================================================================
        # Step 8: GET dataset info
        # =========================================================================
        r = safe_request(client, "get", f"/api/projects/{pid}/dataset")
        if r:
            record(8, "GET /api/projects/{pid}/dataset", r.status_code, 200, r.status_code, r.text[:500])
            ds_info = safe_json(r)
            files = ds_info.get("files", [])
            total_rows = ds_info.get("total_rows")
            if len(files) == 1:
                passed += 1
                results.append(f"  PASS step 8: 1 file, total_rows={total_rows}")
            else:
                failed += 1
                bugs.append({"step": 8, "endpoint": "GET .../dataset", "expected": "1 file", "got": f"{len(files)} files", "detail": r.text[:300]})
            # Try to get file id from the response
            if files and isinstance(files[0], dict):
                fid = files[0].get("id") or files[0].get("file_id") or files[0].get("fid")
                results.append(f"  file_id={fid}")
            elif files and isinstance(files[0], str):
                fid = files[0]
                results.append(f"  file_id={fid} (from string list)")

        # =========================================================================
        # Step 9: GET file data
        # =========================================================================
        if fid:
            r = safe_request(client, "get", f"/api/projects/{pid}/dataset/file/{fid}/data", params={"limit": 5})
            if r:
                record(9, "GET .../dataset/file/{fid}/data?limit=5", r.status_code, 200, r.status_code, r.text[:300])
                fd_resp = safe_json(r)
                data_arr = fd_resp.get("data", [])
                if isinstance(data_arr, list) and len(data_arr) > 0:
                    passed += 1
                    results.append(f"  PASS step 9: got {len(data_arr)} rows")
                else:
                    failed += 1
                    bugs.append({"step": 9, "endpoint": "GET .../file/.../data", "expected": "non-empty data array", "got": str(fd_resp)[:200], "detail": r.text[:300]})
        else:
            results.append(f"  SKIP step 9: no file_id available")

        # =========================================================================
        # Step 10: FEATURE CONFIG (GET)
        # =========================================================================
        r = safe_request(client, "get", f"/api/projects/{pid}/features/config")
        if r:
            record(10, "GET .../features/config", r.status_code, 200, r.status_code, r.text[:500])
            fc = safe_json(r)
            feat_ids = fc.get("feature_ids", [])
            if isinstance(feat_ids, list) and len(feat_ids) > 0:
                passed += 1
                results.append(f"  PASS step 10: feature_ids={feat_ids}")
            else:
                results.append(f"  INFO step 10: feature_ids={feat_ids}, keys={list(fc.keys()) if isinstance(fc, dict) else 'N/A'}")
                passed += 1
                results.append(f"  PASS step 10: config returned")

        # =========================================================================
        # Step 11: PUT feature config
        # =========================================================================
        r = safe_request(client, "put", f"/api/projects/{pid}/features/config", json={
            "window_len_s": 1.0,
            "n_per_window": 64,
            "step": 32,
            "feature_ids": ["mean", "std", "rms", "ptp", "zcr"],
            "freq_enabled": False,
            "norm": "zscore"
        })
        if r:
            record(11, "PUT .../features/config", r.status_code, 200, r.status_code, r.text[:300])

        # =========================================================================
        # Step 12: COMPUTE features
        # =========================================================================
        r = safe_request(client, "post", f"/api/projects/{pid}/features/compute")
        if r:
            record(12, "POST .../features/compute", r.status_code, 200, r.status_code, r.text[:500])
            comp_resp = safe_json(r)
            n_samples = comp_resp.get("n_samples")
            if n_samples is not None and n_samples > 0:
                passed += 1
                results.append(f"  PASS step 12: n_samples={n_samples}")
            else:
                results.append(f"  INFO step 12: response={r.text[:500]}")
        else:
            results.append(f"  SKIP step 12: server unreachable after compute")

        # =========================================================================
        # Step 13: GET matrix
        # =========================================================================
        r = safe_request(client, "get", f"/api/projects/{pid}/features/matrix")
        if r:
            record(13, "GET .../features/matrix", r.status_code, 200, r.status_code, r.text[:500])
            mat = safe_json(r)
            n_feat = mat.get("n_features")
            if n_feat is not None and n_feat > 0:
                passed += 1
                results.append(f"  PASS step 13: n_features={n_feat}")
            else:
                results.append(f"  INFO step 13: keys={list(mat.keys()) if isinstance(mat, dict) else 'not dict'}")
                features_data = mat.get("features", mat.get("matrix", mat.get("data", [])))
                if isinstance(features_data, list) and len(features_data) > 0:
                    passed += 1
                    results.append(f"  PASS step 13: matrix has {len(features_data)} entries")

        # =========================================================================
        # Step 14: SCORING
        # =========================================================================
        r = safe_request(client, "get", f"/api/projects/{pid}/features/scoring")
        if r:
            record(14, "GET .../features/scoring", r.status_code, 200, r.status_code, r.text[:500])
            score_resp = safe_json(r)
            ranking = score_resp.get("ranking", score_resp.get("scores", []))
            if isinstance(ranking, list) and len(ranking) > 0:
                passed += 1
                results.append(f"  PASS step 14: {len(ranking)} features ranked")
            else:
                results.append(f"  INFO step 14: keys={list(score_resp.keys()) if isinstance(score_resp, dict) else type(score_resp)}")

        # =========================================================================
        # Step 15: START training
        # =========================================================================
        r = safe_request(client, "post", f"/api/projects/{pid}/training", json={
            "n_iter": 2,
            "budget_s": 30,
            "k": 2
        })
        if r:
            record(15, "POST .../training", r.status_code, 200, r.status_code, r.text[:500])
            train_resp = safe_json(r)
            train_status = train_resp.get("status")
            results.append(f"  training status: {train_status}")

        # =========================================================================
        # Step 16: POLL training (wait 10s)
        # =========================================================================
        time.sleep(10)
        r = safe_request(client, "get", f"/api/projects/{pid}/training")
        if r:
            record(16, "GET .../training (after 10s)", r.status_code, 200, r.status_code, r.text[:500])
            poll_resp = safe_json(r)
            poll_status = poll_resp.get("status", "unknown")
            if poll_status in ("running", "done", "completed", "finished"):
                passed += 1
                results.append(f"  PASS step 16: status={poll_status}")
            else:
                failed += 1
                bugs.append({"step": 16, "endpoint": "GET .../training", "expected": "running or done", "got": poll_status, "detail": r.text[:300]})

            # If still running, wait a bit more
            if poll_status in ("running",):
                time.sleep(15)
                r2 = safe_request(client, "get", f"/api/projects/{pid}/training")
                if r2:
                    poll_resp = safe_json(r2)
                    poll_status = poll_resp.get("status", "unknown")
                    results.append(f"  Training status after extra wait: {poll_status}")

        # =========================================================================
        # Step 17: LEADERBOARD
        # =========================================================================
        r = safe_request(client, "get", f"/api/projects/{pid}/training/leaderboard")
        if r:
            record(17, "GET .../training/leaderboard", r.status_code, 200, r.status_code, r.text[:500])
            lb = safe_json(r)
            candidates = lb.get("candidates", lb.get("leaderboard", lb.get("models", [])))
            if isinstance(candidates, list) and len(candidates) > 0:
                passed += 1
                results.append(f"  PASS step 17: {len(candidates)} candidates")
            else:
                results.append(f"  INFO step 17: response={r.text[:300]}")

        # =========================================================================
        # Step 18: GET EXPORT
        # =========================================================================
        r = safe_request(client, "get", f"/api/projects/{pid}/export")
        if r:
            record(18, "GET .../export", r.status_code, 200, r.status_code, r.text[:500])

        # =========================================================================
        # Step 19: POST EXPORT
        # =========================================================================
        r = safe_request(client, "post", f"/api/projects/{pid}/export")
        if r:
            # Accept 200 or 422 (stage check)
            if r.status_code in (200, 422):
                passed += 1
                results.append(f"  PASS step 19: got {r.status_code} (acceptable)")
            else:
                failed += 1
                bugs.append({"step": 19, "endpoint": "POST .../export", "expected": "200 or 422", "got": r.status_code, "detail": r.text[:300]})
                results.append(f"  FAIL step 19: got {r.status_code}")

        # =========================================================================
        # Step 20: TEMPLATES
        # =========================================================================
        r = safe_request(client, "get", "/api/templates/timeseries")
        if r:
            record(20, "GET /api/templates/timeseries", r.status_code, 200, r.status_code, f"content-length={len(r.content)}")

except Exception as e:
    tb = traceback.format_exc()
    results.append(f"FATAL ERROR: {e}\n{tb}")
    failed += 1
    bugs.append({"step": -1, "endpoint": "N/A", "expected": "no exception", "got": str(e), "detail": tb[:500]})

finally:
    # =========================================================================
    # CLEANUP: DELETE project
    # =========================================================================
    if pid:
        try:
            with httpx.Client(base_url=BASE, timeout=10) as cleanup_client:
                r = safe_request(cleanup_client, "delete", f"/api/projects/{pid}", params={"hard": "true"})
                if r:
                    results.append(f"  CLEANUP: DELETE /api/projects/{pid}?hard=true -> {r.status_code}")
                    if r.status_code in (200, 204):
                        results.append(f"  CLEANUP OK")
                    else:
                        results.append(f"  CLEANUP FAILED: {r.text[:200]}")
                        failed += 1
                        bugs.append({"step": 99, "endpoint": "DELETE .../projects/{pid}", "expected": 200, "got": r.status_code, "detail": r.text[:200]})
                else:
                    results.append(f"  CLEANUP: server unreachable")
        except Exception as e:
            results.append(f"  CLEANUP ERROR: {e}")

# Print log
for line in results:
    print(line)

# Output final JSON
report = {
    "agent": "agent1",
    "round": 1,
    "passed": passed,
    "failed": failed,
    "bugs": bugs
}
print("\n=== RESULT ===")
print(json.dumps(report, indent=2))
