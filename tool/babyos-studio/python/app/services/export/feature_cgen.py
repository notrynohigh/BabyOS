"""feat_extract C 发射器（design §7.3）：N 点通道缓冲 → 特征向量（时序工程）。

数值口径（与 feature_service._time_features/_freq_features 对齐 + selfcheck ③ 组合容差）：
- C 全链路 float32；Python 侧为 float64 → f32，容差 |a-b| ≤ max(1e-3·|b|, 1e-6)
- zcr = 变号次数 / (N-1)（np.mean(w[:-1]*w[1:]<0) 的分母是 N-1，易错点）
- skew/kurt 零方差防护（m2<=0 → 0）；m2 = 总体二阶中心矩
- FFT：迭代 radix-2 DIT，旋转因子与位反转表全部烘焙 static const
- ch_buf 布局：通道分离 [ch0_N 点][ch1_N 点]

深度绑定（2026-10-01）：
- 信号统计原语预置到 bos/algorithm/algo_signal.h（bAlgoSignal*）
- FFT 原语预置到 bos/algorithm/algo_fft.h（bAlgoFft*）
- 本文件只生成：旋转因子表/位反转表（烘焙常量）+ feat_extract 编排逻辑
"""
from __future__ import annotations

import numpy as np

from ..feature_service import TIME_FEATURES  # noqa: F401  (文档可见性)


def _emit_twiddles(n: int, prefix: str) -> str:
    """烘焙旋转因子表和位反转表（预计算，调用 bAlgoFftGenTwiddle/bAlgoFftGenBitReverse 生成）。"""
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
    return (
        f"static const float {prefix}_tw_re[{n // 2}] = {{\n{re_str}\n}};\n"
        f"static const float {prefix}_tw_im[{n // 2}] = {{\n{im_str}\n}};\n"
        f"static const uint16_t {prefix}_rev[{n}] = {{\n{rev_str}\n}};"
    )


def _time_feature_c(fname: str) -> str:
    """时域特征 C 表达式（调用预置 bAlgoSignal* 原语）。返回赋值语句。

    统计量变量命名约定（调用 bAlgoSignalStats 后）：
    - s_stats: bAlgoSignalStats_t 结构体
    - N: 信号长度
    """
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
    """频域特征 C 表达式（调用预置 bAlgoFft* 原语）。返回赋值语句。

    频域变量命名约定（调用 bAlgoFftMagnitude 后）：
    - s_mag: 幅度数组
    - N: FFT 点数
    - FS: 采样率（烘焙常量）
    """
    m = n // 2 + 1  # rfft bin 数
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


def emit_feat_extract(
    prefix: str,
    channels: list[str],
    exported: list[tuple[str, str]],  # [(ch, feat)] 导出顺序（已含特征子集与顺序）
    n: int,
    fs: float,
    freq_enabled: bool,
    freq_bands: int,
) -> dict:
    """生成 feat_extract 实现。返回 {"decls": str, "helpers": str, "body": str}。

    深度绑定后：
    - decls: 烘焙常量（旋转因子表、位反转表、采样率）
    - helpers: 空字符串（统计/FFT 逻辑已预置到 algo_signal.h/algo_fft.h）
    - body: feat_extract 编排逻辑（调用预置原语）
    """
    n_ch = len(channels)
    m = n // 2 + 1
    has_freq = freq_enabled and any(
        f in ("spec_centroid", "spec_energy", "dominant_freq") or f.startswith("band")
        for _, f in exported
    )

    # 烘焙常量：旋转因子表和位反转表（仅频域特征需要）
    decls = _emit_twiddles(n, prefix) if has_freq else ""
    # 采样率烘焙常量
    if has_freq:
        fs_str = np.format_float_scientific(np.float32(fs), unique=True, trim="-") + "F"
        decls += f"\n#define {prefix}_FS ({fs_str})"

    # helpers 为空：统计和 FFT 逻辑已预置到 bos/algorithm/
    helpers = ""

    # 按通道分组导出特征
    by_ch: dict[str, list[str]] = {}
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
        # 调用预置 FFT 原语
        body_parts.append(f"        bAlgoFft(s_re, s_im, N, {prefix}_tw_re, {prefix}_tw_im, {prefix}_rev);")
        # 计算幅度谱
        body_parts.append("        bAlgoFftMagnitude(s_re, s_im, s_mag, N);")

    body_parts.append("        switch (ch)")
    body_parts.append("        {")
    for ci, ch in enumerate(channels):
        feats = by_ch.get(ch, [])
        body_parts.append(f"        case {ci}: /* {ch} */")
        body_parts.append("        {")
        for f in feats:
            if f in ("spec_centroid", "spec_energy", "dominant_freq") or f.startswith("band"):
                # 频域特征：使用烘焙的采样率常量
                feat_c = _freq_feature_c(f, prefix, n, fs, freq_bands)
                # 替换 FS 为烘焙常量
                feat_c = feat_c.replace("FS", f"{prefix}_FS")
                body_parts.append("    " + feat_c)
            else:
                # 时域特征：调用预置 bAlgoSignal* 原语
                body_parts.append("    " + _time_feature_c(f))
        body_parts.append("            break;")
        body_parts.append("        }")
    body_parts.append("        default:")
    body_parts.append("            break;")
    body_parts.append("        }")
    body_parts.append("    }")
    body = "\n".join(body_parts)

    return {"decls": decls, "helpers": helpers, "body": body}
