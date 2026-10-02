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

/*------------------------------------------------------------
 * 测试 11: bAlgoSignalVariance（新增原语）
 * 期望值与 Python feature_service._time_features 口径一致：
 *   variance = mean((x - mean)^2)  （总体方差，除 N）
 *------------------------------------------------------------*/
int test_signal_variance(void)
{
    bAlgoSignalStats_t stats;

    /* 正常序列 [1,2,3,4,5]：mean=3, var=10/5=2.0
     * d=[-2,-1,0,1,2], Σd²=4+1+0+1+4=10 */
    float known[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    if (bAlgoSignalStats(known, 5, &stats) != 0) {
        printf("bAlgoSignalStats failed");
        return 1;
    }
    float var = bAlgoSignalVariance(&stats, 5);
    if (fabsf(var - 2.0f) > 0.001f) {
        printf("variance = %f, expected 2.0", var);
        return 1;
    }
    /* 与 stats.m2/n 一致 */
    if (fabsf(var - stats.m2 / 5.0f) > 0.000001f) {
        printf("variance %f != m2/n %f", var, stats.m2 / 5.0f);
        return 1;
    }

    /* 负值序列 [-2,-1,0,1,2]：mean=0, var=2.0 */
    float neg[5] = {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f};
    bAlgoSignalStats(neg, 5, &stats);
    var = bAlgoSignalVariance(&stats, 5);
    if (fabsf(var - 2.0f) > 0.001f) {
        printf("negative-seq variance = %f, expected 2.0", var);
        return 1;
    }

    /* 不对称序列 [1.5,2.5,3.5,5.5]：mean=3.25, Σd²=8.75, var=8.75/4=2.1875 */
    float asym[4] = {1.5f, 2.5f, 3.5f, 5.5f};
    bAlgoSignalStats(asym, 4, &stats);
    var = bAlgoSignalVariance(&stats, 4);
    if (fabsf(var - 2.1875f) > 0.001f) {
        printf("asym variance = %f, expected 2.1875", var);
        return 1;
    }

    /* 零方差序列 [3,3,3,3,3] → 0 */
    float zero[5] = {3.0f, 3.0f, 3.0f, 3.0f, 3.0f};
    bAlgoSignalStats(zero, 5, &stats);
    if (bAlgoSignalVariance(&stats, 5) != 0.0f) {
        printf("zero-var variance != 0");
        return 1;
    }

    /* NULL / n==0 防护 → 0 */
    if (bAlgoSignalVariance(NULL, 5) != 0.0f) {
        printf("NULL variance != 0");
        return 1;
    }
    if (bAlgoSignalVariance(&stats, 0) != 0.0f) {
        printf("n=0 variance != 0");
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 12: bAlgoSignalAbsMean（新增原语）
 * 期望值与 Python mean(|x|) 一致
 *------------------------------------------------------------*/
int test_signal_abs_mean(void)
{
    bAlgoSignalStats_t stats;

    /* 正常 [1,2,3,4,5]：(1+2+3+4+5)/5 = 3.0 */
    float known[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    if (bAlgoSignalStats(known, 5, &stats) != 0) {
        printf("bAlgoSignalStats failed");
        return 1;
    }
    float am = bAlgoSignalAbsMean(&stats, 5);
    if (fabsf(am - 3.0f) > 0.001f) {
        printf("abs_mean = %f, expected 3.0", am);
        return 1;
    }

    /* 负值序列 [-2,-1,0,1,2]：(|-2|+|-1|+0+1+2)/5 = 6/5 = 1.2 */
    float neg[5] = {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f};
    bAlgoSignalStats(neg, 5, &stats);
    am = bAlgoSignalAbsMean(&stats, 5);
    if (fabsf(am - 1.2f) > 0.001f) {
        printf("negative-seq abs_mean = %f, expected 1.2", am);
        return 1;
    }

    /* 全负 ramp [-5,-4,-3,-2,-1]：(5+4+3+2+1)/5 = 3.0 */
    float negramp[5] = {-5.0f, -4.0f, -3.0f, -2.0f, -1.0f};
    bAlgoSignalStats(negramp, 5, &stats);
    am = bAlgoSignalAbsMean(&stats, 5);
    if (fabsf(am - 3.0f) > 0.001f) {
        printf("neg-ramp abs_mean = %f, expected 3.0", am);
        return 1;
    }

    /* 常量序列 [3,3,3,3,3]：abs_mean=3.0（零方差下仍有意义，非 0） */
    float zero[5] = {3.0f, 3.0f, 3.0f, 3.0f, 3.0f};
    bAlgoSignalStats(zero, 5, &stats);
    if (fabsf(bAlgoSignalAbsMean(&stats, 5) - 3.0f) > 0.001f) {
        printf("zero-var abs_mean != 3.0");
        return 1;
    }

    /* NULL / n==0 防护 → 0 */
    if (bAlgoSignalAbsMean(NULL, 5) != 0.0f) {
        printf("NULL abs_mean != 0");
        return 1;
    }
    if (bAlgoSignalAbsMean(&stats, 0) != 0.0f) {
        printf("n=0 abs_mean != 0");
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 13: bAlgoSignalAutocorr（新增原语）
 * 口径（与 Python feature_service._time_features 完全一致）：
 *   d = x - mean
 *   autocorr = (Σ_{i=0}^{n-2} d[i]*d[i+1] / (n-1)) / (Σd² / n)
 *   n<=1 或 方差<=0 → 0
 *------------------------------------------------------------*/
int test_signal_autocorr(void)
{
    bAlgoSignalStats_t stats;

    /* 正常 ramp [1,2,3,4,5]：
     * d=[-2,-1,0,1,2]
     * lag1_sum = (-2)(-1)+(-1)(0)+(0)(1)+(1)(2) = 4
     * lag1_mean = 4/4 = 1.0,  m2n = 10/5 = 2.0
     * autocorr = 1.0/2.0 = 0.5 */
    float known[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    if (bAlgoSignalStats(known, 5, &stats) != 0) {
        printf("bAlgoSignalStats failed");
        return 1;
    }
    if (fabsf(stats.lag1_sum - 4.0f) > 0.001f) {
        printf("lag1_sum = %f, expected 4.0", stats.lag1_sum);
        return 1;
    }
    float ac = bAlgoSignalAutocorr(&stats, 5);
    if (fabsf(ac - 0.5f) > 0.001f) {
        printf("autocorr = %f, expected 0.5", ac);
        return 1;
    }

    /* 负值对称 [-2,-1,0,1,2]：mean=0, d=x, 同样 lag1_sum=4, ac=0.5 */
    float neg[5] = {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f};
    bAlgoSignalStats(neg, 5, &stats);
    if (fabsf(stats.lag1_sum - 4.0f) > 0.001f) {
        printf("neg lag1_sum = %f, expected 4.0", stats.lag1_sum);
        return 1;
    }
    ac = bAlgoSignalAutocorr(&stats, 5);
    if (fabsf(ac - 0.5f) > 0.001f) {
        printf("neg autocorr = %f, expected 0.5", ac);
        return 1;
    }

    /* 不对称 [1.5,2.5,3.5,5.5]：
     * mean=3.25, d=[-1.75,-0.75,0.25,2.25]
     * lag1_sum = 1.3125-0.1875+0.5625 = 1.6875
     * lag1_mean = 1.6875/3 = 0.5625
     * m2n = 8.75/4 = 2.1875
     * autocorr = 0.5625/2.1875 = 9/35 ≈ 0.257142857 */
    float asym[4] = {1.5f, 2.5f, 3.5f, 5.5f};
    bAlgoSignalStats(asym, 4, &stats);
    if (fabsf(stats.lag1_sum - 1.6875f) > 0.001f) {
        printf("asym lag1_sum = %f, expected 1.6875", stats.lag1_sum);
        return 1;
    }
    ac = bAlgoSignalAutocorr(&stats, 4);
    if (fabsf(ac - 0.257142857f) > 0.001f) {
        printf("asym autocorr = %f, expected 0.257142857", ac);
        return 1;
    }

    /* 零方差 [3,3,3,3,3] → 0（m2n<=0 防护） */
    float zero[5] = {3.0f, 3.0f, 3.0f, 3.0f, 3.0f};
    bAlgoSignalStats(zero, 5, &stats);
    if (bAlgoSignalAutocorr(&stats, 5) != 0.0f) {
        printf("zero-var autocorr != 0");
        return 1;
    }

    /* n=1 → 0（无 lag-1 可算） */
    float one[1] = {42.0f};
    bAlgoSignalStats(one, 1, &stats);
    if (bAlgoSignalAutocorr(&stats, 1) != 0.0f) {
        printf("n=1 autocorr != 0");
        return 1;
    }
    if (stats.lag1_sum != 0.0f) {
        printf("n=1 lag1_sum = %f, expected 0", stats.lag1_sum);
        return 1;
    }

    /* NULL / n<=1 防护 → 0 */
    if (bAlgoSignalAutocorr(NULL, 5) != 0.0f) {
        printf("NULL autocorr != 0");
        return 1;
    }
    if (bAlgoSignalAutocorr(&stats, 0) != 0.0f) {
        printf("n=0 autocorr != 0");
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 14: 新增统计字段填充（abs_sum / lag1_sum）
 *------------------------------------------------------------*/
int test_signal_stats_new_fields(void)
{
    bAlgoSignalStats_t stats;

    /* [1,2,3,4,5]: abs_sum=15, lag1_sum=4 */
    float known[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    if (bAlgoSignalStats(known, 5, &stats) != 0) {
        printf("bAlgoSignalStats failed");
        return 1;
    }
    if (fabsf(stats.abs_sum - 15.0f) > 0.001f) {
        printf("abs_sum = %f, expected 15.0", stats.abs_sum);
        return 1;
    }
    if (fabsf(stats.lag1_sum - 4.0f) > 0.001f) {
        printf("lag1_sum = %f, expected 4.0", stats.lag1_sum);
        return 1;
    }

    /* [-2,-1,0,1,2]: abs_sum=6, lag1_sum=4 */
    float neg[5] = {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f};
    bAlgoSignalStats(neg, 5, &stats);
    if (fabsf(stats.abs_sum - 6.0f) > 0.001f) {
        printf("neg abs_sum = %f, expected 6.0", stats.abs_sum);
        return 1;
    }
    if (fabsf(stats.lag1_sum - 4.0f) > 0.001f) {
        printf("neg lag1_sum = %f, expected 4.0", stats.lag1_sum);
        return 1;
    }

    /* n=1: lag1_sum 必须为 0 */
    float one[1] = {7.0f};
    bAlgoSignalStats(one, 1, &stats);
    if (stats.lag1_sum != 0.0f) {
        printf("n=1 lag1_sum = %f, expected 0", stats.lag1_sum);
        return 1;
    }
    if (fabsf(stats.abs_sum - 7.0f) > 0.001f) {
        printf("n=1 abs_sum = %f, expected 7.0", stats.abs_sum);
        return 1;
    }

    return 0;
}
