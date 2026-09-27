#!/usr/bin/env python3
"""前后端一致性测试 — 验证同一参数组合在前端和后端产生相同结果。

核心思路：用 Python 重新实现前端 updateWindowInfo() 和 step 校验逻辑，
然后与后端 _validate() / effective_n() 的结果对比。

这是上次漏测的根本补充：上次只测后端，没验证前端计算是否与后端一致。
"""
from __future__ import annotations
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.dirname(os.path.abspath(__file__)))

from app.schemas import ProjectMeta, FeatureConfig
from app.services import feature_service

PASS = 0
FAIL = 0
ERRORS = []


def check(cond, msg):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  ✅ {msg}")
    else:
        FAIL += 1
        ERRORS.append(msg)
        print(f"  ❌ {msg}")


# ============================================================
# 前端逻辑 Python 镜像（与 app.js updateWindowInfo 一致）
# JS Math.round() 向零方向舍入 half-integer，Python round() 是银行家舍入
# ============================================================
import math

def js_round(x: float) -> int:
    """镜像 JS Math.round() — half-integer 向远离零方向舍入。"""
    return int(math.floor(x + 0.5))

def frontend_calc_n(sr: float, ws: float) -> int:
    """镜像前端 updateWindowInfo() 的窗口点数计算。"""
    raw_n = js_round(sr * ws)
    n = max(2, raw_n)
    if n % 2 != 0:
        n += 1
    return n


