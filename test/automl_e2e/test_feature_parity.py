#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""TD1 — 特征数值一致性测试（Python float64 参考 vs C float32 预置原语）。

独立可运行：
    python3 /home/yyds/code/BabyOS/test/automl_e2e/test_feature_parity.py
退出码 0 = 全部通过。

覆盖：
1. 12 时域特征：mean/std/variance/min/max/rms/abs_mean/ptp/zcr/autocorr/skew/kurt
2. 4 频域特征：spec_centroid/spec_energy/dominant_freq/band_ratio
   （频域通过 bAlgoFft* 原语路径验证，与 feature_cgen 生成代码所调用的一致）
3. dominant_freq 含直流 bin（argmax 从 i=0 开始）的专项用例
4. feature_cgen.py 生成路径的存在性/可调用检查
5. 边界：零方差、n=1、纯直流、常量信号

容差：|a-b| <= max(1e-4*|b|, 1e-5)（C float32 vs Python float64）
"""

from __future__ import annotations

import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Dict, List, Tuple

import numpy as np

# ---------------------------------------------------------------------------
# 路径常量（与冻结契约一致）
# ---------------------------------------------------------------------------
REPO = "/home/yyds/code/BabyOS"
ALGO_DIR = os.path.join(REPO, "bos/algorithm")
ALGO_INC = os.path.join(REPO, "bos/algorithm/inc")
EXPORT_PY = os.path.join(REPO, "tool/babyos-studio/python")
E2E_DIR = os.path.join(REPO, "test/automl_e2e")

SIGNAL_C = os.path.join(ALGO_DIR, "algo_signal.c")
FFT_C = os.path.join(ALGO_DIR, "algo_fft.c")

# feature_cgen.py 中生成代码所调用的原语名（存在性检查）
EXPECTED_TIME_PRIM = {
    "mean": "bAlgoSignalMean",
    "std": "bAlgoSignalStd",
    "variance": "bAlgoSignalVariance",
    "min": "s_stats.min",
    "max": "s_stats.max",
    "rms": "bAlgoSignalRms",
    "abs_mean": "bAlgoSignalAbsMean",
    "ptp": "bAlgoSignalPtp",
    "zcr": "bAlgoSignalZcr",
    "autocorr": "bAlgoSignalAutocorr",
    "skew": "bAlgoSignalSkew",
    "kurt": "bAlgoSignalKurt",
}

EXPECTED_FREQ_PRIM = {
    "spec_centroid": "bAlgoFftCentroid",
    "spec_energy": "bAlgoFftEnergy",
    "dominant_freq": "bAlgoFftDominantFreq",
    "band_ratio": "bAlgoFftBandRatio",
}

TIME_FEATURES = [
    "mean", "std", "variance", "min", "max", "rms",
    "abs_mean", "ptp", "zcr", "autocorr", "skew", "kurt",
]
FREQ_FEATURES = ["spec_centroid", "spec_energy", "dominant_freq", "band_ratio"]

TOL_REL = 1e-4
TOL_ABS = 1e-5

PASS_COUNT = 0
FAIL_COUNT = 0
FAILURES: List[str] = []


def close(a: float, b: float) -> bool:
    return abs(a - b) <= max(TOL_REL * abs(b), TOL_ABS)


def check(name: str, got: float, exp: float, note: str = "") -> bool:
    global PASS_COUNT, FAIL_COUNT
    ok = close(got, exp)
    if ok:
        PASS_COUNT += 1
    else:
        FAIL_COUNT += 1
        msg = "%s: got=%.9g exp=%.9g diff=%.3g %s" % (name, got, exp, got - exp, note)
        FAILURES.append(msg)
        print("  FAIL %s" % msg)
    return ok


# ---------------------------------------------------------------------------
# 参考实现（与 feature_service._time_features / _freq_features 口径一致，
# 但用 numpy float64 独立计算，不 import 业务代码以避免耦合）
# ---------------------------------------------------------------------------
def ref_time_features(w: np.ndarray) -> Dict[str, float]:
    n = int(w.shape[0])
    m = float(np.mean(w))
    d = w - m
    m2 = float(np.mean(d * d))
    std = math.sqrt(m2)
    m3 = float(np.mean(d * d * d))
    m4 = float(np.mean(d * d * d * d))
    skew = (m3 / (m2 ** 1.5)) if m2 > 0 else 0.0
    kurt = (m4 / (m2 * m2) - 3.0) if m2 > 0 else 0.0
    zcr = float(np.mean(w[:-1] * w[1:] < 0)) if n > 1 else 0.0
    if n > 1 and m2 > 0:
        lag1 = float(np.mean(d[:-1] * d[1:]))
        autocorr = lag1 / m2
    else:
        autocorr = 0.0
    return {
        "mean": m,
        "std": std,
        "variance": m2,
        "min": float(np.min(w)),
        "max": float(np.max(w)),
        "rms": math.sqrt(float(np.mean(w * w))),
        "abs_mean": float(np.mean(np.abs(w))),
        "ptp": float(np.max(w) - np.min(w)),
        "zcr": zcr,
        "autocorr": autocorr,
        "skew": skew,
        "kurt": kurt,
    }


def ref_freq_features(w: np.ndarray, fs: float, bands: int = 4) -> Dict[str, float]:
    """与 feature_service._freq_features 对齐；用 numpy.fft（与 scipy.fft.rfft 同口径）。"""
    n = int(w.shape[0])
    spec = np.abs(np.fft.rfft(w))  # bins 0..n/2
    k = np.arange(spec.shape[0])
    freqs = k * fs / n
    total_mag = float(np.sum(spec))
    energy = float(np.sum(spec * spec))
    centroid = float(np.sum(freqs * spec) / total_mag) if total_mag > 0 else 0.0
    dom = float(freqs[int(np.argmax(spec))]) if spec.shape[0] else 0.0
    edges = np.linspace(0, spec.shape[0], bands + 1).astype(int)
    out = {
        "spec_centroid": centroid,
        "spec_energy": energy,
        "dominant_freq": dom,
    }
    for b in range(bands):
        seg = spec[edges[b] : edges[b + 1]]
        out["band%d_ratio" % b] = float(np.sum(seg * seg) / energy) if energy > 0 else 0.0
    return out


# ---------------------------------------------------------------------------
# 测试信号（C 侧直接用 float 常量数组；Python 用相同 float64 值再 cast 到 float32）
# ---------------------------------------------------------------------------
def make_signals() -> List[Tuple[str, np.ndarray]]:
    """返回 (case_name, float64 array) 列表；调用方会 cast 到 float32 再生成 C。"""
    sigs: List[Tuple[str, np.ndarray]] = []

    # 1. 正弦 + 直流偏置（含过零）
    n = 64
    t = np.arange(n, dtype=np.float64)
    sigs.append(("sine_offset", 2.0 + 1.5 * np.sin(2.0 * np.pi * 3.0 * t / n)))

    # 2. 纯正弦（无偏置）
    sigs.append(("sine_pure", np.sin(2.0 * np.pi * 5.0 * t / n)))

    # 3. 纯直流（零方差）
    sigs.append(("dc_const", np.full(n, 7.25)))

    # 4. 常量 0
    sigs.append(("zero_const", np.zeros(n)))

    # 5. 随机正负（固定种子）
    rng = np.random.RandomState(42)
    sigs.append(("rand_pm", rng.uniform(-2.0, 3.0, n)))

    # 6. 方波（过零多）
    sq = np.where((t // 8) % 2 == 0, 1.0, -1.0)
    sigs.append(("square", sq.astype(np.float64)))

    # 7. 单调上升（过零 1 次）
    sigs.append(("ramp", np.linspace(-3.0, 5.0, n)))

    # 8. n=1
    sigs.append(("single", np.array([4.0], dtype=np.float64)))

    # 9. n=2
    sigs.append(("two_pt", np.array([1.0, -2.0], dtype=np.float64)))

    # 10. 带高频分量（验证 FFT）
    sigs.append(
        (
            "multi_tone",
            0.5
            + 1.0 * np.sin(2.0 * np.pi * 4.0 * t / n)
            + 0.3 * np.sin(2.0 * np.pi * 11.0 * t / n),
        )
    )

    # 11. 大直流 + 微弱交流（dominant_freq 应为 0）
    sigs.append(
        (
            "dc_dominant",
            5.0 + 0.05 * np.sin(2.0 * np.pi * 7.0 * t / n),
        )
    )

    # 12. 峰度较高的分布
    heavy = rng.standard_normal(n) * 2.0
    heavy[0] = 15.0
    heavy[1] = -12.0
    sigs.append(("heavy_tail", heavy))

    # 13. 全正值（zcr=0）
    sigs.append(("all_positive", np.abs(rng.uniform(-1.0, 4.0, n)) + 0.1))

    # 14. N=128 双音
    n128 = 128
    t128 = np.arange(n128, dtype=np.float64)
    sigs.append(
        (
            "twin_tone_128",
            1.2 * np.sin(2.0 * np.pi * 6.0 * t128 / n128)
            + 0.8 * np.sin(2.0 * np.pi * 20.0 * t128 / n128),
        )
    )

    return sigs


# ---------------------------------------------------------------------------
# C harness 生成
# ---------------------------------------------------------------------------
def c_float_lit(v: float) -> str:
    """Emit a C float literal that always has a decimal point or exponent."""
    s = "%.9g" % float(v)
    if not any(c in s for c in ".eE"):
        s += ".0"
    return s + "f"


def c_array(name: str, arr: np.ndarray) -> str:
    vals = ", ".join(c_float_lit(v) for v in arr)
    return "static const float %s[%d] = {%s};" % (name, len(arr), vals)


def generate_harness(cases: List[Tuple[str, np.ndarray]], fft_n: int = 64) -> str:
    """生成独立 C harness：调用 bAlgoSignal* + bAlgoFft*，按行输出可解析结果。"""
    lines: List[str] = []
    lines.append("/* Auto-generated by test_feature_parity.py — DO NOT EDIT */")
    lines.append("#include <stdio.h>")
    lines.append("#include <math.h>")
    lines.append("#include <stdint.h>")
    lines.append('#include "algo_signal.h"')
    lines.append('#include "algo_fft.h"')
    lines.append("")
    lines.append("#define MAX_N 256")
    lines.append("")
    lines.append("static float s_tw_re[ALGO_FFT_MAX_N / 2];")
    lines.append("static float s_tw_im[ALGO_FFT_MAX_N / 2];")
    lines.append("static uint16_t s_rev[ALGO_FFT_MAX_N];")
    lines.append("")
    lines.append("static void emit_time(const char *name, const float *x, uint16_t n)")
    lines.append("{")
    lines.append("    bAlgoSignalStats_t s;")
    lines.append("    int ret = bAlgoSignalStats(x, n, &s);")
    lines.append("    if (ret != 0) {")
    lines.append('        printf("%s STATS_ERROR %d\\n", name, ret);')
    lines.append("        return;")
    lines.append("    }")
    lines.append(
        '    printf("%s STATS sum=%.9g sq_sum=%.9g min=%.9g max=%.9g '
        'm2=%.9g m3=%.9g m4=%.9g zcr=%u abs_sum=%.9g lag1_sum=%.9g\\n",'
    )
    lines.append(
        "           name, (double)s.sum, (double)s.sq_sum, (double)s.min, (double)s.max,"
    )
    lines.append(
        "           (double)s.m2, (double)s.m3, (double)s.m4, (unsigned)s.zcr,"
    )
    lines.append("           (double)s.abs_sum, (double)s.lag1_sum);")
    lines.append(
        '    printf("%s FEAT mean=%.9g std=%.9g variance=%.9g min=%.9g max=%.9g '
        "rms=%.9g abs_mean=%.9g ptp=%.9g zcr=%.9g autocorr=%.9g skew=%.9g kurt=%.9g\\n\","
    )
    lines.append(
        "           name,"
    )
    lines.append(
        "           (double)bAlgoSignalMean(&s, n), (double)bAlgoSignalStd(&s, n),"
    )
    lines.append(
        "           (double)bAlgoSignalVariance(&s, n), (double)s.min, (double)s.max,"
    )
    lines.append(
        "           (double)bAlgoSignalRms(&s, n), (double)bAlgoSignalAbsMean(&s, n),"
    )
    lines.append(
        "           (double)bAlgoSignalPtp(&s), (double)bAlgoSignalZcr(&s, n),"
    )
    lines.append(
        "           (double)bAlgoSignalAutocorr(&s, n), (double)bAlgoSignalSkew(&s, n),"
    )
    lines.append(
        "           (double)bAlgoSignalKurt(&s, n));"
    )
    lines.append("}")
    lines.append("")
    lines.append(
        "static void emit_freq(const char *name, const float *x, uint16_t n, float fs)"
    )
    lines.append("{")
    lines.append("    static float re[MAX_N], im[MAX_N], mag[MAX_N / 2 + 1];")
    lines.append("    uint16_t i;")
    lines.append("    uint16_t m;")
    lines.append("    int ret;")
    lines.append("    for (i = 0; i < n; i++) {")
    lines.append("        re[i] = x[i];")
    lines.append("        im[i] = 0.0f;")
    lines.append("    }")
    lines.append("    ret = bAlgoFftGenTwiddle(s_tw_re, s_tw_im, n);")
    lines.append('    if (ret != 0) { printf("%s FFT_TW_ERR %d\\n", name, ret); return; }')
    lines.append("    ret = bAlgoFftGenBitReverse(s_rev, n);")
    lines.append('    if (ret != 0) { printf("%s FFT_REV_ERR %d\\n", name, ret); return; }')
    lines.append("    ret = bAlgoFft(re, im, n, s_tw_re, s_tw_im, s_rev);")
    lines.append('    if (ret != 0) { printf("%s FFT_ERR %d\\n", name, ret); return; }')
    lines.append("    ret = bAlgoFftMagnitude(re, im, mag, n);")
    lines.append('    if (ret != 0) { printf("%s MAG_ERR %d\\n", name, ret); return; }')
    lines.append("    m = (uint16_t)(n / 2 + 1);")
    lines.append(
        '    printf("%s FREQ dominant_freq=%.9g spec_centroid=%.9g spec_energy=%.9g\\n",'
    )
    lines.append(
        "           name,"
    )
    lines.append(
        "           (double)bAlgoFftDominantFreq(mag, n, fs),"
    )
    lines.append(
        "           (double)bAlgoFftCentroid(mag, n, fs),"
    )
    lines.append(
        "           (double)bAlgoFftEnergy(mag, n));"
    )
    # band_ratio：按 4 等分 bin 边界，与 Python ref_freq_features 一致
    lines.append("    {")
    lines.append("        int bands = 4;")
    lines.append("        int b;")
    lines.append("        for (b = 0; b < bands; b++) {")
    lines.append(
        "            int lo = (int)((long)b * m / bands);"
    )
    lines.append(
        "            int hi = (int)((long)(b + 1) * m / bands);"
    )
    lines.append(
        "            printf(\"%s BAND%d %.9g\\n\", name, b,"
    )
    lines.append(
        "                   (double)bAlgoFftBandRatio(mag, n, (uint16_t)lo, (uint16_t)hi));"
    )
    lines.append("        }")
    lines.append("    }")
    # mag dump: "<case> MAG m0 m1 ..."（调试用，解析器忽略）
    lines.append('    printf("%s MAG", name);')
    lines.append("    for (i = 0; i < m; i++) {")
    lines.append('        printf(" %.9g", (double)mag[i]);')
    lines.append("    }")
    lines.append('    printf("\\n");')
    lines.append("}")
    lines.append("")
    lines.append("int main(void)")
    lines.append("{")

    # 嵌入信号
    for idx, (name, arr) in enumerate(cases):
        cname = "sig_%d" % idx
        # Python 侧先 cast 到 float32，再以 %.9g 打印，保证 C 数组与 Python 参考同源
        arr32 = arr.astype(np.float32)
        lines.append(c_array(cname, arr32))
        lines.append("    emit_time(\"%s\", %s, (uint16_t)%d);" % (name, cname, len(arr)))
        # 频域只在 2 的幂且 n>=2 时计算
        n = len(arr)
        if n >= 2 and (n & (n - 1)) == 0:
            lines.append(
                "    emit_freq(\"%s\", %s, (uint16_t)%d, 1000.0f);" % (name, cname, n)
            )
        else:
            # 非 2 的幂：单独补一条 FFT 用例（用前 fft_n 个点或 pad）
            pass
    lines.append("    return 0;")
    lines.append("}")
    lines.append("")

    # 专用 FFT 用例：直流主导 / 纯直流 / 非 2^n pad
    n_fft = fft_n
    t_fft = np.arange(n_fft, dtype=np.float64)
    extra = [
        ("fft_dc", np.full(n_fft, 3.0)),
        (
            "fft_dc_dominant",
            8.0 + 0.2 * np.sin(2.0 * np.pi * 3.0 * t_fft / n_fft),
        ),
        (
            "fft_tone5",
            np.sin(2.0 * np.pi * 5.0 * t_fft / n_fft),
        ),
        (
            "fft_tone0_dc",
            2.5 + np.sin(2.0 * np.pi * 0.0 * t_fft / n_fft),  # argmax at bin0? equal to DC
        ),
    ]
    # fft_tone0_dc 是常量，argmax bin0
    # 再加一个：纯 AC 无直流，argmax 应在 tone bin
    extra.append(
        (
            "fft_ac_tone12",
            np.sin(2.0 * np.pi * 12.0 * t_fft / n_fft),
        )
    )
    # 小直流 + 强交流（argmax 在交流 bin）
    extra.append(
        (
            "fft_weak_dc",
            0.01 + 1.0 * np.sin(2.0 * np.pi * 9.0 * t_fft / n_fft),
        )
    )

    # 插入 main 内：在 return 0 前
    # 找到 "    return 0;" 行并替换
    insert_lines: List[str] = []
    for name, arr in extra:
        cname = "sig_extra_%s" % re.sub(r"[^a-zA-Z0-9_]", "_", name)
        arr32 = arr.astype(np.float32)
        insert_lines.append(c_array(cname, arr32))
        insert_lines.append(
            "    emit_time(\"%s\", %s, (uint16_t)%d);" % (name, cname, len(arr))
        )
        insert_lines.append(
            "    emit_freq(\"%s\", %s, (uint16_t)%d, 1000.0f);" % (name, cname, len(arr))
        )

    src = "\n".join(lines)
    marker = "    return 0;"
    src = src.replace(marker, "\n".join(insert_lines) + "\n" + marker)
    return src


# ---------------------------------------------------------------------------
# 编译 / 运行
# ---------------------------------------------------------------------------
def compile_and_run(harness_src: str, workdir: str) -> str:
    src_path = os.path.join(workdir, "harness.c")
    bin_path = os.path.join(workdir, "harness")
    with open(src_path, "w") as f:
        f.write(harness_src)

    cmd = [
        "gcc",
        "-std=c99",
        "-O0",
        "-Wall",
        "-Wextra",
        "-I" + ALGO_DIR,
        "-I" + ALGO_INC,
        "-D_ALGO_SIGNAL_ENABLE=1",
        "-D_ALGO_FFT_ENABLE=1",
        SIGNAL_C,
        FFT_C,
        src_path,
        "-lm",
        "-o",
        bin_path,
    ]
    print("Compile: %s" % " ".join(cmd))
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print("COMPILE FAILED")
        print(proc.stdout)
        print(proc.stderr)
        raise RuntimeError("gcc compile failed")

    print("Run harness...")
    run = subprocess.run([bin_path], capture_output=True, text=True)
    if run.returncode != 0:
        print("RUN FAILED")
        print(run.stdout)
        print(run.stderr)
        raise RuntimeError("harness run failed")
    return run.stdout


# ---------------------------------------------------------------------------
# 解析 harness 输出
# ---------------------------------------------------------------------------
def parse_harness(output: str) -> Dict[str, Dict[str, float]]:
    """返回 case_name -> {feature: value}，含 STATS 字段（前缀 raw_）。"""
    data: Dict[str, Dict[str, float]] = {}
    feat_keys = TIME_FEATURES + [
        "dominant_freq", "spec_centroid", "spec_energy",
        "band0_ratio", "band1_ratio", "band2_ratio", "band3_ratio",
    ]
    stat_keys = ["sum", "sq_sum", "min", "max", "m2", "m3", "m4", "zcr", "abs_sum", "lag1_sum"]

    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        case = parts[0]
        kind = parts[1]
        if case not in data:
            data[case] = {}
        if kind == "STATS" or kind.startswith("STATS_"):
            if kind != "STATS":
                data[case]["_stats_error"] = float(parts[2]) if len(parts) > 2 else -1.0
                continue
            # STATs key=value...
            for kv in parts[2:]:
                if "=" not in kv:
                    continue
                k, v = kv.split("=", 1)
                try:
                    data[case]["raw_" + k] = float(v)
                except ValueError:
                    pass
        elif kind == "FEAT":
            for kv in parts[2:]:
                if "=" not in kv:
                    continue
                k, v = kv.split("=", 1)
                try:
                    data[case][k] = float(v)
                except ValueError:
                    pass
        elif kind == "FREQ":
            for kv in parts[2:]:
                if "=" not in kv:
                    continue
                k, v = kv.split("=", 1)
                try:
                    data[case][k] = float(v)
                except ValueError:
                    pass
        elif kind.startswith("BAND"):
            # "<case> BAND<n> <val>"
            try:
                bidx = int(parts[1].replace("BAND", ""))
                data[case]["band%d_ratio" % bidx] = float(parts[2])
            except (IndexError, ValueError):
                pass
        elif kind == "FFT_ERR" or kind == "MAG_ERR" or kind == "STATS_ERROR":
            data[case]["_error_" + kind] = -1.0
    return data


# ---------------------------------------------------------------------------
# feature_cgen 存在性 / 可调用检查
# ---------------------------------------------------------------------------
def check_feature_cgen() -> int:
    print("\n=== feature_cgen.py 生成路径存在性检查 ===")
    sys.path.insert(0, EXPORT_PY)
    ok_n = 0
    fail_n = 0
    try:
        from app.services.export import feature_cgen  # type: ignore
    except Exception as e:
        print("  FAIL cannot import feature_cgen: %s" % e)
        return 1

    # TIME_FEATURES 完整性
    try:
        from app.services.feature_service import TIME_FEATURES as FS_TIME, FREQ_FEATURES as FS_FREQ
    except Exception:
        FS_TIME, FS_FREQ = TIME_FEATURES, FREQ_FEATURES

    if list(FS_TIME) != TIME_FEATURES:
        print("  FAIL TIME_FEATURES mismatch: %s vs %s" % (FS_TIME, TIME_FEATURES))
        fail_n += 1
    else:
        print("  PASS TIME_FEATURES list matches contract (12)")
        ok_n += 1

    for fname in TIME_FEATURES:
        expr = feature_cgen._time_feature_c(fname)
        prim = EXPECTED_TIME_PRIM[fname]
        if prim not in expr:
            print("  FAIL time %s: expected %r in %r" % (fname, prim, expr))
            fail_n += 1
        else:
            print("  PASS time %-10s -> %s" % (fname, expr.strip()))
            ok_n += 1
        # 必须是可编译的赋值语句形态
        if not expr.strip().startswith("out[oi++]"):
            print("  FAIL time %s: not out[oi++] form: %r" % (fname, expr))
            fail_n += 1
        else:
            ok_n += 1

    for fname in FREQ_FEATURES:
        if fname == "band_ratio":
            # 组合特征：逐 bandN_ratio 检查（band_ratio 本身由 effective_features 展开）
            for b in range(4):
                bname = "band%d_ratio" % b
                expr = feature_cgen._freq_feature_c(bname, "p_", 64, 1000.0, 4)
                if "bAlgoFftBandRatio" not in expr:
                    print("  FAIL freq %s: expected bAlgoFftBandRatio in %r" % (bname, expr))
                    fail_n += 1
                else:
                    print("  PASS freq %-12s -> %s" % (bname, expr.strip()))
                    ok_n += 1
                if not expr.strip().startswith("out[oi++]"):
                    print("  FAIL freq %s: not out[oi++] form: %r" % (bname, expr))
                    fail_n += 1
                else:
                    ok_n += 1
            # effective_features 展开检查
            try:
                from app.services.feature_service import effective_features
                from app.services.feature_service import FeatureConfig  # type: ignore
                # FeatureConfig 可能需要其它字段；直接检查 band_ratio 展开逻辑
                from app.services.feature_service import effective_features as _ef

                class _FakeCfg(object):
                    feature_ids = ["mean", "band_ratio"]
                    freq_bands = 4

                expanded = _ef(_FakeCfg())
                if expanded != ["mean", "band0_ratio", "band1_ratio", "band2_ratio", "band3_ratio"]:
                    print("  FAIL effective_features band_ratio expand: %s" % expanded)
                    fail_n += 1
                else:
                    print("  PASS effective_features expands band_ratio -> %s" % expanded)
                    ok_n += 1
            except Exception as e:
                print("  FAIL effective_features check: %s" % e)
                fail_n += 1
        else:
            expr = feature_cgen._freq_feature_c(fname, "p_", 64, 1000.0, 4)
            prim = EXPECTED_FREQ_PRIM[fname]
            if prim not in expr:
                print("  FAIL freq %s: expected %r in %r" % (fname, prim, expr))
                fail_n += 1
            else:
                print("  PASS freq %-10s -> %s" % (fname, expr.strip()))
                ok_n += 1
            if not expr.strip().startswith("out[oi++]"):
                print("  FAIL freq %s: not out[oi++] form: %r" % (fname, expr))
                fail_n += 1
            else:
                ok_n += 1

    # C 头文件中必须声明这些原语（头文件存在性）
    sig_h = os.path.join(ALGO_INC, "algo_signal.h")
    fft_h = os.path.join(ALGO_INC, "algo_fft.h")
    with open(sig_h) as f:
        sig_txt = f.read()
    with open(fft_h) as f:
        fft_txt = f.read()
    required_sig = [
        "bAlgoSignalVariance", "bAlgoSignalAbsMean", "bAlgoSignalAutocorr",
        "abs_sum", "lag1_sum",
    ]
    required_fft = [
        "bAlgoFftDominantFreq", "bAlgoFftCentroid", "bAlgoFftEnergy", "bAlgoFftBandRatio",
    ]
    for name in required_sig:
        if name not in sig_txt:
            print("  FAIL header algo_signal.h missing %s" % name)
            fail_n += 1
        else:
            ok_n += 1
    for name in required_fft:
        if name not in fft_txt:
            print("  FAIL header algo_fft.h missing %s" % name)
            fail_n += 1
        else:
            ok_n += 1
    print("  header symbol checks: %d ok" % len(required_sig + required_fft))

    # dominant_freq 实现必须从 i=0 开始（含直流）
    with open(FFT_C) as f:
        fft_src = f.read()
    # 找 bAlgoFftDominantFreq 函数体内的 for 循环
    m = re.search(
        r"bAlgoFftDominantFreq\s*\([^)]*\)\s*\{(.*?)^\}",
        fft_src,
        re.M | re.S,
    )
    if not m:
        print("  FAIL cannot locate bAlgoFftDominantFreq body")
        fail_n += 1
    else:
        body = m.group(1)
        if re.search(r"for\s*\(\s*i\s*=\s*1\s*;", body):
            print("  FAIL bAlgoFftDominantFreq loop starts at i=1 (skips DC bin)")
            fail_n += 1
        elif re.search(r"for\s*\(\s*i\s*=\s*0\s*;", body):
            print("  PASS bAlgoFftDominantFreq searches from i=0 (includes DC)")
            ok_n += 1
        else:
            print("  FAIL bAlgoFftDominantFreq: unexpected loop form")
            fail_n += 1

    print("feature_cgen checks: %d pass, %d fail" % (ok_n, fail_n))
    return fail_n


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------
def run_parity() -> int:
    global PASS_COUNT, FAIL_COUNT

    cases = make_signals()
    print("=== C harness 数值一致性测试 ===")
    print("signals: %d cases" % len(cases))

    harness_src = generate_harness(cases, fft_n=64)
    workdir = tempfile.mkdtemp(prefix="feat_parity_")
    try:
        output = compile_and_run(harness_src, workdir)
        with open(os.path.join(workdir, "harness_output.txt"), "w") as f:
            f.write(output)
        print("harness output saved: %s" % os.path.join(workdir, "harness_output.txt"))
        parsed = parse_harness(output)
    finally:
        # 保留 workdir 供排查；正式通过后可删
        pass

    # 逐 case 比对
    for name, arr in cases:
        n = len(arr)
        print("\n--- case %s (n=%d) ---" % (name, n))
        if name not in parsed:
            print("  FAIL missing harness output for %s" % name)
            FAIL_COUNT += 1
            FAILURES.append("missing output: %s" % name)
            continue

        got = parsed[name]
        # C 侧用 float32 输入；参考也用 float32 cast 后的值（避免输入本身不同源）
        arr32 = arr.astype(np.float32).astype(np.float64)
        exp_time = ref_time_features(arr32)

        # STATS 字段比对（独立检查 abs_sum / lag1_sum）
        if "raw_abs_sum" in got:
            exp_abs_sum = float(np.sum(np.abs(arr32)))
            check("%s.stats.abs_sum" % name, got["raw_abs_sum"], exp_abs_sum)
        if "raw_lag1_sum" in got and n >= 2:
            d = arr32 - float(np.mean(arr32))
            exp_lag1 = float(np.sum(d[:-1] * d[1:]))
            check("%s.stats.lag1_sum" % name, got["raw_lag1_sum"], exp_lag1)
        elif "raw_lag1_sum" in got and n < 2:
            check("%s.stats.lag1_sum" % name, got["raw_lag1_sum"], 0.0)

        # 12 时域特征
        for feat in TIME_FEATURES:
            if feat not in got:
                print("  FAIL %s.%s missing in harness output" % (name, feat))
                FAIL_COUNT += 1
                FAILURES.append("missing feat %s.%s" % (name, feat))
                continue
            check("%s.%s" % (name, feat), got[feat], exp_time[feat])

        # 频域（仅 2^n 且 n>=2）
        if n >= 2 and (n & (n - 1)) == 0:
            fs = 1000.0
            exp_freq = ref_freq_features(arr32, fs, bands=4)
            for feat in ["dominant_freq", "spec_centroid", "spec_energy"]:
                if feat not in got:
                    print("  FAIL %s.%s missing" % (name, feat))
                    FAIL_COUNT += 1
                    FAILURES.append("missing freq %s.%s" % (name, feat))
                    continue
                check("%s.%s" % (name, feat), got[feat], exp_freq[feat])
            for b in range(4):
                bkey = "band%d_ratio" % b
                if bkey not in got:
                    print("  FAIL %s.%s missing" % (name, bkey))
                    FAIL_COUNT += 1
                    FAILURES.append("missing %s.%s" % (name, bkey))
                    continue
                check("%s.%s" % (name, bkey), got[bkey], exp_freq[bkey])

    # 额外 FFT 专用用例
    extra_names = ["fft_dc", "fft_dc_dominant", "fft_tone5", "fft_ac_tone12", "fft_weak_dc"]
    print("\n=== dominant_freq 专项（含直流）===")
    n_fft = 64
    fs = 1000.0
    t_fft = np.arange(n_fft, dtype=np.float64)
    extra_map = {
        "fft_dc": np.full(n_fft, 3.0),
        "fft_dc_dominant": 8.0 + 0.2 * np.sin(2.0 * np.pi * 3.0 * t_fft / n_fft),
        "fft_tone5": np.sin(2.0 * np.pi * 5.0 * t_fft / n_fft),
        "fft_ac_tone12": np.sin(2.0 * np.pi * 12.0 * t_fft / n_fft),
        "fft_weak_dc": 0.01 + 1.0 * np.sin(2.0 * np.pi * 9.0 * t_fft / n_fft),
    }
    expected_dom = {
        "fft_dc": 0.0,               # bin0
        "fft_dc_dominant": 0.0,      # DC 幅度最大
        "fft_tone5": 5.0 * fs / n_fft,  # bin5
        "fft_ac_tone12": 12.0 * fs / n_fft,  # bin12
        "fft_weak_dc": 9.0 * fs / n_fft,  # 交流远大于直流
    }
    for en in extra_names:
        if en not in parsed:
            print("  FAIL missing extra case %s" % en)
            FAIL_COUNT += 1
            FAILURES.append("missing extra %s" % en)
            continue
        got = parsed[en]
        exp_dom = expected_dom[en]
        got_dom = got.get("dominant_freq", float("nan"))
        ok = close(got_dom, exp_dom)
        if ok:
            PASS_COUNT += 1
            print("  PASS %s dominant_freq=%.6g (expected %.6g)" % (en, got_dom, exp_dom))
        else:
            FAIL_COUNT += 1
            msg = "%s dominant_freq: got=%.6g expected=%.6g" % (en, got_dom, exp_dom)
            FAILURES.append(msg)
            print("  FAIL %s" % msg)
        # 也跑一遍完整 ref 比对
        arr32 = extra_map[en].astype(np.float32).astype(np.float64)
        exp_freq = ref_freq_features(arr32, fs, bands=4)
        for feat in ["dominant_freq", "spec_centroid", "spec_energy"]:
            if feat in got:
                check("%s.%s" % (en, feat), got[feat], exp_freq[feat])
        for b in range(4):
            bkey = "band%d_ratio" % b
            if bkey in got:
                check("%s.%s" % (en, bkey), got[bkey], exp_freq[bkey])

    return 0


def main() -> int:
    print("BabyOS AutoML E2E — feature parity (TD1)")
    print("repo: %s" % REPO)
    print("tolerance: |a-b| <= max(%g*|b|, %g)" % (TOL_REL, TOL_ABS))
    print()

    run_parity()
    cgen_fails = check_feature_cgen()

    global FAIL_COUNT, PASS_COUNT, FAILURES
    FAIL_COUNT += cgen_fails
    # cgen 的 pass 不计入 PASS_COUNT 的 parity 项，单独汇总

    print("\n" + "=" * 60)
    print("SUMMARY: %d parity checks passed, %d failed" % (PASS_COUNT, FAIL_COUNT))
    if FAIL_COUNT > 0:
        print("\nFailures:")
        for f in FAILURES:
            print("  - %s" % f)
        print("\nRESULT: FAIL")
        return 1
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
