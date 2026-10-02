"""bundle 组装（FR-8.1）：algo_<name>.h/.c、feat、common、example、README、export_report。

头文件 checklist 见 design §7.2a —— 本模块逐条落实：
Doxygen 头注释 / ERR_ARG·ERR_NOMEM / N_FEATURES·N_CLASSES·WIN_LEN 烘焙 /
class_names（__deleted__ 槽位）/ R-INT-7 调用上下文注释。
"""
from __future__ import annotations

import json
import re
from pathlib import Path

from . import c_common, feature_cgen, model_cgen

MODEL_DESC = {
    # 分类
    "dt": "决策树",
    "rf": "随机森林",
    "et": "极端随机树",
    "lr": "逻辑回归",
    "nb": "高斯朴素贝叶斯",
    "mlp": "单隐层神经网络",
    "simple_nn": "简易神经网络",
    "xgb": "XGBoost 梯度提升树",
    "lgbm": "LightGBM 梯度提升树",
    # 回归
    "dt_r": "回归决策树",
    "rf_r": "回归随机森林",
    "et_r": "回归极端随机树",
    "lr_r": "线性回归",
    "simple_nn_r": "回归简易神经网络",
    "xgb_r": "XGBoost 回归提升树",
    "lgbm_r": "LightGBM 回归提升树",
}


def _slug(name: str) -> str:
    # ASCII-only C 标识符：非 [A-Za-z0-9_] → '_'；空 → 'model'
    # 注意：'-' 也替换为 '_'，否则 macro `ALGO_<NAME>_X` 出现 `ALGO_foo-bar_X` 非法
    s = re.sub(r"[^A-Za-z0-9_]", "_", name).strip("_").lower()
    # 首字符必须 [A-Za-z_]
    if not s or not (s[0].isalpha() or s[0] == "_"):
        s = "m_" + s
    return s or "model"


def _c_str(s: str) -> str:
    return json.dumps(s, ensure_ascii=False)


HEADER_TMPL = """/**
 * \\file        algo_{name}.h
 * \\brief        {proj} 自动导出模型 — {desc}（BabyOS AutoML）
 * \\date        {date}
 */
#ifndef _ALGO_{NAME}_H_
#define _ALGO_{NAME}_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define ALGO_{NAME}_ERR_ARG   (-1)   /* 指针为 NULL，或 n_features/n 不等于烘焙常量 */
#define ALGO_{NAME}_ERR_NOMEM (-2)   /* bMalloc 分配失败 */

#define ALGO_{NAME}_N_FEATURES ({nf})   /* 烘焙：特征向量维度 */
#define ALGO_{NAME}_N_CLASSES  ({nc})   /* 烘焙：类别数 */
{win_def}
/* 类别映射（FR-2.5）：id 永不重用，已删除标签槽位为 "__deleted__" */
extern const char * const algo_{name}_class_names[ALGO_{NAME}_N_CLASSES];

/**
 * \\brief  模型推理：特征向量 → 类别 id
 * \\param  features   特征向量（未归一化，归一化已烘焙进内部，FR-8.4）
 * \\param  n_features 必须等于 ALGO_{NAME}_N_FEATURES
 * \\param  proba_out  NULL 则只返回 id；否则缓冲区必须 ≥ N_CLASSES 个 float
 * \\return ≥0 = 类别 id；<0 = 错误码
 * \\note   调用上下文（R-INT-7）：任务上下文、于 bInit() 之后调用，不可在中断中调用
 */
int algo_{name}_predict(const float *features, uint32_t n_features, float *proba_out);
{feat_proto}
#ifdef __cplusplus
}}
#endif

#endif
"""

