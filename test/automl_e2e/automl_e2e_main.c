/**
 * \file        automl_e2e_main.c
 * \brief        BabyOS AutoML 端到端测试入口
 * \date        2026-09-30
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

/* 测试函数声明 */
extern int test_feat_extract_basic(void);
extern int test_feat_extract_null_ptr(void);
extern int test_feat_extract_wrong_length(void);
extern int test_predict_basic(void);
extern int test_predict_with_proba(void);
extern int test_predict_null_ptr(void);
extern int test_predict_wrong_dim(void);
extern int test_class_names(void);
extern int test_feat_extract_consistency(void);
extern int test_full_pipeline(void);
extern int test_constants(void);
extern int test_algo_signal_basic(void);
extern int test_algo_fft_basic(void);
extern int test_deep_binding(void);

typedef int (*test_func_t)(void);

typedef struct {
    const char *name;
    test_func_t func;
} test_case_t;

static test_case_t tests[] = {
    {"test_constants", test_constants},
    {"test_class_names", test_class_names},
    {"test_algo_signal_basic", test_algo_signal_basic},
    {"test_algo_fft_basic", test_algo_fft_basic},
    {"test_deep_binding", test_deep_binding},
    {"test_feat_extract_basic", test_feat_extract_basic},
    {"test_feat_extract_null_ptr", test_feat_extract_null_ptr},
    {"test_feat_extract_wrong_length", test_feat_extract_wrong_length},
    {"test_feat_extract_consistency", test_feat_extract_consistency},
    {"test_predict_basic", test_predict_basic},
    {"test_predict_with_proba", test_predict_with_proba},
    {"test_predict_null_ptr", test_predict_null_ptr},
    {"test_predict_wrong_dim", test_predict_wrong_dim},
    {"test_full_pipeline", test_full_pipeline},
};

#define NUM_TESTS (sizeof(tests) / sizeof(tests[0]))

int main(void)
{
    int pass = 0;
    int fail = 0;

    printf("\n========================================\n");
    printf("  BabyOS AutoML E2E 测试\n");
    printf("========================================\n\n");

    for (int i = 0; i < NUM_TESTS; i++) {
        printf("  [%d/%d] %s ... ", i + 1, (int)NUM_TESTS, tests[i].name);
        fflush(stdout);

        int result = tests[i].func();
        if (result == 0) {
            printf("✅ PASS\n");
            pass++;
        } else {
            printf("❌ FAIL\n");
            fail++;
        }
    }

    printf("\n========================================\n");
    printf("  测试结果: %d 通过, %d 失败\n", pass, fail);
    printf("========================================\n\n");

    return fail > 0 ? 1 : 0;
}
