/**
 * \file        algo_e2e_test_automl.h
 * \brief        e2e_test_automl 自动导出模型 — 随机森林（BabyOS AutoML）
 * \date        2026-09-29 17:00 UTC
 */
#ifndef _ALGO_E2E_TEST_AUTOML_H_
#define _ALGO_E2E_TEST_AUTOML_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALGO_E2E_TEST_AUTOML_ERR_ARG   (-1)   /* 指针为 NULL，或 n_features/n 不等于烘焙常量 */
#define ALGO_E2E_TEST_AUTOML_ERR_NOMEM (-2)   /* bMalloc 分配失败 */

#define ALGO_E2E_TEST_AUTOML_N_FEATURES (5)   /* 烘焙：特征向量维度 */
#define ALGO_E2E_TEST_AUTOML_N_CLASSES  (3)   /* 烘焙：类别数 */
#define ALGO_E2E_TEST_AUTOML_WIN_LEN    (100)   /* 烘焙：滑窗长度（采样点） */
/* 类别映射（FR-2.5）：id 永不重用，已删除标签槽位为 "__deleted__" */
extern const char * const algo_e2e_test_automl_class_names[ALGO_E2E_TEST_AUTOML_N_CLASSES];

/**
 * \brief  模型推理：特征向量 → 类别 id
 * \param  features   特征向量（未归一化，归一化已烘焙进内部，FR-8.4）
 * \param  n_features 必须等于 ALGO_E2E_TEST_AUTOML_N_FEATURES
 * \param  proba_out  NULL 则只返回 id；否则缓冲区必须 ≥ N_CLASSES 个 float
 * \return ≥0 = 类别 id；<0 = 错误码
 * \note   调用上下文（R-INT-7）：任务上下文、于 bInit() 之后调用，不可在中断中调用
 */
int algo_e2e_test_automl_predict(const float *features, uint32_t n_features, float *proba_out);

/**
 * \brief  时序特征提取：N 点通道缓冲 → 特征向量（与 predict 的 features 同序同口径）
 * \param  ch_buf 通道分离布局：[ch0_N 点][ch1_N 点]...（通道序见下）
 * \param  n      必须等于 ALGO_E2E_TEST_AUTOML_WIN_LEN
 * \param  out    缓冲区必须 ≥ N_FEATURES 个 float
 * \return 0 = 成功；<0 = 错误码
 * \note   内部零堆分配；通道顺序：accel_x, accel_y, accel_z
 */
int algo_e2e_test_automl_feat_extract(const float *ch_buf, uint32_t n, float *out);

#ifdef __cplusplus
}
#endif

#endif
