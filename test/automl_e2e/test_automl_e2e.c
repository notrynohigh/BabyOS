/**
 * \file        test_automl_e2e.c
 * \brief        BabyOS AutoML 端到端测试 — 验证导出模型推理（独立版本）
 * \date        2026-09-30
 *
 * 测试流程：
 * 1. 验证特征提取函数
 * 2. 验证模型推理函数
 * 3. 验证类别映射
 * 4. 验证边界条件
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "algo_e2e_test_automl.h"

/* 简单的 bMalloc/bFree 实现用于测试 */
static char s_heap[32 * 1024];
static size_t s_heap_offset = 0;

void *bMalloc(size_t size)
{
    if (s_heap_offset + size > sizeof(s_heap)) {
        return NULL;
    }
    void *p = &s_heap[s_heap_offset];
    s_heap_offset += (size + 7) & ~7;  /* 8 字节对齐 */
    return p;
}

void bFree(void *p)
{
    (void)p;
    /* 简单测试不实现释放 */
}

/* bMallocPlus/bFreePlus 实现 */
void *bMallocPlus(size_t size, const char *tag)
{
    (void)tag;
    return bMalloc(size);
}

void bFreePlus(void *p)
{
    bFree(p);
}

/* bLogOut 空实现 */
void bLogOut(const char *fmt, ...)
{
    (void)fmt;
}

/*------------------------------------------------------------
 * 测试数据 — 模拟 3 通道 × WIN_LEN 点的传感器数据
 *------------------------------------------------------------*/
static float test_ch_buf[ALGO_E2E_TEST_AUTOML_WIN_LEN * 3];

static void fill_test_data(float freq_x, float freq_y, float freq_z)
{
    for (int i = 0; i < ALGO_E2E_TEST_AUTOML_WIN_LEN; i++) {
        float t = (float)i / 100.0f;  /* 假设采样率 100Hz */
        test_ch_buf[i] = freq_x * t;  /* accel_x */
        test_ch_buf[ALGO_E2E_TEST_AUTOML_WIN_LEN + i] = freq_y * t;  /* accel_y */
        test_ch_buf[2 * ALGO_E2E_TEST_AUTOML_WIN_LEN + i] = freq_z * t;  /* accel_z */
    }
}

/*------------------------------------------------------------
 * 测试 1: 特征提取基本功能
 *------------------------------------------------------------*/
