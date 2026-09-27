#!/usr/bin/env python3
"""窗口/步进计算修复 — 全面测试用例

测试目标：
1. 窗口点数计算：sampling_rate × window_len_s（不再强制最小16）
2. 步进默认值：step=1（不再256）
3. 步进自动调整：窗长变化时 step 自动调整为 max(1, floor(n/10))
4. 步进校验：step < n/10 → 422 STEP_TOO_SMALL
5. 步进超样本长度：step > min_seg_len → 422 STEP_EXCEEDS_SAMPLE
6. 特征计算实际使用 cfg.step（而非硬编码 n//2）
7. 边界情况：极小采样率、极大窗长、步进=1、步进=n 等
"""
from __future__ import annotations

import io
import os
import sys
import shutil
import tempfile

import numpy as np
import pandas as pd

# 确保可以导入 app 模块
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.dirname(os.path.abspath(__file__)))

from app.schemas import ProjectCreate, FeatureConfig, ProjectMeta
from app.services.project_service import ProjectService
from app.services import feature_service
from app.deps import project_dir, atomic_write_json, save_meta, read_json

svc = ProjectService()
PASS = 0
FAIL = 0
ERRORS = []


def check(condition: bool, msg: str):
    global PASS, FAIL
    if condition:
        PASS += 1
        print(f"  ✅ {msg}")
    else:
        FAIL += 1
        ERRORS.append(msg)
        print(f"  ❌ {msg}")


class MockUploadFile:
    def __init__(self, filename: str, content: bytes):
        self.filename = filename
        self.file = io.BytesIO(content)
        self.size = len(content)


def make_project(name: str, sr: float = 100.0) -> str:
    """创建测试项目并返回 pid。"""
    body = ProjectCreate(name=name, mode="timeseries", task_type="classification", sampling_rate=sr)
    result = svc.create(body)
    return result.project_id


