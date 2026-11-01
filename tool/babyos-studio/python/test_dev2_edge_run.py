#!/usr/bin/env python3
"""Dev-2 edge case test: 20 steps against BabyOS AutoML backend."""
import httpx
import json
import time
import traceback

BASE = "http://127.0.0.1:18080"
c = httpx.Client(base_url=BASE, timeout=30.0)

bugs = []
passed = 0
failed = 0
pids_to_cleanup = []

def check(step, method, url, expected_status, desc="", **kwargs):
    global passed, failed
    try:
        r = getattr(c, method)(url, **kwargs)
        status = r.status_code
        if status == expected_status:
            passed += 1
            print(f"  PASS step {step}: {method.upper()} {url} -> {status} ({desc})")
            return r
        else:
            failed += 1
            detail = ""
            try:
                detail = r.json()
            except Exception:
                detail = r.text[:200]
            bugs.append({
                "step": step,
                "endpoint": f"{method.upper()} {url}",
                "expected": expected_status,
                "got": status,
                "detail": f"{desc} | response={detail}",
            })
            print(f"  FAIL step {step}: {method.upper()} {url} -> {status} (expected {expected_status}) ({desc})")
            return r
    except Exception as e:
        failed += 1
        bugs.append({"step": step, "endpoint": f"{method.upper()} {url}", "expected": expected_status, "got": "EXCEPTION", "detail": str(e)})
        print(f"  FAIL step {step}: {method.upper()} {url} -> EXCEPTION ({e})")
        return None


# ---- Step 1: POST /api/projects {} -> 422 ----
print("Step 1: POST /api/projects {} -> 422")
r = check(1, "post", "/api/projects", 422, "empty body")

# ---- Step 2: POST /api/projects {"name":""} -> 422 ----
print("Step 2: POST /api/projects {'name':''} -> 422")
r = check(2, "post", "/api/projects", 422, "empty name", json={"name": ""})

# ---- Step 3: GET /api/projects/nonexistent -> 404 ----
print("Step 3: GET /api/projects/nonexistent -> 404")
r = check(3, "get", "/api/projects/nonexistent", 404, "nonexistent pid")

# ---- Step 4: PATCH /api/projects/nonexistent -> 404 ----
print("Step 4: PATCH /api/projects/nonexistent -> 404")
r = check(4, "patch", "/api/projects/nonexistent", 404, "nonexistent pid", json={"name": "x"})

# ---- Step 5: DELETE /api/projects/nonexistent -> 404 ----
print("Step 5: DELETE /api/projects/nonexistent -> 404")
r = check(5, "delete", "/api/projects/nonexistent", 404, "nonexistent pid")

# ---- Step 6: POST /api/projects with valid data -> 201 ----
print("Step 6: POST /api/projects dev2_edge -> 201")
r = check(6, "post", "/api/projects", 201, "create project",
          json={"name": "dev2_edge", "mode": "timeseries", "sampling_rate": 100.0})
pid = r.json()["project_id"]
pids_to_cleanup.append(pid)
print(f"    created pid={pid}")

# ---- Step 7: Import CSV without label column -> 200 ----
print("Step 7: Import CSV without label column -> 200 (timeseries mode)")
# timeseries mode: label_col is optional; channels is required
csv_content = b"a,b,c\n1.0,2.0,3.0\n4.0,5.0,6.0\n7.0,8.0,9.0\n"
mapping = json.dumps({"channels": ["a", "b", "c"]})
r = c.post(
    f"/api/projects/{pid}/dataset",
    files=[("files", ("test.csv", csv_content, "text/csv"))],
    data={"mapping": mapping, "import_kind": "append"},
)
if r.status_code == 200:
    passed += 1
    print(f"  PASS step 7: POST dataset import -> {r.status_code} (no label col)")
else:
    failed += 1
    bugs.append({"step": 7, "endpoint": "POST /api/projects/{pid}/dataset", "expected": 200, "got": r.status_code, "detail": r.text[:300]})
    print(f"  FAIL step 7: -> {r.status_code}")

# extract file_id for later steps
import_result = r.json()
file_id = None
if import_result.get("results"):
    for res in import_result["results"]:
        if res.get("ok"):
            file_id = res.get("file_id")
            break
print(f"    file_id={file_id}")

