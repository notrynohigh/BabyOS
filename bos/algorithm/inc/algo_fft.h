/**
 *!
 * \file        algo_fft.h
 * \version     v0.0.1
 * \date        2026/10/01
 * \author      BabyOS Team
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 BabyOS Team
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
#ifndef __B_ALGO_FFT_H__
#define __B_ALGO_FFT_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

/**
 * \addtogroup ALGORITHM
 * \{
 */

/**
 * \addtogroup FFT
 * \{
 */

/**
 * \defgroup FFT_Exported_Defines
 * \{
 */

/**
 * \brief FFT 最大点数契约：支持 2 的幂次 FFT，上限 1024 点
 *        AutoML 生成器在导出期断言 WIN_LEN ≤ 本值，超限拒绝导出
 */
#define ALGO_FFT_MAX_N 1024

/**
 * \}
 */

#if (defined(_ALGO_FFT_ENABLE) && (_ALGO_FFT_ENABLE == 1))

/**
 * \defgroup FFT_Exported_Functions
 * \{
 */

/**
 * \brief  2 的幂次 FFT（radix-2 DIT 蝶形，原地计算）
 * \param  re    实部数组（原地修改）
 * \param  im    虚部数组（原地修改）
 * \param  n     FFT 点数（必须是 2 的幂，且 ≤ ALGO_FFT_MAX_N）
 * \param  tw_re 旋转因子实部表（长度 n/2，预计算烘焙）
 * \param  tw_im 旋转因子虚部表（长度 n/2，预计算烘焙）
 * \param  rev   位反转置换表（长度 n，预计算烘焙）
 * \return 0 = 成功；-1 = 参数非法
 */
int bAlgoFft(float *re, float *im, uint16_t n,
             const float *tw_re, const float *tw_im, const uint16_t *rev);

/**
 * \brief  计算频谱幅度（单边 rfft）
 * \param  re  实部数组（输入）
 * \param  im  虚部数组（输入）
 * \param  mag 幅度输出数组（长度 n/2+1）
 * \param  n   FFT 点数
 * \return 0 = 成功；-1 = 参数非法
 */
int bAlgoFftMagnitude(const float *re, const float *im, float *mag, uint16_t n);

/**
 * \brief  计算频谱重心
 * \param  mag 幅度数组（长度 n/2+1）
 * \param  n   FFT 点数
 * \param  fs  采样率（Hz）
 * \return 频谱重心（Hz）；幅度全零时返回 0
 */
float bAlgoFftCentroid(const float *mag, uint16_t n, float fs);

/**
 * \brief  计算频谱能量
 * \param  mag 幅度数组（长度 n/2+1）
 * \param  n   FFT 点数
 * \return 能量（幅度平方和）
 */
float bAlgoFftEnergy(const float *mag, uint16_t n);

/**
 * \brief  计算主频（最大幅度对应的频率）
 * \param  mag 幅度数组（长度 n/2+1）
 * \param  n   FFT 点数
 * \param  fs  采样率（Hz）
 * \return 主频（Hz）
 */
float bAlgoFftDominantFreq(const float *mag, uint16_t n, float fs);

/**
 * \brief  计算频带能量比
 * \param  mag     幅度数组（长度 n/2+1）
 * \param  n       FFT 点数
 * \param  band_lo 频带起始 bin（包含）
 * \param  band_hi 频带结束 bin（不包含）
 * \return 频带能量 / 总能量；总能量为零时返回 0
 */
float bAlgoFftBandRatio(const float *mag, uint16_t n, uint16_t band_lo, uint16_t band_hi);

/**
 * \brief  生成旋转因子表（预计算烘焙，避免运行时计算）
 * \param  tw_re 实部输出表（长度 n/2）
 * \param  tw_im 虚部输出表（长度 n/2）
 * \param  n     FFT 点数（必须是 2 的幂）
 * \return 0 = 成功；-1 = 参数非法
 */
int bAlgoFftGenTwiddle(float *tw_re, float *tw_im, uint16_t n);

/**
 * \brief  生成位反转置换表（预计算烘焙）
 * \param  rev 输出表（长度 n）
 * \param  n   FFT 点数（必须是 2 的幂）
 * \return 0 = 成功；-1 = 参数非法
 */
int bAlgoFftGenBitReverse(uint16_t *rev, uint16_t n);

/**
 * \}
 */

#endif /* _ALGO_FFT_ENABLE */

/**
 * \}
 */

/**
 * \}
 */

#ifdef __cplusplus
}
#endif

#endif /* __B_ALGO_FFT_H__ */

/************************ Copyright (c) 2026 BabyOS Team *****END OF FILE****/
