/**
 *!
 * \file        algo_ml.c
 * \version     v0.0.1
 * \date        2026/09/09
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
#if !defined(_ALGO_ML_ENABLE)
/* 命令行未定义时先取工程配置，再解析头文件门控（自检锚点场景由 -D 注入，不依赖本头） */
#include "b_config.h"
#endif

#include "inc/algo_ml.h"

#if (defined(_ALGO_ML_ENABLE) && (_ALGO_ML_ENABLE == 1))

#include <math.h>
#include <stddef.h>

/**
 * \addtogroup ALGORITHM
 * \{
 */

/**
 * \addtogroup ML
 * \{
 */

/**
 * \defgroup ML_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ML_Private_Functions
 * \{
 */

/**
 * \}
 */

/**
 * \addtogroup ML_Exported_Functions
 * \{
 */

int bAlgoMlArgmax(const float *v, uint32_t n)
{
    uint32_t i;
    int      best = 0;
    float    bestv = v[0];
    for (i = 1; i < n; i++)
    {
        if (v[i] > bestv)
        {
            bestv = v[i];
            best  = (int)i;
        }
    }
    return best;
}

int bAlgoMlSoftmax(float *dst, const float *src, uint32_t n)
{
    uint32_t i;
    float    m = src[0];
    float    sum = 0.0f;
    for (i = 1; i < n; i++)
    {
        if (src[i] > m)
        {
            m = src[i];
        }
    }
    for (i = 0; i < n; i++)
    {
        dst[i] = bAlgoMlExp(src[i] - m);
    }
    for (i = 0; i < n; i++)
    {
        sum = sum + dst[i];
    }
    for (i = 0; i < n; i++)
    {
        dst[i] = dst[i] / sum;
    }
    return 0;
}

float bAlgoMlDot(const float *a, const float *b, uint32_t n)
{
    uint32_t i;
    float    acc = 0.0f;
    for (i = 0; i < n; i++)
    {
        acc = acc + a[i] * b[i];
    }
    return acc;
}

void bAlgoMlNormalize(float *dst, const float *src, const float *offset,
                      const float *inv_scale, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
    {
        dst[i] = (src[i] - offset[i]) * inv_scale[i];
    }
}

float bAlgoMlRelu(float x)
{
    return (x > 0.0f) ? x : 0.0f;
}

float bAlgoMlSigmoid(float z)
{
    return 1.0f / (1.0f + bAlgoMlExp(-z));
}

float bAlgoMlExp(float x)
{
    return expf(x);
}

int bAlgoMlTreePredict(const bAlgoMlTreeNode_t *nodes, uint32_t n_nodes,
                       const float *xf, uint32_t n_features)
{
    int16_t  cur = 0;
    uint32_t depth = 0; /* 已越过的分裂数；上限见 ALGO_ML_TREE_MAX_DEPTH */

    (void)n_features; /* 特征索引合法性由调用方（生成代码）保证 */

    if (nodes == NULL || xf == NULL || n_nodes == 0)
    {
        return -1;
    }
    while (nodes[cur].feature >= 0)
    {
        if (depth >= ALGO_ML_TREE_MAX_DEPTH)
        {
            return -2; /* 深度超限（异常树；生成器导出期已拦截） */
        }
        depth++;
        cur = ((double)xf[nodes[cur].feature] <= nodes[cur].threshold)
                  ? nodes[cur].left
                  : nodes[cur].right;
    }
    return nodes[cur].proba_off;
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