SOURCE_TMPL = """/**
 * \\file        algo_{name}.c
 * \\brief        {proj} 自动导出模型 — {desc}
 * \\date        {date}
 *
 * 本文件由 BabyOS AutoML 平台生成，请勿手工修改（重新导出会覆盖）。
 * 数值一致性以 float32 参考实现为权威口径（见导出 README）。
 */
#include "algo_{name}.h"
#include "b_os.h"
#include "algo_ml.h"          /* BabyOS 预置 ML 原语（FR-10，需 _ALGO_ML_ENABLE=1） */
{signal_include}{fft_include}
#include <math.h>

{scaler_decls}

{model_decls}

{helpers}

static void {name}_predict_core(const float *xf, float *out)
{predict_core}

{feat_impl}
int algo_{name}_predict(const float *features, uint32_t n_features, float *proba_out)
{{
    float *xf;
    float *out;
    int    id;

    if (features == NULL || n_features != ALGO_{NAME}_N_FEATURES)
    {{
        b_log_e("{name}: bad args\\r\\n");
        return ALGO_{NAME}_ERR_ARG;
    }}
    xf = (float *)bMalloc(ALGO_{NAME}_N_FEATURES * sizeof(float));
    if (xf == NULL)
    {{
        b_log_e("{name}: nomem xf\\r\\n");
        return ALGO_{NAME}_ERR_NOMEM;
    }}
    out = (float *)bMalloc(ALGO_{NAME}_N_CLASSES * sizeof(float));
    if (out == NULL)
    {{
        b_log_e("{name}: nomem out\\r\\n");
        bFree(xf);
        return ALGO_{NAME}_ERR_NOMEM;
    }}
    bAlgoMlNormalize(xf, features, s_offset, s_inv_scale, ALGO_{NAME}_N_FEATURES);
    {name}_predict_core(xf, out);
    id = bAlgoMlArgmax(out, ALGO_{NAME}_N_CLASSES);
    if (proba_out != NULL)
    {{
        int k;
        for (k = 0; k < ALGO_{NAME}_N_CLASSES; k++)
        {{
            proba_out[k] = out[k];
        }}
    }}
    bFree(out);
    bFree(xf);
    return id;
}}
"""

SCALER_TMPL = """/* 归一化参数（FR-8.4）：x' = (x - offset) * inv_scale；零尺度特征已回退 inv_scale=1 */
static const float s_offset[{nf}] = {{
{offset}
}};
static const float s_inv_scale[{nf}] = {{
{inv_scale}
}};"""

CLASS_NAMES_TMPL = "const char * const algo_{name}_class_names[ALGO_{NAME}_N_CLASSES] = {{\n{items}\n}};"

FEAT_HEADER_EXTRA = """
/**
 * \\brief  时序特征提取：N 点通道缓冲 → 特征向量（与 predict 的 features 同序同口径）
 * \\param  ch_buf 通道分离布局：[ch0_N 点][ch1_N 点]...（通道序见下）
 * \\param  n      必须等于 ALGO_{NAME}_WIN_LEN
 * \\param  out    缓冲区必须 ≥ N_FEATURES 个 float
 * \\return 0 = 成功；<0 = 错误码
 * \\note   内部零堆分配；通道顺序：{channels}
 */
int algo_{name}_feat_extract(const float *ch_buf, uint32_t n, float *out);
"""

FEAT_IMPL_TMPL = """int algo_{name}_feat_extract(const float *ch_buf, uint32_t n, float *out)
{{
    if (ch_buf == NULL || out == NULL || n != ALGO_{NAME}_WIN_LEN)
    {{
        b_log_e("{name}: feat bad args\\r\\n");
        return ALGO_{NAME}_ERR_ARG;
    }}
{body}
    return 0;
}}
"""

EXAMPLE_TMPL_TS = """/**
 * \\file        algo_{name}_test.c
 * \\brief        BabyOS 集成示例：注册轮询任务周期性推理（BOS_REG_POLLING_FUNC）
 * \\date        {date}
 *
 * 用法：把本文件加入工程编译，确保 bExec() 在主循环中被调用。
 * 特征数据源（传感器采集/N 点滑窗填充 ch_buf）由用户接入。
 */
#include "b_os.h"
#include "algo_{name}.h"

static float s_ch_buf[{n_ch}][ALGO_{NAME}_WIN_LEN];
static float s_features[ALGO_{NAME}_N_FEATURES];

PT_THREAD({name}_demo_task)(struct pt *pt, void *arg)
{{
    (void)arg;
    PT_BEGIN(pt);
    while (1)
    {{
        /* TODO: 用户接入 —— 采集 {n_ch} 通道 × ALGO_{NAME}_WIN_LEN 点填入 s_ch_buf */
        if (algo_{name}_feat_extract(&s_ch_buf[0][0], ALGO_{NAME}_WIN_LEN, s_features) == 0)
        {{
            int id = algo_{name}_predict(s_features, ALGO_{NAME}_N_FEATURES, NULL);
            if (id >= 0)
            {{
                b_log_i("{name}: class=%d\\r\\n", id);
            }}
        }}
        PT_DELAY_MS(pt, 100);
    }}
    PT_END(pt);
}}
BOS_REG_POLLING_FUNC({name}_demo_task);
"""

