/**
 *!
 * \file        algo_signal.h
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
#ifndef __B_ALGO_SIGNAL_H__
#define __B_ALGO_SIGNAL_H__

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
 * \addtogroup SIGNAL
 * \{
 */

/**
 * \defgroup SIGNAL_Exported_TypesDefinitions
 * \{
 */

/**
 * \brief 信号统计结果结构体（单次遍历计算）
 */
typedef struct
{
    float    sum;     /*!< 总和 */
    float    sq_sum;  /*!< 平方和 */
    float    min;     /*!< 最小值 */
    float    max;     /*!< 最大值 */
    float    m2;      /*!< 二阶中心矩之和（方差 * N） */
    float    m3;      /*!< 三阶中心矩之和（偏度相关） */
    float    m4;      /*!< 四阶中心矩之和（峰度相关） */
    uint32_t zcr;     /*!< 过零率计数 */
    /* 以下为新增字段，追加到结构体末尾以保持 ABI 兼容 */
    float    abs_sum;  /*!< Σ|x[i]| 绝对值之和 */
    float    lag1_sum; /*!< Σ d[i]*d[i+1], i=0..n-2；n<2 时为 0（d = x - mean） */
} bAlgoSignalStats_t;

/**
 * \}
 */

#if (defined(_ALGO_SIGNAL_ENABLE) && (_ALGO_SIGNAL_ENABLE == 1))

/**
 * \defgroup SIGNAL_Exported_Functions
 * \{
 */

/**
 * \brief  计算信号统计量（单次遍历）
 * \param  x     输入信号
 * \param  n     信号长度（≥1）
 * \param  stats 输出统计结果
 * \return 0 = 成功；-1 = 参数非法
 */
int bAlgoSignalStats(const float *x, uint16_t n, bAlgoSignalStats_t *stats);

/**
 * \brief  计算均值
 * \param  stats 统计结果（需先调用 bAlgoSignalStats）
 * \param  n     信号长度（≥1）
 * \return 均值
 */
float bAlgoSignalMean(const bAlgoSignalStats_t *stats, uint16_t n);

/**
 * \brief  计算标准差（总体标准差，除以 N）
 * \param  stats 统计结果
 * \param  n     信号长度（≥1）
 * \return 标准差
 */
float bAlgoSignalStd(const bAlgoSignalStats_t *stats, uint16_t n);

/**
 * \brief  计算 RMS（均方根）
 * \param  stats 统计结果
 * \param  n     信号长度（≥1）
 * \return RMS
 */
float bAlgoSignalRms(const bAlgoSignalStats_t *stats, uint16_t n);

/**
 * \brief  计算过零率（zcr / (n-1)）
 * \param  stats 统计结果
 * \param  n     信号长度（≥2）
 * \return 过零率
 */
float bAlgoSignalZcr(const bAlgoSignalStats_t *stats, uint16_t n);

/**
 * \brief  计算偏度（零方差防护：m2 <= 0 时返回 0）
 * \param  stats 统计结果
 * \param  n     信号长度（≥1）
 * \return 偏度
 */
float bAlgoSignalSkew(const bAlgoSignalStats_t *stats, uint16_t n);

/**
 * \brief  计算峰度（零方差防护：m2 <= 0 时返回 0）
 * \param  stats 统计结果
 * \param  n     信号长度（≥1）
 * \return 峰度（已减 3，与 scipy.stats.kurtosis 默认口径一致）
 */
float bAlgoSignalKurt(const bAlgoSignalStats_t *stats, uint16_t n);

/**
 * \brief  计算峰峰值（max - min）
 * \param  stats 统计结果
 * \return 峰峰值
 */
float bAlgoSignalPtp(const bAlgoSignalStats_t *stats);

/**
 * \brief  计算总体方差（m2 / N，与 Python mean((x-mean)^2) 口径一致）
 * \param  stats 统计结果
 * \param  n     信号长度（≥1）
 * \return 方差；stats==NULL 或 n==0 时返回 0
 */
float bAlgoSignalVariance(const bAlgoSignalStats_t *stats, uint16_t n);

/**
 * \brief  计算绝对值均值（abs_sum / N = mean(|x|)）
 * \param  stats 统计结果
 * \param  n     信号长度（≥1）
 * \return 绝对值均值；stats==NULL 或 n==0 时返回 0
 */
float bAlgoSignalAbsMean(const bAlgoSignalStats_t *stats, uint16_t n);

/**
 * \brief  计算 lag-1 自相关（与 Python 参考实现口径一致）
 *         autocorr = (lag1_sum / (n-1)) / (m2 / n)，其中 d = x - mean
 * \param  stats 统计结果
 * \param  n     信号长度
 * \return 自相关系数；stats==NULL 或 n<=1 或方差<=0 时返回 0
 */
float bAlgoSignalAutocorr(const bAlgoSignalStats_t *stats, uint16_t n);

/**
 * \}
 */

#endif /* _ALGO_SIGNAL_ENABLE */

/**
 * \}
 */

/**
 * \}
 */

#ifdef __cplusplus
}
#endif

#endif /* __B_ALGO_SIGNAL_H__ */

/************************ Copyright (c) 2026 BabyOS Team *****END OF FILE****/
