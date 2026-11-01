#!/usr/bin/env python3
"""AutoML 端到端测试 — 从训练到导出C代码到编译验证。

完整流程：创建项目 → 导入数据 → 标注 → 特征工程 → 训练模型 → 导出C代码 → 编译验证
"""
import json
import os
import shutil
import sys
import time
import zipfile
from pathlib import Path

import httpx
import numpy as np
import pandas as pd

BASE = "http://127.0.0.1:18080"
PASS = 0
FAIL = 0
ERRORS = []
_pid = None


def check(cond, msg):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  ✅ {msg}")
    else:
        FAIL += 1
        ERRORS.append(msg)
        print(f"  ❌ {msg}")


def api(method, path, json=None, files=None, data=None, expect=200):
    """发起 HTTP 请求并验证状态码。"""
    r = httpx.request(method, f"{BASE}{path}", json=json, files=files, data=data, timeout=60)
    if expect is not None:
        if isinstance(expect, int):
            check(r.status_code == expect, f"{method} {path} → {r.status_code}（期望 {expect}）")
        elif isinstance(expect, list):
            check(r.status_code in expect, f"{method} {path} → {r.status_code}（期望 {expect}）")
    return r


def gen_csv(n_rows=2000, sr=100.0, channels=None):
    """生成测试 CSV 字节 — 三分类数据。"""
    if channels is None:
        channels = ["accel_x", "accel_y", "accel_z"]
    rng = np.random.default_rng(42)
    t = np.arange(n_rows) / sr
    data = {}
    for i, ch in enumerate(channels):
        # 不同类别用不同频率的正弦波
        data[ch] = np.sin(2 * np.pi * (i + 1) * t) + rng.normal(0, 0.1, n_rows)
    data["timestamp"] = t
    block = n_rows // 3
    data["label"] = np.array([0] * block + [1] * block + [2] * (n_rows - 2 * block))[:n_rows]
    df = pd.DataFrame(data)
    return df.to_csv(index=False).encode()


def wait_training(pid, timeout=120):
    """轮询训练状态直到完成。"""
    start = time.time()
    while time.time() - start < timeout:
        r = api("GET", f"/api/projects/{pid}/training", expect=200)
        status = r.json()
        if status.get("status") in ("done", "failed"):
            return status
        time.sleep(2)
    return {"status": "timeout"}


# ============================================================
# P0: 清理残留
# ============================================================
print("=" * 60)
print("P0: 清理残留测试项目")
print("=" * 60)

r = api("GET", "/api/projects", expect=200)
for p in r.json():
    if p.get("name", "").startswith("e2e_test_"):
        api("DELETE", f"/api/projects/{p['project_id']}", expect=[204, 404])
        print(f"  🧹 清理: {p['name']}")

# ============================================================
# P1: 创建项目 + 导入数据
# ============================================================
print("\n" + "=" * 60)
print("P1: 创建项目 + 导入数据")
print("=" * 60)

r = api("POST", "/api/projects", json={
    "name": "e2e_test_automl",
    "mode": "timeseries",
    "task_type": "classification",
    "sampling_rate": 100.0
}, expect=201)
_pid = r.json().get("project_id")
check(_pid is not None, f"创建项目 pid={_pid}")

if not _pid:
    print("\n❌ 项目创建失败，无法继续测试")
    sys.exit(1)

# 导入 CSV
csv_bytes = gen_csv()
mapping = json.dumps({"channels": ["accel_x", "accel_y", "accel_z"], "label_col": "label"})
r = api("POST", f"/api/projects/{_pid}/dataset",
        files={"files": ("test.csv", csv_bytes, "text/csv")},
        data={"mapping": mapping},
        expect=200)
check(r.json().get("imported") == 1, "导入数据成功")

# ============================================================
# P2: 特征工程
# ============================================================
print("\n" + "=" * 60)
print("P2: 特征工程")
print("=" * 60)

# 配置特征
r = api("PUT", f"/api/projects/{_pid}/features/config", json={
    "window_len_s": 1.0,
    "n_per_window": 100,
    "step": 50,
    "feature_ids": ["mean", "std"],
    "freq_enabled": False,
    "norm": "zscore"
}, expect=200)
check(r.json().get("step") == 50, "特征配置保存成功")