def frontend_calc_min_step(sr: float, ws: float) -> int:
    """镜像前端的 minStep 计算。"""
    n = frontend_calc_n(sr, ws)
    return max(1, n // 10)


def backend_calc_n(sr: float, ws: float) -> int:
    """后端 effective_n()。"""
    meta = ProjectMeta(project_id="x", name="x", mode="timeseries", sampling_rate=sr)
    cfg = FeatureConfig(window_len_s=ws, step=1, feature_ids=["mean"])
    return feature_service.effective_n(meta, cfg)


def backend_validate_step(sr: float, ws: float, step: int) -> tuple[bool, str]:
    """后端校验步进（含 schema 层 + _validate 层，mock 文件系统）。"""
    import tempfile, json, os
    from pathlib import Path
    from unittest.mock import patch

    meta = ProjectMeta(project_id="x", name="x", mode="timeseries", sampling_rate=sr)

    # schema 层校验（step < 1 会被 Pydantic 拒绝）
    try:
        cfg = FeatureConfig(window_len_s=ws, step=step, feature_ids=["mean"])
    except Exception:
        return False, f"schema 拒绝 step={step}"

    with tempfile.TemporaryDirectory() as tmpdir:
        labeling_dir = Path(tmpdir) / "labeling"
        labeling_dir.mkdir(exist_ok=True)
        (labeling_dir / "segments.json").write_text("[]")

        def fake_project_dir(pid):
            return Path(tmpdir)

        with patch("app.services.feature_service.project_dir", side_effect=fake_project_dir):
            try:
                feature_service._validate(meta, cfg)
                return True, "ok"
            except Exception as e:
                return False, str(e)


# ============================================================
# CL-1: 窗口点数一致性
# ============================================================
print("=" * 60)
print("CL-1: 窗口点数前后端一致性")
print("=" * 60)

test_cases = [
    # (sr, ws, expected_n)
    (1.0, 2.0, 2),       # 用户场景: 1Hz×2s
    (100.0, 0.5, 50),    # 典型: 100Hz×0.5s
    (10.0, 3.0, 30),     # 奇数→偶数
    (10.0, 2.5, 26),     # 25→26
    (1.0, 1.0, 2),       # 1→2
    (1.0, 0.5, 2),       # 0→2
    (1000.0, 0.01, 10),  # 高采样率
    (0.1, 10.0, 2),      # 极低采样率
    (50.0, 1.0, 50),     # 50Hz×1s
    (200.0, 0.1, 20),    # 200Hz×0.1s
    (33.3, 3.0, 100),    # 非整数采样率
    (10.0, 0.3, 4),      # 3→4
    (10.0, 0.29, 4),      # round(2.9)=3→4（偶数对齐）
]

for sr, ws, expected_n in test_cases:
    fe_n = frontend_calc_n(sr, ws)
    be_n = backend_calc_n(sr, ws)
    check(fe_n == be_n,
          f"CL-1: sr={sr}×ws={ws} → fe_n={fe_n} == be_n={be_n} (期望 {expected_n})")
    check(fe_n == expected_n,
          f"CL-1: sr={sr}×ws={ws} → n={fe_n} == 期望 {expected_n}")


# ============================================================
# CL-2: min_step 一致性
# ============================================================
print("\n" + "=" * 60)
print("CL-2: min_step 前后端一致性")
print("=" * 60)

for sr, ws, _ in test_cases:
    fe_min = frontend_calc_min_step(sr, ws)
    be_n = backend_calc_n(sr, ws)
    be_min = max(1, be_n // 10)
    check(fe_min == be_min,
          f"CL-2: sr={sr}×ws={ws} → fe_min={fe_min} == be_min={be_min}")


# ============================================================
# CL-3: step 校验一致性（前端认为合法的，后端也应合法）
# ============================================================
print("\n" + "=" * 60)
print("CL-3: step 校验前后端一致性")
print("=" * 60)

# (sr, ws, step) → 前端是否合法, 后端是否合法
step_cases = [
    (1.0, 2.0, 1, True),      # 用户场景
    (1.0, 2.0, 2, True),
    (1.0, 2.0, 0, False),     # step=0
    (100.0, 2.0, 20, True),   # sr=100, min_step=20
    (100.0, 2.0, 19, False),  # sr=100, step<min_step
    (100.0, 2.0, 1, False),   # sr=100, step<<min_step
    (10.0, 2.0, 2, True),     # sr=10, n=20, min_step=2
    (10.0, 2.0, 1, False),    # sr=10, step<min_step
    (10.0, 3.0, 3, True),     # sr=10, n=30, min_step=3
    (10.0, 3.0, 2, False),    # sr=10, step<min_step
]

for sr, ws, step, expect_ok in step_cases:
    fe_n = frontend_calc_n(sr, ws)
    fe_min = max(1, fe_n // 10)
    fe_ok = step >= fe_min and step >= 1
    be_ok, be_msg = backend_validate_step(sr, ws, step)

    # 前端和后端的判定必须一致
    check(fe_ok == be_ok,
          f"CL-3: sr={sr}×ws={ws} step={step} → fe={'OK' if fe_ok else 'REJECT'} == be={'OK' if be_ok else 'REJECT'}")
    if fe_ok != be_ok:
        print(f"       前端: n={fe_n} min_step={fe_min} → {'通过' if fe_ok else '拒绝'}")
        print(f"       后端: {be_msg}")


# ============================================================
# CL-4: 采样率同步场景（PATCH sr → 验证后端行为变化）
# ============================================================
print("\n" + "=" * 60)
print("CL-4: 采样率变化对校验的影响")
print("=" * 60)

# 场景：用户先设 sr=1，step=1（合法），然后改 sr=100，step=1（应被拒）
fe_n_1 = frontend_calc_n(1.0, 2.0)  # 2
fe_min_1 = max(1, fe_n_1 // 10)     # 1
check(1 >= fe_min_1, f"CL-4: sr=1 step=1 → 前端合法 (min_step={fe_min_1})")

fe_n_100 = frontend_calc_n(100.0, 2.0)  # 200
fe_min_100 = max(1, fe_n_100 // 10)     # 20
check(1 < fe_min_100, f"CL-4: sr=100 step=1 → 前端拒绝 (min_step={fe_min_100})")

# 后端同样
be_ok_1, _ = backend_validate_step(1.0, 2.0, 1)
be_ok_100, _ = backend_validate_step(100.0, 2.0, 1)
check(be_ok_1 and not be_ok_100,
      f"CL-4: 后端 sr=1→合法, sr=100→拒绝")


# ============================================================
# CL-5: 奇数 n 偶数对齐边界
# ============================================================
print("\n" + "=" * 60)
print("CL-5: 奇数 n 偶数对齐边界")
print("=" * 60)

# 关键 case：raw n 为奇数时，前端和后端都应向上取偶
odd_cases = [
    (10.0, 2.9, 30),   # 29→30
    (10.0, 3.1, 32),   # 31→32
    (10.0, 3.9, 40),   # 39→40
    (10.0, 4.1, 42),   # 41→42
    (100.0, 0.29, 30), # 29→30
    (100.0, 0.39, 40), # 39→40
]

for sr, ws, expected_n in odd_cases:
    fe_n = frontend_calc_n(sr, ws)
    be_n = backend_calc_n(sr, ws)
    check(fe_n == be_n == expected_n,
          f"CL-5: sr={sr}×ws={ws} → fe={fe_n} be={be_n} 期望={expected_n}")


# ============================================================
# CL-6: JS Math.round() vs Python round() 已知分歧点
# Python round() 是银行家舍入 (round half to even)
# JS Math.round() 是 half-integer 向远离零方向舍入
# 这导致 sr*ws 为 x.5 时，前端和后端计算出不同的 n
# ============================================================
print("\n" + "=" * 60)
print("CL-6: JS/Python 舍入分歧 — 已知差异记录")
print("=" * 60)

# 后端已修复：使用 int(math.floor(x+0.5)) 对齐 JS Math.round()
# 验证所有 x.5 产品现在前后端一致
x_half_cases = [
    # (sr, ws, product, expected_n) — expected_n 基于 JS Math.round
    (10.0, 0.25, 2.5, 4),    # 2.5→3(odd→4)
    (10.0, 0.45, 4.5, 6),    # 4.5→5(odd→6)
    (10.0, 0.65, 6.5, 8),    # 6.5→7(odd→8)
    (10.0, 1.85, 18.5, 20),  # 18.5→19(odd→20)
    (10.0, 2.05, 20.5, 22),  # 20.5→21(odd→22)
]

for sr, ws, prod, expected_n in x_half_cases:
    fe_n = frontend_calc_n(sr, ws)
    be_n = backend_calc_n(sr, ws)
    check(fe_n == be_n == expected_n,
          f"CL-6: sr={sr}×ws={ws} ({prod}) → fe={fe_n} be={be_n} 期望={expected_n}")


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
    print("\n🎉 前后端完全一致！")
    sys.exit(0)
