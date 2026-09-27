"""Agent-2: Edge Case & Error Handler Tests for BabyOS AutoML backend."""
from __future__ import annotations

import io
import json
import sys
import time

import httpx

BASE = "http://127.0.0.1:18080"
TIMEOUT = httpx.Timeout(10.0)
client = httpx.Client(base_url=BASE, timeout=TIMEOUT)

results: list[dict] = []
bugs: list[dict] = []
created_pids: list[str] = []


def step(n: int, desc: str):
    """Decorator that wraps a test function and records pass/fail."""
    def decorator(fn):
        def wrapper():
            try:
                fn()
                results.append({"step": n, "desc": desc, "status": "PASS"})
                print(f"  STEP {n}: PASS  {desc}")
            except AssertionError as e:
                results.append({"step": n, "desc": desc, "status": "FAIL", "detail": str(e)})
                print(f"  STEP {n}: FAIL  {desc}  -> {e}")
            except Exception as e:
                results.append({"step": n, "desc": desc, "status": "ERROR", "detail": repr(e)})
                print(f"  STEP {n}: ERROR {desc}  -> {repr(e)}")
        wrapper.__name__ = fn.__name__
        return wrapper
    return decorator


# ────────────────────────────────────────────────────
# Step 1: CREATE project without name → expect 422
# ────────────────────────────────────────────────────
@step(1, "CREATE project without name → 422")
def test_create_no_name():
    r = client.post("/api/projects", json={})
    assert r.status_code == 422, f"expected 422, got {r.status_code}: {r.text}"


# ────────────────────────────────────────────────────
# Step 2: CREATE project with empty name → expect 422
# ────────────────────────────────────────────────────
@step(2, "CREATE project with empty name → 422")
def test_create_empty_name():
    r = client.post("/api/projects", json={"name": ""})
    assert r.status_code == 422, f"expected 422, got {r.status_code}: {r.text}"


# ────────────────────────────────────────────────────
# Step 3: GET nonexistent project → expect 404
# ────────────────────────────────────────────────────
@step(3, "GET nonexistent project → 404")
def test_get_nonexistent():
    r = client.get("/api/projects/nonexistent-id-999")
    assert r.status_code == 404, f"expected 404, got {r.status_code}: {r.text}"


# ────────────────────────────────────────────────────
# Step 4: PATCH nonexistent → expect 404
# ────────────────────────────────────────────────────
@step(4, "PATCH nonexistent → 404")
def test_patch_nonexistent():
    r = client.patch("/api/projects/nonexistent-id-999", json={"name": "x"})
    assert r.status_code == 404, f"expected 404, got {r.status_code}: {r.text}"


# ────────────────────────────────────────────────────
# Step 5: DELETE nonexistent → expect 404
# ────────────────────────────────────────────────────
@step(5, "DELETE nonexistent → 404")
def test_delete_nonexistent():
    r = client.delete("/api/projects/nonexistent-id-999")
    assert r.status_code == 404, f"expected 404, got {r.status_code}: {r.text}"


# ────────────────────────────────────────────────────
# Step 6: CREATE valid project → expect 201
# ────────────────────────────────────────────────────
PID = None

@step(6, "CREATE valid project → 201")
def test_create_valid():
    global PID
    r = client.post("/api/projects", json={
        "name": "agent2_edge",
        "mode": "timeseries",
        "sampling_rate": 100.0,
    })
    assert r.status_code == 201, f"expected 201, got {r.status_code}: {r.text}"
    data = r.json()
    PID = data["project_id"]
    created_pids.append(PID)
    assert data["name"] == "agent2_edge"
    assert data["mode"] == "timeseries"
    print(f"    created PID={PID}")


# ────────────────────────────────────────────────────
# Step 7: IMPORT CSV without label column → expect 200
# ────────────────────────────────────────────────────
FID = None

@step(7, "IMPORT CSV without label column (timeseries mode) → 200")
def test_import_csv_no_label():
    global FID
    csv_content = "timestamp,accel_x,accel_y,accel_z\n"
    for i in range(200):
        csv_content += f"{i*0.01},{i*0.1},{i*0.2},{i*0.3}\n"
    files = [("files", ("test_no_label.csv", io.BytesIO(csv_content.encode()), "text/csv"))]
    mapping = json.dumps({
        "channels": ["accel_x", "accel_y", "accel_z"],
        "label_col": None,
    })
    r = client.post(
        f"/api/projects/{PID}/dataset",
        files=files,
        data={"mapping": mapping, "import_kind": "append"},
    )
    assert r.status_code == 200, f"expected 200, got {r.status_code}: {r.text}"
    data = r.json()
    # Get the file_id from the import results
    results_list = data.get("results", [])
    assert len(results_list) > 0, f"no results in import response: {data}"
    FID = results_list[0]["file_id"]
    print(f"    imported FID={FID}")


# ────────────────────────────────────────────────────
# Step 8: GET segments (should be empty) → expect []
# ────────────────────────────────────────────────────
@step(8, "GET segments (should be empty) → []")
def test_segments_empty():
    r = client.get(f"/api/projects/{PID}/segments")
    assert r.status_code == 200, f"expected 200, got {r.status_code}: {r.text}"
    data = r.json()
    assert data == [], f"expected empty list, got {data}"


