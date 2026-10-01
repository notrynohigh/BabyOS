/**
 *!
 * \file        algo_signal.c
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

/*Includes ----------------------------------------------*/
#if !defined(_ALGO_SIGNAL_ENABLE)
/* 命令行未定义时先取工程配置，再解析头文件门控（自检锚点场景由 -D 注入，不依赖本头） */
#include "b_config.h"
#endif

#include "inc/algo_signal.h"

#if (defined(_ALGO_SIGNAL_ENABLE) && (_ALGO_SIGNAL_ENABLE == 1))

#include <math.h>
#include <stddef.h>

/**
 * \addtogroup ALGORITHM
 * \{
 */

/**
 * \addtogroup SIGNAL
 * \{
 */

/**
 * \defgroup SIGNAL_Exported_Functions
 * \{
 */

int bAlgoSignalStats(const float *x, uint16_t n, bAlgoSignalStats_t *stats)
{
    uint16_t i;
    float    s, q, mn, mx;
    uint32_t zc;
    float    mu, m2, m3, m4;

    if (x == NULL || stats == NULL || n == 0)
    {
        return -1;
    }

    /* 初始化：单元素边界情况 */
    s  = x[0];
    q  = x[0] * x[0];
    mn = x[0];
    mx = x[0];
    zc = 0;

    /* 第一遍：累加和、平方和、极值、过零率 */
    for (i = 1; i < n; i++)
    {
        s += x[i];
        q += x[i] * x[i];
        if (x[i] < mn)
        {
            mn = x[i];
        }
        if (x[i] > mx)
        {
            mx = x[i];
        }
        if (x[i - 1] * x[i] < 0.0f)
        {
            zc++;
        }
    }

    /* 第二遍：中心矩（m2/m3/m4） */
    mu = s / (float)n;
    m2 = 0.0f;
    m3 = 0.0f;
    m4 = 0.0f;
    for (i = 0; i < n; i++)
    {
        float d  = x[i] - mu;
        float d2 = d * d;
        m2 += d2;
        m3 += d2 * d;
        m4 += d2 * d2;
    }

    /* 填充结果 */
    stats->sum    = s;
    stats->sq_sum = q;
    stats->min    = mn;
    stats->max    = mx;
    stats->m2     = m2;
    stats->m3     = m3;
    stats->m4     = m4;
    stats->zcr    = zc;

    return 0;
}

float bAlgoSignalMean(const bAlgoSignalStats_t *stats, uint16_t n)
{
    if (stats == NULL || n == 0)
    {
        return 0.0f;
    }
    return stats->sum / (float)n;
}

float bAlgoSignalStd(const bAlgoSignalStats_t *stats, uint16_t n)
{
    if (stats == NULL || n == 0)
    {
        return 0.0f;
    }
    return sqrtf(stats->m2 / (float)n);
}

float bAlgoSignalRms(const bAlgoSignalStats_t *stats, uint16_t n)
{
    if (stats == NULL || n == 0)
    {
        return 0.0f;
    }
    return sqrtf(stats->sq_sum / (float)n);
}

float bAlgoSignalZcr(const bAlgoSignalStats_t *stats, uint16_t n)
{
    if (stats == NULL || n < 2)
    {
        return 0.0f;
    }
    return (float)stats->zcr / (float)(n - 1);
}

float bAlgoSignalSkew(const bAlgoSignalStats_t *stats, uint16_t n)
{
    float m2n;
    if (stats == NULL || n == 0)
    {
        return 0.0f;
    }
    m2n = stats->m2 / (float)n;
    if (m2n <= 0.0f)
    {
        return 0.0f; /* 零方差防护 */
    }
    return (stats->m3 / (float)n) / powf(m2n, 1.5f);
}

float bAlgoSignalKurt(const bAlgoSignalStats_t *stats, uint16_t n)
{
    float m2n;
    if (stats == NULL || n == 0)
    {
        return 0.0f;
    }
    m2n = stats->m2 / (float)n;
    if (m2n <= 0.0f)
    {
        return 0.0f; /* 零方差防护 */
    }
    /* 峰度 = (m4/N) / (m2/N)^2 - 3（减 3 使正态分布为 0） */
    return (stats->m4 / (float)n) / (m2n * m2n) - 3.0f;
}

float bAlgoSignalPtp(const bAlgoSignalStats_t *stats)
{
    if (stats == NULL)
    {
        return 0.0f;
    }
    return stats->max - stats->min;
}

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

#endif

/************************ Copyright (c) 2026 BabyOS Team *****END OF FILE****/
