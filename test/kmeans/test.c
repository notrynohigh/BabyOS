/**
 * @file test_kmeans.c
 * @brief Test for K-Means clustering algorithm
 * 
 * Tests: 2 clusters in 2D space
 * Cluster 0: points around (1, 1)
 * Cluster 1: points around (10, 10)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "algo_kmeans.h"

#define NUM_SAMPLES  8
#define NUM_CLUSTERS 2
#define FEATURE_DIM  2

/* Test data: 2 clusters */
static int16_t data[NUM_SAMPLES][FEATURE_DIM] = {
    /* Cluster 0: near (1, 1) */
    {1*256, 1*256},   /* (1, 1) */
    {2*256, 1*256},   /* (2, 1) */
    {1*256, 2*256},   /* (1, 2) */
    {2*256, 2*256},   /* (2, 2) */
    /* Cluster 1: near (10, 10) */
    {10*256, 10*256}, /* (10, 10) */
    {11*256, 10*256}, /* (11, 10) */
    {10*256, 11*256}, /* (10, 11) */
    {11*256, 11*256}, /* (11, 11) */
};

int main(void)
{
    kmeans_config_t config;
    kmeans_sample_t samples[NUM_SAMPLES];
    kmeans_cluster_t clusters[NUM_CLUSTERS];
    kmeans_result_t result;
    int i;
    int ret;

    printf("\n");
    printf("========================================\n");
    printf("   K-Means Clustering Test (BabyOS)   \n");
    printf("========================================\n");

    /* Initialize */
    config.k = NUM_CLUSTERS;
    config.max_iterations = 50;
    config.feature_dim = FEATURE_DIM;
    config.type = KMEANS_INT16;
    config.random_seed = 42;

    ret = kmeans_init(&config);
    if (ret != 0) {
        printf("FAIL: init returned %d\n", ret);
        return -1;
    }
    printf("OK: K-Means initialized (k=%d, dim=%d)\n", NUM_CLUSTERS, FEATURE_DIM);

    /* Prepare samples */
    for (i = 0; i < NUM_SAMPLES; i++) {
        samples[i].features = data[i];
        printf("Sample %d: (%.2f, %.2f)\n", 
               i, (float)data[i][0]/256.0f, (float)data[i][1]/256.0f);
    }

    /* Fit */
    printf("\nFitting...\n");
    ret = kmeans_fit(samples, NUM_SAMPLES, clusters);
    if (ret != 0) {
        printf("FAIL: fit returned %d\n", ret);
        return -1;
    }
    printf("OK: K-Means fitted\n");

    /* Print centroids */
    printf("\n=== Centroids ===\n");
    for (i = 0; i < NUM_CLUSTERS; i++) {
        int16_t *c = (int16_t *)clusters[i].centroid;
        printf("Cluster %d: (%.2f, %.2f), count=%d\n",
               clusters[i].id,
               (float)c[0]/256.0f, (float)c[1]/256.0f,
               clusters[i].count);
    }

    /* Predict test points */
    printf("\n=== Prediction Tests ===\n");
    int16_t test_points[4][FEATURE_DIM] = {
        {1*256, 1*256},   /* Should be cluster 0 */
        {10*256, 10*256}, /* Should be cluster 1 */
        {5*256, 5*256},   /* Near boundary */
        {1*256, 10*256},  /* Far from both */
    };

    for (i = 0; i < 4; i++) {
        kmeans_sample_t sp;
        sp.features = test_points[i];
        ret = kmeans_predict(&sp, &result);
        if (ret == 0) {
            printf("Point (%.2f, %.2f) -> Cluster %d (dist=%.4f)\n",
                   (float)test_points[i][0]/256.0f,
                   (float)test_points[i][1]/256.0f,
                   result.cluster_id,
                   (float)result.distance/1000.0f);
        } else {
            printf("Point (%d, %d) -> FAIL %d\n",
                   test_points[i][0], test_points[i][1], ret);
        }
    }

    kmeans_reset();

    printf("\n========================================\n");
    printf("   K-Means Clustering Test PASSED\n");
    printf("========================================\n");

    return 0;
}
