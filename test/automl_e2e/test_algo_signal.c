/**
 * \file        test_algo_signal.c
 * \brief        BabyOS algo_signal 预置原语测试
 * \date        2026-10-01
 *
 * 测试信号统计原语：
 * 1. bAlgoSignalStats 基本功能
 * 2. bAlgoSignalMean/Std/Rms/Zcr/Skew/Kurt/Ptp 派生特征
 * 3. 边界条件（NULL、n=0、n=1）
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "algo_signal.h"

/*------------------------------------------------------------
 * 测试数据
 *------------------------------------------------------------*/
static float test_signal[100];

static void fill_signal(float freq, float amp, float offset)
{
    for (int i = 0; i < 100; i++) {
        float t = (float)i / 100.0f;
        test_signal[i] = offset + amp * sinf(2.0f * 3.14159265f * freq * t);
    }
}

/*------------------------------------------------------------
 * 测试 1: bAlgoSignalStats 基本功能
 *------------------------------------------------------------*/
int test_signal_stats_basic(void)
{
    bAlgoSignalStats_t stats;
    fill_signal(1.0f, 1.0f, 0.0f);

    int ret = bAlgoSignalStats(test_signal, 100, &stats);
    if (ret != 0) {
        printf("bAlgoSignalStats returned %d, expected 0", ret);
        return 1;
    }

    /* 验证统计量非全零 */
    if (stats.sum == 0.0f && stats.sq_sum == 0.0f) {
        printf("stats are all zero");
        return 1;
    }

    /* 验证 min <= max */
    if (stats.min > stats.max) {
        printf("min (%f) > max (%f)", stats.min, stats.max);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 2: bAlgoSignalStats 参数校验
 *------------------------------------------------------------*/
int test_signal_stats_null(void)
{
    bAlgoSignalStats_t stats;

    /* NULL 输入应返回错误 */
    int ret = bAlgoSignalStats(NULL, 100, &stats);
    if (ret != -1) {
        printf("NULL input returned %d, expected -1", ret);
        return 1;
    }

    /* NULL 输出应返回错误 */
    ret = bAlgoSignalStats(test_signal, 100, NULL);
    if (ret != -1) {
        printf("NULL output returned %d, expected -1", ret);
        return 1;
    }

    /* n=0 应返回错误 */
    ret = bAlgoSignalStats(test_signal, 0, &stats);
    if (ret != -1) {
        printf("n=0 returned %d, expected -1", ret);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 3: bAlgoSignalMean
 *------------------------------------------------------------*/
int test_signal_mean(void)
{
    bAlgoSignalStats_t stats;

    /* 已知信号：[1, 2, 3, 4, 5]，mean = 3.0 */
    float known[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    bAlgoSignalStats(known, 5, &stats);

    float mean = bAlgoSignalMean(&stats, 5);
    if (fabsf(mean - 3.0f) > 0.001f) {
        printf("mean = %f, expected 3.0", mean);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 4: bAlgoSignalStd
 *------------------------------------------------------------*/
int test_signal_std(void)
{
    bAlgoSignalStats_t stats;

    /* 已知信号：[1, 2, 3, 4, 5]，std = sqrt(2.0) ≈ 1.4142 */
    float known[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    bAlgoSignalStats(known, 5, &stats);

    float std = bAlgoSignalStd(&stats, 5);
    float expected = sqrtf(2.0f);  /* 方差 = 2.0 */
    if (fabsf(std - expected) > 0.001f) {
        printf("std = %f, expected %f", std, expected);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 5: bAlgoSignalRms
 *------------------------------------------------------------*/
int test_signal_rms(void)
{
    bAlgoSignalStats_t stats;

    /* 已知信号：[3, 4]，RMS = sqrt((9+16)/2) = sqrt(12.5) ≈ 3.5355 */
    float known[2] = {3.0f, 4.0f};
    bAlgoSignalStats(known, 2, &stats);

    float rms = bAlgoSignalRms(&stats, 2);
    float expected = sqrtf(12.5f);
    if (fabsf(rms - expected) > 0.001f) {
        printf("rms = %f, expected %f", rms, expected);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 6: bAlgoSignalZcr
 *------------------------------------------------------------*/
int test_signal_zcr(void)
{
    bAlgoSignalStats_t stats;

    /* 已知信号：[1, -1, 1, -1, 1]，过零率 = 4/4 = 1.0 */
    float known[5] = {1.0f, -1.0f, 1.0f, -1.0f, 1.0f};
    bAlgoSignalStats(known, 5, &stats);

    float zcr = bAlgoSignalZcr(&stats, 5);
    if (fabsf(zcr - 1.0f) > 0.001f) {
        printf("zcr = %f, expected 1.0", zcr);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 7: bAlgoSignalSkew 零方差防护
 *------------------------------------------------------------*/
int test_signal_skew_zero_var(void)
{
    bAlgoSignalStats_t stats;

    /* 常量信号：方差为 0，skew 应返回 0 */
    float known[5] = {3.0f, 3.0f, 3.0f, 3.0f, 3.0f};
    bAlgoSignalStats(known, 5, &stats);

    float skew = bAlgoSignalSkew(&stats, 5);
    if (skew != 0.0f) {
        printf("skew = %f, expected 0.0 (zero variance)", skew);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 8: bAlgoSignalKurt 零方差防护
 *------------------------------------------------------------*/
int test_signal_kurt_zero_var(void)
{
    bAlgoSignalStats_t stats;

    /* 常量信号：方差为 0，kurt 应返回 0 */
    float known[5] = {3.0f, 3.0f, 3.0f, 3.0f, 3.0f};
    bAlgoSignalStats(known, 5, &stats);

    float kurt = bAlgoSignalKurt(&stats, 5);
    if (kurt != 0.0f) {
        printf("kurt = %f, expected 0.0 (zero variance)", kurt);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 9: bAlgoSignalPtp
 *------------------------------------------------------------*/
int test_signal_ptp(void)
{
    bAlgoSignalStats_t stats;

    /* 已知信号：[1, 5, 3]，ptp = 5 - 1 = 4.0 */
    float known[3] = {1.0f, 5.0f, 3.0f};
    bAlgoSignalStats(known, 3, &stats);

    float ptp = bAlgoSignalPtp(&stats);
    if (fabsf(ptp - 4.0f) > 0.001f) {
        printf("ptp = %f, expected 4.0", ptp);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 10: 单元素信号边界情况
 *------------------------------------------------------------*/
int test_signal_single_element(void)
{
    bAlgoSignalStats_t stats;

    /* 单元素信号：所有统计量都应基于该元素 */
    float known[1] = {42.0f};
    int ret = bAlgoSignalStats(known, 1, &stats);
    if (ret != 0) {
        printf("single element returned %d, expected 0", ret);
        return 1;
    }

    if (fabsf(stats.sum - 42.0f) > 0.001f) {
        printf("sum = %f, expected 42.0", stats.sum);
        return 1;
    }

    if (stats.min != 42.0f || stats.max != 42.0f) {
        printf("min/max = %f/%f, expected 42.0/42.0", stats.min, stats.max);
        return 1;
    }

    return 0;
}
