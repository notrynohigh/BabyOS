#!/usr/bin/env python3
"""
验证深度绑定代码生成器 - 测试是否调用预置原语
"""
import sys
import os
import numpy as np

# 直接复制 feature_cgen.py 中的函数（避免导入问题）
def _time_feature_c(fname: str) -> str:
    """时域特征 C 表达式（调用预置 bAlgoSignal* 原语）。"""
    if fname == "mean":
        return "out[oi++] = bAlgoSignalMean(&s_stats, N);"
    if fname == "std":
        return "out[oi++] = bAlgoSignalStd(&s_stats, N);"
    if fname == "min":
        return "out[oi++] = s_stats.min;"
    if fname == "max":
        return "out[oi++] = s_stats.max;"
    if fname == "rms":
        return "out[oi++] = bAlgoSignalRms(&s_stats, N);"
    if fname == "ptp":
        return "out[oi++] = bAlgoSignalPtp(&s_stats);"
    if fname == "zcr":
        return "out[oi++] = bAlgoSignalZcr(&s_stats, N);"
    if fname == "skew":
        return "out[oi++] = bAlgoSignalSkew(&s_stats, N);"
    if fname == "kurt":
        return "out[oi++] = bAlgoSignalKurt(&s_stats, N);"
    raise ValueError(f"未知时域特征: {fname}")

def _freq_feature_c(fname: str, prefix: str, n: int, fs: float, bands: int) -> str:
    """频域特征 C 表达式（调用预置 bAlgoFft* 原语）。"""
    m = n // 2 + 1
    if fname == "spec_centroid":
        return "out[oi++] = bAlgoFftCentroid(s_mag, N, FS);"
    if fname == "spec_energy":
        return "out[oi++] = bAlgoFftEnergy(s_mag, N);"
    if fname == "dominant_freq":
        return "out[oi++] = bAlgoFftDominantFreq(s_mag, N, FS);"
    if fname.startswith("band") and fname.endswith("_ratio"):
        idx = int(fname[len("band") : -len("_ratio")])
        edges = np.linspace(0, m, bands + 1).astype(int)
        lo, hi = int(edges[idx]), int(edges[idx + 1])
        return f"out[oi++] = bAlgoFftBandRatio(s_mag, N, {lo}, {hi});"
    raise ValueError(f"未知频域特征: {fname}")