EXAMPLE_TMPL_TABLE = """/**
 * \\file        algo_{name}_test.c
 * \\brief        BabyOS 集成示例：注册轮询任务周期性推理（BOS_REG_POLLING_FUNC）
 * \\date        {date}
 *
 * 用法：把本文件加入工程编译，确保 bExec() 在主循环中被调用。
 * 特征数据源（传感器/计算得到的特征向量）由用户接入。
 */
#include "b_os.h"
#include "algo_{name}.h"

static float s_features[ALGO_{NAME}_N_FEATURES];

PT_THREAD({name}_demo_task)(struct pt *pt, void *arg)
{{
    (void)arg;
    PT_BEGIN(pt);
    while (1)
    {{
        /* TODO: 用户接入 —— 采集/计算 ALGO_{NAME}_N_FEATURES 维特征填入 s_features */
        {{
            int id = algo_{name}_predict(s_features, ALGO_{NAME}_N_FEATURES, NULL);
            if (id >= 0)
            {{
                b_log_i("{name}: class=%d\\r\\n", id);
            }}
        }}
        PT_DELAY_MS(pt, 100);
    }}
    PT_END(pt);
}}
BOS_REG_POLLING_FUNC({name}_demo_task);
"""

README_TMPL = """# {proj} — BabyOS AutoML 导出 bundle

- 模型：{desc}（`{mt}`），特征 {nf} 维，类别 {nc} 个，随机种子 {seed}
- 导出时间：{date}
- **一致性权威口径：float32 参考实现**（Python sklearn f64 训练 → f32 重放存在固有舍入差）

## 文件清单

| 文件 | 说明 |
|------|------|
| `algo_{name}.h/.c` | 模型推理（predict） |
| `algo_{name}_feat.h/.c` | 时序特征提取（N 点 → 特征向量） |
| `example/algo_{name}_test.c` | BabyOS 轮询任务集成示例 |
| `export_report.json` | 自检结果（编译/数值/特征链/静态扫描/符号清单） |

## 集成步骤（三情形）

1. **glob/Makefile**：bundle 目录拷入工程，`algo_*.c` 加入编译；公共 ML 原语
   （argmax/softmax/dot/normalize/relu/sigmoid/exp/树遍历）由 BabyOS 仓库预置
   `bos/algorithm/algo_ml.c` 提供，**不要**为 bundle 单独复制实现——确认工程编译
   glob 已覆盖 `bos/algorithm/*.c`，且 `_ALGO_ML_ENABLE=1`。
2. **Keil**：同上添加文件；注意将 `algo_{name}.c` 的浮点优化级别与工程一致，
   且**关闭浮点表达式收缩**（Keil ARMCC: `--fp_contract=off`；默认收缩行为差异
   详见下方已知边界）。
3. **IAR**：添加文件；ICCARM 浮点收缩默认开启时类别近 tie 的边界样本 id 理论上可能翻转。

## API

```c
int algo_{name}_predict(const float *features, uint32_t n_features, float *proba_out);
{feat_doc}
```

- `features` 传未归一化特征（归一化已烘焙进 predict 内部，FR-8.4）
- 返回 ≥0 类别 id；`ALGO_{NAME}_ERR_ARG` / `ALGO_{NAME}_ERR_NOMEM`
- 调用上下文（R-INT-7）：任务上下文、`bInit()` 后、禁中断调用
- 内部内存：predict 走 `bMalloc`/`bFree`（{mem_bytes} B 瞬时）；feat_extract 零堆分配
- **依赖**：BabyOS `algo_ml` 模块（首个含 `_ALGO_ML_ENABLE` 的版本）；同目录下其它导
  出 bundle 共库——链接 `bos/algorithm/algo_ml.c` 一次即可，不要复制原语实现

## 内存估算

- Flash（权重/阈值/旋转因子烘焙）：约 {flash_kb} KB
- RAM 瞬时：predict {mem_bytes} B；FFT 静态缓冲（如启用频域特征）{fft_bytes} B

## 已知边界（README 明示，回应评审 P3）

一致性验证基于 **gcc + `-ffp-contract=off`**。Keil/IAR 的 ARMCC/ICCARM 默认浮点
收缩行为不同，类别近 tie 的边界样本 id 理论上可能翻转，属已知边界；
非边界样本不受浮点收缩影响（树判定走 double 比较，完全免疫）。
"""


