"""feat_extract C 发射器（design §7.3）：N 点通道缓冲 → 特征向量（时序工程）。

数值口径（与 feature_service._time_features/_freq_features 对齐 + selfcheck ③ 组合容差）：
- C 全链路 float32；Python 侧为 float64 → f32，容差 |a-b| ≤ max(1e-3·|b|, 1e-6)
- zcr = 变号次数 / (N-1)（np.mean(w[:-1]*w[1:]<0) 的分母是 N-1，易错点）
- skew/kurt 零方差防护（m2<=0 → 0）；m2 = 总体二阶中心矩
- FFT：迭代 radix-2 DIT，旋转因子与位反转表全部烘焙 static const
- ch_buf 布局：通道分离 [ch0_N 点][ch1_N 点]...
"""
from __future__ import annotations

import numpy as np

from ..feature_service import TIME_FEATURES  # noqa: F401  (文档可见性)

TIME_C = {
    "mean": "({sum_x} / (float)N)",
}


def _emit_twiddles(n: int, prefix: str) -> str:
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


def _fft_helper(n: int, prefix: str) -> str:
    return f"""static void {prefix}_fft(float *re, float *im)
{{
    uint16_t i, j, len, half;
    /* 位反转置换 */
    for (i = 0; i < {n}; i++)
    {{
        j = {prefix}_rev[i];
        if (j > i)
        {{
            float t;
            t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }}
    }}
    /* 迭代 DIT 蝶形：tw 索引 = j * ({n} / len) */
    for (len = 2; len <= {n}; len <<= 1)
    {{
        half = len >> 1;
        for (i = 0; i < {n}; i += len)
        {{
            for (j = 0; j < half; j++)
            {{
                uint32_t tw = (uint32_t)j * ({n} / len);
                uint16_t a = (uint16_t)(i + j);
                uint16_t b = (uint16_t)(i + j + half);
                float wr = {prefix}_tw_re[tw];
                float wi = {prefix}_tw_im[tw];
                float tr = re[b] * wr - im[b] * wi;
                float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] = re[a] + tr;
                im[a] = im[a] + ti;
            }}
        }}
    }}
}}
"""


def _time_feature_c(fname: str, x: str, n: str) -> str:
    """单特征 C 表达式（基于已算好的统计量变量）。返回赋值语句。"""
    if fname == "mean":
        return "out[oi++] = s_sum / (float)N;"
    if fname == "std":
        return "out[oi++] = sqrtf(s_m2 / (float)N);"
    if fname == "min":
        return "out[oi++] = s_min;"
    if fname == "max":
        return "out[oi++] = s_max;"
    if fname == "rms":
        return "out[oi++] = sqrtf(s_sq / (float)N);"
    if fname == "ptp":
        return "out[oi++] = s_max - s_min;"
    if fname == "zcr":
        return "out[oi++] = (float)s_zcr / (float)(N - 1);"
    if fname == "skew":
        return "out[oi++] = (s_m2 > 0.0f) ? ((s_m3 / (float)N) / powf(s_m2 / (float)N, 1.5f)) : 0.0f;"
    if fname == "kurt":
        return "out[oi++] = (s_m2 > 0.0f) ? ((s_m4 / (float)N) / ((s_m2 / (float)N) * (s_m2 / (float)N)) - 3.0f) : 0.0f;"
    raise ValueError(f"未知时域特征: {fname}")


