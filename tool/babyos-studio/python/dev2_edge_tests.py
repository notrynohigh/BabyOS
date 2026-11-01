#!/usr/bin/env python3
"""Dev-2: Edge Case Tests for BabyOS AutoML backend."""

import httpx
import json
import sys

BASE = "http://127.0.0.1:18080"
results = []


def api(method, url, payload=None, files=None, data=None):
    """Make an API call, return (status_code, response_json_or_text)."""
    try:
        if method == "POST":
            if files:
                r = httpx.post(f"{BASE}{url}", files=files, data=data or {}, timeout=30)
            elif payload is not None:
                r = httpx.post(f"{BASE}{url}", json=payload, timeout=30)
            else:
                r = httpx.post(f"{BASE}{url}", timeout=30)
        elif method == "GET":
            r = httpx.get(f"{BASE}{url}", timeout=30)
        elif method == "PATCH":
            r = httpx.patch(f"{BASE}{url}", json=payload, timeout=30)
        elif method == "DELETE":
            r = httpx.delete(f"{BASE}{url}", timeout=30)
        else:
            return (0, f"Unknown method {method}")
        try:
            body = r.json()
        except:
            body = r.text
        return (r.status_code, body)
    except Exception as e:
        return (-1, str(e))


def check(step, method, url, expected, payload=None, files=None, data=None, desc=""):
    """Run a test and record result."""
    status, body = api(method, url, payload=payload, files=files, data=data)
    passed = status == expected
    detail = f"status={status}"
    if not passed:
        detail += f" body={str(body)[:300]}"
    results.append({
        "step": step, "expected": expected, "got": status,
        "detail": detail, "passed": passed, "desc": desc
    })
    tag = "PASS" if passed else "FAIL"
    print(f"Step {step}: {tag} {detail}")
    return status, body


def create_project(name="dev2_edge"):
    """Create a timeseries project and return project_id."""
    status, body = api("POST", "/api/projects",
                       payload={"name": name, "mode": "timeseries", "sampling_rate": 100.0})
    if status == 201 and isinstance(body, dict):
        return body.get("project_id")
    return None


def get_trash_id(pid):
    """Find trash_id for a soft-deleted project."""
    status, body = api("GET", "/api/projects/trash")
    if status == 200 and isinstance(body, list):
        for item in body:
            if item.get("project_id") == pid:
                return item.get("trash_id")
    return None


def cleanup():
    """Hard-delete all dev2 test projects."""
    try:
        # Clean active projects
        status, body = api("GET", "/api/projects")
        if status == 200 and isinstance(body, list):
            for p in body:
                name = p.get("name", "")
                if "dev2_" in name:
                    pid = p.get("project_id")
                    api("DELETE", f"/api/projects/{pid}?hard=true")
        # Clean trash
        status, body = api("GET", "/api/projects/trash")
        if status == 200 and isinstance(body, list):
            for p in body:
                name = p.get("name", "")
                if "dev2_" in name:
                    trash_id = p.get("trash_id")
                    if trash_id:
                        api("DELETE", f"/api/projects/trash/{trash_id}")
    except Exception as e:
        print(f"Cleanup error: {e}", file=sys.stderr)


