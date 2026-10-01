/**
 * \file        test_algo_fft.c
 * \brief        BabyOS algo_fft 预置原语测试
 * \date        2026-10-01
 *
 * 测试 FFT 原语：
 * 1. bAlgoFft 基本功能（正弦信号频谱峰值）
 * 2. bAlgoFftMagnitude 幅度谱
 * 3. bAlgoFftCentroid 频谱质心
 * 4. bAlgoFftEnergy 频谱能量
 * 5. bAlgoFftDominantFreq 主频检测
 * 6. bAlgoFftBandRatio 频带比
 * 7. bAlgoFftGenTwiddle / bAlgoFftGenBitReverse 旋转因子/位反转表生成
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "algo_fft.h"

#define TEST_N  64
#define TEST_FS 1000.0f

/*------------------------------------------------------------
 * 测试数据
 *------------------------------------------------------------*/
static float test_re[TEST_N];
static float test_im[TEST_N];
static float test_mag[TEST_N / 2 + 1];
static float tw_re[TEST_N / 2];
static float tw_im[TEST_N / 2];
static uint16_t rev[TEST_N];

static void generate_sine(float freq, float amp)
{
    for (int i = 0; i < TEST_N; i++) {
        float t = (float)i / TEST_FS;
        test_re[i] = amp * sinf(2.0f * 3.14159265f * freq * t);
        test_im[i] = 0.0f;
    }
}

static void setup_fft_tables(void)
{
    bAlgoFftGenTwiddle(tw_re, tw_im, TEST_N);
    bAlgoFftGenBitReverse(rev, TEST_N);
}

/*------------------------------------------------------------
 * 测试 1: bAlgoFft 正弦信号频谱峰值
 *------------------------------------------------------------*/
