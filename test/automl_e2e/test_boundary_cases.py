#!/usr/bin/env python3
"""TD3 边界用例：W1 C 原语（信号/FFT）+ W3 模型导出拒绝路径。

不修改任何源码。C 侧通过临时编译 harness 验证（与 Makefile 同 CFLAGS）；
Python 侧验证 feature_cgen / model_cgen 契约与拒绝路径。

边界清单（对应任务要求）：
  - n=1 信号：variance=0, abs_mean=|x|, autocorr=0, zcr=0
  - 全零信号：std=0, skew=0, kurt=0, autocorr=0, rms=0
  - 常数信号：同上（且 abs_mean=|c|, variance=0）
  - FFT n=2 最小点数
  - 树深度接近 128 的导出 / 超限拒绝路径存在
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

REPO = Path("/home/yyds/code/BabyOS")
C_DIR = REPO / "test/automl_e2e"
ALGO_DIR = REPO / "bos/algorithm"
PY_DIR = REPO / "tool/babyos-studio/python"

# 与 test/automl_e2e/Makefile 对齐
CFLAGS = [
    "-std=c99",
    "-Wall",
    "-ffp-contract=off",
    "-O0",
    "-g",
    "-D_ALGO_ML_ENABLE=1",
    "-D_ALGO_SIGNAL_ENABLE=1",
    "-D_ALGO_FFT_ENABLE=1",
    f"-I{REPO}/bos",
    f"-I{ALGO_DIR}/inc",
    f"-I{C_DIR}/_config",
]


def _ensure_venv_path() -> None:
    pydir = str(PY_DIR)
    if pydir not in sys.path:
        sys.path.insert(0, pydir)


def _close(a, b, tol=1e-4):
    return abs(float(a) - float(b)) <= tol


def ref_time_features(w):
    """Python 参考（feature_service._time_features 口径，float64）。"""
    x = np.asarray(w, dtype=np.float64)
    n = x.shape[0]
    m = float(np.mean(x))
    d = x - m
    m2 = float(np.mean(d * d))
    m3 = float(np.mean(d * d * d))
    m4 = float(np.mean(d * d * d * d))
    zcr = float(np.mean(x[:-1] * x[1:] < 0)) if n > 1 else 0.0
    if n > 1 and m2 > 0:
        autocorr = float(np.mean(d[:-1] * d[1:])) / m2
    else:
        autocorr = 0.0
    return {
        "mean": m,
        "std": float(np.sqrt(m2)),
        "variance": m2,
        "min": float(np.min(x)),
        "max": float(np.max(x)),
        "rms": float(np.sqrt(np.mean(x * x))),
        "abs_mean": float(np.mean(np.abs(x))),
        "ptp": float(np.max(x) - np.min(x)),
        "zcr": zcr,
        "autocorr": autocorr,
        "skew": (m3 / (m2 ** 1.5)) if m2 > 0 else 0.0,
        "kurt": (m4 / (m2 * m2) - 3.0) if m2 > 0 else 0.0,
    }


BOUNDARY_C_SRC = r"""
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "algo_signal.h"
#include "algo_fft.h"

static int g_fails = 0;

#define _close(a, b) (fabsf((float)(a) - (float)(b)) <= 1e-4f)

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL: %s\n", msg);                                         \
            g_fails++;                                                         \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, msg)                                                  \
    do {                                                                       \
        float _a = (float)(a), _b = (float)(b);                                \
        if (fabsf(_a - _b) > 1e-4f) {                                          \
            printf("FAIL: %s (got %f want %f)\n", msg, (double)_a, (double)_b);\
            g_fails++;                                                         \
        }                                                                      \
    } while (0)

