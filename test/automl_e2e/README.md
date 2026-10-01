# e2e_test_automl — BabyOS AutoML 导出 bundle

- 模型：随机森林（`rf`），特征 5 维，类别 3 个，随机种子 42
- 导出时间：2026-09-29 17:00 UTC
- **一致性权威口径：float32 参考实现**（Python sklearn f64 训练 → f32 重放存在固有舍入差）

## 文件清单

| 文件 | 说明 |
|------|------|
| `algo_e2e_test_automl.h/.c` | 模型推理（predict） |
| `algo_e2e_test_automl_feat.h/.c` | 时序特征提取（N 点 → 特征向量） |
| `example/algo_e2e_test_automl_test.c` | BabyOS 轮询任务集成示例 |
| `export_report.json` | 自检结果（编译/数值/特征链/静态扫描/符号清单） |

## 集成步骤（三情形）

1. **glob/Makefile**：bundle 目录拷入工程，`algo_*.c` 加入编译；公共 ML 原语
   （argmax/softmax/dot/normalize/relu/sigmoid/exp/树遍历）由 BabyOS 仓库预置
   `bos/algorithm/algo_ml.c` 提供，**不要**为 bundle 单独复制实现——确认工程编译
   glob 已覆盖 `bos/algorithm/*.c`，且 `_ALGO_ML_ENABLE=1`。
2. **Keil**：同上添加文件；注意将 `algo_e2e_test_automl.c` 的浮点优化级别与工程一致，
   且**关闭浮点表达式收缩**（Keil ARMCC: `--fp_contract=off`；默认收缩行为差异
   详见下方已知边界）。
3. **IAR**：添加文件；ICCARM 浮点收缩默认开启时类别近 tie 的边界样本 id 理论上可能翻转。

## API

```c
int algo_e2e_test_automl_predict(const float *features, uint32_t n_features, float *proba_out);
int algo_e2e_test_automl_feat_extract(const float *ch_buf, uint32_t n, float *out);
```

- `features` 传未归一化特征（归一化已烘焙进 predict 内部，FR-8.4）
- 返回 ≥0 类别 id；`ALGO_E2E_TEST_AUTOML_ERR_ARG` / `ALGO_E2E_TEST_AUTOML_ERR_NOMEM`
- 调用上下文（R-INT-7）：任务上下文、`bInit()` 后、禁中断调用
- 内部内存：predict 走 `bMalloc`/`bFree`（32 B 瞬时）；feat_extract 零堆分配
- **依赖**：BabyOS `algo_ml` 模块（首个含 `_ALGO_ML_ENABLE` 的版本）；同目录下其它导
  出 bundle 共库——链接 `bos/algorithm/algo_ml.c` 一次即可，不要复制原语实现

## 内存估算

- Flash（权重/阈值/旋转因子烘焙）：约 58.5 KB
- RAM 瞬时：predict 32 B；FFT 静态缓冲（如启用频域特征）0 B

## 已知边界（README 明示，回应评审 P3）

一致性验证基于 **gcc + `-ffp-contract=off`**。Keil/IAR 的 ARMCC/ICCARM 默认浮点
收缩行为不同，类别近 tie 的边界样本 id 理论上可能翻转，属已知边界；
非边界样本不受浮点收缩影响（树判定走 double 比较，完全免疫）。


## 可选 BabyOS 模块集成（best practice）

### a. 用 `b_mod_kv` 持久化最近一次预测结果

```c
#include "b_mod_kv.h"
int id = algo_e2e_test_automl_predict(features, ALGO_E2E_TEST_AUTOML_N_FEATURES, NULL);
if (id >= 0) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", id);
    bKVSet("ml/last_class", buf, strlen(buf) + 1);
}
```

### b. 用 `b_mod_button` 按键触发一次性推理

```c
#include "b_mod_button.h"
static void on_btn_single(void *arg) {
    (void)arg;
    int id = algo_e2e_test_automl_predict(features, ALGO_E2E_TEST_AUTOML_N_FEATURES, NULL);
    b_log_i("btn-triggered: class=%d\r\n", id);
}
BUTTON_REG(EVT_SINGLE_CLICK, on_btn_single);
```

### c. 用 `b_mod_shell` 在串口 shell 调推理

```c
#include "b_mod_shell.h"
static int cmd_ml_run(int argc, char **argv) {
    int id = algo_e2e_test_automl_predict(features, ALGO_E2E_TEST_AUTOML_N_FEATURES, NULL);
    shell_printf("class=%d name=%s\r\n", id, algo_e2e_test_automl_class_names[id]);
    return 0;
}
SHELL_REG_CMD(cmd_ml_run, "ml.run", "ml.run f0 f1 ...", "run inference");
```

## 必读文档

- [BabyOS 主页](https://babyos.cn/doc/)
- `CLAUDE.md` §1（bInit/bExec）§2（PT）§4（MCU 资源）
- `bos/algorithm/inc/algo_ml.h` — ML 原语列表与数值契约