# ────────────────────────────────────────────────────
# Step 9: COMPUTE features with no segments → expect 422 (BUG-2 fix)
# ────────────────────────────────────────────────────
@step(9, "COMPUTE features with no segments → 422 (not 500)")
def test_compute_no_segments():
    r = client.post(f"/api/projects/{PID}/features/compute")
    assert r.status_code == 422, f"expected 422 (BUG-2 fix), got {r.status_code}: {r.text}"


# ────────────────────────────────────────────────────
# Step 10: SCORING with no matrix → expect 422 (BUG-3 fix)
# ────────────────────────────────────────────────────
@step(10, "SCORING with no matrix → 422 (not 500)")
def test_scoring_no_matrix():
    r = client.get(f"/api/projects/{PID}/features/scoring")
    assert r.status_code == 422, f"expected 422 (BUG-3 fix), got {r.status_code}: {r.text}"


# ────────────────────────────────────────────────────
# Step 11: TRAINING without labels → expect 422 (stage not ready)
# ────────────────────────────────────────────────────
@step(11, "TRAINING without labels → 422 (stage not ready)")
def test_training_no_labels():
    r = client.post(f"/api/projects/{PID}/training", json={
        "n_iter": 2, "budget_s": 30, "k": 2,
    })
    assert r.status_code == 422, f"expected 422, got {r.status_code}: {r.text}"


# ────────────────────────────────────────────────────
# Step 12: ADD segment with invalid label_id → expect 422
# ────────────────────────────────────────────────────
@step(12, "ADD segment with invalid label_id → 422")
def test_segment_invalid_label():
    r = client.post(f"/api/projects/{PID}/segments", json={
        "file_id": FID, "start": 0, "end": 10, "label_id": 999,
    })
    assert r.status_code == 422, f"expected 422, got {r.status_code}: {r.text}"


# ────────────────────────────────────────────────────
# Step 13: ADD overlapping segments → expect 422
# ────────────────────────────────────────────────────
@step(13, "ADD overlapping segments → 422")
def test_segment_overlap():
    # First add a label
    r = client.post(f"/api/projects/{PID}/labels", json={"name": "test_label"})
    assert r.status_code == 201, f"add label failed: {r.status_code}: {r.text}"
    lbl = r.json()
    label_id = lbl["label_id"]
    print(f"    added label_id={label_id}")

    # Add first segment
    r = client.post(f"/api/projects/{PID}/segments", json={
        "file_id": FID, "start": 0, "end": 50, "label_id": label_id,
    })
    assert r.status_code == 201, f"add first segment failed: {r.status_code}: {r.text}"
    print(f"    added segment 0-50")

    # Add overlapping segment → should be 422
    r = client.post(f"/api/projects/{PID}/segments", json={
        "file_id": FID, "start": 10, "end": 30, "label_id": label_id,
    })
    assert r.status_code == 422, f"expected 422 for overlap, got {r.status_code}: {r.text}"
    print(f"    overlap correctly rejected")


# ────────────────────────────────────────────────────
# Step 14: DELETE label that has segments → 409
# ────────────────────────────────────────────────────
LABEL_ID_FOR_STEP14 = None

@step(14, "DELETE label that has segments → 409 (or 200)")
def test_delete_label_in_use():
    global LABEL_ID_FOR_STEP14
    # Get current labels to find the one with segments
    r = client.get(f"/api/projects/{PID}/labels")
    labels = r.json()
    # We added "test_label" in step 13, which has segments
    test_lbl = [l for l in labels if l["name"] == "test_label"]
    assert len(test_lbl) > 0, "test_label not found"
    LABEL_ID_FOR_STEP14 = test_lbl[0]["label_id"]

    r = client.delete(f"/api/projects/{PID}/labels/{LABEL_ID_FOR_STEP14}")
    # Behavior depends on implementation: 409 (in use) or 200 (allowed)
    # Either is acceptable; just record what happens
    print(f"    delete label with segments: {r.status_code}")
    assert r.status_code in (200, 204, 409), f"unexpected status: {r.status_code}: {r.text}"
    if r.status_code == 409:
        print(f"    correctly rejected (label in use)")
    else:
        print(f"    label deleted despite segments (behavior noted)")


# ────────────────────────────────────────────────────
# Step 15: COPY project → 201
# ────────────────────────────────────────────────────
COPY_PID = None

@step(15, "COPY project → 201")
def test_copy_project():
    global COPY_PID
    r = client.post(f"/api/projects/{PID}/copy")
    assert r.status_code == 201, f"expected 201, got {r.status_code}: {r.text}"
    COPY_PID = r.json()["project_id"]
    created_pids.append(COPY_PID)
    print(f"    copy PID={COPY_PID}")


# ────────────────────────────────────────────────────
# Step 16: TRASH list → 200
# ────────────────────────────────────────────────────
@step(16, "GET trash list → 200")
def test_trash_list():
    r = client.get("/api/projects/trash")
    assert r.status_code == 200, f"expected 200, got {r.status_code}: {r.text}"
    assert isinstance(r.json(), list)