def _freq_feature_c(fname: str, prefix: str, n: int, fs: float, bands: int) -> str:
    m = n // 2 + 1  # rfft bin 数
    if fname == "spec_centroid":
        return (
            "{\n"
            "        float num = 0.0f, den = 0.0f;\n"
            f"        for (k = 0; k < {m}; k++)\n"
            "        {\n"
            "            float f = (float)k * FS / (float)N;\n"
            "            num += f * s_mag[k];\n"
            "            den += s_mag[k];\n"
            "        }\n"
            "        out[oi++] = (den > 0.0f) ? (num / den) : 0.0f;\n"
            "    }"
        ).replace("FS", f"{np.format_float_scientific(np.float32(fs), unique=True, trim='-')}F")
    if fname == "spec_energy":
        return (
            "{\n"
            "        float e = 0.0f;\n"
            f"        for (k = 0; k < {m}; k++) {{ e += s_mag[k] * s_mag[k]; }}\n"
            "        out[oi++] = e;\n"
            "    }"
        )
    if fname == "dominant_freq":
        return (
            "{\n"
            "        uint16_t best = 0;\n"
            f"        for (k = 1; k < {m}; k++) {{ if (s_mag[k] > s_mag[best]) {{ best = k; }} }}\n"
            f"        out[oi++] = (float)best * {np.format_float_scientific(np.float32(fs), unique=True, trim='-')}F / (float)N;\n"
            "    }"
        )
    if fname.startswith("band") and fname.endswith("_ratio"):
        idx = int(fname[len("band") : -len("_ratio")])
        edges = np.linspace(0, m, bands + 1).astype(int)
        lo, hi = int(edges[idx]), int(edges[idx + 1])
        return (
            "{\n"
            "        float e = 0.0f, tot = 0.0f;\n"
            f"        for (k = {lo}; k < {hi}; k++) {{ e += s_mag[k] * s_mag[k]; }}\n"
            f"        for (k = 0; k < {m}; k++) {{ tot += s_mag[k] * s_mag[k]; }}\n"
            "        out[oi++] = (tot > 0.0f) ? (e / tot) : 0.0f;\n"
            "    }"
        )
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
    """生成 feat_extract 实现。返回 {"decls": str, "helpers": str, "body": str}。"""
    n_ch = len(channels)
    m = n // 2 + 1
    decls = _emit_twiddles(n, prefix) if freq_enabled else ""
    helpers = _fft_helper(n, prefix) if freq_enabled else ""

    # 统计量计算（每通道一次遍历 + 二次中心矩遍历）
    stats = f"""static void {prefix}_stats(const float *x, uint16_t N,
                          float *p_sum, float *p_sq, float *p_min, float *p_max,
                          float *p_m2, float *p_m3, float *p_m4, uint32_t *p_zcr)
{{
    uint16_t i;
    float s = 0.0f, q = 0.0f, mn, mx;
    uint32_t zc = 0;
    s = x[0]; q = x[0] * x[0]; mn = x[0]; mx = x[0];
    for (i = 1; i < N; i++)
    {{
        s += x[i];
        q += x[i] * x[i];
        if (x[i] < mn) {{ mn = x[i]; }}
        if (x[i] > mx) {{ mx = x[i]; }}
        if (x[i - 1] * x[i] < 0.0f) {{ zc++; }}
    }}
    {{
        float mu = s / (float)N;
        float m2 = 0.0f, m3 = 0.0f, m4 = 0.0f;
        for (i = 0; i < N; i++)
        {{
            float d = x[i] - mu;
            float d2 = d * d;
            m2 += d2;
            m3 += d2 * d;
            m4 += d2 * d2;
        }}
        *p_m2 = m2; *p_m3 = m3; *p_m4 = m4;
    }}
    *p_sum = s; *p_sq = q; *p_min = mn; *p_max = mx; *p_zcr = zc;
}}
"""

    # 按通道分组导出特征
    by_ch: dict[str, list[str]] = {}
    for ch, f in exported:
        by_ch.setdefault(ch, []).append(f)

    body_parts = []
    body_parts.append(f"    uint16_t N = (uint16_t){n};")
    body_parts.append("    uint16_t ch, i, k;")
    body_parts.append("    uint32_t oi = 0;")
    body_parts.append("    (void)i; (void)k;")
    body_parts.append("    for (ch = 0; ch < %d; ch++)" % n_ch)
    body_parts.append("    {")
    body_parts.append("        const float *x = &ch_buf[(uint32_t)ch * %d];" % n)
    body_parts.append("        float s_sum, s_sq, s_min, s_max, s_m2, s_m3, s_m4;")
    body_parts.append("        uint32_t s_zcr;")
    body_parts.append(f"        {prefix}_stats(x, N, &s_sum, &s_sq, &s_min, &s_max, &s_m2, &s_m3, &s_m4, &s_zcr);")
    if freq_enabled:
        body_parts.append(f"        static float s_re[{n}];")
        body_parts.append(f"        static float s_im[{n}];")
        body_parts.append(f"        static float s_mag[{m}];")
        body_parts.append("        for (i = 0; i < N; i++) { s_re[i] = x[i]; s_im[i] = 0.0f; }")
        body_parts.append(f"        {prefix}_fft(s_re, s_im);")
        body_parts.append(f"        for (k = 0; k < {m}; k++) {{ s_mag[k] = sqrtf(s_re[k] * s_re[k] + s_im[k] * s_im[k]); }}")
    body_parts.append("        switch (ch)")
    body_parts.append("        {")
    for ci, ch in enumerate(channels):
        feats = by_ch.get(ch, [])
        body_parts.append(f"        case {ci}: /* {ch} */")
        body_parts.append("        {")
        # 频域缓冲只在有频域特征时声明——已在外层统一声明，无频域特征时 s_mag 不存在，
        # 因此按特征类型发射时须区分；频域特征仅当 freq_enabled（配置校验保证）
        for f in feats:
            if f in ("spec_centroid", "spec_energy", "dominant_freq") or f.startswith("band"):
                body_parts.append("    " + _freq_feature_c(f, prefix, n, fs, freq_bands))
            else:
                body_parts.append("    " + _time_feature_c(f, "x", "N"))
        body_parts.append("            break;")
        body_parts.append("        }")
    body_parts.append("        default:")
    body_parts.append("            break;")
    body_parts.append("        }")
    body_parts.append("    }")
    body = "\n".join(body_parts)

    return {"decls": decls, "helpers": helpers + "\n" + stats, "body": body}
