"""Dev-2: Edge Case Tester for BabyOS AutoML backend."""
import httpx
import json
import sys
import tempfile
import os
from pathlib import Path

BASE = "http://127.0.0.1:18080"
results = []
bugs = []
passed = 0
failed = 0


def test(step, method, endpoint, expected_status, body=None, files=None, data=None, params=None):
    global passed, failed
    url = f"{BASE}{endpoint}"
    try:
        if method == "GET":
            r = httpx.get(url, params=params, timeout=30)
        elif method == "POST":
            if files:
                r = httpx.post(url, files=files, data=data or {}, timeout=30)
            elif body is not None:
                r = httpx.post(url, json=body, timeout=30)
            else:
                r = httpx.post(url, timeout=30)
        elif method == "PATCH":
            r = httpx.patch(url, json=body, timeout=30)
        elif method == "DELETE":
            r = httpx.delete(url, params=params, timeout=30)
        else:
            print(f"Step {step}: Unknown method {method}")
            failed += 1
            return None

        ok = r.status_code == expected_status
        if ok:
            passed += 1
            print(f"  Step {step}: PASS ({r.status_code})")
        else:
            failed += 1
            detail = r.text[:200] if r.text else ""
            bugs.append({"step": step, "endpoint": f"{method} {endpoint}", "expected": expected_status, "got": r.status_code, "detail": detail})
            print(f"  Step {step}: FAIL expected={expected_status} got={r.status_code} detail={detail[:100]}")
        return r
    except Exception as e:
        failed += 1
        bugs.append({"step": step, "endpoint": f"{method} {endpoint}", "expected": expected_status, "got": "EXCEPTION", "detail": str(e)})
        print(f"  Step {step}: EXCEPTION {e}")
        return None


def cleanup_project(pid):
    """Hard delete a project, ignoring errors."""
    if pid:
        try:
            httpx.delete(f"{BASE}/api/projects/{pid}?hard=true", timeout=10)
        except:
            pass