def code_size_hint(files: dict[str, str]) -> int:
    return sum(len(c.encode("utf-8")) for c in files.values())


# ============================================================
# BabyOS 工程集成模板（评审 B1/B2/B3）
# ============================================================

# 完整 main.c 模板（含 bInit/bExec + bInit 返值检查）
MAIN_C_TMPL = """/**
 * \\file        main.c
 * \\brief        {proj} — BabyOS 集成入口（AutoML 导出 bundle 模板）
 * \\date        {date}
 *
 * 编译要点：
 * 1. 链接 `bos/algorithm/algo_ml.c`（ML 原语：argmax/softmax/dot/normalize/relu/...）
 * 2. `b_config.h` 中定义 `_ALGO_ML_ENABLE 1`（参考同目录 b_config_snippet.h）
 * 3. 工程 glob 含 `bundle/algo_{name}.c` 与 `bundle/example/algo_{name}_test.c`
 * 4. 在主循环调用 `bExec()` —— Polling 任务靠它驱动
 * 5. 用户按 MCU 实现 `b_hal_if.c`（bHalInit 等）
 *
 * 上下文约束（R-INT-7）：predict 必须在 PT 任务 / 普通函数调用，禁止 ISR 内调用。
 *
 * 自检 / host 编译：本文件用 `#ifndef AUTOML_SKIP_MAIN` 包裹，避免 host 自检
 * 编译（无完整 BabyOS 链接）时与 `bInit()` 重复定义冲突。
 */
#include "b_os.h"
#include "algo_{name}.h"

#ifndef AUTOML_SKIP_MAIN
extern int bHalInit(void);  /* 用户按 MCU 平台实现 */

int main(void)
{{
    bHalInit();
    if (bInit() != 0)
    {{
        return 1;
    }}
    while (1)
    {{
        bExec();
    }}
    return 0;
}}
#endif
"""

# Makefile 集成片段（评审 B1）
MAKEFILE_SNIPPET_TMPL = """# BabyOS AutoML bundle ({name}) 集成片段
# 用法：把下面几行加到你工程 Makefile

BUNDLE_SRC := bundle/algo_{name}.c
BUNDLE_SRC += $(wildcard bundle/example/*.c)
BUNDLE_SRC += $(BUNDLE_SRC_DIR)/bos/algorithm/algo_ml.c
{extra_src}
CFLAGS  += -D_ALGO_ML_ENABLE=1
{extra_cflags}CFLAGS  += -ffp-contract=off
INCLUDES += -Ibundle
INCLUDES += -I$(BUNDLE_SRC_DIR)/bos/algorithm
INCLUDES += -I$(BUNDLE_SRC_DIR)/bos/algorithm/inc
SRCS += $(BUNDLE_SRC)
"""

# b_config.h 最小片段（评审 B1/B2：含 Kconfig 路径 + template 配置）
# 启用方式（任选其一）：
#   (a) 把下面宏贴到工程 _config/b_config.h
#   (b) 在 menuconfig 中按路径选中：Algorithm Configuration →
#       [ ] Algorithm Enable/Disable                       (_BOS_ALGO_ENABLE)
#           └─ [ ] ML Basic Primitives (AutoML)            (_ALGO_ML_ENABLE)
#   (c) 直接改源码：bos/algorithm/Kconfig 行 39 `_ALGO_ML_ENABLE default y`
BCONFIG_SNIPPET_TMPL = """/* BabyOS AutoML bundle ({name}) — b_config.h 最小片段
 *
 * 三种启用方式（任选其一）：
 *   (a) 把下面宏块贴到工程 _config/b_config.h
 *   (b) make menuconfig → Algorithm Configuration →
 *           [*] Algorithm Enable/Disable          (_BOS_ALGO_ENABLE, 默认 y)
 *           └─ [*] ML Basic Primitives (AutoML)   (_ALGO_ML_ENABLE, 默认 y)
 *   (c) 直接编辑 bos/algorithm/Kconfig 行 39，default 改为 y
 *
 * 注：_ALGO_ML_ENABLE 默认已 y；但若上游 menuconfig 曾显式置 n，必须重新启用。
 *     深度绑定（2026-10-01）：时序工程需启用 _ALGO_SIGNAL_ENABLE 和 _ALGO_FFT_ENABLE。
 */

#ifndef _BOS_ALGO_ENABLE
#define _BOS_ALGO_ENABLE   1   /* 算法总开关（Kconfig 默认 y） */
#endif

#ifndef _ALGO_ML_ENABLE
#define _ALGO_ML_ENABLE    1   /* 启用 ML 原语（Kconfig 默认 y） */
#endif
{extra_config}"""

