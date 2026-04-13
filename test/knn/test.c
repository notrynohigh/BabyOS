/**
 * @file test_knn.c
 * @brief Test for KNN (K-Nearest Neighbors) algorithm
 * 
 * Tests: 2-class classification in 2D feature space
 * Class 0: points around (1, 1)
 * Class 1: points around (10, 10)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "algo_knn.h"

#define NUM_TRAINING   8
#define NUM_CLASSES    2
#define FEATURE_DIM    2
#define K             3

/* Training data: 2 clusters in 2D space (Q8.8 format) */
static int16_t train_data[NUM_TRAINING][FEATURE_DIM] = {
    /* Class 0: near (1, 1) */
    {1*256, 1*256},   /* (1, 1) */
    {2*256, 1*256},   /* (2, 1) */
    {1*256, 2*256},   /* (1, 2) */
    {2*256, 2*256},   /* (2, 2) */
    /* Class 1: near (10, 10) */
    {10*256, 10*256}, /* (10, 10) */
    {11*256, 10*256}, /* (11, 10) */
    {10*256, 11*256}, /* (10, 11) */
    {11*256, 11*256}, /* (11, 11) */
};

static int16_t train_labels[NUM_TRAINING] = {0, 0, 0, 0, 1, 1, 1, 1};

int main(void)
{
    knn_config_t config;
    knn_sample_t samples[NUM_TRAINING];
    knn_result_t result;
    int i, ret;

    printf("\n");
    printf("========================================\n");
    printf("    KNN (K-Nearest Neighbors) Test     \n");
    printf("========================================\n");

    /* Initialize KNN */
    config.k = K;
    config.dist_type = KNN_DISTANCE_EUCLIDEAN;
    config.data_type = KNN_DATA_TYPE_INT16;
    config.num_classes = NUM_CLASSES;
    config.feature_dim = FEATURE_DIM;

    ret = knn_init(&config);
    if (ret != 0) {
        printf("FAIL: knn_init returned %d\n", ret);
        return -1;
    }
    printf("OK: KNN initialized (k=%d, dim=%d, classes=%d)\n", K, FEATURE_DIM, NUM_CLASSES);
    printf("    Distance: Euclidean, Data type: int16 Q8.8\n");

    /* Prepare training samples */
    for (i = 0; i < NUM_TRAINING; i++) {
        samples[i].label = train_labels[i];
        samples[i].features = train_data[i];
        printf("Sample %d: (%.2f, %.2f) -> Class %d\n", 
               i, 
               (float)train_data[i][0]/256.0f,
               (float)train_data[i][1]/256.0f,
               train_labels[i]);
    }

    /* Train */
    printf("\nTraining...\n");
    ret = knn_train(samples, NUM_TRAINING);
    if (ret != 0) {
        printf("FAIL: knn_train returned %d\n", ret);
        return -1;
    }
    printf("OK: Trained with %d samples\n", knn_get_trained_count());

    /* Test predictions */
    printf("\n=== Prediction Tests ===\n");
    
    struct {
        int16_t features[FEATURE_DIM];
        int16_t expected_class;
        const char *desc;
    } test_cases[] = {
        /* Class 0 tests */
        {{1*256, 1*256}, 0, "near cluster 0 center"},
        {{2*256, 1*256}, 0, "near cluster 0"},
        {{1*256, 2*256}, 0, "near cluster 0"},
        /* Class 1 tests */
        {{10*256, 10*256}, 1, "near cluster 1 center"},
        {{11*256, 10*256}, 1, "near cluster 1"},
        {{10*256, 11*256}, 1, "near cluster 1"},
        /* Boundary tests */
        {{5*256, 5*256}, -1, "boundary (equidistant)"},
        {{1*256, 10*256}, -1, "far from both"},
    };

    int num_tests = sizeof(test_cases) / sizeof(test_cases[0]);
    int passed = 0;

    for (i = 0; i < num_tests; i++) {
        ret = knn_predict(test_cases[i].features, &result);
        if (ret != 0) {
            printf("FAIL: predict returned %d for test %d (%s)\n", ret, i, test_cases[i].desc);
            continue;
        }

        /* For boundary tests, just check prediction is valid */
        int is_correct = (test_cases[i].expected_class == -1) ? 
                         (result.predicted_label >= 0 && result.predicted_label < NUM_CLASSES) :
                         (result.predicted_label == test_cases[i].expected_class);

        printf("Test %d: (%.2f, %.2f) -> Class %d (conf=%d%%) [%s] %s\n",
               i,
               (float)test_cases[i].features[0]/256.0f,
               (float)test_cases[i].features[1]/256.0f,
               result.predicted_label,
               result.confidence,
               test_cases[i].desc,
               is_correct ? "PASS" : "FAIL");

        if (is_correct) passed++;
    }

    knn_reset();

    printf("\n=== Summary ===\n");
    printf("Passed: %d/%d\n", passed, num_tests);

    printf("\n========================================\n");
    if (passed == num_tests) {
        printf("    KNN Test PASSED\n");
    } else {
        printf("    KNN Test FAILED\n");
    }
    printf("========================================\n");

    return (passed == num_tests) ? 0 : -1;
}