int test_fft_sine_peak(void)
{
    float freq = 100.0f;
    float expected_bin = (float)(freq / TEST_FS * TEST_N);
    int expected_idx = (int)expected_bin;

    generate_sine(freq, 1.0f);
    setup_fft_tables();

    /* 执行 FFT */
    int ret = bAlgoFft(test_re, test_im, TEST_N, tw_re, tw_im, rev);
    if (ret != 0) {
        printf("bAlgoFft returned %d, expected 0", ret);
        return 1;
    }

    /* 计算幅度谱 */
    ret = bAlgoFftMagnitude(test_re, test_im, test_mag, TEST_N);
    if (ret != 0) {
        printf("bAlgoFftMagnitude returned %d, expected 0", ret);
        return 1;
    }

    /* 寻找峰值 */
    int peak_idx = 0;
    float peak_val = test_mag[0];
    for (int i = 1; i < TEST_N / 2 + 1; i++) {
        if (test_mag[i] > peak_val) {
            peak_val = test_mag[i];
            peak_idx = i;
        }
    }

    /* 峰值应出现在预期 bin 附近（允许 ±1 误差） */
    if (abs(peak_idx - expected_idx) > 1) {
        printf("peak at bin %d (val=%f), expected bin %d (freq=%f)",
               peak_idx, peak_val, expected_idx, freq);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 2: bAlgoFft 参数校验
 *------------------------------------------------------------*/
int test_fft_null_params(void)
{
    generate_sine(100.0f, 1.0f);
    setup_fft_tables();

    /* NULL 输入应返回错误 */
    int ret = bAlgoFft(NULL, test_im, TEST_N, tw_re, tw_im, rev);
    if (ret != -1) {
        printf("NULL re returned %d, expected -1", ret);
        return 1;
    }

    ret = bAlgoFft(test_re, NULL, TEST_N, tw_re, tw_im, rev);
    if (ret != -1) {
        printf("NULL im returned %d, expected -1", ret);
        return 1;
    }

    /* 非 2 的幂次长度应返回错误 */
    ret = bAlgoFft(test_re, test_im, 100, tw_re, tw_im, rev);
    if (ret != -1) {
        printf("n=100 returned %d, expected -1", ret);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 3: bAlgoFftCentroid
 *------------------------------------------------------------*/
int test_fft_centroid(void)
{
    /* 构造纯直流信号：所有能量集中在 bin 0 */
    for (int i = 0; i < TEST_N; i++) {
        test_re[i] = 1.0f;
        test_im[i] = 0.0f;
    }
    setup_fft_tables();

    int ret = bAlgoFft(test_re, test_im, TEST_N, tw_re, tw_im, rev);
    if (ret != 0) {
        printf("bAlgoFft returned %d", ret);
        return 1;
    }

    ret = bAlgoFftMagnitude(test_re, test_im, test_mag, TEST_N);
    if (ret != 0) {
        printf("bAlgoFftMagnitude returned %d", ret);
        return 1;
    }

    float centroid = bAlgoFftCentroid(test_mag, TEST_N, TEST_FS);
    /* 直流信号频谱质心应接近 0 */
    if (centroid > 1.0f) {
        printf("centroid = %f, expected ~0", centroid);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 4: bAlgoFftEnergy
 *------------------------------------------------------------*/
int test_fft_energy(void)
{
    /* 构造已知信号：全部能量在 bin 0 */
    for (int i = 0; i < TEST_N; i++) {
        test_re[i] = 1.0f;
        test_im[i] = 0.0f;
    }
    setup_fft_tables();

    int ret = bAlgoFft(test_re, test_im, TEST_N, tw_re, tw_im, rev);
    if (ret != 0) {
        printf("bAlgoFft returned %d", ret);
        return 1;
    }

    ret = bAlgoFftMagnitude(test_re, test_im, test_mag, TEST_N);
    if (ret != 0) {
        printf("bAlgoFftMagnitude returned %d", ret);
        return 1;
    }

    float energy = bAlgoFftEnergy(test_mag, TEST_N);
    /* 直流信号：bin 0 幅度 = N * amplitude = 64，能量 = 64^2 = 4096 */
    float expected = (float)TEST_N * (float)TEST_N;  /* N^2 */
    if (fabsf(energy - expected) > expected * 0.05f) {
        printf("energy = %f, expected %f", energy, expected);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 5: bAlgoFftDominantFreq
 *------------------------------------------------------------*/
int test_fft_dominant_freq(void)
{
    float freq = 150.0f;
    generate_sine(freq, 1.0f);
    setup_fft_tables();

    int ret = bAlgoFft(test_re, test_im, TEST_N, tw_re, tw_im, rev);
    if (ret != 0) {
        printf("bAlgoFft returned %d", ret);
        return 1;
    }

    ret = bAlgoFftMagnitude(test_re, test_im, test_mag, TEST_N);
    if (ret != 0) {
        printf("bAlgoFftMagnitude returned %d", ret);
        return 1;
    }

    float dom_freq = bAlgoFftDominantFreq(test_mag, TEST_N, TEST_FS);
    /* 频率分辨率 = FS/N = 1000/64 ≈ 15.625 Hz，允许 ±1 bin 误差 */
    float freq_resolution = TEST_FS / (float)TEST_N;
    if (fabsf(dom_freq - freq) > freq_resolution * 1.5f) {
        printf("dominant_freq = %f, expected %f (±%f)",
               dom_freq, freq, freq_resolution);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 6: bAlgoFftBandRatio
 *------------------------------------------------------------*/
int test_fft_band_ratio(void)
{
    /* 构造 100Hz 信号，检查 [0,5] 与 [5,33] 频带的能量分布 */
    generate_sine(100.0f, 1.0f);
    setup_fft_tables();

    int ret = bAlgoFft(test_re, test_im, TEST_N, tw_re, tw_im, rev);
    if (ret != 0) {
        printf("bAlgoFft returned %d", ret);
        return 1;
    }

    ret = bAlgoFftMagnitude(test_re, test_im, test_mag, TEST_N);
    if (ret != 0) {
        printf("bAlgoFftMagnitude returned %d", ret);
        return 1;
    }

    /* 100Hz 在 bin 6.4 附近，所以 [0,5] 频带能量应很低 */
    float low_band = bAlgoFftBandRatio(test_mag, TEST_N, 0, 5);
    float high_band = bAlgoFftBandRatio(test_mag, TEST_N, 5, 33);

    /* 高频段能量应远高于低频段 */
    if (low_band > 0.3f || high_band < 0.5f) {
        printf("low_band = %f, high_band = %f (expected low < 0.3, high > 0.5)",
               low_band, high_band);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 7: bAlgoFftGenTwiddle
 *------------------------------------------------------------*/
int test_fft_gen_twiddle(void)
{
    int n = 8;
    float tw_re_local[4];
    float tw_im_local[4];

    int ret = bAlgoFftGenTwiddle(tw_re_local, tw_im_local, n);
    if (ret != 0) {
        printf("bAlgoFftGenTwiddle returned %d, expected 0", ret);
        return 1;
    }

    /* tw_re[0] 应为 cos(0) = 1.0 */
    if (fabsf(tw_re_local[0] - 1.0f) > 0.001f) {
        printf("tw_re[0] = %f, expected 1.0", tw_re_local[0]);
        return 1;
    }

    /* tw_im[0] 应为 -sin(0) = 0.0 */
    if (fabsf(tw_im_local[0] - 0.0f) > 0.001f) {
        printf("tw_im[0] = %f, expected 0.0", tw_im_local[0]);
        return 1;
    }

    /* tw_re[1] 应为 cos(2π/8) = cos(π/4) ≈ 0.7071 */
    if (fabsf(tw_re_local[1] - 0.7071f) > 0.001f) {
        printf("tw_re[1] = %f, expected 0.7071", tw_re_local[1]);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 8: bAlgoFftGenBitReverse
 *------------------------------------------------------------*/
int test_fft_gen_bit_reverse(void)
{
    int n = 8;
    uint16_t rev_local[8];

    int ret = bAlgoFftGenBitReverse(rev_local, n);
    if (ret != 0) {
        printf("bAlgoFftGenBitReverse returned %d, expected 0", ret);
        return 1;
    }

    /* n=8 位反转表：
     * 0 (000) -> 0 (000)
     * 1 (001) -> 4 (100)
     * 2 (010) -> 2 (010)
     * 3 (011) -> 6 (110)
     * 4 (100) -> 1 (001)
     * 5 (101) -> 5 (101)
     * 6 (110) -> 3 (011)
     * 7 (111) -> 7 (111)
     */
    uint16_t expected[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    for (int i = 0; i < n; i++) {
        if (rev_local[i] != expected[i]) {
            printf("rev[%d] = %d, expected %d", i, rev_local[i], expected[i]);
            return 1;
        }
    }

    return 0;
}