static int test_n1(void)
{
    int before = g_fails;
    bAlgoSignalStats_t st;
    float x[1] = {3.5f};
    CHECK(bAlgoSignalStats(x, 1, &st) == 0, "n=1 stats ret");
    CHECK(_close(bAlgoSignalVariance(&st, 1), 0.0), "n=1 variance==0");
    CHECK(_close(bAlgoSignalAbsMean(&st, 1), 3.5), "n=1 abs_mean==|x|");
    CHECK(bAlgoSignalAutocorr(&st, 1) == 0.0f, "n=1 autocorr==0");
    CHECK(bAlgoSignalZcr(&st, 1) == 0.0f, "n=1 zcr==0");
    CHECK(_close(bAlgoSignalStd(&st, 1), 0.0), "n=1 std==0");
    CHECK(_close(bAlgoSignalSkew(&st, 1), 0.0), "n=1 skew==0");
    CHECK(_close(bAlgoSignalKurt(&st, 1), 0.0), "n=1 kurt==0");
    CHECK(_close(bAlgoSignalRms(&st, 1), 3.5), "n=1 rms==|x|");
    CHECK(st.abs_sum == 3.5f, "n=1 abs_sum");
    CHECK(st.lag1_sum == 0.0f, "n=1 lag1_sum==0");
    return g_fails - before;
}

static int test_zero_and_constant(void)
{
    int before = g_fails;
    bAlgoSignalStats_t st;
    float z[5] = {0, 0, 0, 0, 0};
    float c[5] = {2.0f, 2.0f, 2.0f, 2.0f, 2.0f};

    CHECK(bAlgoSignalStats(z, 5, &st) == 0, "zero stats ret");
    CHECK(_close(bAlgoSignalStd(&st, 5), 0.0), "zero std");
    CHECK(_close(bAlgoSignalSkew(&st, 5), 0.0), "zero skew");
    CHECK(_close(bAlgoSignalKurt(&st, 5), 0.0), "zero kurt");
    CHECK(_close(bAlgoSignalAutocorr(&st, 5), 0.0), "zero autocorr");
    CHECK(_close(bAlgoSignalRms(&st, 5), 0.0), "zero rms");
    CHECK(_close(bAlgoSignalVariance(&st, 5), 0.0), "zero variance");
    CHECK(_close(bAlgoSignalAbsMean(&st, 5), 0.0), "zero abs_mean");
    CHECK(st.zcr == 0, "zero zcr count");
    CHECK(_close(bAlgoSignalZcr(&st, 5), 0.0), "zero zcr rate");
    CHECK(_close(bAlgoSignalPtp(&st), 0.0), "zero ptp");

    CHECK(bAlgoSignalStats(c, 5, &st) == 0, "const stats ret");
    CHECK(_close(bAlgoSignalStd(&st, 5), 0.0), "const std");
    CHECK(_close(bAlgoSignalSkew(&st, 5), 0.0), "const skew");
    CHECK(_close(bAlgoSignalKurt(&st, 5), 0.0), "const kurt");
    CHECK(_close(bAlgoSignalAutocorr(&st, 5), 0.0), "const autocorr");
    CHECK(_close(bAlgoSignalRms(&st, 5), 2.0), "const rms==|c|");
    CHECK(_close(bAlgoSignalVariance(&st, 5), 0.0), "const variance");
    CHECK(_close(bAlgoSignalAbsMean(&st, 5), 2.0), "const abs_mean==|c|");
    CHECK(_close(bAlgoSignalPtp(&st), 0.0), "const ptp");
    CHECK(_close(bAlgoSignalMean(&st, 5), 2.0), "const mean");
    return g_fails - before;
}

