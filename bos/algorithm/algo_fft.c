/**
 *!
 * \file        algo_fft.c
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
#if !defined(_ALGO_FFT_ENABLE)
/* 命令行未定义时先取工程配置，再解析头文件门控（自检锚点场景由 -D 注入，不依赖本头） */
#include "b_config.h"
#endif

#include "inc/algo_fft.h"

#if (defined(_ALGO_FFT_ENABLE) && (_ALGO_FFT_ENABLE == 1))

#include <math.h>
#include <stddef.h>

/**
 * \addtogroup ALGORITHM
 * \{
 */

/**
 * \addtogroup FFT
 * \{
 */

/**
 * \defgroup FFT_Private_Functions
 * \{
 */

/**
 * \brief  检查 n 是否是 2 的幂
 * \param  n 待检查的值
 * \return 1 = 是 2 的幂；0 = 否
 */
static int _is_power_of_two(uint16_t n)
{
    return (n > 0) && ((n & (n - 1)) == 0);
}

/**
 * \}
 */

/**
 * \defgroup FFT_Exported_Functions
 * \{
 */

int bAlgoFft(float *re, float *im, uint16_t n,
             const float *tw_re, const float *tw_im, const uint16_t *rev)
{
    uint16_t i, j, len, half;

    if (re == NULL || im == NULL || tw_re == NULL || tw_im == NULL || rev == NULL)
    {
        return -1;
    }
    if (!_is_power_of_two(n) || n > ALGO_FFT_MAX_N)
    {
        return -1;
    }

    /* 位反转置换 */
    for (i = 0; i < n; i++)
    {
        j = rev[i];
        if (j > i)
        {
            float t;
            t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

    /* 迭代 DIT 蝶形：tw 索引 = j * (n / len) */
    for (len = 2; len <= n; len <<= 1)
    {
        half = len >> 1;
        for (i = 0; i < n; i += len)
        {
            for (j = 0; j < half; j++)
            {
                uint32_t tw = (uint32_t)j * (n / len);
                uint16_t a  = (uint16_t)(i + j);
                uint16_t b  = (uint16_t)(i + j + half);
                float    wr = tw_re[tw];
                float    wi = tw_im[tw];
                float    tr = re[b] * wr - im[b] * wi;
                float    ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] = re[a] + tr;
                im[a] = im[a] + ti;
            }
        }
    }

    return 0;
}

int bAlgoFftMagnitude(const float *re, const float *im, float *mag, uint16_t n)
{
    uint16_t i;
    uint16_t m;

    if (re == NULL || im == NULL || mag == NULL || n == 0)
    {
        return -1;
    }

    m = (uint16_t)(n / 2 + 1);
    for (i = 0; i < m; i++)
    {
        mag[i] = sqrtf(re[i] * re[i] + im[i] * im[i]);
    }

    return 0;
}

float bAlgoFftCentroid(const float *mag, uint16_t n, float fs)
{
    uint16_t i, m;
    float    num, den;

    if (mag == NULL || n == 0)
    {
        return 0.0f;
    }

    m   = (uint16_t)(n / 2 + 1);
    num = 0.0f;
    den = 0.0f;
    for (i = 0; i < m; i++)
    {
        float f = (float)i * fs / (float)n;
        num += f * mag[i];
        den += mag[i];
    }

    return (den > 0.0f) ? (num / den) : 0.0f;
}

float bAlgoFftEnergy(const float *mag, uint16_t n)
{
    uint16_t i, m;
    float    e;

    if (mag == NULL || n == 0)
    {
        return 0.0f;
    }

    m = (uint16_t)(n / 2 + 1);
    e = 0.0f;
    for (i = 0; i < m; i++)
    {
        e += mag[i] * mag[i];
    }

    return e;
}

float bAlgoFftDominantFreq(const float *mag, uint16_t n, float fs)
{
    uint16_t i, m, best;

    if (mag == NULL || n == 0)
    {
        return 0.0f;
    }

    m    = (uint16_t)(n / 2 + 1);
    best = 0;
    for (i = 1; i < m; i++)
    {
        if (mag[i] > mag[best])
        {
            best = i;
        }
    }

    return (float)best * fs / (float)n;
}

float bAlgoFftBandRatio(const float *mag, uint16_t n, uint16_t band_lo, uint16_t band_hi)
{
    uint16_t i, m;
    float    e, tot;

    if (mag == NULL || n == 0)
    {
        return 0.0f;
    }

    m = (uint16_t)(n / 2 + 1);
    if (band_lo >= m || band_hi > m || band_lo >= band_hi)
    {
        return 0.0f;
    }

    e   = 0.0f;
    tot = 0.0f;
    for (i = band_lo; i < band_hi; i++)
    {
        e += mag[i] * mag[i];
    }
    for (i = 0; i < m; i++)
    {
        tot += mag[i] * mag[i];
    }

    return (tot > 0.0f) ? (e / tot) : 0.0f;
}

int bAlgoFftGenTwiddle(float *tw_re, float *tw_im, uint16_t n)
{
    uint16_t i, half;

    if (tw_re == NULL || tw_im == NULL)
    {
        return -1;
    }
    if (!_is_power_of_two(n) || n > ALGO_FFT_MAX_N)
    {
        return -1;
    }

    half = n / 2;
    for (i = 0; i < half; i++)
    {
        /* 旋转因子：exp(-j * 2 * pi * i / n) */
        float angle = 2.0f * 3.14159265358979f * (float)i / (float)n;
        tw_re[i] = cosf(angle);
        tw_im[i] = -sinf(angle);
    }

    return 0;
}

int bAlgoFftGenBitReverse(uint16_t *rev, uint16_t n)
{
    uint16_t i, bits, r, x, b;

    if (rev == NULL)
    {
        return -1;
    }
    if (!_is_power_of_two(n) || n > ALGO_FFT_MAX_N)
    {
        return -1;
    }

    /* 计算位数：n = 2^bits */
    bits = 0;
    x    = n;
    while (x > 1)
    {
        x >>= 1;
        bits++;
    }

    /* 生成位反转表 */
    for (i = 0; i < n; i++)
    {
        r = 0;
        x = i;
        for (b = 0; b < bits; b++)
        {
            r = (uint16_t)((r << 1) | (x & 1));
            x >>= 1;
        }
        rev[i] = r;
    }

    return 0;
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