# 链接段说明（评审 B3）
SECTION_TXT_TMPL = """BabyOS 链接段约定（评审 B3）

| 符号形态                              | 推荐段                | 备注 |
|---------------------------------------|-----------------------|------|
| `BOS_REG_POLLING_FUNC(name)` 内部 fn  | `.bos_polling`        | BabyOS 自动扫描驱动 |
| 静态 FFT 表 `s_re/s_im/s_tw/s_rev`    | `.bss` / `.fastram`   | 大时可放 RAM 段 |
| `s_offset` / `s_inv_scale` / `nodes_*` | `.rodata`            | 模型权重 |
| `algo_{name}_predict` / `_feat_extract` | `.text`              | 用户主动调用 |
"""

# README 末尾追加（评审 B2：b_mod_kv/button/shell/state + 文档链接）
INTEGRATION_NOTES_TMPL = """

## 可选 BabyOS 模块集成（best practice）

### a. 用 `b_mod_kv` 持久化最近一次预测结果

```c
#include "b_mod_kv.h"
int id = algo_{name}_predict(features, ALGO_{NAME}_N_FEATURES, NULL);
if (id >= 0) {{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", id);
    bKVSet("ml/last_class", buf, strlen(buf) + 1);
}}
```

### b. 用 `b_mod_button` 按键触发一次性推理

```c
#include "b_mod_button.h"
static void on_btn_single(void *arg) {{
    (void)arg;
    int id = algo_{name}_predict(features, ALGO_{NAME}_N_FEATURES, NULL);
    b_log_i("btn-triggered: class=%d\\r\\n", id);
}}
BUTTON_REG(EVT_SINGLE_CLICK, on_btn_single);
```

### c. 用 `b_mod_shell` 在串口 shell 调推理

```c
#include "b_mod_shell.h"
static int cmd_ml_run(int argc, char **argv) {{
    int id = algo_{name}_predict(features, ALGO_{NAME}_N_FEATURES, NULL);
    shell_printf("class=%d name=%s\\r\\n", id, algo_{name}_class_names[id]);
    return 0;
}}
SHELL_REG_CMD(cmd_ml_run, "ml.run", "ml.run f0 f1 ...", "run inference");
```

## 必读文档

- [BabyOS 主页](https://babyos.cn/doc/)
- `CLAUDE.md` §1（bInit/bExec）§2（PT）§4（MCU 资源）
- `bos/algorithm/inc/algo_ml.h` — ML 原语列表与数值契约
"""


