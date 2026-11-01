#!/usr/bin/env python3
"""AutoML API 全链路测试 — 模拟用户完整操作路径。

测试维度：通过 HTTP 请求走完 用户操作 → 后端处理 → 状态验证 全流程。
不再只测服务层函数，而是测 API 端到端行为。
"""
import io
import os
import sys
import shutil
import time

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
    r = httpx.request(method, f"{BASE}{path}", json=json, files=files, data=data, timeout=30)
    if expect is not None:
        if isinstance(expect, int):
            check(r.status_code == expect, f"{method} {path} → {r.status_code}（期望 {expect}）: {r.text[:200]}")
        elif isinstance(expect, list):
            check(r.status_code in expect, f"{method} {path} → {r.status_code}（期望 {expect}）: {r.text[:200]}")
    return r


def gen_csv(n_rows=1000, sr=100.0, channels=None):
    """生成测试 CSV 字节。"""
    if channels is None:
        channels = ["accel_x", "accel_y", "accel_z"]
    rng = np.random.default_rng(42)
    t = np.arange(n_rows) / sr
    data = {ch: rng.normal(0, 1, n_rows) for ch in channels}
    data["timestamp"] = t
    block = max(1, n_rows // 3)
    data["label"] = np.array([0] * block + [1] * block + [2] * (n_rows - 2 * block))[:n_rows]
    df = pd.DataFrame(data)
    return df.to_csv(index=False).encode()


# ============================================================
# P0: 清理残留测试项目
# ============================================================
print("=" * 60)
print("P0: 清理残留测试项目")
print("=" * 60)

r = api("GET", "/api/projects", expect=200)
for p in r.json():
    if p.get("name", "").startswith("api_test_"):
        api("DELETE", f"/api/projects/{p['project_id']}", expect=[204, 404])
        print(f"  🧹 清理残留: {p['name']}")

# ============================================================
# P1: 项目 CRUD
# ============================================================
print("\n" + "=" * 60)
print("P1: 项目 CRUD")
print("=" * 60)

r = api("GET", "/api/projects", expect=200)
check(isinstance(r.json(), list), "GET /api/projects 返回列表")

r = api("POST", "/api/projects", json={
    "name": "api_test_pipeline", "mode": "timeseries",
    "task_type": "classification", "sampling_rate": 100.0
}, expect=201)
_pid = r.json().get("project_id")
check(_pid is not None, f"创建项目 pid={_pid}")

if not _pid:
    print("\n❌ 项目创建失败，无法继续测试")
    sys.exit(1)

r = api("GET", f"/api/projects/{_pid}", expect=200)
meta = r.json()
check(meta["sampling_rate"] == 100.0, f"采样率=100（创建时设置）")
check(meta["mode"] == "timeseries", f"模式=timeseries")

# P1.1: PATCH 采样率
r = api("PATCH", f"/api/projects/{_pid}", json={"sampling_rate": 1.0}, expect=200)
check(r.json()["sampling_rate"] == 1.0, f"PATCH sr=1 → {r.json()['sampling_rate']}")

# P1.2: PATCH 无效采样率
api("PATCH", f"/api/projects/{_pid}", json={"sampling_rate": 0}, expect=422)
api("PATCH", f"/api/projects/{_pid}", json={"sampling_rate": -1}, expect=422)

# 恢复 sr=100
api("PATCH", f"/api/projects/{_pid}", json={"sampling_rate": 100.0}, expect=200)


# ============================================================
# P2: 数据导入
# ============================================================
print("\n" + "=" * 60)
print("P2: 数据导入")
print("=" * 60)

if _pid:
    csv_bytes = gen_csv(1000, 100.0)
    files = [("files", ("test.csv", csv_bytes, "text/csv"))]
    mapping = '{"channels":["accel_x","accel_y","accel_z"],"ts_col":"timestamp","label_col":"label"}'
    r = api("POST", f"/api/projects/{_pid}/dataset",
            files=files, data={"mapping": mapping, "import_kind": "replace"}, expect=200)
    result = r.json()
    check(result.get("imported", 0) >= 1, f"导入成功 imported={result.get('imported')}")

    # 验证导入后元数据
    r = api("GET", f"/api/projects/{_pid}", expect=200)
    meta = r.json()
    check(meta.get("stage") in ("data_imported", "labeled"), f"阶段={meta.get('stage')}")
    check(len(meta.get("channels", [])) == 3, f"通道数={len(meta.get('channels', []))}")

    # 验证 segments 存在
    r = api("GET", f"/api/projects/{_pid}/segments", expect=200)
    segs = r.json()
    check(len(segs) >= 1, f"分段数={len(segs)}")


# ============================================================
# P3: 采样率同步（核心回归测试）
# ============================================================
print("\n" + "=" * 60)
print("P3: 采样率同步 — 前后端一致性")
print("=" * 60)

if _pid:
    # P3.1: PATCH sr=1 → 验证后端存储
    api("PATCH", f"/api/projects/{_pid}", json={"sampling_rate": 1.0}, expect=200)
    r = api("GET", f"/api/projects/{_pid}", expect=200)
    check(r.json()["sampling_rate"] == 1.0, f"P3.1: PATCH sr=1 → GET 确认 sr={r.json()['sampling_rate']}")

    # P3.2: sr=1, window=2, step=1 → 应合法（n=2, min_step=1）
    r = api("PUT", f"/api/projects/{_pid}/features/config", json={
        "window_len_s": 2.0, "n_per_window": 512, "step": 1,
        "feature_ids": ["mean", "std"], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"
    }, expect=200)
    check(r.json()["step"] == 1, f"P3.2: sr=1×2s step=1 → 保存成功 step={r.json()['step']}")

    # P3.3: PATCH sr=100 → 验证 sr 变化后 step=1 被拒
    api("PATCH", f"/api/projects/{_pid}", json={"sampling_rate": 100.0}, expect=200)
    r = api("PUT", f"/api/projects/{_pid}/features/config", json={
        "window_len_s": 2.0, "n_per_window": 512, "step": 1,
        "feature_ids": ["mean", "std"], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"
    }, expect=422)
    check("STEP_TOO_SMALL" in r.text, f"P3.3: sr=100×2s step=1 → 被拒")

    # P3.4: sr=100, window=2, step=20 → 应合法（n=200, min_step=20）
    r = api("PUT", f"/api/projects/{_pid}/features/config", json={
        "window_len_s": 2.0, "n_per_window": 512, "step": 20,
        "feature_ids": ["mean", "std"], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"
    }, expect=200)
    check(r.json()["step"] == 20, f"P3.4: sr=100×2s step=20 → 保存成功")

    # P3.5: sr=100, window=2, step=19 → 应被拒
    r = api("PUT", f"/api/projects/{_pid}/features/config", json={
        "window_len_s": 2.0, "n_per_window": 512, "step": 19,
        "feature_ids": ["mean", "std"], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"
    }, expect=422)
    check("STEP_TOO_SMALL" in r.text, f"P3.5: sr=100×2s step=19 → 被拒")

    # 恢复 sr=1 继续后续测试
    api("PATCH", f"/api/projects/{_pid}", json={"sampling_rate": 100.0}, expect=200)


# ============================================================
# P4: 特征工程全链路
# ============================================================
print("\n" + "=" * 60)
print("P4: 特征工程全链路")
print("=" * 60)

if _pid:
    # P4.1: 设置合法配置（sr=100, min_step=20）
    r = api("PUT", f"/api/projects/{_pid}/features/config", json={
        "window_len_s": 2.0, "n_per_window": 512, "step": 20,
        "feature_ids": ["mean", "std", "rms"], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"
    }, expect=200)
    check(True, "P4.1: PUT config 成功")

    # P4.2: 读取配置
    r = api("GET", f"/api/projects/{_pid}/features/config", expect=200)
    cfg = r.json()
    check(cfg["step"] == 20, f"P4.2: GET config step={cfg['step']}")
    check(cfg["window_len_s"] == 2.0, f"P4.2: GET config window={cfg['window_len_s']}")

    # P4.3: 计算特征
    r = api("POST", f"/api/projects/{_pid}/features/compute", json={}, expect=200)
    info = r.json()
    check(info["n_samples"] > 0, f"P4.3: compute → n_samples={info['n_samples']}")
    check(info["n_features"] > 0, f"P4.3: compute → n_features={info['n_features']}")

    # P4.4: 获取矩阵信息
    r = api("GET", f"/api/projects/{_pid}/features/matrix", expect=200)
    check("n_samples" in r.json(), "P4.4: GET matrix 成功")

    # P4.5: 特征评分
    r = api("GET", f"/api/projects/{_pid}/features/scoring", expect=200)
    check("ranking" in r.json(), "P4.5: GET scoring 成功")

    # P4.6: 特征分类
    r = api("GET", f"/api/projects/{_pid}/features/categories", expect=200)
    check("categories" in r.json(), "P4.6: GET categories 成功")


# ============================================================
# P5: 窗口参数边界测试
# ============================================================
print("\n" + "=" * 60)
print("P5: 窗口参数边界测试")
print("=" * 60)

if _pid:
    # P5.1: 窗长过小 → WINDOW_TOO_SMALL
    r = api("PUT", f"/api/projects/{_pid}/features/config", json={
        "window_len_s": 0.001, "n_per_window": 512, "step": 1,
        "feature_ids": ["mean"], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"
    }, expect=422)
    check("WINDOW_TOO_SMALL" in r.text, f"P5.1: window=0.001 → WINDOW_TOO_SMALL")

    # P5.2: 窗长超过分段 → WINDOW_TOO_LARGE（step 必须 >= min_step 才能到达窗口检查）
    r = api("PUT", f"/api/projects/{_pid}/features/config", json={
        "window_len_s": 1000.0, "n_per_window": 512, "step": 10000,
        "feature_ids": ["mean"], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"
    }, expect=422)
    check("WINDOW_TOO_LARGE" in r.text, f"P5.2: window=1000 → WINDOW_TOO_LARGE")

    # P5.3: 步进超出样本长度 → STEP_EXCEEDS_SAMPLE
    r = api("PUT", f"/api/projects/{_pid}/features/config", json={
        "window_len_s": 2.0, "n_per_window": 512, "step": 99999,
        "feature_ids": ["mean"], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"
    }, expect=422)
    check("STEP_EXCEEDS_SAMPLE" in r.text, f"P5.3: step=99999 → STEP_EXCEEDS_SAMPLE")

    # P5.5: 未知特征 → BAD_FEATURE
    r = api("PUT", f"/api/projects/{_pid}/features/config", json={
        "window_len_s": 2.0, "n_per_window": 512, "step": 20,
        "feature_ids": ["nonexistent_feature"], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"
    }, expect=422)
    check("BAD_FEATURE" in r.text, f"P5.5: nonexistent_feature → BAD_FEATURE")

    # P5.6: 时序模式空特征列表 → NO_FEATURES_SELECTED
    r = api("PUT", f"/api/projects/{_pid}/features/config", json={
        "window_len_s": 2.0, "n_per_window": 512, "step": 20,
        "feature_ids": [], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"
    }, expect=422)
    check("NO_FEATURES_SELECTED" in r.text, f"P5.6: feature_ids=[] → NO_FEATURES_SELECTED")

    # P5.4: 恢复合法配置（sr=100, min_step=20）
    api("PUT", f"/api/projects/{_pid}/features/config", json={
        "window_len_s": 2.0, "n_per_window": 512, "step": 20,
        "feature_ids": ["mean", "std", "rms"], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"
    }, expect=200)


# ============================================================
# P6: 配置保存 → 计算 → 重新计算 一致性
# ============================================================
print("\n" + "=" * 60)
print("P6: 配置变更 → 重算一致性")
print("=" * 60)

if _pid:
    # P6.1: step=20 计算（sr=100, min_step=20）
    api("PUT", f"/api/projects/{_pid}/features/config", json={
        "window_len_s": 2.0, "n_per_window": 512, "step": 20,
        "feature_ids": ["mean", "std"], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"
    }, expect=200)
    r1 = api("POST", f"/api/projects/{_pid}/features/compute", json={}, expect=200)
    samples_1 = r1.json()["n_samples"]

    # P6.2: step=50 计算（步进更大 → 样本更少）
    api("PUT", f"/api/projects/{_pid}/features/config", json={
        "window_len_s": 2.0, "n_per_window": 512, "step": 50,
        "feature_ids": ["mean", "std"], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"
    }, expect=200)
    r2 = api("POST", f"/api/projects/{_pid}/features/compute", json={}, expect=200)
    samples_2 = r2.json()["n_samples"]

    check(samples_1 > samples_2,
          f"P6: step=20({samples_1}) > step=50({samples_2})，步进影响样本数")


# ============================================================
# P7: 清理
# ============================================================
print("\n" + "=" * 60)
print("P7: 清理")
print("=" * 60)

if _pid:
    api("DELETE", f"/api/projects/{_pid}", expect=204)
    check(True, "P7: 项目已删除")

    # P7.1: 验证已删除 → GET 应 404
    api("GET", f"/api/projects/{_pid}", expect=404)

    # P7.2: DELETE 幂等性 → 再删一次应 404
    api("DELETE", f"/api/projects/{_pid}", expect=404)


# ============================================================
# 结果汇总
# ============================================================
print("\n" + "=" * 60)
print(f"测试结果: {PASS} 通过, {FAIL} 失败")
print("=" * 60)
if ERRORS:
    print("\n失败用例:")
    for e in ERRORS:
        print(f"  ❌ {e}")
    sys.exit(1)
else:
    print("\n🎉 全部通过！")
    sys.exit(0)
