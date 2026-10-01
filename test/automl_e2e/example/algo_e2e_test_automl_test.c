/**
 * \file        algo_e2e_test_automl_test.c
 * \brief        BabyOS 集成示例：注册轮询任务周期性推理（BOS_REG_POLLING_FUNC）
 * \date        2026-09-29 17:00 UTC
 *
 * 用法：把本文件加入工程编译，确保 bExec() 在主循环中被调用。
 * 特征数据源（传感器采集/N 点滑窗填充 ch_buf）由用户接入。
 */
#include "b_os.h"
#include "algo_e2e_test_automl.h"

static float s_ch_buf[3][ALGO_E2E_TEST_AUTOML_WIN_LEN];
static float s_features[ALGO_E2E_TEST_AUTOML_N_FEATURES];

PT_THREAD(e2e_test_automl_demo_task)(struct pt *pt, void *arg)
{
    (void)arg;
    PT_BEGIN(pt);
    while (1)
    {
        /* TODO: 用户接入 —— 采集 3 通道 × ALGO_E2E_TEST_AUTOML_WIN_LEN 点填入 s_ch_buf */
        if (algo_e2e_test_automl_feat_extract(&s_ch_buf[0][0], ALGO_E2E_TEST_AUTOML_WIN_LEN, s_features) == 0)
        {
            int id = algo_e2e_test_automl_predict(s_features, ALGO_E2E_TEST_AUTOML_N_FEATURES, NULL);
            if (id >= 0)
            {
                b_log_i("e2e_test_automl: class=%d\r\n", id);
            }
        }
        PT_DELAY_MS(pt, 100);
    }
    PT_END(pt);
}
BOS_REG_POLLING_FUNC(e2e_test_automl_demo_task);