def main():
    global passed, failed
    pid = None
    copy_pid = None

    print("=== Dev-2 Edge Case Tests ===\n")

    # Step 1: POST /api/projects {} -> 422
    print("Step 1: POST /api/projects {} -> 422")
    test(1, "POST", "/api/projects", 422, body={})

    # Step 2: POST /api/projects {"name":""} -> 422
    print("Step 2: POST /api/projects {name:''} -> 422")
    test(2, "POST", "/api/projects", 422, body={"name": ""})

    # Step 3: GET /api/projects/nonexistent -> 404
    print("Step 3: GET /api/projects/nonexistent -> 404")
    test(3, "GET", "/api/projects/nonexistent", 404)

    # Step 4: PATCH /api/projects/nonexistent {"name":"x"} -> 404
    print("Step 4: PATCH /api/projects/nonexistent -> 404")
    test(4, "PATCH", "/api/projects/nonexistent", 404, body={"name": "x"})

    # Step 5: DELETE /api/projects/nonexistent -> 404
    print("Step 5: DELETE /api/projects/nonexistent -> 404")
    test(5, "DELETE", "/api/projects/nonexistent", 404)

    # Step 6: POST /api/projects {"name":"dev2_edge",...} -> 201
    print("Step 6: Create project -> 201")
    r6 = test(6, "POST", "/api/projects", 201, body={
        "name": "dev2_edge",
        "mode": "timeseries",
        "sampling_rate": 100.0
    })
    if r6 and r6.status_code == 201:
        pid = r6.json().get("project_id")
        print(f"    Created project: {pid}")

    # Step 7: Import CSV without label column -> 200
    print("Step 7: Import CSV without label column -> 200")
    csv_content = b"ch1,ch2,ch3\n1.0,2.0,3.0\n4.0,5.0,6.0\n7.0,8.0,9.0\n"
    with tempfile.NamedTemporaryFile(suffix=".csv", delete=False, mode="wb") as f:
        f.write(csv_content)
        tmp_csv = f.name
    try:
        with open(tmp_csv, "rb") as f:
            mapping = json.dumps({"channels": ["ch1", "ch2", "ch3"]})
            test(7, "POST", f"/api/projects/{pid}/dataset", 200,
                 files={"files": ("test.csv", f, "text/csv")},
                 data={"mapping": mapping, "import_kind": "append"})
    finally:
        os.unlink(tmp_csv)

    # Step 8: GET /api/projects/{pid}/segments -> []
    print("Step 8: GET segments -> []")
    r8 = test(8, "GET", f"/api/projects/{pid}/segments", 200)
    if r8 and r8.status_code == 200:
        segs = r8.json()
        if segs == []:
            print(f"    Correctly returned empty list")
        else:
            print(f"    WARNING: Expected [], got {segs}")

    # Step 9: POST /api/projects/{pid}/features/compute -> 422 (no segments)
    print("Step 9: Compute features without segments -> 422")
    test(9, "POST", f"/api/projects/{pid}/features/compute", 422)

    # Step 10: GET /api/projects/{pid}/features/scoring -> 422 (no matrix)
    print("Step 10: GET scoring without matrix -> 422")
    test(10, "GET", f"/api/projects/{pid}/features/scoring", 422)

    # Step 11: POST /api/projects/{pid}/training -> 422 (stage not ready)
    print("Step 11: Start training without features -> 422")
    test(11, "POST", f"/api/projects/{pid}/training", 422, body={
        "n_iter": 2, "budget_s": 30, "k": 2
    })

    # Step 12: POST segment with invalid label_id -> 422
    print("Step 12: Add segment with invalid label_id -> 422")
    test(12, "POST", f"/api/projects/{pid}/segments", 422, body={
        "file_id": "x", "start": 0, "end": 10, "label_id": 999
    })

    # Step 13: Add label, add segment, then add overlapping segment -> 422
    print("Step 13: Overlapping segments -> 422")
    # First add a label
    r_label = test(13, "POST", f"/api/projects/{pid}/labels", 201, body={"name": "normal"})
    if r_label and r_label.status_code == 201:
        label_id = r_label.json().get("label_id", 0)
        # Get the file_id from dataset info
        r_ds = httpx.get(f"{BASE}/api/projects/{pid}/dataset", timeout=10)
        if r_ds.status_code == 200:
            ds_info = r_ds.json()
            files = ds_info.get("files", [])
            if files:
                file_id = files[0].get("file_id", "")
                # Add first segment
                seg1 = test("13a", "POST", f"/api/projects/{pid}/segments", 201, body={
                    "file_id": file_id, "start": 0, "end": 50, "label_id": label_id
                })
                # Add overlapping segment
                if seg1 and seg1.status_code == 201:
                    test("13b", "POST", f"/api/projects/{pid}/segments", 422, body={
                        "file_id": file_id, "start": 25, "end": 75, "label_id": label_id
                    })
                else:
                    print("    Skipping overlap test (first segment failed)")
                    failed += 1
                    bugs.append({"step": "13", "endpoint": "POST segments (overlap)", "expected": 422, "got": "SKIPPED", "detail": "First segment add failed"})
            else:
                print("    No files found in dataset")
                failed += 1
                bugs.append({"step": "13", "endpoint": "POST segments (overlap)", "expected": 422, "got": "SKIPPED", "detail": "No files in dataset"})
        else:
            print("    Could not get dataset info")
            failed += 1
            bugs.append({"step": "13", "endpoint": "POST segments (overlap)", "expected": 422, "got": "SKIPPED", "detail": "Dataset info request failed"})

    # Step 14: POST /api/projects/{pid}/copy -> 201
    print("Step 14: Copy project -> 201")
    r14 = test(14, "POST", f"/api/projects/{pid}/copy", 201)
    if r14 and r14.status_code == 201:
        copy_pid = r14.json().get("project_id")
        print(f"    Copied project: {copy_pid}")

    # Step 15: GET /api/projects/trash -> 200
    print("Step 15: GET trash -> 200")
    test(15, "GET", "/api/projects/trash", 200)

    # Step 16: DELETE /api/projects/{pid} -> 204 (soft)
    print("Step 16: Soft delete project -> 204")
    test(16, "DELETE", f"/api/projects/{pid}", 204)

    # Step 17: Restore from trash -> 200
    print("Step 17: Restore from trash -> 200")
    # First get trash list to find trash_id
    r_trash = httpx.get(f"{BASE}/api/projects/trash", timeout=10)
    trash_id = None
    if r_trash.status_code == 200:
        trash_list = r_trash.json()
        for item in trash_list:
            if item.get("original_id") == pid or item.get("project_id") == pid:
                trash_id = item.get("trash_id") or item.get("project_id")
                break
        if not trash_id and trash_list:
            trash_id = trash_list[0].get("trash_id") or trash_list[0].get("project_id")
    if trash_id:
        r17 = test(17, "POST", f"/api/projects/trash/{trash_id}/restore", 200)
        if r17 and r17.status_code == 200:
            pid = r17.json().get("project_id", pid)
            print(f"    Restored project: {pid}")
    else:
        print("    No trash entries found")
        failed += 1
        bugs.append({"step": 17, "endpoint": "POST trash/restore", "expected": 201, "got": "SKIPPED", "detail": "No trash_id found"})

    # Step 18: DELETE /api/projects/{pid}?hard=true -> 204
    print("Step 18: Hard delete project -> 204")
    test(18, "DELETE", f"/api/projects/{pid}?hard=true", 204)
    pid = None

    # Step 19: DELETE copy ?hard=true -> 204
    print("Step 19: Hard delete copy -> 204")
    test(19, "DELETE", f"/api/projects/{copy_pid}?hard=true", 204)
    copy_pid = None

    # Step 20: GET /api/projects/{pid}/dataset/file/nonexistent/data -> 404
    print("Step 20: GET nonexistent file data -> 404")
    # Need a valid project for this test
    r20_proj = httpx.post(f"{BASE}/api/projects", json={"name": "dev2_edge_test20", "mode": "table"}, timeout=10)
    test20_pid = None
    if r20_proj.status_code == 201:
        test20_pid = r20_proj.json().get("project_id")
        test(20, "GET", f"/api/projects/{test20_pid}/dataset/file/nonexistent/data", 404)
        # Cleanup
        if test20_pid:
            httpx.delete(f"{BASE}/api/projects/{test20_pid}?hard=true", timeout=10)
    else:
        print("    Could not create test project for step 20")
        failed += 1
        bugs.append({"step": 20, "endpoint": "GET file/nonexistent/data", "expected": 404, "got": "SKIPPED", "detail": "Could not create test project"})

    # Final cleanup
    cleanup_project(pid)
    cleanup_project(copy_pid)

    # Report
    print(f"\n=== RESULTS ===")
    print(f"Passed: {passed}, Failed: {failed}")
    if bugs:
        print(f"\nBugs found:")
        for b in bugs:
            print(f"  Step {b['step']}: {b['endpoint']} expected={b['expected']} got={b['got']} detail={b['detail'][:80]}")

    report = {
        "agent": "dev2",
        "round": 2,
        "passed": passed,
        "failed": failed,
        "bugs": bugs
    }
    print(f"\nREPORT: {json.dumps(report)}")


if __name__ == "__main__":
    main()
