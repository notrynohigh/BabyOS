/**
 *!
 * \file        algo_ml.h
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
#ifndef __B_ALGO_ML_H__
#define __B_ALGO_ML_H__

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
 * \addtogroup ML
 * \{
 */

/**
 * \defgroup ML_Exported_Defines
 * \{
 */

/**
 * \brief 树遍历深度契约：单条根→叶路径允许的最大分裂数。
 *        AutoML 生成器在导出期断言训练树深度 ≤ 本值，超限拒绝导出，
 *        固件运行时永不越界；遍历步数超限时 bAlgoMlTreePredict 返回 -2。
 */
#define ALGO_ML_TREE_MAX_DEPTH 128

/**
 * \}
 */

#if (defined(_ALGO_ML_ENABLE) && (_ALGO_ML_ENABLE == 1))
/**
 * \defgroup ML_Exported_TypesDefinitions
 * \{
 */

/**
 * \brief 决策树节点（生成器 ↔ BabyOS 模块的跨侧契约，节点序 = sklearn 原始节点序）。
 *        节点索引/特征索引合法性由调用方（生成代码）保证，本模块不做防御性检查。
 */
typedef struct
{
    int16_t feature;   /*!< 分裂特征索引；<0 = 叶节点 */
    int16_t left;      /*!< 左子节点索引（(double)xf[feature] <= threshold 走向）；叶节点为 -1 */
    int16_t right;     /*!< 右子节点索引；叶节点为 -1 */
    int16_t proba_off; /*!< 叶概率在模型 proba 表中的行偏移（= 按节点索引升序的叶序号）；非叶节点为 -1 */
    double  threshold; /*!< 分裂阈值（double 烘焙）：x 先 f32 归一化再转 f64 比较，
                            与 sklearn f64 阈值精确对齐 */
} bAlgoMlTreeNode_t;

/**
 * \}
 */

/**
 * \defgroup ML_Exported_Functions
 * \{
 */

/**
 * \brief  最大值的**最小索引**（严格 `>` 扫描，从索引 0 起；并列取最小索引，对齐 sklearn argmax）
 * \param  v 概率/得分向量
 * \param  n 元素个数（≥1）
 * \return 最大值的索引
 */
int bAlgoMlArgmax(const float *v, uint32_t n);

/**
 * \brief  softmax：先减 max 再 exp（防溢出），exp 结果按 f32 顺序累加求和，逐元素相除归一
 * \param  dst 输出缓冲（可与 src 原地）
 * \param  src 输入向量
 * \param  n   元素个数（≥1）
 * \return 0
 * \note   累加序与 AutoML 参考实现（reference.py _softmax_f32）逐操作一致；
 *         编译需保证浮点表达式不被重排（gcc: -ffp-contract=off）
 */
int bAlgoMlSoftmax(float *dst, const float *src, uint32_t n);

/**
 * \brief  f32 顺序累加点积：acc = f32(acc + f32(a[i]*b[i]))，严禁编译器重排
 * \param  a 向量 a
 * \param  b 向量 b
 * \param  n 元素个数
 * \return 点积结果
 */
float bAlgoMlDot(const float *a, const float *b, uint32_t n);

/**
 * \brief  归一化：dst[i] = (src[i] - offset[i]) * inv_scale[i]，全 f32。
 *         inv_scale 为调用方以 f64 求倒数再 f32 烘焙的预烘焙值（FR-8.4）
 * \param  dst       输出缓冲（可与 src 原地）
 * \param  src       原始特征向量
 * \param  offset    偏移量表（长度 n）
 * \param  inv_scale 倒数尺度表（长度 n）
 * \param  n         元素个数
 */
void bAlgoMlNormalize(float *dst, const float *src, const float *offset,
                      const float *inv_scale, uint32_t n);

/**
 * \brief  ReLU：x > 0.0f 取 x，否则 0.0f（MLP 隐层激活，FR-6.2 钉死 relu）
 */
float bAlgoMlRelu(float x);

/**
 * \brief  Sigmoid：1/(1+expf(-z))，**不裁剪**——expf 上溢得 inf 时结果自然为 0，
 *         与 numpy float32 参考行为逐位对齐（与参考实现差异 ≤1 ulp，容差内一致）
 */
float bAlgoMlSigmoid(float z);

/**
 * \brief  Exp：直接包装 expf，留统一替换近似实现的位置（v2 定点化锚点）。
 *         与 numpy f32 exp 差异 ≤1 ulp，容差内一致
 */
float bAlgoMlExp(float x);

/**
 * \brief  决策树迭代遍历（无递归）：自根节点按 `(double)xf[feature] <= threshold`
 *         逐层下行，返回叶节点的 proba_off（≥0，模型以 proba[off*n_classes + k]
 *         取各类概率）
 * \param  nodes     节点表（节点序 = sklearn 原始节点序）
 * \param  n_nodes   节点个数（≥1）
 * \param  xf        已归一化的特征向量
 * \param  n_features 特征向量维度（仅用于契约记录；特征索引合法性由调用方保证）
 * \return ≥0 = 叶 proba_off；-1 = 参数非法（NULL 或 n_nodes 为 0）；
 *          -2 = 遍历深度超过 ALGO_ML_TREE_MAX_DEPTH（异常树，生成器导出期已拦截）
 */
int bAlgoMlTreePredict(const bAlgoMlTreeNode_t *nodes, uint32_t n_nodes,
                       const float *xf, uint32_t n_features);

/**
 * \}
 */
#endif

/**
 * \}
 */

/**
 * \}
 */

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2026 BabyOS Team *****END OF FILE****/