static int test_fft_n2(void)
{
    int before = g_fails;
    float tw_re[1], tw_im[1];
    uint16_t rev[2];
    float re[2], im[2], mag[2];
    float fs = 1000.0f;

    /* n=2 最小点数：旋转因子/位反转/FFT/幅度/dominant_freq 全链路 */
    CHECK(bAlgoFftGenTwiddle(tw_re, tw_im, 2) == 0, "fft n=2 twiddle");
    CHECK(bAlgoFftGenBitReverse(rev, 2) == 0, "fft n=2 rev");
    CHECK(rev[0] == 0 && rev[1] == 1, "fft n=2 rev values");

    /* [1, 0] → DFT [1, 1]，argmax 并列取 bin0 → dominant=0 */
    re[0] = 1.0f; re[1] = 0.0f; im[0] = 0.0f; im[1] = 0.0f;
    CHECK(bAlgoFft(re, im, 2, tw_re, tw_im, rev) == 0, "fft n=2 run");
    CHECK(bAlgoFftMagnitude(re, im, mag, 2) == 0, "fft n=2 mag");
    CHECK_NEAR(mag[0], 1.0f, "fft n=2 [1,0] mag0");
    CHECK_NEAR(mag[1], 1.0f, "fft n=2 [1,0] mag1");
    CHECK(_close(bAlgoFftDominantFreq(mag, 2, fs), 0.0), "fft n=2 tie→bin0");

    /* [1, 1] → DC 主导 → dominant=0（修复后含 bin0） */
    re[0] = 1.0f; re[1] = 1.0f; im[0] = 0.0f; im[1] = 0.0f;
    bAlgoFft(re, im, 2, tw_re, tw_im, rev);
    bAlgoFftMagnitude(re, im, mag, 2);
    CHECK(_close(bAlgoFftDominantFreq(mag, 2, fs), 0.0), "fft n=2 DC-dominant");
    /* mag=[2,0] → energy = 4+0 = 4（与 Python sum(|rfft|^2) 一致） */
    CHECK_NEAR(bAlgoFftEnergy(mag, 2), 4.0f, "fft n=2 energy");

    /* [1, -1] → bin1 主导 → dominant=fs/2 */
    re[0] = 1.0f; re[1] = -1.0f; im[0] = 0.0f; im[1] = 0.0f;
    bAlgoFft(re, im, 2, tw_re, tw_im, rev);
    bAlgoFftMagnitude(re, im, mag, 2);
    CHECK_NEAR(mag[0], 0.0f, "fft n=2 [1,-1] mag0");
    CHECK_NEAR(mag[1], 2.0f, "fft n=2 [1,-1] mag1");
    CHECK(_close(bAlgoFftDominantFreq(mag, 2, fs), fs / 2.0f),
          "fft n=2 bin1 dominant");

    /* 全零谱 → 0 */
    mag[0] = 0.0f; mag[1] = 0.0f;
    CHECK(_close(bAlgoFftDominantFreq(mag, 2, fs), 0.0), "fft n=2 zero-spectrum");

    /* 非法参数 */
    CHECK(bAlgoFft(re, im, 3, tw_re, tw_im, rev) == -1, "fft n=3 rejected");
    CHECK(bAlgoFftGenTwiddle(tw_re, tw_im, 0) == -1, "fft n=0 twiddle rejected");
    return g_fails - before;
}

static int test_fft_dc_includes_bin0(void)
{
    int before = g_fails;
    /* n=8 常数信号：能量全在 bin0，dominant_freq 必须为 0（而非跳过 bin0 后误报） */
    float tw_re[4], tw_im[4];
    uint16_t rev[8];
    float re[8], im[8], mag[5];
    float fs = 800.0f;
    int i;

    bAlgoFftGenTwiddle(tw_re, tw_im, 8);
    bAlgoFftGenBitReverse(rev, 8);
    for (i = 0; i < 8; i++) {
        re[i] = 2.0f;
        im[i] = 0.0f;
    }
    bAlgoFft(re, im, 8, tw_re, tw_im, rev);
    bAlgoFftMagnitude(re, im, mag, 8);
    CHECK_NEAR(mag[0], 16.0f, "constant n=8 mag0 (DC)");
    CHECK(_close(bAlgoFftDominantFreq(mag, 8, fs), 0.0),
          "constant n=8 dominant_freq==0");
    return g_fails - before;
}