# 计算特征
r = api("POST", f"/api/projects/{_pid}/features/compute", expect=200)
check(r.json().get("n_samples", 0) > 0, f"特征计算成功 n_samples={r.json().get('n_samples')}")

# ============================================================
# P3: 训练模型
# ============================================================
print("\n" + "=" * 60)
print("P3: 训练模型")
print("=" * 60)

r = api("POST", f"/api/projects/{_pid}/training", json={
    "models": ["rf"],
    "timeout_s": 60
}, expect=200)
check(r.json().get("status") == "started", "训练启动成功")

# 等待训练完成
print("  ⏳ 等待训练完成...")
status = wait_training(_pid, timeout=120)
check(status.get("status") == "done", f"训练完成 status={status.get('status')}")

if status.get("status") != "done":
    print(f"  ❌ 训练失败: {status.get('error', 'unknown')}")
    print("\n❌ 训练失败，无法继续测试")
    sys.exit(1)

# ============================================================
# P4: 导出 C 代码
# ============================================================
print("\n" + "=" * 60)
print("P4: 导出 C 代码")
print("=" * 60)

r = api("POST", f"/api/projects/{_pid}/export", expect=200)
report = r.json()
check(report.get("ok") == True, f"导出成功 ok={report.get('ok')}")
check(report.get("compile", {}).get("ok") == True, "编译自检通过")
check(report.get("predict_consistency", {}).get("ok") == True, "predict 一致性通过")
check(report.get("static_scan", {}).get("ok") == True, "静态扫描通过")
check(report.get("symbol_map", {}).get("ok") == True, "符号映射通过")

# 获取导出文件列表
r = api("GET", f"/api/projects/{_pid}/export", expect=200)
status = r.json()
zips = status.get("zips", [])
check(len(zips) > 0, f"导出包存在: {zips}")

if not zips:
    print("\n❌ 无导出包，无法继续测试")
    sys.exit(1)

# ============================================================
# P5: 下载并验证导出包
# ============================================================
print("\n" + "=" * 60)
print("P5: 下载并验证导出包")
print("=" * 60)

zip_name = zips[0]
r = httpx.get(f"{BASE}/api/projects/{_pid}/export/download/{zip_name}", timeout=30)
check(r.status_code == 200, f"下载成功 {zip_name}")

# 保存到本地
export_dir = Path("/home/yyds/code/BabyOS/test/automl_e2e")
export_dir.mkdir(exist_ok=True)
zip_path = export_dir / zip_name
zip_path.write_bytes(r.content)
check(zip_path.exists(), f"保存到 {zip_path}")

# 解压验证
with zipfile.ZipFile(zip_path, 'r') as z:
    names = z.namelist()
    check(any(n.endswith('.h') for n in names), f"包含头文件: {[n for n in names if n.endswith('.h')]}")
    check(any(n.endswith('.c') for n in names), f"包含源文件: {[n for n in names if n.endswith('.c')]}")
    check('README.md' in names, "包含 README.md")
    check('export_report.json' in names, "包含 export_report.json")

    # 解压所有文件
    z.extractall(export_dir)

print(f"\n  📦 导出包解压到: {export_dir}")

# ============================================================
# P6: 输出测试报告
# ============================================================
print("\n" + "=" * 60)
print("P6: 测试报告")
print("=" * 60)

# 生成测试报告
report_data = {
    "test_name": "e2e_automl",
    "project_id": _pid,
    "pass": PASS,
    "fail": FAIL,
    "errors": ERRORS,
    "export_dir": str(export_dir),
    "export_files": [str(f) for f in Path(export_dir).rglob("*") if f.is_file()]
}

report_path = export_dir / "test_report.json"
with open(report_path, 'w', encoding='utf-8') as f:
    json.dump(report_data, f, ensure_ascii=False, indent=2)

print(f"  📊 测试报告: {report_path}")
print(f"\n{'=' * 60}")
print(f"测试结果: {PASS} 通过, {FAIL} 失败")
print(f"{'=' * 60}")

if ERRORS:
    print("\n❌ 失败项:")
    for e in ERRORS:
        print(f"  - {e}")

# 清理项目
print("\n🧹 清理测试项目...")
api("DELETE", f"/api/projects/{_pid}", expect=[204, 404])

sys.exit(0 if FAIL == 0 else 1)