# ---- Step 8: GET /api/projects/{pid}/segments -> [] ----
print("Step 8: GET /api/projects/{pid}/segments -> []")
r = check(8, "get", f"/api/projects/{pid}/segments", 200, "no segments yet")
if r and r.json() != []:
    # Might not be empty if something auto-created segments; check it's a list
    if not isinstance(r.json(), list):
        failed += 1
        bugs.append({"step": 8, "endpoint": f"GET /api/projects/{pid}/segments", "expected": "[]", "got": str(r.json())[:100], "detail": "not a list"})
        print(f"  FAIL step 8: expected list, got {type(r.json())}")
    else:
        print(f"    segments={r.json()}")

# ---- Step 9: POST features/compute -> 422 (no segments) ----
print("Step 9: POST features/compute -> 422 (no segments)")
r = check(9, "post", f"/api/projects/{pid}/features/compute", 422, "no segments")

# ---- Step 10: GET features/scoring -> 422 (no matrix) ----
print("Step 10: GET features/scoring -> 422 (no matrix)")
r = check(10, "get", f"/api/projects/{pid}/features/scoring", 422, "no matrix")

# ---- Step 11: POST training -> 422 (stage not ready) ----
print("Step 11: POST training -> 422 (stage not ready, need 'labeled')")
r = check(11, "post", f"/api/projects/{pid}/training", 422, "stage not ready",
          json={"n_iter": 2, "budget_s": 30, "k": 2})

# ---- Step 12: POST segment with invalid label_id -> 422 ----
print("Step 12: POST segment with nonexistent label_id=999 -> 422")
if file_id:
    r = check(12, "post", f"/api/projects/{pid}/segments", 422, "invalid label_id",
              json={"file_id": file_id, "start": 0, "end": 10, "label_id": 999})
else:
    # no file_id means step 7 failed, skip
    failed += 1
    bugs.append({"step": 12, "endpoint": "POST /api/projects/{pid}/segments", "expected": 422, "got": "SKIPPED", "detail": "no file_id from step 7"})
    print("  SKIP step 12: no file_id")

# ---- Step 13: Add label, add segment, add overlapping segment -> 422 ----
print("Step 13: Add label + segment + overlapping segment -> 422")
# First add a label
r_label = c.post(f"/api/projects/{pid}/labels", json={"name": "test_label"})
if r_label.status_code == 201:
    label_id = r_label.json()["label_id"]
    print(f"    label_id={label_id}")
    # Add first segment
    if file_id:
        r_seg = c.post(f"/api/projects/{pid}/segments",
                       json={"file_id": file_id, "start": 0, "end": 100, "label_id": label_id})
        if r_seg.status_code == 201:
            passed += 1
            print(f"  PASS step 13a: first segment added -> 201")
            # Add overlapping segment (should fail)
            r = check(13, "post", f"/api/projects/{pid}/segments", 422, "overlapping segment",
                      json={"file_id": file_id, "start": 50, "end": 150, "label_id": label_id})
        else:
            failed += 1
            bugs.append({"step": 13, "endpoint": "POST segments (first)", "expected": 201, "got": r_seg.status_code, "detail": r_seg.text[:200]})
            print(f"  FAIL step 13a: first segment -> {r_seg.status_code}")
    else:
        failed += 1
        bugs.append({"step": 13, "endpoint": "POST segments", "expected": 201, "got": "SKIPPED", "detail": "no file_id"})
else:
    failed += 1
    bugs.append({"step": 13, "endpoint": "POST labels", "expected": 201, "got": r_label.status_code, "detail": r_label.text[:200]})
    print(f"  FAIL step 13: add label -> {r_label.status_code}")

# ---- Step 14: POST /api/projects/{pid}/copy -> 201 ----
print("Step 14: POST /api/projects/{pid}/copy -> 201")
r = check(14, "post", f"/api/projects/{pid}/copy", 201, "copy project")
if r and r.status_code == 201:
    copy_pid = r.json()["project_id"]
    pids_to_cleanup.append(copy_pid)
    print(f"    copy_pid={copy_pid}")
else:
    copy_pid = None
    print("  (no copy_pid)")

# ---- Step 15: GET /api/projects/trash -> 200 ----
print("Step 15: GET /api/projects/trash -> 200")
r = check(15, "get", "/api/projects/trash", 200, "list trash")

# ---- Step 16: DELETE /api/projects/{pid} -> 204 (soft delete) ----
print("Step 16: DELETE /api/projects/{pid} -> 204 (soft)")
r = check(16, "delete", f"/api/projects/{pid}", 204, "soft delete")

