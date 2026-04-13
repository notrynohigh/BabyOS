/**
 *!
 * \file        algo_kmeans.c
 * \version     v0.0.1
 * \date        2026/04/13
 * \author      aiclaw
 *******************************************************************************
 * @brief K-Means clustering implementation (Lloyd's algorithm)
 *******************************************************************************
 */
#include "inc/algo_kmeans.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if (defined(_ALGO_KMEANS_ENABLE) && (_ALGO_KMEANS_ENABLE == 1))

/**
 * \addtogroup ALGORITHM
 * \{
 */

/**
 * \addtogroup KMEANS
 * \{
 */

#define KMEANS_MAX_K        16
#define KMEANS_MAX_DIM      64
#define KMEANS_MAX_SAMPLES  512

static kmeans_config_t g_kmeans_config;
static kmeans_cluster_t g_kmeans_centers[KMEANS_MAX_K];
static double g_centroids_double[KMEANS_MAX_K][KMEANS_MAX_DIM];
static double g_temp_sum[KMEANS_MAX_K][KMEANS_MAX_DIM];
static uint16_t g_temp_count[KMEANS_MAX_K];
static uint8_t g_initialized = 0;
static uint8_t g_kmeans_k;

/**
 * @brief Simple pseudo-random number generator (LCG)
 */
static uint32_t g_rand_state;
static uint32_t kmeans_rand(void)
{
    g_rand_state = (g_rand_state * 1103515245 + 12345) & 0x7FFFFFFF;
    return g_rand_state;
}

static void kmeans_srand(uint32_t seed)
{
    g_rand_state = seed;
}

/**
 * @brief Convert sample to double array
 */
static void kmeans_to_double(void *src, double *dst, uint8_t dim, kmeans_type_t type)
{
    int i;
    switch (type) {
        case KMEANS_INT16: {
            int16_t *s = (int16_t *)src;
            for (i = 0; i < dim; i++) dst[i] = (double)s[i] / 256.0;
            break;
        }
        case KMEANS_INT32: {
            int32_t *s = (int32_t *)src;
            for (i = 0; i < dim; i++) dst[i] = (double)s[i] / 65536.0;
            break;
        }
        case KMEANS_FLOAT: {
            float *s = (float *)src;
            for (i = 0; i < dim; i++) dst[i] = (double)s[i];
            break;
        }
    }
}

/**
 * @brief Squared Euclidean distance between two double vectors
 */
static double kmeans_dist_sq(double *a, double *b, uint8_t dim)
{
    double sum = 0.0;
    int i;
    for (i = 0; i < dim; i++) {
        double d = a[i] - b[i];
        sum += d * d;
    }
    return sum;
}

int kmeans_init(kmeans_config_t *config)
{
    if (config == NULL) return -1;
    if (config->k < 2 || config->k > KMEANS_MAX_K) return -2;
    if (config->feature_dim == 0 || config->feature_dim > KMEANS_MAX_DIM) return -3;

    g_kmeans_config = *config;
    g_kmeans_k = config->k;
    g_initialized = 1;

    kmeans_srand(config->random_seed);

    return 0;
}