def build_bundle(
    workdir: Path,
    proj_name: str,
    payload: dict,
    feature_meta: dict | None,  # None = 表格工程
    date: str,
) -> dict:
    """在 workdir 生成完整 bundle。返回 {"name": slug, "files": [...], "nf": n, ...}。"""
    name = _slug(proj_name)
    NAME = name.upper()
    nc = len(payload["labels"])
    nf = len(payload["feature_indices"])
    mt = payload["model_type"]
    is_ts = feature_meta is not None

    arrs = model_cgen.extract_arrays(payload, nc)
    prefix = name
    nclass_macro = f"ALGO_{NAME}_N_CLASSES"
    emitted = model_cgen.emit_model(payload, arrs, prefix, nc, nf, nclass_macro)

    # 归一化（f64 求倒数再 f32 烘焙，与 reference._normalize_f32 完全一致）
    import numpy as np

    off = payload["scaler"]["offset"]
    inv = [float(np.float32(1.0 / np.float64(s))) for s in payload["scaler"]["scale"]]
    scaler_decls = SCALER_TMPL.format(
        nf=nf,
        offset=",\n".join("    " + model_cgen._f32(v) for v in off),
        inv_scale=",\n".join("    " + model_cgen._f32(v) for v in inv),
    )

    # scaler 快照按导出特征子集保存（trainer 在 X[:, feat_idx] 上 fit），长度即 nf
    win_def = ""
    feat_proto = ""
    feat_impl = ""
    feat_doc = ""
    example = ""
    n_ch = 0
    signal_include = ""
    fft_include = ""
    if is_ts:
        n = feature_meta["n"]
        n_ch = feature_meta["n_channels"]
        win_def = f"#define ALGO_{NAME}_WIN_LEN    ({n})   /* 烘焙：滑窗长度（采样点） */"
        feat_proto = FEAT_HEADER_EXTRA.format(NAME=NAME, name=name, channels=", ".join(feature_meta["channels"]))
        fe = feature_cgen.emit_feat_extract(
            prefix, feature_meta["channels"], feature_meta["exported"], n,
            feature_meta["fs"], feature_meta["freq_enabled"], feature_meta["freq_bands"],
        )
        feat_impl = (
            fe["decls"] + "\n" + fe["helpers"] + "\n"
            + FEAT_IMPL_TMPL.format(name=name, NAME=NAME, body=fe["body"])
        )
        feat_doc = f"int algo_{name}_feat_extract(const float *ch_buf, uint32_t n, float *out);"
        example = EXAMPLE_TMPL_TS.format(name=name, NAME=NAME, n_ch=n_ch, date=date)

        # 深度绑定：根据特征类型添加预置原语 include
        signal_include = '#include "algo_signal.h"      /* BabyOS 预置信号统计原语（需 _ALGO_SIGNAL_ENABLE=1） */\n'
        has_freq = feature_meta["freq_enabled"] and any(
            f in ("spec_centroid", "spec_energy", "dominant_freq") or f.startswith("band")
            for _, f in feature_meta["exported"]
        )
        if has_freq:
            fft_include = '#include "algo_fft.h"         /* BabyOS 预置 FFT 原语（需 _ALGO_FFT_ENABLE=1） */\n'
    else:
        # 表格工程 example 必含（FR-8.7，每次导出必生成）
        example = EXAMPLE_TMPL_TABLE.format(name=name, NAME=NAME, date=date)

    header = HEADER_TMPL.format(
        name=name, NAME=NAME, proj=proj_name, desc=MODEL_DESC[mt], date=date,
        nf=nf, nc=nc, win_def=win_def, feat_proto=feat_proto,
    )
    class_names = CLASS_NAMES_TMPL.format(
        name=name, NAME=NAME,
        items=",\n".join(f"    {_c_str(l['name'])}" for l in payload["labels"]),
    )
    source = SOURCE_TMPL.format(
        name=name, NAME=NAME, proj=proj_name, desc=MODEL_DESC[mt], date=date,
        scaler_decls=scaler_decls,
        model_decls=emitted["decls"] + "\n\n" + class_names,
        helpers=emitted["helpers"],
        predict_core=emitted["predict_core"],
        feat_impl=feat_impl,
        signal_include=signal_include,
        fft_include=fft_include,
    )

    # 文件落盘（v1.6：公共 ML 原语由 BabyOS algo_ml 模块提供，bundle 不再带
    # inc/algo_ml_common.*；自检 host 编译会从仓库 root 复制真实 algo_ml.c）
    files: dict[str, str] = {}
    files[f"algo_{name}.h"] = header
    files[f"algo_{name}.c"] = source
    if is_ts:
        files[f"algo_{name}_feat.h"] = (
            f"/**\n * \\file        algo_{name}_feat.h\n * \\brief        特征提取接口（声明集中于 algo_{name}.h，此处仅包含之）\n"
            f" * \\date        {date}\n */\n#ifndef _ALGO_{NAME}_FEAT_H_\n#define _ALGO_{NAME}_FEAT_H_\n"
            f'#include "algo_{name}.h"\n#endif\n'
        )
        # feat_extract 实现与模型同编译单元（共享静态 FFT 缓冲零开销），feat.h 仅含包含指引
    if example:
        files[f"example/algo_{name}_test.c"] = example

    # README（内存估算：Flash ≈ 代码 + 烘焙表；RAM = predict bMalloc + FFT 静态缓冲）
    mem_bytes = (nf + nc) * 4
    n = feature_meta["n"] if is_ts else 0
    fft_bytes = (2 * n + (n // 2 + 1)) * 4 + n * 2 if (is_ts and feature_meta["freq_enabled"]) else 0
    files["README.md"] = README_TMPL.format(
        proj=proj_name, desc=MODEL_DESC[mt], mt=mt, nf=nf, nc=nc,
        seed=payload["seed"], date=date, name=name, NAME=NAME,
        feat_doc=feat_doc, mem_bytes=mem_bytes,
        flash_kb=round(code_size_hint(files) / 1024, 1),
        fft_bytes=fft_bytes,
    ) + INTEGRATION_NOTES_TMPL.format(name=name, NAME=NAME)

    # 评审 B1/B3：bundle 内带 main.c + Makefile snippet + b_config.h + section.txt
    # 让用户拿到 bundle 就能在已有 BabyOS 工程里编译运行
    files["main.c"] = MAIN_C_TMPL.format(proj=proj_name, date=date, name=name)

    # 深度绑定：根据特征类型添加预置原语的编译配置
    extra_src = ""
    extra_cflags = ""
    extra_config = ""
    if is_ts:
        has_freq = feature_meta["freq_enabled"] and any(
            f in ("spec_centroid", "spec_energy", "dominant_freq") or f.startswith("band")
            for _, f in feature_meta["exported"]
        )
        extra_src = "BUNDLE_SRC += $(BUNDLE_SRC_DIR)/bos/algorithm/algo_signal.c\n"
        extra_cflags = "CFLAGS  += -D_ALGO_SIGNAL_ENABLE=1\n"
        extra_config = """
#ifndef _ALGO_SIGNAL_ENABLE
#define _ALGO_SIGNAL_ENABLE 1  /* 启用信号统计原语（时序工程需要） */
#endif
"""
        if has_freq:
            extra_src += "BUNDLE_SRC += $(BUNDLE_SRC_DIR)/bos/algorithm/algo_fft.c\n"
            extra_cflags += "CFLAGS  += -D_ALGO_FFT_ENABLE=1\n"
            extra_config += """
#ifndef _ALGO_FFT_ENABLE
#define _ALGO_FFT_ENABLE    1  /* 启用 FFT 原语（频域特征需要） */
#endif
"""

    files["Makefile.snippet"] = MAKEFILE_SNIPPET_TMPL.format(
        name=name, extra_src=extra_src, extra_cflags=extra_cflags
    )
    files["b_config_snippet.h"] = BCONFIG_SNIPPET_TMPL.format(
        name=name, extra_config=extra_config
    )
    files["section.txt"] = SECTION_TXT_TMPL.format(name=name)

    for rel, content in files.items():
        p = workdir / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content, encoding="utf-8")

    code_size = code_size_hint(files)
    # 评审 B5：资源预算量化 — flash/ram_static/stack_max 三档明细
    # - flash_kb_total：bundle 自身（已含）+ algo_ml.c 链接贡献（≈ 6KB BabyOS 算法基元）
    # - ram_static_bytes：BSS 内静态缓冲（s_re/s_im/s_mag + twiddle/reverse + scaler）
    # - stack_max_bytes：PT 任务最坏栈深（max(N_FEATURES, N_CLASSES) float 局部）
    algo_ml_flash_kb = 6  # bos/algorithm/algo_ml.c 估算
    flash_kb_total = round((code_size + algo_ml_flash_kb * 1024) / 1024, 1)
    ram_static_bytes = fft_bytes + nf * 4 + nc * 4  # FFT 表 + offset/inv_scale + class_names ptr
    stack_max_bytes = max(nf, nc) * 4  # predict_core 局部 float[]

    return {
        "name": name,
        "files": sorted(files.keys()),
        "nf": nf,
        "nc": nc,
        "code_size_bytes": code_size,
        "emitted": emitted,
        "is_ts": is_ts,
        "n_ch": n_ch,
        "hidden": emitted.get("hidden", 0),
        "resource_budget": {
            "flash_kb_total": flash_kb_total,       # 含 algo_ml.c 链接贡献
            "flash_kb_bundle": round(code_size / 1024, 1),  # 仅 bundle 自身
            "ram_peak_bytes": mem_bytes,            # predict 双 bMalloc 瞬时
            "ram_static_bytes": ram_static_bytes,   # FFT + scaler + class_names ptr
            "stack_max_bytes": stack_max_bytes,     # PT 任务最坏栈深
            "fft_bytes": fft_bytes,
        },
    }
