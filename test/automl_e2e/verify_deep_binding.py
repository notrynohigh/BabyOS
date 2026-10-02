#!/usr/bin/env python3
"""验证深度绑定代码生成器：必须调用真实 feature_cgen 模块（无内联副本）。

用法:
    python3 test/automl_e2e/verify_deep_binding.py
"""
from __future__ import annotations

import os
import sys

# 导入真实模块（与 test_regression_suite / test_feature_parity 同一路径）
_REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
_PY = os.path.join(_REPO, "tool", "babyos-studio", "python")
if _PY not in sys.path:
    sys.path.insert(0, _PY)

from app.services.export import feature_cgen  # noqa: E402
from app.services.feature_service import TIME_FEATURES  # noqa: E402

# 别名，保持脚本内断言可读
_time_feature_c = feature_cgen._time_feature_c
_freq_feature_c = feature_cgen._freq_feature_c
emit_feat_extract = feature_cgen.emit_feat_extract


def test_time_features():
    """测试时域特征生成（走真实 feature_cgen，覆盖全部 TIME_FEATURES）"""
    print("=" * 60)
    print("测试时域特征生成（调用预置 bAlgoSignal* 原语）")
    print("=" * 60)

    # 模拟 3 通道，覆盖全部时域特征（含新增 variance/abs_mean/autocorr）
    exported = [("ch0", f) for f in TIME_FEATURES] + [
        ("ch1", "mean"),
        ("ch1", "std"),
        ("ch2", "mean"),
        ("ch2", "std"),
    ]
    channels = ["ch0", "ch1", "ch2"]

    result = emit_feat_extract(
        prefix="test_model",
        channels=channels,
        exported=exported,
        n=100,
        fs=1000.0,
        freq_enabled=False,
        freq_bands=0,
    )

    body = result["body"]
    helpers = result["helpers"]

    print("\n生成的 feat_extract 函数体:")
    print(body)

    print("\nhelpers (应为空，原语已预置):")
    print(repr(helpers))

    # 验证是否调用了预置原语（全部时域特征）
    expected = {
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
    checks = [(sym, f"调用 {sym}") for sym in expected.values()]

    print("\n验证调用预置原语:")
    all_ok = True
    for symbol, desc in checks:
        found = symbol in body
        status = "✅" if found else "❌"
        print(f"  {status} {desc} ({symbol})")
        if not found:
            all_ok = False

    # 验证没有内联实现
    inline_checks = [
        ("s_sum / (float)N", "不应有内联 mean"),
        ("sqrtf(s_m2 / (float)N)", "不应有内联 std"),
    ]

    print("\n验证无内联实现:")
    for symbol, desc in inline_checks:
        found = symbol in body
        status = "✅" if not found else "❌"
        print(f"  {status} {desc} ({symbol})")
        if found:
            all_ok = False

    # 直接调用真实 _time_feature_c，覆盖新增特征
    print("\n验证真实 _time_feature_c（含新增特征）:")
    for fname in ("variance", "abs_mean", "autocorr"):
        expr = _time_feature_c(fname)
        ok = "bAlgoSignal" in expr
        status = "✅" if ok else "❌"
        print(f"  {status} {fname} -> {expr}")
        if not ok:
            all_ok = False

    return all_ok


def test_freq_features():
    """测试频域特征生成"""
    print("\n" + "=" * 60)
    print("测试频域特征生成（调用预置 bAlgoFft* 原语）")
    print("=" * 60)

    # 模拟 3 通道，每个通道提取频域特征
    exported = [
        ("ch0", "spec_centroid"),
        ("ch0", "spec_energy"),
        ("ch0", "dominant_freq"),
        ("ch1", "spec_centroid"),
        ("ch1", "spec_energy"),
        ("ch2", "dominant_freq"),
    ]

    result = emit_feat_extract(
        prefix="test_model",
        channels=["ch0", "ch1", "ch2"],
        exported=exported,
        n=128,
        fs=1000.0,
        freq_enabled=True,
        freq_bands=8,
    )

    body = result["body"]
    decls = result["decls"]

    print("\n烘焙常量 (decls):")
    print(decls[:200] + "..." if len(decls) > 200 else decls)

    print("\n生成的 feat_extract 函数体:")
    print(body)

    # 验证是否调用了预置原语
    checks = [
        ("bAlgoFft", "调用 bAlgoFft"),
        ("bAlgoFftMagnitude", "调用 bAlgoFftMagnitude"),
        ("bAlgoFftCentroid", "调用 bAlgoFftCentroid"),
        ("bAlgoFftEnergy", "调用 bAlgoFftEnergy"),
        ("bAlgoFftDominantFreq", "调用 bAlgoFftDominantFreq"),
        ("s_mag", "使用幅度数组"),
    ]

    print("\n验证调用预置原语:")
    all_ok = True
    for symbol, desc in checks:
        found = symbol in body
        status = "✅" if found else "❌"
        print(f"  {status} {desc} ({symbol})")
        if not found:
            all_ok = False

    # 验证烘焙了旋转因子表
    if "tw_re" in decls and "tw_im" in decls:
        print("\n✅ 正确烘焙旋转因子表")
    else:
        print("\n❌ 未烘焙旋转因子表")
        all_ok = False

    return all_ok


def test_mixed_features():
    """测试混合特征生成"""
    print("\n" + "=" * 60)
    print("测试混合特征生成（时域 + 频域）")
    print("=" * 60)

    # 模拟混合特征（含新增时域特征）
    exported = [
        ("ch0", "mean"),
        ("ch0", "std"),
        ("ch0", "variance"),
        ("ch0", "spec_centroid"),
        ("ch0", "dominant_freq"),
        ("ch1", "rms"),
        ("ch1", "abs_mean"),
        ("ch1", "autocorr"),
        ("ch1", "spec_energy"),
    ]

    result = emit_feat_extract(
        prefix="test_model",
        channels=["ch0", "ch1"],
        exported=exported,
        n=64,
        fs=500.0,
        freq_enabled=True,
        freq_bands=4,
    )

    body = result["body"]

    print("\n生成的 feat_extract 函数体:")
    print(body)

    # 验证是否同时调用了两类原语
    checks = [
        ("bAlgoSignalStats", "调用信号统计"),
        ("bAlgoSignalMean", "调用信号 mean"),
        ("bAlgoSignalRms", "调用信号 rms"),
        ("bAlgoSignalVariance", "调用信号 variance"),
        ("bAlgoSignalAbsMean", "调用信号 abs_mean"),
        ("bAlgoSignalAutocorr", "调用信号 autocorr"),
        ("bAlgoFft", "调用 FFT"),
        ("bAlgoFftMagnitude", "调用 FFT 幅度"),
        ("bAlgoFftCentroid", "调用 FFT centroid"),
        ("bAlgoFftEnergy", "调用 FFT energy"),
        ("bAlgoFftDominantFreq", "调用 FFT dominant_freq"),
    ]

    print("\n验证调用预置原语:")
    all_ok = True
    for symbol, desc in checks:
        found = symbol in body
        status = "✅" if found else "❌"
        print(f"  {status} {desc} ({symbol})")
        if not found:
            all_ok = False

    return all_ok


if __name__ == "__main__":
    print("BabyOS 深度绑定代码生成器验证")
    print("=" * 60)
    print(f"使用真实 feature_cgen: {feature_cgen.__file__}")

    results = []

    # 测试 1: 时域特征
    results.append(("时域特征", test_time_features()))

    # 测试 2: 频域特征
    results.append(("频域特征", test_freq_features()))

    # 测试 3: 混合特征
    results.append(("混合特征", test_mixed_features()))

    # 总结
    print("\n" + "=" * 60)
    print("测试总结")
    print("=" * 60)

    all_ok = True
    for name, ok in results:
        status = "✅ 通过" if ok else "❌ 失败"
        print(f"  {name}: {status}")
        if not ok:
            all_ok = False

    print("\n" + "=" * 60)
    if all_ok:
        print("✅ 所有测试通过！深度绑定实现正确。")
        print("   生成代码正确调用预置原语 bAlgoSignal* 和 bAlgoFft*")
        print("   使用真实 feature_cgen 模块（无内联副本）")
    else:
        print("❌ 部分测试失败")
        sys.exit(1)
    print("=" * 60)
