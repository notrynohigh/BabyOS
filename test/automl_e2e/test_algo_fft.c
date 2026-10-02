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

/*------------------------------------------------------------
 * 测试 9: bAlgoFftDominantFreq — argmax 必须包含直流 bin0（单元级）
 * 契约：与 numpy argmax(spec) 一致，bin0 参与比较，平局取较小索引
 *------------------------------------------------------------*/
int test_fft_dominant_freq_includes_dc(void)
{
    float mag[TEST_N / 2 + 1];
    int m = TEST_N / 2 + 1;
    float dom;

    /* 用例 1: bin0 严格最大 → 必须返回 0 Hz
     * 若搜索从 i=1 且 best 初值未把 bin0 计入，会误报其他 bin */
    for (int i = 0; i < m; i++) {
        mag[i] = 0.0f;
    }
    mag[0] = 100.0f;
    dom = bAlgoFftDominantFreq(mag, TEST_N, TEST_FS);
    if (fabsf(dom - 0.0f) > 1e-6f) {
        printf("bin0-max dominant_freq = %f, expected 0.0", dom);
        return 1;
    }

    /* 用例 2: bin9 严格最大 → 9 * fs / n = 9*1000/64 = 140.625 Hz */
    for (int i = 0; i < m; i++) {
        mag[i] = 0.0f;
    }
    mag[9] = 50.0f;
    dom = bAlgoFftDominantFreq(mag, TEST_N, TEST_FS);
    float expected = 9.0f * TEST_FS / (float)TEST_N;
    if (fabsf(dom - expected) > 1e-4f) {
        printf("bin9-max dominant_freq = %f, expected %f", dom, expected);
        return 1;
    }

    /* 用例 3: bin0 与 bin9 平局 → numpy argmax 取首次出现（较小索引）→ 0 Hz
     * 若实现用 >= 或从 i=1 且 best=9 初始化，会误报 bin9 */
    for (int i = 0; i < m; i++) {
        mag[i] = 0.0f;
    }
    mag[0] = 50.0f;
    mag[9] = 50.0f;
    dom = bAlgoFftDominantFreq(mag, TEST_N, TEST_FS);
    if (fabsf(dom - 0.0f) > 1e-6f) {
        printf("tie dominant_freq = %f, expected 0.0 (bin0 wins ties)", dom);
        return 1;
    }

    /* 用例 4: 全零谱 → 0 */
    for (int i = 0; i < m; i++) {
        mag[i] = 0.0f;
    }
    dom = bAlgoFftDominantFreq(mag, TEST_N, TEST_FS);
    if (fabsf(dom - 0.0f) > 1e-6f) {
        printf("zero-spectrum dominant_freq = %f, expected 0.0", dom);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 10: DC 占优混合信号 — 端到端（真实 FFT）
 * x[i] = 10.0 + 0.5*sin(2π*150*i/fs)
 * DC bin 幅度 ≈ 10*N = 640，正弦 bin 幅度 ≈ 0.5*N/2 ≈ 16 → bin0 为 argmax
 * 期望 dominant_freq ≈ 0；若 argmax 跳过 bin0 会误报 ~150Hz
 *------------------------------------------------------------*/
int test_fft_dominant_freq_dc_mixed(void)
{
    for (int i = 0; i < TEST_N; i++) {
        float t = (float)i / TEST_FS;
        test_re[i] = 10.0f + 0.5f * sinf(2.0f * 3.14159265f * 150.0f * t);
        test_im[i] = 0.0f;
    }
    setup_fft_tables();

    int ret = bAlgoFft(test_re, test_im, TEST_N, tw_re, tw_im, rev);
    if (ret != 0) {
        printf("bAlgoFft returned %d, expected 0", ret);
        return 1;
    }
    ret = bAlgoFftMagnitude(test_re, test_im, test_mag, TEST_N);
    if (ret != 0) {
        printf("bAlgoFftMagnitude returned %d, expected 0", ret);
        return 1;
    }

    /* 先验证 bin0 确实是 argmax（与 Python argmax(spec) 口径一致） */
    int peak = 0;
    for (int i = 1; i < TEST_N / 2 + 1; i++) {
        if (test_mag[i] > test_mag[peak]) {
            peak = i;
        }
    }
    if (peak != 0) {
        printf("argmax bin = %d (mag=%f), expected 0 (mag=%f)",
               peak, test_mag[peak], test_mag[0]);
        return 1;
    }

    float dom = bAlgoFftDominantFreq(test_mag, TEST_N, TEST_FS);
    float freq_resolution = TEST_FS / (float)TEST_N;
    if (dom > freq_resolution * 0.5f) {
        printf("dc-mixed dominant_freq = %f, expected ~0 (DC dominant)", dom);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 11: 纯直流信号 — 主频必须为 0
 *------------------------------------------------------------*/
int test_fft_dominant_freq_pure_dc(void)
{
    for (int i = 0; i < TEST_N; i++) {
        test_re[i] = 1.0f;
        test_im[i] = 0.0f;
    }
    setup_fft_tables();

    int ret = bAlgoFft(test_re, test_im, TEST_N, tw_re, tw_im, rev);
    if (ret != 0) {
        printf("bAlgoFft returned %d, expected 0", ret);
        return 1;
    }
    ret = bAlgoFftMagnitude(test_re, test_im, test_mag, TEST_N);
    if (ret != 0) {
        printf("bAlgoFftMagnitude returned %d, expected 0", ret);
        return 1;
    }

    float dom = bAlgoFftDominantFreq(test_mag, TEST_N, TEST_FS);
    if (fabsf(dom - 0.0f) > 1e-6f) {
        printf("pure-DC dominant_freq = %f, expected 0.0", dom);
        return 1;
    }

    return 0;
}

/*------------------------------------------------------------
 * 测试 12: 纯正弦（无 DC）— 主频仍正确，且 bin0 不是 argmax
 *------------------------------------------------------------*/
int test_fft_dominant_freq_pure_sine(void)
{
    float freq = 100.0f;  /* 100*64/1000 = 6.4 bin（非整数，含泄漏） */
    generate_sine(freq, 1.0f);  /* offset=0，无直流分量 */
    setup_fft_tables();

    int ret = bAlgoFft(test_re, test_im, TEST_N, tw_re, tw_im, rev);
    if (ret != 0) {
        printf("bAlgoFft returned %d, expected 0", ret);
        return 1;
    }
    ret = bAlgoFftMagnitude(test_re, test_im, test_mag, TEST_N);
    if (ret != 0) {
        printf("bAlgoFftMagnitude returned %d, expected 0", ret);
        return 1;
    }

    /* 无 DC 时 argmax 不应落在 bin0 */
    int peak = 0;
    for (int i = 1; i < TEST_N / 2 + 1; i++) {
        if (test_mag[i] > test_mag[peak]) {
            peak = i;
        }
    }
    if (peak == 0) {
        printf("pure-sine argmax bin = 0, expected non-DC bin");
        return 1;
    }

    float dom = bAlgoFftDominantFreq(test_mag, TEST_N, TEST_FS);
    float freq_resolution = TEST_FS / (float)TEST_N;
    if (fabsf(dom - freq) > freq_resolution * 1.5f) {
        printf("pure-sine dominant_freq = %f, expected %f (±%f)",
               dom, freq, freq_resolution);
        return 1;
    }

    return 0;
}