int kmeans_fit(kmeans_sample_t *samples, uint16_t num_samples, kmeans_cluster_t *clusters)
{
    uint16_t i, j, iter;
    uint8_t dim = g_kmeans_config.feature_dim;
    uint8_t k = g_kmeans_k;
    double (*centroids)[KMEANS_MAX_DIM] = g_centroids_double;
    double min_dist, dist;
    int changed;
    uint16_t min_idx;

    if (!g_initialized) return -1;
    if (samples == NULL || num_samples == 0 || clusters == NULL) return -2;

    /* Initialize centroids using K-Means++ style: pick random samples */
    /* Simple approach: evenly spaced samples (for reproducibility) */
    for (i = 0; i < k; i++) {
        uint16_t idx = (i * num_samples) / k;
        if (idx >= num_samples) idx = num_samples - 1;
        kmeans_to_double(samples[idx].features, centroids[i], dim, g_kmeans_config.type);
    }

    /* Lloyd's algorithm iterations */
    for (iter = 0; iter < g_kmeans_config.max_iterations; iter++) {
        /* Reset accumulators */
        memset(g_temp_sum, 0, sizeof(g_temp_sum));
        memset(g_temp_count, 0, sizeof(g_temp_count));

        /* Assign each sample to nearest centroid */
        changed = 0;
        for (i = 0; i < num_samples; i++) {
            double sample_vec[KMEANS_MAX_DIM];
            kmeans_to_double(samples[i].features, sample_vec, dim, g_kmeans_config.type);

            min_dist = 1e30;
            min_idx = 0;
            for (j = 0; j < k; j++) {
                dist = kmeans_dist_sq(sample_vec, centroids[j], dim);
                if (dist < min_dist) {
                    min_dist = dist;
                    min_idx = j;
                }
            }

            /* Accumulate for centroid update */
            for (j = 0; j < dim; j++) {
                g_temp_sum[min_idx][j] += sample_vec[j];
            }
            g_temp_count[min_idx]++;
            (void)changed;  /* suppress unused warning if never read */
        }

        /* Update centroids */
        for (j = 0; j < k; j++) {
            if (g_temp_count[j] > 0) {
                for (i = 0; i < dim; i++) {
                    centroids[j][i] = g_temp_sum[j][i] / g_temp_count[j];
                }
            }
        }
    }

    /* Final assignment for cluster counts */
    for (j = 0; j < k; j++) {
        g_temp_count[j] = 0;
    }
    for (i = 0; i < num_samples; i++) {
        double sample_vec[KMEANS_MAX_DIM];
        kmeans_to_double(samples[i].features, sample_vec, dim, g_kmeans_config.type);

        min_dist = 1e30;
        min_idx = 0;
        for (j = 0; j < k; j++) {
            dist = kmeans_dist_sq(sample_vec, centroids[j], dim);
            if (dist < min_dist) {
                min_dist = dist;
                min_idx = j;
            }
        }
        g_temp_count[min_idx]++;
    }

    /* Fill output clusters */
    for (j = 0; j < k; j++) {
        clusters[j].id = j;
        clusters[j].centroid = centroids[j];
        clusters[j].count = g_temp_count[j];
        g_kmeans_centers[j] = clusters[j];
    }

    return 0;
}

int kmeans_predict(kmeans_sample_t *sample, kmeans_result_t *result)
{
    double sample_vec[KMEANS_MAX_DIM];
    uint8_t dim = g_kmeans_config.feature_dim;
    uint8_t k = g_kmeans_k;
    double min_dist = 1e30;
    uint8_t min_idx = 0;
    uint8_t j;
    double dist;

    if (!g_initialized) return -1;
    if (sample == NULL || result == NULL) return -2;

    kmeans_to_double(sample->features, sample_vec, dim, g_kmeans_config.type);

    for (j = 0; j < k; j++) {
        dist = kmeans_dist_sq(sample_vec, g_centroids_double[j], dim);
        if (dist < min_dist) {
            min_dist = dist;
            min_idx = j;
        }
    }

    result->cluster_id = min_idx;
    result->distance = (uint32_t)(min_dist * 1000);  /* Scale to avoid float */

    return 0;
}

kmeans_cluster_t *kmeans_get_centroids(void)
{
    return g_kmeans_centers;
}

void kmeans_reset(void)
{
    g_initialized = 0;
    memset(g_centroids_double, 0, sizeof(g_centroids_double));
    memset(g_temp_sum, 0, sizeof(g_temp_sum));
    memset(g_temp_count, 0, sizeof(g_temp_count));
}

/**
 * \}
 */

/**
 * \}
 */

#endif

/************************ Copyright (c) 2026 aiclaw *****END OF FILE****/