# ────────────────────────────────────────────────────
# Step 17: SOFT delete → 204
# ────────────────────────────────────────────────────
TRASH_ID = None

@step(17, "SOFT delete → 204")
def test_soft_delete():
    global TRASH_ID
    r = client.delete(f"/api/projects/{COPY_PID}")
    assert r.status_code == 204, f"expected 204, got {r.status_code}: {r.text}"

    # Verify it's gone from project list
    r2 = client.get(f"/api/projects/{COPY_PID}")
    assert r2.status_code == 404, f"deleted project should be 404, got {r2.status_code}"

    # Find it in trash
    r3 = client.get("/api/projects/trash")
    trash = r3.json()
    matching = [t for t in trash if t["project_id"] == COPY_PID]
    assert len(matching) > 0, "deleted project not found in trash"
    TRASH_ID = matching[0]["trash_id"]
    print(f"    found in trash as TRASH_ID={TRASH_ID}")


# ────────────────────────────────────────────────────
# Step 18: RESTORE from trash → 200
# ────────────────────────────────────────────────────
@step(18, "RESTORE from trash → 200")
def test_restore():
    global COPY_PID
    r = client.post(f"/api/projects/trash/{TRASH_ID}/restore")
    assert r.status_code == 200, f"expected 200, got {r.status_code}: {r.text}"
    restored = r.json()
    assert restored["project_id"] == COPY_PID
    print(f"    restored PID={COPY_PID}")

    # Verify it's back in project list
    r2 = client.get(f"/api/projects/{COPY_PID}")
    assert r2.status_code == 200, f"restored project should be 200, got {r2.status_code}"


# ────────────────────────────────────────────────────
# Step 19: HARD delete all → 204
# ────────────────────────────────────────────────────
@step(19, "HARD delete all → 204")
def test_hard_delete():
    global COPY_PID
    # Hard delete the copy
    r = client.delete(f"/api/projects/{COPY_PID}?hard=true")
    assert r.status_code == 204, f"expected 204, got {r.status_code}: {r.text}"
    # Verify gone
    r2 = client.get(f"/api/projects/{COPY_PID}")
    assert r2.status_code == 404
    # Also verify not in trash
    r3 = client.get("/api/projects/trash")
    trash = r3.json()
    matching = [t for t in trash if t["project_id"] == COPY_PID]
    assert len(matching) == 0, "hard-deleted project still in trash"
    if COPY_PID in created_pids:
        created_pids.remove(COPY_PID)
    print(f"    hard deleted COPY_PID={COPY_PID}")


# ────────────────────────────────────────────────────
# Step 20: FILE DATA nonexistent → 404
# ────────────────────────────────────────────────────
@step(20, "FILE DATA nonexistent → 404")
def test_file_data_nonexistent():
    r = client.get(f"/api/projects/{PID}/dataset/file/nonexistent_file/data")
    assert r.status_code == 404, f"expected 404, got {r.status_code}: {r.text}"


# ════════════════════════════════════════════════════
# CLEANUP
# ════════════════════════════════════════════════════
def cleanup():
    print("\n=== CLEANUP ===")
    for pid in list(created_pids):
        try:
            r = client.delete(f"/api/projects/{pid}?hard=true")
            print(f"  hard-deleted {pid}: {r.status_code}")
        except Exception as e:
            print(f"  failed to clean {pid}: {e}")


# ════════════════════════════════════════════════════
# MAIN
# ════════════════════════════════════════════════════
def main():
    print("=" * 60)
    print("Agent-2: Edge Case & Error Handler Tests")
    print("=" * 60)

    tests = [
        test_create_no_name,
        test_create_empty_name,
        test_get_nonexistent,
        test_patch_nonexistent,
        test_delete_nonexistent,
        test_create_valid,
        test_import_csv_no_label,
        test_segments_empty,
        test_compute_no_segments,
        test_scoring_no_matrix,
        test_training_no_labels,
        test_segment_invalid_label,
        test_segment_overlap,
        test_delete_label_in_use,
        test_copy_project,
        test_trash_list,
        test_soft_delete,
        test_restore,
        test_hard_delete,
        test_file_data_nonexistent,
    ]

    for t in tests:
        t()

    cleanup()
    client.close()

    # Report
    passed = sum(1 for r in results if r["status"] == "PASS")
    failed = sum(1 for r in results if r["status"] != "PASS")

    print("\n" + "=" * 60)
    print("REPORT")
    print("=" * 60)
    for r in results:
        marker = "OK" if r["status"] == "PASS" else "XX"
        detail = f"  [{r.get('detail', '')}]" if r.get("detail") else ""
        print(f"  [{marker}] Step {r['step']:2d}: {r['desc']}{detail}")

    print(f"\nPassed: {passed}, Failed: {failed}")
    report = {
        "agent": "agent2",
        "round": 2,
        "passed": passed,
        "failed": failed,
        "bugs": bugs,
    }
    print("\nJSON REPORT:")
    print(json.dumps(report, indent=2))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