int main(void)
{
    int f0, f1, f2, f3;
    f0 = test_n1();
    f1 = test_zero_and_constant();
    f2 = test_fft_n2();
    f3 = test_fft_dc_includes_bin0();
    g_fails = f0 + f1 + f2 + f3;
    if (g_fails == 0) {
        printf("BOUNDARY_C: ALL PASS\n");
        return 0;
    }
    printf("BOUNDARY_C: %d FAIL\n", g_fails);
    return 1;
}
"""


def run_c_boundary_harness():
    """临时编译 C 边界 harness（不改仓库源文件）。"""
    with tempfile.TemporaryDirectory(prefix="td3_boundary_") as td:
        tdp = Path(td)
        cfile = tdp / "boundary_c.c"
        binfile = tdp / "boundary_c"
        cfile.write_text(BOUNDARY_C_SRC, encoding="utf-8")
        cmd = (
            ["gcc"]
            + CFLAGS
            + [str(cfile), str(ALGO_DIR / "algo_signal.c"), str(ALGO_DIR / "algo_fft.c"),
               "-lm", "-o", str(binfile)]
        )
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            return False, "C harness compile failed:\n" + (r.stderr or "")[-2000:]
        r2 = subprocess.run([str(binfile)], capture_output=True, text=True)
        out = (r2.stdout or "") + (r2.stderr or "")
        ok = r2.returncode == 0 and "ALL PASS" in out
        return ok, out


def check_python_ref_boundary():
    """Python 参考实现对边界输入的期望值（与 C 契约对照）。"""
    cases = {
        "n1": np.array([3.5], dtype=np.float64),
        "zero": np.zeros(5),
        "const": np.full(5, 2.0),
    }
    expect = {
        "n1": {"variance": 0.0, "abs_mean": 3.5, "autocorr": 0.0, "zcr": 0.0, "std": 0.0},
        "zero": {"std": 0.0, "skew": 0.0, "kurt": 0.0, "autocorr": 0.0, "rms": 0.0},
        "const": {"std": 0.0, "skew": 0.0, "kurt": 0.0, "autocorr": 0.0, "rms": 2.0},
    }
    msgs = []
    for name, sig in cases.items():
        got = ref_time_features(sig)
        for key, want in expect[name].items():
            if not _close(got[key], want, 1e-9):
                msgs.append("ref %s.%s=%r want %r" % (name, key, got[key], want))
    return (not msgs), ("OK" if not msgs else "\n".join(msgs))


def check_feature_cgen_boundary_names():
    """feature_cgen 对边界特征名不抛非预期异常（n=1/零方差相关表达式仍可生成）。"""
    _ensure_venv_path()
    from app.services.export import feature_cgen
    from app.services.feature_service import TIME_FEATURES, FREQ_FEATURES

    msgs = []
    for f in TIME_FEATURES:
        try:
            s = feature_cgen._time_feature_c(f)
            if "out[oi++]" not in s:
                msgs.append("time %s missing out[]: %r" % (f, s))
        except Exception as e:  # noqa: BLE001
            msgs.append("time %s raised %r" % (f, e))
    # band_ratio 组合名必须先展开；直接传 band_ratio 应显式 ValueError（契约）
    try:
        feature_cgen._freq_feature_c("band_ratio", "p", 64, 100.0, 4)
        msgs.append("band_ratio unexpanded should raise ValueError")
    except ValueError:
        pass
    for b in range(1, 8):
        fname = "band%d_ratio" % b
        try:
            s = feature_cgen._freq_feature_c(fname, "p", 64, 100.0, b + 1)
            if "bAlgoFftBandRatio" not in s:
                msgs.append("band expr missing primitive: %r" % s)
        except Exception as e:  # noqa: BLE001
            msgs.append("%s raised %r" % (fname, e))
    # FFT n=2 烘焙表可生成
    try:
        fe = feature_cgen.emit_feat_extract(
            "bd", ["ch0"], [("ch0", "dominant_freq")], n=2, fs=500.0,
            freq_enabled=True, freq_bands=1,
        )
        if "_tw_re[1]" not in fe["decls"] or "_rev[2]" not in fe["decls"]:
            msgs.append("n=2 twiddle/rev tables wrong:\n" + fe["decls"][:400])
    except Exception as e:  # noqa: BLE001
        msgs.append("emit n=2 raised %r" % e)
    return (not msgs), ("OK" if not msgs else "\n".join(msgs))


def check_model_depth_and_reject_paths():
    """树深度契约 + LGBM categorical / average_output 拒绝路径。"""
    _ensure_venv_path()
    from app.services.export import model_cgen
    from app.services.export.generator import MODEL_DESC  # noqa: F401

    msgs = []

    # 1) 深度常量两侧一致
    if model_cgen.ALGO_ML_TREE_MAX_DEPTH != 128:
        msgs.append("model_cgen.ALGO_ML_TREE_MAX_DEPTH=%r"
                    % model_cgen.ALGO_ML_TREE_MAX_DEPTH)
    algo_ml_h = (ALGO_DIR / "inc" / "algo_ml.h").read_text(encoding="utf-8")
    if "ALGO_ML_TREE_MAX_DEPTH 128" not in algo_ml_h and \
       "ALGO_ML_TREE_MAX_DEPTH  128" not in algo_ml_h:
        if "#define ALGO_ML_TREE_MAX_DEPTH 128" not in algo_ml_h:
            msgs.append("algo_ml.h ALGO_ML_TREE_MAX_DEPTH != 128")

    # 2) sklearn 真实深树 → _check_depth 拒绝
    from sklearn.tree import DecisionTreeClassifier
    rng = np.random.RandomState(0)
    X = np.arange(400, dtype=np.float64).reshape(-1, 1)
    y = (np.sin(X[:, 0] / 3.0) > 0).astype(int)
    clf = DecisionTreeClassifier(max_depth=200, random_state=0)
    clf.fit(X, y)
    depth = int(clf.get_depth())
    payload = {"estimator": clf, "model_type": "dt",
               "scaler": {"offset": [0.0], "scale": [1.0]},
               "labels": [{"name": "a"}, {"name": "b"}],
               "feature_indices": [0], "seed": 0}
    try:
        model_cgen._check_depth(payload, [clf])
        if depth > 128:
            msgs.append("deep sklearn tree depth=%d should raise TREE_TOO_DEEP" % depth)
    except ValueError as e:
        if "TREE_TOO_DEEP" not in str(e) or depth <= 128:
            msgs.append("unexpected depth reject: %r (depth=%d)" % (str(e), depth))

    # 3) 合成深链 LGBM tree_structure → _parse_lgbm_tree 拒绝
    def make_lgbm_chain(n_split):
        node = {"leaf_value": 0.25}
        for _ in range(n_split):
            node = {
                "split_feature": 0,
                "threshold": 0.5,
                "decision_type": "<=",
                "default_left": True,
                "num_cat": 0,
                "left_child": node,
                "right_child": {"leaf_value": -0.125},
            }
        return node

    nodes = []
    try:
        model_cgen._parse_lgbm_tree(make_lgbm_chain(129), nodes)
        msgs.append("lgbm chain depth=129 should raise TREE_TOO_DEEP")
    except ValueError as e:
        if "TREE_TOO_DEEP" not in str(e):
            msgs.append("lgbm chain error not TREE_TOO_DEEP: %r" % str(e))

    nodes = []
    try:
        model_cgen._parse_lgbm_tree(make_lgbm_chain(128), nodes)
        if not nodes:
            msgs.append("lgbm chain depth=128 should be accepted")
    except ValueError as e:
        msgs.append("lgbm chain depth=128 should pass, got %r" % str(e))

    # 4) 合成深链 XGB dump → _parse_xgb_tree + _check_gbd_depth 拒绝
    def make_xgb_chain_dump(n_split):
        lines = []
        # left-chain: node i split, yes=i+1, no=last leaf
        last = n_split + 1  # right leaf index
        for i in range(n_split):
            lines.append(
                "%d:[f0<0.5] yes=%d,no=%d,missing=%d,gain=1.0,cover=1.0"
                % (i, i + 1, last, i + 1)
            )
        # left leaves for each level (optional simplicity: only final left leaf + shared right)
        # Ensure node indices continuous 0..last
        lines.append("%d:leaf=0.1,cover=1.0" % (n_split))
        lines.append("%d:leaf=0.2,cover=1.0" % last)
        return "\n".join(lines)

    try:
        nodes_list = [model_cgen._parse_xgb_tree(make_xgb_chain_dump(129))]
        trees = model_cgen._pack_trees(nodes_list)
        model_cgen._check_gbd_depth(trees)
        msgs.append("xgb chain depth=129 should raise TREE_TOO_DEEP")
    except ValueError as e:
        if "TREE_TOO_DEEP" not in str(e):
            msgs.append("xgb chain error not TREE_TOO_DEEP: %r" % str(e))
    except Exception as e:  # noqa: BLE001
        msgs.append("xgb chain unexpected %r" % e)

    # 5) LGBM categorical 拒绝
    cat_node = {
        "split_feature": 1,
        "threshold": 3.0,
        "decision_type": "==",
        "default_left": True,
        "num_cat": 4,
        "left_child": {"leaf_value": 0.1},
        "right_child": {"leaf_value": -0.1},
    }
    nodes = []
    try:
        model_cgen._parse_lgbm_tree(cat_node, nodes)
        msgs.append("lgbm categorical should raise ValueError")
    except ValueError as e:
        msg = str(e)
        if "categorical" not in msg and "decision_type" not in msg:
            msgs.append("lgbm cat reject msg unexpected: %r" % msg)

    # 6) LGBM average_output=True 拒绝（走 _gbdt_classify_arrays mock）
    class _FakeLGBMBooster:
        def dump_model(self):
            return {
                "num_class": 1,
                "num_tree_per_iteration": 1,
                "objective": "binary",
                "average_output": True,
                "tree_info": [{"tree_structure": {"leaf_value": 0.1}}],
            }

    class _FakeLGBMClf:
        booster_ = _FakeLGBMBooster()

    try:
        model_cgen._gbdt_classify_arrays(_FakeLGBMClf(), "lgbm", 2)
        msgs.append("lgbm average_output=True should raise ValueError")
    except ValueError as e:
        if "average_output" not in str(e):
            msgs.append("lgbm average_output msg unexpected: %r" % str(e))
    except Exception as e:  # noqa: BLE001
        msgs.append("lgbm average_output unexpected %r" % e)

    # 7) XGB nextafter 判定方向等价（对任意 float32 x）
    dump = "0:[f0<1.5] yes=1,no=2,missing=1,gain=1.0,cover=1.0\n" \
           "1:leaf=0.25,cover=1.0\n2:leaf=-0.5,cover=1.0\n"
    nodes = model_cgen._parse_xgb_tree(dump)
    thr_c = float(nodes[0]["thr"])
    thr_f = np.float32(1.5)
    probe = np.array(
        [1.5, np.nextafter(1.5, -np.inf), np.nextafter(np.float32(1.5), np.float32(-np.inf)),
         1.4999999, -1.0, 0.0, 2.0, np.float32(1.5).astype(np.float64)],
        dtype=np.float64,
    )
    for x in probe:
        xf = np.float32(x)
        left_xgb = float(xf) < float(thr_f)
        left_c = float(np.float64(xf)) <= thr_c
        if left_xgb != left_c:
            msgs.append("nextafter mismatch x=%r xgb=%s c=%s thr_c=%r"
                        % (x, left_xgb, left_c, thr_c))
            break
    if not (thr_c < float(thr_f)):
        msgs.append("thr_c should be < thr_f (got %r vs %r)" % (thr_c, float(thr_f)))

    return (not msgs), ("OK" if not msgs else "\n".join(msgs))


def run_all():
    results = []

    ok, out = run_c_boundary_harness()
    results.append(("C边界 harness (n=1/全零/常数/FFT n=2/DC)", ok, out))

    ok, out = check_python_ref_boundary()
    results.append(("Python 参考边界期望值", ok, out))

    ok, out = check_feature_cgen_boundary_names()
    results.append(("feature_cgen 边界特征名 + FFT n=2 烘焙", ok, out))

    ok, out = check_model_depth_and_reject_paths()
    results.append(("树深度/LGBM拒绝/nextafter", ok, out))

    print("=" * 60)
    print("TD3 边界用例结果")
    print("=" * 60)
    failed = 0
    for name, ok, out in results:
        status = "PASS" if ok else "FAIL"
        print("  [%s] %s" % (status, name))
        if not ok:
            failed += 1
            for line in str(out).splitlines()[:30]:
                print("      %s" % line)
    print("-" * 60)
    if failed:
        print("边界用例: %d/%d 失败" % (failed, len(results)))
        return 1
    print("边界用例: 全部通过 (%d 组)" % len(results))
    return 0


if __name__ == "__main__":
    sys.exit(run_all())