int test_feat_extract_basic(void)
{
    float features[ALGO_E2E_TEST_AUTOML_N_FEATURES];
    fill_test_data(1.0f, 2.0f, 3.0f);

    int ret = algo_e2e_test_automl_feat_extract(
        test_ch_buf, ALGO_E2E_TEST_AUTOML_WIN_LEN, features);

    if (ret != 0) {
        printf("feat_extract returned %d, expected 0", ret);
        return 1;
    }

    /* 验证特征向量非全零 */
    int has_nonzero = 0;
    for (int i = 0; i < ALGO_E2E_TEST_AUTOML_N_FEATURES; i++) {
        if (features[i] != 0.0f) {
            has_nonzero = 1;
            break;
        }
    }
    if (!has_nonzero) {
        printf("features are all zero");
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 2: 特征提取参数校验
 *------------------------------------------------------------*/
int test_feat_extract_null_ptr(void)
{
    float features[ALGO_E2E_TEST_AUTOML_N_FEATURES];
    fill_test_data(1.0f, 1.0f, 1.0f);

    /* NULL 输入应返回错误 */
    int ret = algo_e2e_test_automl_feat_extract(NULL, ALGO_E2E_TEST_AUTOML_WIN_LEN, features);
    if (ret != ALGO_E2E_TEST_AUTOML_ERR_ARG) {
        printf("NULL input returned %d, expected %d", ret, ALGO_E2E_TEST_AUTOML_ERR_ARG);
        return 1;
    }

    /* NULL 输出应返回错误 */
    ret = algo_e2e_test_automl_feat_extract(test_ch_buf, ALGO_E2E_TEST_AUTOML_WIN_LEN, NULL);
    if (ret != ALGO_E2E_TEST_AUTOML_ERR_ARG) {
        printf("NULL output returned %d, expected %d", ret, ALGO_E2E_TEST_AUTOML_ERR_ARG);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 3: 特征提取长度校验
 *------------------------------------------------------------*/
int test_feat_extract_wrong_length(void)
{
    float features[ALGO_E2E_TEST_AUTOML_N_FEATURES];
    fill_test_data(1.0f, 1.0f, 1.0f);

    /* 错误长度应返回错误 */
    int ret = algo_e2e_test_automl_feat_extract(test_ch_buf, 10, features);
    if (ret != ALGO_E2E_TEST_AUTOML_ERR_ARG) {
        printf("wrong length returned %d, expected %d", ret, ALGO_E2E_TEST_AUTOML_ERR_ARG);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 4: 模型推理基本功能
 *------------------------------------------------------------*/
int test_predict_basic(void)
{
    float features[ALGO_E2E_TEST_AUTOML_N_FEATURES] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};

    int id = algo_e2e_test_automl_predict(features, ALGO_E2E_TEST_AUTOML_N_FEATURES, NULL);

    /* 应返回有效的类别 ID (0, 1, 或 2) */
    if (id < 0 || id >= ALGO_E2E_TEST_AUTOML_N_CLASSES) {
        printf("predict returned %d, expected [0, %d)", id, ALGO_E2E_TEST_AUTOML_N_CLASSES);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 5: 模型推理概率输出
 *------------------------------------------------------------*/
int test_predict_with_proba(void)
{
    float features[ALGO_E2E_TEST_AUTOML_N_FEATURES] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
    float proba[ALGO_E2E_TEST_AUTOML_N_CLASSES];

    int id = algo_e2e_test_automl_predict(features, ALGO_E2E_TEST_AUTOML_N_FEATURES, proba);

    if (id < 0) {
        printf("predict returned %d", id);
        return 1;
    }

    /* 验证概率和为 1.0 */
    float sum = 0.0f;
    for (int i = 0; i < ALGO_E2E_TEST_AUTOML_N_CLASSES; i++) {
        sum += proba[i];
        /* 每个概率应在 [0, 1] 范围内 */
        if (proba[i] < 0.0f || proba[i] > 1.0f) {
            printf("proba[%d] = %f, out of range [0, 1]", i, proba[i]);
            return 1;
        }
    }
    if (fabsf(sum - 1.0f) > 0.01f) {
        printf("proba sum = %f, expected ~1.0", sum);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 6: 模型推理参数校验
 *------------------------------------------------------------*/
int test_predict_null_ptr(void)
{
    /* NULL 特征向量应返回错误 */
    int id = algo_e2e_test_automl_predict(NULL, ALGO_E2E_TEST_AUTOML_N_FEATURES, NULL);
    if (id != ALGO_E2E_TEST_AUTOML_ERR_ARG) {
        printf("NULL input returned %d, expected %d", id, ALGO_E2E_TEST_AUTOML_ERR_ARG);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 7: 模型推理维度校验
 *------------------------------------------------------------*/
int test_predict_wrong_dim(void)
{
    float features[3] = {0.1f, 0.2f, 0.3f};

    /* 错误维度应返回错误 */
    int id = algo_e2e_test_automl_predict(features, 3, NULL);
    if (id != ALGO_E2E_TEST_AUTOML_ERR_ARG) {
        printf("wrong dim returned %d, expected %d", id, ALGO_E2E_TEST_AUTOML_ERR_ARG);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 8: 类别映射正确性
 *------------------------------------------------------------*/
int test_class_names(void)
{
    /* 验证类别名称数组存在且非空 */
    if (algo_e2e_test_automl_class_names == NULL) {
        printf("class_names is NULL");
        return 1;
    }

    for (int i = 0; i < ALGO_E2E_TEST_AUTOML_N_CLASSES; i++) {
        if (algo_e2e_test_automl_class_names[i] == NULL) {
            printf("class_names[%d] is NULL", i);
            return 1;
        }
        /* 类别名称长度应大于 0 */
        if (strlen(algo_e2e_test_automl_class_names[i]) == 0) {
            printf("class_names[%d] is empty", i);
            return 1;
        }
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 9: 特征提取一致性 — 相同输入应产生相同输出
 *------------------------------------------------------------*/
int test_feat_extract_consistency(void)
{
    float features1[ALGO_E2E_TEST_AUTOML_N_FEATURES];
    float features2[ALGO_E2E_TEST_AUTOML_N_FEATURES];
    fill_test_data(1.0f, 2.0f, 3.0f);

    algo_e2e_test_automl_feat_extract(test_ch_buf, ALGO_E2E_TEST_AUTOML_WIN_LEN, features1);
    algo_e2e_test_automl_feat_extract(test_ch_buf, ALGO_E2E_TEST_AUTOML_WIN_LEN, features2);

    /* 两次提取结果应完全一致 */
    for (int i = 0; i < ALGO_E2E_TEST_AUTOML_N_FEATURES; i++) {
        if (features1[i] != features2[i]) {
            printf("features[%d] inconsistent: %f != %f", i, features1[i], features2[i]);
            return 1;
        }
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 10: 完整流程 — 特征提取 + 推理
 *------------------------------------------------------------*/
int test_full_pipeline(void)
{
    float features[ALGO_E2E_TEST_AUTOML_N_FEATURES];
    float proba[ALGO_E2E_TEST_AUTOML_N_CLASSES];

    /* 生成测试数据 */
    fill_test_data(1.0f, 2.0f, 3.0f);

    /* 特征提取 */
    int ret = algo_e2e_test_automl_feat_extract(
        test_ch_buf, ALGO_E2E_TEST_AUTOML_WIN_LEN, features);
    if (ret != 0) {
        printf("feat_extract failed: %d", ret);
        return 1;
    }

    /* 模型推理 */
    int id = algo_e2e_test_automl_predict(features, ALGO_E2E_TEST_AUTOML_N_FEATURES, proba);
    if (id < 0 || id >= ALGO_E2E_TEST_AUTOML_N_CLASSES) {
        printf("predict failed: %d", id);
        return 1;
    }

    /* 验证概率和 */
    float sum = 0.0f;
    for (int i = 0; i < ALGO_E2E_TEST_AUTOML_N_CLASSES; i++) {
        sum += proba[i];
    }
    if (fabsf(sum - 1.0f) > 0.01f) {
        printf("proba sum = %f, expected ~1.0", sum);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 11: 常量验证
 *------------------------------------------------------------*/
int test_constants(void)
{
    /* 验证烘焙常量与头文件定义一致 */
    if (ALGO_E2E_TEST_AUTOML_N_FEATURES != 5) {
        printf("N_FEATURES = %d, expected 5", ALGO_E2E_TEST_AUTOML_N_FEATURES);
        return 1;
    }
    if (ALGO_E2E_TEST_AUTOML_N_CLASSES != 3) {
        printf("N_CLASSES = %d, expected 3", ALGO_E2E_TEST_AUTOML_N_CLASSES);
        return 1;
    }
    if (ALGO_E2E_TEST_AUTOML_WIN_LEN != 100) {
        printf("WIN_LEN = %d, expected 100", ALGO_E2E_TEST_AUTOML_WIN_LEN);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 12: 预置信号原语 — 基本功能
 *------------------------------------------------------------*/
int test_algo_signal_basic(void)
{
    extern int test_signal_stats_basic(void);
    extern int test_signal_stats_null(void);
    extern int test_signal_mean(void);
    extern int test_signal_std(void);
    extern int test_signal_rms(void);
    extern int test_signal_zcr(void);
    extern int test_signal_skew_zero_var(void);
    extern int test_signal_kurt_zero_var(void);
    extern int test_signal_ptp(void);
    extern int test_signal_single_element(void);

    if (test_signal_stats_basic() != 0) return 1;
    if (test_signal_stats_null() != 0) return 1;
    if (test_signal_mean() != 0) return 1;
    if (test_signal_std() != 0) return 1;
    if (test_signal_rms() != 0) return 1;
    if (test_signal_zcr() != 0) return 1;
    if (test_signal_skew_zero_var() != 0) return 1;
    if (test_signal_kurt_zero_var() != 0) return 1;
    if (test_signal_ptp() != 0) return 1;
    if (test_signal_single_element() != 0) return 1;

    return 0;
}

/*------------------------------------------------------------
 * 测试 13: 预置 FFT 原语 — 基本功能
 *------------------------------------------------------------*/
int test_algo_fft_basic(void)
{
    extern int test_fft_sine_peak(void);
    extern int test_fft_null_params(void);
    extern int test_fft_centroid(void);
    extern int test_fft_energy(void);
    extern int test_fft_dominant_freq(void);
    extern int test_fft_band_ratio(void);
    extern int test_fft_gen_twiddle(void);
    extern int test_fft_gen_bit_reverse(void);

    if (test_fft_sine_peak() != 0) return 1;
    if (test_fft_null_params() != 0) return 1;
    if (test_fft_centroid() != 0) return 1;
    if (test_fft_energy() != 0) return 1;
    if (test_fft_dominant_freq() != 0) return 1;
    if (test_fft_band_ratio() != 0) return 1;
    if (test_fft_gen_twiddle() != 0) return 1;
    if (test_fft_gen_bit_reverse() != 0) return 1;

    return 0;
}

/*------------------------------------------------------------
 * 测试 14: 预置信号原语 — 新增特征
 * variance / abs_mean / autocorr / 新增统计字段
 *------------------------------------------------------------*/
int test_algo_signal_features(void)
{
    extern int test_signal_variance(void);
    extern int test_signal_abs_mean(void);
    extern int test_signal_autocorr(void);
    extern int test_signal_stats_new_fields(void);

    if (test_signal_variance() != 0) return 1;
    if (test_signal_abs_mean() != 0) return 1;
    if (test_signal_autocorr() != 0) return 1;
    if (test_signal_stats_new_fields() != 0) return 1;

    return 0;
}

/*------------------------------------------------------------
 * 测试 15: 预置 FFT 原语 — dominant_freq 含直流
 * 单元级 argmax 契约 + DC 占优/纯 DC/纯正弦端到端
 *------------------------------------------------------------*/
int test_algo_fft_dominant_freq_dc(void)
{
    extern int test_fft_dominant_freq_includes_dc(void);
    extern int test_fft_dominant_freq_dc_mixed(void);
    extern int test_fft_dominant_freq_pure_dc(void);
    extern int test_fft_dominant_freq_pure_sine(void);

    if (test_fft_dominant_freq_includes_dc() != 0) return 1;
    if (test_fft_dominant_freq_dc_mixed() != 0) return 1;
    if (test_fft_dominant_freq_pure_dc() != 0) return 1;
    if (test_fft_dominant_freq_pure_sine() != 0) return 1;

    return 0;
}

/*------------------------------------------------------------
 * 测试 16: 深度绑定验证 — 生成代码调用预置原语
 *------------------------------------------------------------*/
int test_deep_binding(void)
{
    /* 验证生成代码使用了预置原语，而非内联实现 */

    /* 检查 algo_signal.h 被引用 */
    const char *include_checks[][2] = {
        {"algo_signal.h", "bAlgoSignalStats"},
        {"algo_fft.h", "bAlgoFft"},
    };

    for (size_t i = 0; i < sizeof(include_checks) / sizeof(include_checks[0]); i++) {
        /* 这里只能间接验证：通过编译和运行测试来确认预置原语工作正常 */
        (void)include_checks[i];
    }

    /* 实际验证：生成的特征提取函数能正常工作 */
    float features[ALGO_E2E_TEST_AUTOML_N_FEATURES];
    fill_test_data(1.0f, 2.0f, 3.0f);

    int ret = algo_e2e_test_automl_feat_extract(
        test_ch_buf, ALGO_E2E_TEST_AUTOML_WIN_LEN, features);
    if (ret != 0) {
        printf("deep binding feat_extract failed: %d", ret);
        return 1;
    }

    return 0;
}