def run_tests():
    global results
    results = []
    cleanup()

    # ── Step 1: POST /api/projects {} → 422 ──
    check(1, "POST", "/api/projects", 422, payload={}, desc="Empty body")

    # ── Step 2: POST /api/projects {"name":""} → 422 ──
    check(2, "POST", "/api/projects", 422, payload={"name": ""}, desc="Empty name")

    # ── Step 3: GET /api/projects/nonexistent → 404 ──
    check(3, "GET", "/api/projects/nonexistent", 404, desc="GET nonexistent project")

    # ── Step 4: PATCH /api/projects/nonexistent → 404 ──
    check(4, "PATCH", "/api/projects/nonexistent", 404, payload={"name": "x"},
          desc="PATCH nonexistent project")

    # ── Step 5: DELETE /api/projects/nonexistent → 404 ──
    check(5, "DELETE", "/api/projects/nonexistent", 404, desc="DELETE nonexistent project")

    # ── Step 6: POST /api/projects create → 201 ──
    pid = create_project("dev2_edge")
    status = 201 if pid else -1
    results.append({
        "step": 6, "expected": 201, "got": status,
        "detail": f"project_id={pid}", "passed": pid is not None,
        "desc": "Create project"
    })
    print(f"Step 6: {'PASS' if pid else 'FAIL'} project_id={pid}")

    if not pid:
        print("FATAL: Could not create project")
        return

    # ── Step 7: Import CSV without label column → 200 ──
    csv_content = b"timestamp,accel_x,accel_y,accel_z\n0,0.1,0.2,0.3\n1,0.4,0.5,0.6\n2,0.7,0.8,0.9\n"
    mapping = json.dumps({"channels": ["accel_x", "accel_y", "accel_z"], "timestamp": "timestamp"})
    status, body = api("POST", f"/api/projects/{pid}/dataset",
                       files={"files": ("no_label.csv", csv_content, "text/csv")},
                       data={"mapping": mapping})
    passed = status == 200
    # Verify label_col_present is false
    label_present = None
    if passed and isinstance(body, dict):
        results_list = body.get("results", [])
        if results_list:
            label_present = results_list[0].get("label_col_present")
    detail = f"status={status} label_col_present={label_present}"
    results.append({
        "step": 7, "expected": 200, "got": status,
        "detail": detail, "passed": passed, "desc": "Import CSV without label"
    })
    print(f"Step 7: {'PASS' if passed else 'FAIL'} {detail}")

    # Get file_id from the import result
    file_id = None
    if isinstance(body, dict):
        results_list = body.get("results", [])
        if results_list:
            file_id = results_list[0].get("file_id")
    print(f"  file_id={file_id}")

    # ── Step 8: GET /api/projects/{pid}/segments → [] ──
    status, body = api("GET", f"/api/projects/{pid}/segments")
    passed = status == 200 and body == []
    detail = f"status={status} body={body}"
    results.append({
        "step": 8, "expected": 200, "got": status,
        "detail": detail, "passed": passed, "desc": "GET segments empty"
    })
    print(f"Step 8: {'PASS' if passed else 'FAIL'} {detail}")

    # ── Step 9: POST /api/projects/{pid}/features/compute → 422 (no segments) ──
    check(9, "POST", f"/api/projects/{pid}/features/compute", 422, payload={},
          desc="Compute features no segments")

    # ── Step 10: GET /api/projects/{pid}/features/scoring → 422 (no matrix) ──
    check(10, "GET", f"/api/projects/{pid}/features/scoring", 422,
          desc="Feature scoring no matrix")

    # ── Step 11: POST /api/projects/{pid}/training → 422 (stage not ready) ──
    check(11, "POST", f"/api/projects/{pid}/training", 422,
          payload={"n_iter": 2, "budget_s": 30, "k": 2},
          desc="Training stage not ready")

    # ── Step 12: POST /api/projects/{pid}/segments with invalid label → 422 ──
    if file_id:
        check(12, "POST", f"/api/projects/{pid}/segments", 422,
              payload={"file_id": file_id, "start": 0, "end": 10, "label_id": 999},
              desc="Segment with invalid label_id")
    else:
        results.append({
            "step": 12, "expected": 422, "got": -1,
            "detail": "SKIPPED: no file_id", "passed": False,
            "desc": "Segment with invalid label_id"
        })
        print("Step 12: SKIP (no file_id)")

    # ── Step 13: Add label, segment, then overlapping segment → 422 ──
    # Add label
    label_id = None
    status, body = api("POST", f"/api/projects/{pid}/labels",
                       payload={"name": "normal", "color": "#00ff00"})
    if status in (200, 201) and isinstance(body, dict):
        # Use explicit check for label_id (0 is valid but falsy in Python)
        if "label_id" in body:
            label_id = body["label_id"]
        elif "id" in body:
            label_id = body["id"]
    print(f"  Created label: label_id={label_id}")

    if label_id is not None and file_id:
        # Add first segment (rows 0-2)
        status1, body1 = api("POST", f"/api/projects/{pid}/segments",
                             payload={"file_id": file_id, "start": 0, "end": 2, "label_id": label_id})
        print(f"  First segment: {status1} {body1}")

        # Add overlapping segment (rows 1-3) → should be 422
        check(13, "POST", f"/api/projects/{pid}/segments", 422,
              payload={"file_id": file_id, "start": 1, "end": 3, "label_id": label_id},
              desc="Overlapping segment")
    else:
        results.append({
            "step": 13, "expected": 422, "got": -1,
            "detail": f"SKIPPED: label_id={label_id} file_id={file_id}",
            "passed": False, "desc": "Overlapping segment"
        })
        print(f"Step 13: SKIP")

    # ── Step 14: POST /api/projects/{pid}/copy → 201 ──
    status, body = api("POST", f"/api/projects/{pid}/copy",
                       payload={"name": "dev2_copy"})
    copy_id = None
    if status == 201 and isinstance(body, dict):
        copy_id = body.get("project_id")
    passed = status == 201
    detail = f"status={status} copy_id={copy_id}"
    results.append({
        "step": 14, "expected": 201, "got": status,
        "detail": detail, "passed": passed, "desc": "Copy project"
    })
    print(f"Step 14: {'PASS' if passed else 'FAIL'} {detail}")

    # ── Step 15: GET /api/projects/trash → 200 ──
    check(15, "GET", "/api/projects/trash", 200, desc="GET trash")

    # ── Step 16: DELETE /api/projects/{pid} → 204 (soft delete) ──
    check(16, "DELETE", f"/api/projects/{pid}", 204, desc="Soft delete project")

    # ── Step 17: Restore from trash → 200 ──
    # First get the trash_id
    trash_id = get_trash_id(pid)
    if trash_id:
        status, body = api("POST", f"/api/projects/trash/{trash_id}/restore")
        passed = status == 200
        detail = f"status={status} trash_id={trash_id}"
    else:
        status = -1
        passed = False
        detail = f"SKIPPED: no trash_id for pid={pid}"
    results.append({
        "step": 17, "expected": 200, "got": status,
        "detail": detail, "passed": passed, "desc": "Restore from trash"
    })
    print(f"Step 17: {'PASS' if passed else 'FAIL'} {detail}")

    # ── Step 18: DELETE /api/projects/{pid}?hard=true → 204 ──
    # After restore, the project should be back in active list
    check(18, "DELETE", f"/api/projects/{pid}?hard=true", 204, desc="Hard delete project")

    # ── Step 19: DELETE copy ?hard=true → 204 ──
    if copy_id:
        check(19, "DELETE", f"/api/projects/{copy_id}?hard=true", 204,
              desc="Hard delete copy")
    else:
        results.append({
            "step": 19, "expected": 204, "got": -1,
            "detail": "SKIPPED: no copy_id", "passed": False,
            "desc": "Hard delete copy"
        })
        print("Step 19: SKIP (no copy_id)")

    # ── Step 20: GET /api/projects/{pid}/dataset/file/nonexistent/data → 404 ──
    check(20, "GET", "/api/projects/nonexistent/dataset/file/nonexistent/data", 404,
          desc="GET nonexistent file data")

    # Final cleanup
    cleanup()


def main():
    run_tests()

    passed = sum(1 for r in results if r["passed"])
    failed = sum(1 for r in results if not r["passed"])

    print(f"\n{'='*60}")
    print(f"RESULTS: {passed} passed, {failed} failed out of {len(results)} tests")
    print(f"{'='*60}")

    bugs = []
    for r in results:
        if not r["passed"]:
            bugs.append({
                "step": r["step"],
                "endpoint": r["desc"],
                "expected": r["expected"],
                "got": r["got"],
                "detail": r["detail"]
            })

    report = {
        "agent": "dev2",
        "round": 5,
        "passed": passed,
        "failed": failed,
        "bugs": bugs
    }

    print(f"\nREPORT: {json.dumps(report, ensure_ascii=False)}")
    return report


if __name__ == "__main__":
    main()