# Verify it's gone from project list
r_list = c.get("/api/projects")
project_ids = [p["project_id"] for p in r_list.json()]
if pid in project_ids:
    failed += 1
    bugs.append({"step": 16, "endpoint": "GET /api/projects after soft delete", "expected": "pid not in list", "got": "pid still in list", "detail": "soft delete did not remove from project list"})
    print("  FAIL step 16: pid still in project list after soft delete")

# ---- Step 17: Restore from trash -> 200 ----
print("Step 17: Restore from trash -> 200")
# Find the trash entry for our pid
r_trash = c.get("/api/projects/trash")
trash_entries = r_trash.json()
trash_id = None
for entry in trash_entries:
    if entry.get("project_id") == pid:
        trash_id = entry["trash_id"]
        break
if trash_id:
    r = check(17, "post", f"/api/projects/trash/{trash_id}/restore", 200, "restore from trash")
else:
    failed += 1
    bugs.append({"step": 17, "endpoint": "GET trash", "expected": "trash_id found", "got": "not found", "detail": f"trash entries: {trash_entries}"})
    print(f"  FAIL step 17: no trash entry for pid={pid}")

# ---- Step 18: DELETE /api/projects/{pid}?hard=true -> 204 ----
print("Step 18: DELETE /api/projects/{pid}?hard=true -> 204")
r = check(18, "delete", f"/api/projects/{pid}?hard=true", 204, "hard delete")

# ---- Step 19: DELETE copy?hard=true -> 204 ----
print("Step 19: DELETE copy?hard=true -> 204")
if copy_pid:
    r = check(19, "delete", f"/api/projects/{copy_pid}?hard=true", 204, "hard delete copy")
else:
    failed += 1
    bugs.append({"step": 19, "endpoint": "DELETE copy ?hard=true", "expected": 204, "got": "SKIPPED", "detail": "no copy_pid from step 14"})
    print("  SKIP step 19: no copy_pid")

# ---- Step 20: GET /api/projects/{pid}/dataset/file/nonexistent/data -> 404 ----
print("Step 20: GET /api/projects/{pid}/dataset/file/nonexistent/data -> 404")
# Need a valid project for this - create a temporary one
r_temp = c.post("/api/projects", json={"name": "dev2_temp_20", "mode": "timeseries", "sampling_rate": 100.0})
if r_temp.status_code == 201:
    temp_pid = r_temp.json()["project_id"]
    pids_to_cleanup.append(temp_pid)
    r = check(20, "get", f"/api/projects/{temp_pid}/dataset/file/nonexistent/data", 404, "nonexistent file")
else:
    # Step 18 deleted the original pid; if that failed, try with copy_pid
    if copy_pid:
        r = check(20, "get", f"/api/projects/{copy_pid}/dataset/file/nonexistent/data", 404, "nonexistent file")
    else:
        failed += 1
        bugs.append({"step": 20, "endpoint": "GET dataset/file/nonexistent/data", "expected": 404, "got": "SKIPPED", "detail": "no project to test with"})
        print("  SKIP step 20: no project available")


# ---- CLEANUP ----
print("\n=== CLEANUP ===")
for cleanup_pid in pids_to_cleanup:
    try:
        # Hard delete any remaining projects
        r = c.delete(f"/api/projects/{cleanup_pid}?hard=true")
        if r.status_code == 204:
            print(f"  cleaned up pid={cleanup_pid}")
        else:
            # Try soft delete
            r2 = c.delete(f"/api/projects/{cleanup_pid}")
            print(f"  cleanup pid={cleanup_pid}: hard={r.status_code}, soft={r2.status_code}")
    except Exception as e:
        print(f"  cleanup pid={cleanup_pid}: error={e}")

# Clean up any temp projects we might have missed
try:
    all_projects = c.get("/api/projects").json()
    for p in all_projects:
        if "dev2" in p.get("name", ""):
            c.delete(f"/api/projects/{p['project_id']}?hard=true")
            print(f"  cleanup extra pid={p['project_id']} name={p['name']}")
except Exception as e:
    print(f"  cleanup sweep error: {e}")

# ---- REPORT ----
print(f"\n{'='*60}")
print(f"REPORT: {json.dumps({'agent':'dev2','round':3,'passed':passed,'failed':failed,'bugs':bugs})}")
print(f"{'='*60}")