def make_csv_and_import(pid: str, n_rows: int = 1000, sr: float = 100.0,
                        channels: list[str] | None = None) -> dict:
    """生成 CSV 并导入，返回 import 结果。
    label 用连续块（每块 n_rows//3 行），确保分段长度足够窗口滑动。"""
    if channels is None:
        channels = ["accel_x", "accel_y", "accel_z"]
    rng = np.random.default_rng(42)
    t = np.arange(n_rows) / sr
    data = {ch: rng.normal(0, 1, n_rows) for ch in channels}
    data["timestamp"] = t
    # 连续标签块：每块 n_rows//3 行，产生 3 个长分段
    block = max(1, n_rows // 3)
    label = np.array([0] * block + [1] * block + [2] * (n_rows - 2 * block))[:n_rows]
    data["label"] = label
    df = pd.DataFrame(data)
    csv_bytes = df.to_csv(index=False).encode()
    uf = MockUploadFile("test.csv", csv_bytes)
    mapping = {"channels": channels, "ts_col": "timestamp", "label_col": "label"}
    from app.services.dataset_service import import_files
    meta = svc.get(pid)
    return import_files(meta, [uf], mapping, "replace")


def cleanup(pid: str):
    """清理项目目录。"""
    pdir = project_dir(pid)
    if pdir.exists():
        shutil.rmtree(pdir, ignore_errors=True)
    try:
        svc.delete(pid)
    except Exception:
        pass


# ============================================================
# TC-1: effective_n 计算正确性
# ============================================================
print("=" * 60)
print("TC-1: effective_n 计算正确性")
print("=" * 60)

pid1 = make_project("test_eff_n", sr=1.0)
meta1 = svc.get(pid1)

# TC-1.1: 1Hz × 2s = 2 点（不是16）
cfg = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean"])
n = feature_service.effective_n(meta1, cfg)
check(n == 2, f"TC-1.1: 1Hz×2s → n={n}，期望=2")

# TC-1.2: 100Hz × 0.5s = 50 点
meta1.sampling_rate = 100.0
cfg2 = FeatureConfig(window_len_s=0.5, step=1, feature_ids=["mean"])
n2 = feature_service.effective_n(meta1, cfg2)
check(n2 == 50, f"TC-1.2: 100Hz×0.5s → n={n2}，期望=50")

# TC-1.3: 10Hz × 3s = 30 点（奇数 → 偶数修正 = 30）
meta1.sampling_rate = 10.0
cfg3 = FeatureConfig(window_len_s=3.0, step=1, feature_ids=["mean"])
n3 = feature_service.effective_n(meta1, cfg3)
check(n3 == 30, f"TC-1.3: 10Hz×3s → n={n3}，期望=30")

# TC-1.4: 10Hz × 2.5s = 25 点（奇数 → 偶数修正 = 26）
meta1.sampling_rate = 10.0
cfg4 = FeatureConfig(window_len_s=2.5, step=1, feature_ids=["mean"])
n4 = feature_service.effective_n(meta1, cfg4)
check(n4 == 26, f"TC-1.4: 10Hz×2.5s → n={n4}（奇→偶修正），期望=26")

# TC-1.5: 1Hz × 1s = 1 点（最小边界）
meta1.sampling_rate = 1.0
cfg5 = FeatureConfig(window_len_s=1.0, step=1, feature_ids=["mean"])
n5 = feature_service.effective_n(meta1, cfg5)
check(n5 == 2, f"TC-1.5: 1Hz×1s → n={n5}（奇→偶修正），期望=2")

# TC-1.6: 1Hz × 0.5s = 1 点（奇→偶修正 = 2）
meta1.sampling_rate = 1.0
cfg6 = FeatureConfig(window_len_s=0.5, step=1, feature_ids=["mean"])
n6 = feature_service.effective_n(meta1, cfg6)
check(n6 == 2, f"TC-1.6: 1Hz×0.5s → n={n6}（奇→偶修正），期望=2")

cleanup(pid1)


# ============================================================
# TC-2: 步进默认值
# ============================================================
print("\n" + "=" * 60)
print("TC-2: 步进默认值")
print("=" * 60)

# TC-2.1: FeatureConfig 默认 step=1
cfg_default = FeatureConfig()
check(cfg_default.step == 1, f"TC-2.1: FeatureConfig 默认 step={cfg_default.step}，期望=1")

# TC-2.2: DEFAULT_CONFIG step=1
check(feature_service.DEFAULT_CONFIG.step == 1,
      f"TC-2.2: DEFAULT_CONFIG step={feature_service.DEFAULT_CONFIG.step}，期望=1")


# ============================================================
# TC-3: 步进校验 step < n/10
# ============================================================
print("\n" + "=" * 60)
print("TC-3: 步进校验 step < n/10")
print("=" * 60)

pid3 = make_project("test_step_validate", sr=100.0)
meta3 = svc.get(pid3)
make_csv_and_import(pid3, n_rows=10000, sr=100.0)

# TC-3.1: step=1, n=100 (100Hz×1s), min_step=10 → 应拒绝
cfg_bad = FeatureConfig(window_len_s=1.0, step=1, feature_ids=["mean", "std"])
try:
    feature_service.put_config(meta3, cfg_bad)
    check(False, "TC-3.1: step=1 < min_step=10 应抛出 STEP_TOO_SMALL，但未抛出")
except Exception as e:
    check("STEP_TOO_SMALL" in str(e) or "最小" in str(e),
          f"TC-3.1: step=1 < min_step=10 → 错误信息: {e}")

# TC-3.2: step=10, n=100, min_step=10 → 应通过
cfg_ok = FeatureConfig(window_len_s=1.0, step=10, feature_ids=["mean", "std"])
try:
    result = feature_service.put_config(meta3, cfg_ok)
    check(result.step == 10, f"TC-3.2: step=10 = min_step=10 → 通过，step={result.step}")
except Exception as e:
    check(False, f"TC-3.2: step=10 = min_step=10 应通过，但报错: {e}")

# TC-3.3: step=5, n=20 (100Hz×0.2s), min_step=2 → 应通过
cfg_ok2 = FeatureConfig(window_len_s=0.2, step=5, feature_ids=["mean", "std"])
try:
    result2 = feature_service.put_config(meta3, cfg_ok2)
    check(result2.step == 5, f"TC-3.3: step=5 ≥ min_step=2 → 通过，step={result2.step}")
except Exception as e:
    check(False, f"TC-3.3: step=5 ≥ min_step=2 应通过，但报错: {e}")

# TC-3.4: step=1, n=2 (1Hz×2s), min_step=1 → 应通过
meta3.sampling_rate = 1.0
save_meta(meta3)
cfg_small = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean", "std"])
try:
    result3 = feature_service.put_config(meta3, cfg_small)
    check(result3.step == 1, f"TC-3.4: step=1 = min_step=1 (n=2) → 通过")
except Exception as e:
    check(False, f"TC-3.4: step=1 = min_step=1 应通过，但报错: {e}")

# 恢复采样率
meta3.sampling_rate = 100.0
save_meta(meta3)

cleanup(pid3)


# ============================================================
# TC-4: 步进超样本长度
# ============================================================
print("\n" + "=" * 60)
print("TC-4: 步进超样本长度")
print("=" * 60)

pid4 = make_project("test_step_exceed", sr=100.0)
meta4 = svc.get(pid4)
make_csv_and_import(pid4, n_rows=100, sr=100.0)  # 100 行数据

# TC-4.1: step=200 > 最短分段长度 → 应拒绝
# 先设置一个合法的窗长（n=2, min_step=1），然后 step=200 远超分段长度
meta4.sampling_rate = 1.0
save_meta(meta4)
cfg_big_step = FeatureConfig(window_len_s=2.0, step=200, feature_ids=["mean", "std"])
try:
    feature_service.put_config(meta4, cfg_big_step)
    check(False, "TC-4.1: step=200 > 样本长度 应抛出错误，但未抛出")
except Exception as e:
    err_str = str(e)
    check("STEP_EXCEEDS_SAMPLE" in err_str or "超出" in err_str or "WINDOW_TOO_LARGE" in err_str,
          f"TC-4.1: step=200 超出分段 → 错误信息: {e}")
# 恢复采样率
meta4.sampling_rate = 100.0
save_meta(meta4)

cleanup(pid4)


# ============================================================
# TC-5: 特征计算使用 cfg.step（而非硬编码 n//2）
# ============================================================
print("\n" + "=" * 60)
print("TC-5: 特征计算使用 cfg.step")
print("=" * 60)

pid5 = make_project("test_step_used", sr=10.0)
meta5 = svc.get(pid5)
make_csv_and_import(pid5, n_rows=500, sr=10.0)

# TC-5.1: 设置 step=2（n=20, min_step=2），验证配置被保存
cfg_step1 = FeatureConfig(window_len_s=2.0, step=2, feature_ids=["mean", "std", "rms"])
result5 = feature_service.put_config(meta5, cfg_step1)
check(result5.step == 2, f"TC-5.1: put_config step=2 → 返回 step={result5.step}")

# TC-5.2: 读取配置确认 step=2
cfg_read = feature_service.get_config(meta5)
check(cfg_read.step == 2, f"TC-5.2: get_config → step={cfg_read.step}，期望=2")

# TC-5.3: 执行特征计算，验证使用 cfg.step=2（而非硬编码 n//2=10）
info5 = feature_service.compute(meta5)
check(info5["n_samples"] > 0, f"TC-5.3: compute → n_samples={info5.get('n_samples')}，应 > 0")

# TC-5.4: 对比 step=2 vs step=10 的窗口数
# 清理重新计算
shutil.rmtree(project_dir(pid5) / "features", ignore_errors=True)
cfg_step10 = FeatureConfig(window_len_s=2.0, step=10, feature_ids=["mean", "std", "rms"])
feature_service.put_config(meta5, cfg_step10)
info5b = feature_service.compute(meta5)

# step=2 的行数应大于 step=10 的行数
samples_step2 = info5["n_samples"]
samples_step10 = info5b["n_samples"]
check(samples_step2 > samples_step10,
      f"TC-5.4: step=2 n_samples={samples_step2} > step=10 n_samples={samples_step10}，步进影响窗口数")

cleanup(pid5)


# ============================================================
# TC-6: 极端边界情况
# ============================================================
print("\n" + "=" * 60)
print("TC-6: 极端边界情况")
print("=" * 60)

# TC-6.1: sampling_rate=0.1Hz, window_len_s=10s → n=1（奇→偶=2）
pid6a = make_project("test_edge_1", sr=0.1)
meta6a = svc.get(pid6a)
cfg_edge = FeatureConfig(window_len_s=10.0, step=1, feature_ids=["mean"])
n_edge = feature_service.effective_n(meta6a, cfg_edge)
check(n_edge == 2, f"TC-6.1: 0.1Hz×10s → n={n_edge}，期望=2")

# TC-6.2: sampling_rate=1000Hz, window_len_s=0.01s → n=10
meta6a.sampling_rate = 1000.0
save_meta(meta6a)
cfg_fast = FeatureConfig(window_len_s=0.01, step=1, feature_ids=["mean"])
n_fast = feature_service.effective_n(meta6a, cfg_fast)
check(n_fast == 10, f"TC-6.2: 1000Hz×0.01s → n={n_fast}，期望=10")

# TC-6.3: window_len_s 极小 → n < 2 → 应拒绝
meta6a.sampling_rate = 1.0
save_meta(meta6a)
cfg_tiny = FeatureConfig(window_len_s=0.001, step=1, feature_ids=["mean"])
try:
    feature_service.put_config(meta6a, cfg_tiny)
    check(False, "TC-6.3: window_len=0.001s → n<2 应拒绝，但未拒绝")
except Exception as e:
    check("WINDOW_TOO_SMALL" in str(e) or "至少" in str(e),
          f"TC-6.3: window_len=0.001s → 错误: {e}")

cleanup(pid6a)

# TC-6.4: step=n（步进等于窗口大小，不重叠）
pid6d = make_project("test_step_eq_n", sr=10.0)
meta6d = svc.get(pid6d)
make_csv_and_import(pid6d, n_rows=500, sr=10.0)
# n=20, step=20 → 合法（min_step=2）
cfg_eq = FeatureConfig(window_len_s=2.0, step=20, feature_ids=["mean", "std"])
try:
    result6d = feature_service.put_config(meta6d, cfg_eq)
    check(result6d.step == 20, f"TC-6.4: step=n=20 → 通过")
except Exception as e:
    check(False, f"TC-6.4: step=n=20 应通过，但报错: {e}")
cleanup(pid6d)

# TC-6.5: step=0 → schema 层拒绝（ge=1）
pid6e = make_project("test_step_zero", sr=10.0)
meta6e = svc.get(pid6e)
make_csv_and_import(pid6e, n_rows=500, sr=10.0)
try:
    cfg_zero = FeatureConfig(window_len_s=2.0, step=0, feature_ids=["mean", "std"])
    check(False, "TC-6.5: step=0 → schema 应拒绝")
except Exception as e:
    check("greater_than_equal" in str(e) or "step" in str(e).lower(),
          f"TC-6.5: step=0 → schema 拒绝: {e}")
cleanup(pid6e)


# ============================================================
# TC-7: 前端默认值一致性（后端模拟）
# ============================================================
print("\n" + "=" * 60)
print("TC-7: 默认值一致性验证")
print("=" * 60)

# TC-7.1: 新项目默认配置的 step=1
pid7 = make_project("test_defaults", sr=100.0)
meta7 = svc.get(pid7)
cfg_default7 = feature_service.get_config(meta7)
check(cfg_default7.step == 1, f"TC-7.1: 新项目默认 step={cfg_default7.step}，期望=1")
check(cfg_default7.window_len_s == 2.0, f"TC-7.1: 默认 window_len_s={cfg_default7.window_len_s}，期望=2.0")
cleanup(pid7)


# ============================================================
# TC-8: 向后兼容性（旧配置 step=256）
# ============================================================
print("\n" + "=" * 60)
print("TC-8: 向后兼容性")
print("=" * 60)

pid8 = make_project("test_compat", sr=100.0)
meta8 = svc.get(pid8)
make_csv_and_import(pid8, n_rows=5000, sr=100.0)

# TC-8.1: 旧项目 config.json 中 step=256，读取后应仍能正常工作
# 手动写入旧格式 config
old_cfg = {"window_len_s": 2.0, "n_per_window": 512, "step": 256,
           "feature_ids": ["mean", "std"], "freq_enabled": False, "freq_bands": 5, "norm": "zscore"}
atomic_write_json(project_dir(pid8) / "features" / "config.json", old_cfg)
cfg_old = feature_service.get_config(meta8)
check(cfg_old.step == 256, f"TC-8.1: 旧配置 step=256 → 读取 step={cfg_old.step}")

# TC-8.2: 旧配置 step=256 仍能通过验证（n=100, min_step=10, step=256 ≥ 10）
try:
    result8 = feature_service.put_config(meta8, cfg_old)
    check(result8.step == 256, f"TC-8.2: 旧 step=256 仍合法 → step={result8.step}")
except Exception as e:
    check(False, f"TC-8.2: 旧 step=256 应合法，但报错: {e}")

# TC-8.3: 旧配置 step=256 能正常计算特征
info8 = feature_service.compute(meta8)
check(info8["n_samples"] > 0, f"TC-8.3: 旧 step=256 compute → n_samples={info8.get('n_samples')}")

cleanup(pid8)


# ============================================================
# TC-9: table 模式不受影响
# ============================================================
print("\n" + "=" * 60)
print("TC-9: table 模式不受影响")
print("=" * 60)

# TC-9.1: table 模式 put_config 跳过窗口校验
from app.schemas import ProjectCreate
body9 = ProjectCreate(name="test_table_mode", mode="table", task_type="classification")
meta9 = svc.create(body9)
pid9 = meta9.project_id

cfg_table = FeatureConfig(window_len_s=0.001, step=1, feature_ids=[])
try:
    result9 = feature_service.put_config(meta9, cfg_table)
    # table 模式跳过窗口校验，step=1 直接通过
    check(result9.step >= 1, f"TC-9.1: table 模式 step=1 → 通过 step={result9.step}")
except Exception as e:
    check(False, f"TC-9.1: table 模式应跳过校验，但报错: {e}")

cleanup(pid9)


# ============================================================
# TC-10: 多文件多分段场景
# ============================================================
print("\n" + "=" * 60)
print("TC-10: 多文件多分段场景")
print("=" * 60)

pid10 = make_project("test_multi_seg", sr=10.0)
meta10 = svc.get(pid10)

# 导入两个文件，产生不同长度的分段
# 文件1: 1000 行 → 3 个分段，每段约 333 行
make_csv_and_import(pid10, n_rows=1000, sr=10.0)

# TC-10.1: n=20(10Hz×2s), step=2, min_step=2 → 应通过
cfg10 = FeatureConfig(window_len_s=2.0, step=2, feature_ids=["mean", "std"])
try:
    result10 = feature_service.put_config(meta10, cfg10)
    check(result10.step == 2, f"TC-10.1: multi-seg step=2 → 通过")
except Exception as e:
    check(False, f"TC-10.1: multi-seg step=2 应通过，但报错: {e}")

# TC-10.2: 特征计算应成功
info10 = feature_service.compute(meta10)
check(info10["n_samples"] > 0, f"TC-10.2: multi-seg compute → n_samples={info10.get('n_samples')}")
check(info10["dropped_short"] == 0, f"TC-10.2: dropped_short={info10.get('dropped_short')}，期望=0")

cleanup(pid10)


# ============================================================
# TC-11: step 边界值精确测试
# ============================================================
print("\n" + "=" * 60)
print("TC-11: step 边界值精确测试")
print("=" * 60)

pid11 = make_project("test_step_boundary", sr=100.0)
meta11 = svc.get(pid11)
make_csv_and_import(pid11, n_rows=5000, sr=100.0)

# n=200 (100Hz×2s), min_step=20
# TC-11.1: step=19 < 20 → 拒绝
cfg11a = FeatureConfig(window_len_s=2.0, step=19, feature_ids=["mean"])
try:
    feature_service.put_config(meta11, cfg11a)
    check(False, "TC-11.1: step=19 < min_step=20 应拒绝")
except Exception as e:
    check("STEP_TOO_SMALL" in str(e), f"TC-11.1: step=19 → {e}")

# TC-11.2: step=20 = min_step → 通过
cfg11b = FeatureConfig(window_len_s=2.0, step=20, feature_ids=["mean"])
try:
    result11b = feature_service.put_config(meta11, cfg11b)
    check(result11b.step == 20, f"TC-11.2: step=20 = min_step → 通过")
except Exception as e:
    check(False, f"TC-11.2: step=20 应通过，但报错: {e}")

# TC-11.3: step=21 > min_step → 通过
cfg11c = FeatureConfig(window_len_s=2.0, step=21, feature_ids=["mean"])
try:
    result11c = feature_service.put_config(meta11, cfg11c)
    check(result11c.step == 21, f"TC-11.3: step=21 > min_step → 通过")
except Exception as e:
    check(False, f"TC-11.3: step=21 应通过，但报错: {e}")

# TC-11.4: step=200 = n → 通过（不重叠窗口）
cfg11d = FeatureConfig(window_len_s=2.0, step=200, feature_ids=["mean"])
try:
    result11d = feature_service.put_config(meta11, cfg11d)
    check(result11d.step == 200, f"TC-11.4: step=200 = n → 通过")
except Exception as e:
    check(False, f"TC-11.4: step=200 应通过，但报错: {e}")

cleanup(pid11)


# ============================================================
# TC-12: WINDOW_TOO_LARGE 错误码
# ============================================================
print("\n" + "=" * 60)
print("TC-12: WINDOW_TOO_LARGE 错误码")
print("=" * 60)

pid12 = make_project("test_window_too_large", sr=100.0)
meta12 = svc.get(pid12)
# 导入短数据：100 行 → segment 约 33 行
make_csv_and_import(pid12, n_rows=100, sr=100.0)

# TC-12.1: window_len=1.0s → n=100 > 33 → WINDOW_TOO_LARGE
cfg12 = FeatureConfig(window_len_s=1.0, step=10, feature_ids=["mean"])
try:
    feature_service.put_config(meta12, cfg12)
    check(False, "TC-12.1: n=100 > seg=33 应触发 WINDOW_TOO_LARGE")
except Exception as e:
    check("WINDOW_TOO_LARGE" in str(e),
          f"TC-12.1: → 错误码: {e}")

cleanup(pid12)


# ============================================================
# TC-13: sampling_rate=0 跳过窗口校验
# ============================================================
print("\n" + "=" * 60)
print("TC-13: sampling_rate=0 跳过窗口校验")
print("=" * 60)

pid13 = make_project("test_sr_zero", sr=1.0)
meta13 = svc.get(pid13)
# 手动将 sampling_rate 设为 0（模拟未设置采样率的场景）
meta13.sampling_rate = 0
save_meta(meta13)

# TC-13.1: sampling_rate=0 时，任意 window/step 配置都能保存
cfg13 = FeatureConfig(window_len_s=0.001, step=1, feature_ids=["mean", "std"])
try:
    result13 = feature_service.put_config(meta13, cfg13)
    check(True, "TC-13.1: sampling_rate=0 → 跳过窗口校验，配置保存成功")
except Exception as e:
    check(False, f"TC-13.1: sampling_rate=0 应跳过校验，但报错: {e}")

cleanup(pid13)


# ============================================================
# TC-14: 负数 step/window_len_s
# ============================================================
print("\n" + "=" * 60)
print("TC-14: 负数 step/window_len_s")
print("=" * 60)

pid14 = make_project("test_negative", sr=10.0)
meta14 = svc.get(pid14)
make_csv_and_import(pid14, n_rows=500, sr=10.0)

# TC-14.1: step=-5 → schema 层拒绝（ge=1）
try:
    cfg14a = FeatureConfig(window_len_s=2.0, step=-5, feature_ids=["mean"])
    check(False, "TC-14.1: step=-5 应被 schema 拒绝")
except Exception as e:
    check("greater_than_equal" in str(e) or "step" in str(e).lower(),
          f"TC-14.1: step=-5 → schema 拒绝: {e}")

# TC-14.2: window_len_s=-2.0 → n=round(-20)=-20, n<2 → WINDOW_TOO_SMALL
cfg14b = FeatureConfig(window_len_s=-2.0, step=1, feature_ids=["mean"])
try:
    feature_service.put_config(meta14, cfg14b)
    check(False, "TC-14.2: window_len=-2.0 应被拒绝")
except Exception as e:
    check("WINDOW_TOO_SMALL" in str(e) or "至少" in str(e),
          f"TC-14.2: window_len=-2.0 → {e}")

cleanup(pid14)


# ============================================================
# TC-15: n_per_window 字段被忽略（不影响计算）
# ============================================================
print("\n" + "=" * 60)
print("TC-15: n_per_window 字段被忽略")
print("=" * 60)

pid15 = make_project("test_n_per_window_ignored", sr=10.0)
meta15 = svc.get(pid15)
make_csv_and_import(pid15, n_rows=500, sr=10.0)

# TC-15.1: 设置 n_per_window=1000（远大于实际 n=20），验证不影响计算
cfg15a = FeatureConfig(window_len_s=2.0, n_per_window=1000, step=2, feature_ids=["mean", "std"])
feature_service.put_config(meta15, cfg15a)
info15a = feature_service.compute(meta15)

# 清理重新计算
shutil.rmtree(project_dir(pid15) / "features", ignore_errors=True)

# TC-15.2: 设置 n_per_window=16（远小于实际 n=20），验证不影响计算
cfg15b = FeatureConfig(window_len_s=2.0, n_per_window=16, step=2, feature_ids=["mean", "std"])
feature_service.put_config(meta15, cfg15b)
info15b = feature_service.compute(meta15)

# 两次计算结果应完全相同（n_per_window 不影响）
check(info15a["n_samples"] == info15b["n_samples"],
      f"TC-15: n_per_window=1000 vs 16 → n_samples 相同: {info15a['n_samples']}=={info15b['n_samples']}")

cleanup(pid15)


# ============================================================
# TC-16: FREQ_NEEDS_POW2 和 FREQ_NOT_ENABLED
# ============================================================
print("\n" + "=" * 60)
print("TC-16: 频域校验")
print("=" * 60)

pid16 = make_project("test_freq", sr=100.0)
meta16 = svc.get(pid16)
make_csv_and_import(pid16, n_rows=1000, sr=100.0)

# TC-16.1: freq_enabled=True, n=30(100Hz×0.3s), 非2的幂 → FREQ_NEEDS_POW2
cfg16a = FeatureConfig(window_len_s=0.3, step=3, feature_ids=["mean"], freq_enabled=True)
try:
    feature_service.put_config(meta16, cfg16a)
    check(False, "TC-16.1: n=30 非2的幂+freq 应触发 FREQ_NEEDS_POW2")
except Exception as e:
    check("FREQ_NEEDS_POW2" in str(e),
          f"TC-16.1: → {e}")

# TC-16.2: 选了频域特征但 freq_enabled=False → FREQ_NOT_ENABLED
cfg16b = FeatureConfig(window_len_s=0.32, step=3, feature_ids=["mean", "spec_centroid"],
                       freq_enabled=False)
try:
    feature_service.put_config(meta16, cfg16b)
    check(False, "TC-16.2: 选频域特征+freq_enabled=False 应触发 FREQ_NOT_ENABLED")
except Exception as e:
    check("FREQ_NOT_ENABLED" in str(e),
          f"TC-16.2: → {e}")

cleanup(pid16)


# ============================================================
# TC-17: 用户实际场景 — sr=1Hz, window=2s, step=1
# ============================================================
print("\n" + "=" * 60)
print("TC-17: 用户实际场景 sr=1Hz window=2s step=1")
print("=" * 60)

pid17 = make_project("test_user_scenario", sr=1.0)
meta17 = svc.get(pid17)
# 导入100行数据，sr=1Hz → 100秒，产生长分段
make_csv_and_import(pid17, n_rows=100, sr=1.0)

# TC-17.1: sr=1, window=2, step=1 → n=2, min_step=1, step=1 应合法
cfg17 = FeatureConfig(window_len_s=2.0, step=1, feature_ids=["mean", "std"])
try:
    result17 = feature_service.put_config(meta17, cfg17)
    check(result17.step == 1, f"TC-17.1: sr=1×2s → step=1 合法，step={result17.step}")
except Exception as e:
    check(False, f"TC-17.1: sr=1×2s step=1 应合法，但报错: {e}")

# TC-17.2: sr=1, window=2, step=1 → 能正常计算特征
info17 = feature_service.compute(meta17)
check(info17["n_samples"] > 0, f"TC-17.2: compute → n_samples={info17.get('n_samples')}")

cleanup(pid17)


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