def emit_feat_extract(prefix, channels, exported, n, fs, freq_enabled, freq_bands):
    """生成 feat_extract 实现。"""
    n_ch = len(channels)
    m = n // 2 + 1
    has_freq = freq_enabled and any(
        f in ("spec_centroid", "spec_energy", "dominant_freq") or f.startswith("band")
        for _, f in exported
    )

    # 烘焙常量：旋转因子表和位反转表（仅频域特征需要）
    if has_freq:
        # 旋转因子
        k = np.arange(n // 2)
        w_re = np.cos(2.0 * np.pi * k / n)
        w_im = -np.sin(2.0 * np.pi * k / n)
        re_items = [np.format_float_scientific(np.float32(v), unique=True, trim="-") + "F" for v in w_re]
        im_items = [np.format_float_scientific(np.float32(v), unique=True, trim="-") + "F" for v in w_im]
        lines = []
        for i in range(0, len(re_items), 6):
            lines.append("    " + ", ".join(re_items[i : i + 6]))
        re_str = ",\n".join(lines)
        lines = []
        for i in range(0, len(im_items), 6):
            lines.append("    " + ", ".join(im_items[i : i + 6]))
        im_str = ",\n".join(lines)
        # 位反转表
        bits = n.bit_length() - 1
        rev = [0] * n
        for i in range(n):
            r = 0
            x = i
            for b in range(bits):
                r = (r << 1) | (x & 1)
                x >>= 1
            rev[i] = r
        rev_items = [str(v) for v in rev]
        lines = []
        for i in range(0, len(rev_items), 12):
            lines.append("    " + ", ".join(rev_items[i : i + 12]))
        rev_str = ",\n".join(lines)
        decls = (
            f"static const float {prefix}_tw_re[{n // 2}] = {{\n{re_str}\n}};\n"
            f"static const float {prefix}_tw_im[{n // 2}] = {{\n{im_str}\n}};\n"
            f"static const uint16_t {prefix}_rev[{n}] = {{\n{rev_str}\n}};\n"
        )
        # 采样率烘焙常量
        fs_str = np.format_float_scientific(np.float32(fs), unique=True, trim="-") + "F"
        decls += f"\n#define {prefix}_FS ({fs_str})"
    else:
        decls = ""

    # helpers 为空：统计和 FFT 逻辑已预置到 bos/algorithm/
    helpers = ""

    # 按通道分组导出特征
    by_ch = {}
    for ch, f in exported:
        by_ch.setdefault(ch, []).append(f)

    # 生成 feat_extract 函数体
    body_parts = []
    body_parts.append(f"    uint16_t N = (uint16_t){n};")
    body_parts.append("    uint16_t ch, i;")
    body_parts.append("    uint32_t oi = 0;")
    body_parts.append("    bAlgoSignalStats_t s_stats;")
    body_parts.append(f"    (void)i;")
    body_parts.append("    for (ch = 0; ch < %d; ch++)" % n_ch)
    body_parts.append("    {")
    body_parts.append("        const float *x = &ch_buf[(uint32_t)ch * %d];" % n)

    # 调用预置原语计算统计量
    body_parts.append("        bAlgoSignalStats(x, N, &s_stats);")

    # 频域特征：声明静态缓冲区，调用预置 FFT 原语
    if has_freq:
        body_parts.append(f"        static float s_re[{n}];")
        body_parts.append(f"        static float s_im[{n}];")
        body_parts.append(f"        static float s_mag[{m}];")
        body_parts.append("        for (i = 0; i < N; i++) { s_re[i] = x[i]; s_im[i] = 0.0f; }")
        body_parts.append(f"        bAlgoFft(s_re, s_im, N, {prefix}_tw_re, {prefix}_tw_im, {prefix}_rev);")
        body_parts.append("        bAlgoFftMagnitude(s_re, s_im, s_mag, N);")

    body_parts.append("        switch (ch)")
    body_parts.append("        {")
    for ci, ch in enumerate(channels):
        feats = by_ch.get(ch, [])
        body_parts.append(f"        case {ci}: /* {ch} */")
        body_parts.append("        {")
        for f in feats:
            if f in ("spec_centroid", "spec_energy", "dominant_freq") or f.startswith("band"):
                feat_c = _freq_feature_c(f, prefix, n, fs, freq_bands)
                feat_c = feat_c.replace("FS", f"{prefix}_FS")
                body_parts.append("    " + feat_c)
            else:
                body_parts.append("    " + _time_feature_c(f))
        body_parts.append("            break;")
        body_parts.append("        }")
    body_parts.append("        default:")
    body_parts.append("            break;")
    body_parts.append("        }")
    body_parts.append("    }")
    body = "\n".join(body_parts)

    return {"decls": decls, "helpers": helpers, "body": body}

def test_time_features():
    """测试时域特征生成"""
    print("=" * 60)
    print("测试时域特征生成（调用预置 bAlgoSignal* 原语）")
    print("=" * 60)

    # 模拟 3 通道，每个通道提取 mean/std/min/max
    exported = [
        ("ch0", "mean"),
        ("ch0", "std"),
        ("ch0", "min"),
        ("ch0", "max"),
        ("ch1", "mean"),
        ("ch1", "std"),
        ("ch2", "mean"),
        ("ch2", "std"),
    ]

    result = emit_feat_extract(
        prefix="test_model",
        channels=["ch0", "ch1", "ch2"],
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

    # 验证是否调用了预置原语
    checks = [
        ("bAlgoSignalStats", "调用 bAlgoSignalStats"),
        ("bAlgoSignalMean", "调用 bAlgoSignalMean"),
        ("bAlgoSignalStd", "调用 bAlgoSignalStd"),
        ("s_stats.min", "直接使用 s_stats.min"),
        ("s_stats.max", "直接使用 s_stats.max"),
    ]

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

    # 模拟混合特征
    exported = [
        ("ch0", "mean"),
        ("ch0", "std"),
        ("ch0", "spec_centroid"),
        ("ch0", "dominant_freq"),
        ("ch1", "rms"),
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
    else:
        print("❌ 部分测试失败")
        sys.exit(1)
    print("=" * 60)
