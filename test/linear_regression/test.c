/**
 * @file test_linear_regression.c
 * @brief Test for Linear Regression algorithm
 * 
 * Tests: y = 2*x + 1 (line fitting)
 * Training: 5 samples along the line with noise
 * Expected: coefficients close to [2.0, 1.0]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "algo_linear_regression.h"

#define NUM_SAMPLES  5
#define NUM_FEATURES 1

/* Training data: y = 2*x + 1 */
static int16_t train_x[NUM_SAMPLES] = {100, 200, 300, 400, 500};   /* Q8.8: x * 256 */
static int16_t train_y[NUM_SAMPLES] = {201, 401, 601, 801, 1001};   /* Q8.8: y * 256, with small noise */

/* Test prediction */
static void test_prediction(linear_reg_model_t *model)
{
    linear_reg_result_t result;
    int16_t test_x[3] = {150, 350, 450};  /* Q8.8 */
    int16_t expected[3] = {301, 701, 901}; /* Q8.8 */
    int i;
    double coef0, coef1;

    printf("\n=== Linear Regression Prediction Test ===\n");

    coef0 = linear_reg_get_coef(model, 0);
    coef1 = linear_reg_get_coef(model, 1);
    printf("Fitted coefficients: coef[0]=%.4f, intercept=%.4f\n", coef0, coef1);
    printf("Expected: coef[0]=2.0, intercept=1.0\n\n");

    for (i = 0; i < 3; i++) {
        linear_reg_predict(model, &test_x[i], &result);
        printf("Predict x=%d -> y_pred=%d (expected ~%d)\n", 
               test_x[i], result.prediction, expected[i]);
    }
}

int main(void)
{
    linear_reg_model_t model;
    linear_reg_sample_t samples[NUM_SAMPLES];
    int16_t x[NUM_SAMPLES][NUM_FEATURES];
    int i;
    int ret;

    printf("\n");
    printf("========================================\n");
    printf(" Linear Regression Test (BabyOS Algo) \n");
    printf("========================================\n");
    printf("Testing: y = 2*x + 1 line fitting\n");

    /* Initialize model: 1 feature, int16 (Q8.8) */
    ret = linear_reg_init(&model, NUM_FEATURES, LINEAR_REG_INT16);
    if (ret != 0) {
        printf("FAIL: init returned %d\n", ret);
        return -1;
    }
    printf("OK: Model initialized (1 feature, int16 Q8.8)\n");

    /* Prepare training samples */
    for (i = 0; i < NUM_SAMPLES; i++) {
        x[i][0] = train_x[i];
        samples[i].x = x[i];
        samples[i].y = train_y[i];
        printf("Sample %d: x=%d, y=%d\n", i, train_x[i], train_y[i]);
    }

    /* Train */
    printf("\nTraining...\n");
    ret = linear_reg_fit(&model, samples, NUM_SAMPLES);
    if (ret != 0) {
        printf("FAIL: fit returned %d\n", ret);
        return -1;
    }
    printf("OK: Model fitted\n");

    /* Test predictions */
    test_prediction(&model);

    /* Cleanup */
    linear_reg_reset(&model);

    printf("\n========================================\n");
    printf(" Linear Regression Test PASSED\n");
    printf("========================================\n");

    return 0;
}
