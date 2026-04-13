/**
 *!
 * \file        algo_knn.c
 * \version     v0.0.1
 * \date        2026/04/13
 * \author      aiclaw
 *******************************************************************************
 * @attention
 *
 * KNN (K-Nearest Neighbors) algorithm implementation for embedded systems.
 * Pure C implementation, no dynamic memory allocation.
 *
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "inc/algo_knn.h"
#include <math.h>
#include <stddef.h>

#if (defined(_ALGO_KNN_ENABLE) && (_ALGO_KNN_ENABLE == 1))

/**
 * \addtogroup ALGORITHM
 * \{
 */

/**
 * \addtogroup KNN
 * \{
 */

/**
 * \defgroup KNN_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup KNN_Private_Types
 * \{
 */

/**
 * @brief Internal distance + label record for sorting
 */
typedef struct {
    uint32_t distance;
    int16_t label;
} knn_dist_label_t;

/**
 * \}
 */

/**
 * \defgroup KNN_Private_Variables
 * \{
 */

static knn_config_t g_knn_config;
static knn_sample_t g_knn_samples[KNN_MAX_SAMPLES];
static uint16_t g_knn_num_samples = 0;
static uint8_t g_knn_initialized = 0;

/**
 * \}
 */

/**
 * \defgroup KNN_Private_FunctionPrototypes
 * \{
 */

static void knn_swap(knn_dist_label_t *a, knn_dist_label_t *b);
static void knn_quick_sort(knn_dist_label_t *arr, int left, int right);
static uint32_t knn_abs_diff(int32_t a, int32_t b);

/**
 * \}
 */

/**
 * \defgroup KNN_Private_Functions
 * \{
 */

static void knn_swap(knn_dist_label_t *a, knn_dist_label_t *b)
{
    knn_dist_label_t tmp = *a;
    *a = *b;
    *b = tmp;
}

static void knn_quick_sort(knn_dist_label_t *arr, int left, int right)
{
    int i, j;
    knn_dist_label_t pivot;

    if (left >= right) {
        return;
    }

    pivot = arr[left];
    i = left + 1;
    j = right;

    while (i <= j) {
        while (i <= right && arr[i].distance <= pivot.distance) {
            i++;
        }
        while (j >= left && arr[j].distance > pivot.distance) {
            j--;
        }
        if (i < j) {
            knn_swap(&arr[i], &arr[j]);
        }
    }
    knn_swap(&arr[left], &arr[j]);
    knn_quick_sort(arr, left, j - 1);
    knn_quick_sort(arr, j + 1, right);
}

static uint32_t knn_abs_diff(int32_t a, int32_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

/**
 * @brief Calculate squared Euclidean distance for different data types
 */
static uint32_t knn_calc_dist_euclidean_int16(int16_t *f1, int16_t *f2, uint8_t dim)
{
    uint32_t sum = 0;
    uint8_t i;
    int32_t diff;

    for (i = 0; i < dim; i++) {
        diff = (int32_t)f1[i] - (int32_t)f2[i];
        sum += (uint32_t)(diff * diff);
    }
    return sum;
}

static uint32_t knn_calc_dist_euclidean_int32(int32_t *f1, int32_t *f2, uint8_t dim)
{
    uint64_t sum = 0;
    uint8_t i;
    int64_t diff;

    for (i = 0; i < dim; i++) {
        diff = (int64_t)f1[i] - (int64_t)f2[i];
        sum += (uint64_t)(diff * diff);
    }
    return (uint32_t)(sum > 0xFFFFFFFF ? 0xFFFFFFFF : sum);
}

static uint32_t knn_calc_dist_euclidean_float(float *f1, float *f2, uint8_t dim)
{
    double sum = 0.0;
    uint8_t i;
    double diff;

    for (i = 0; i < dim; i++) {
        diff = (double)f1[i] - (double)f2[i];
        sum += diff * diff;
    }
    return (uint32_t)(sum > 0xFFFFFFFF ? 0xFFFFFFFF : (uint32_t)sum);
}

static uint32_t knn_calc_dist_manhattan_int16(int16_t *f1, int16_t *f2, uint8_t dim)
{
    uint32_t sum = 0;
    uint8_t i;

    for (i = 0; i < dim; i++) {
        sum += knn_abs_diff((int32_t)f1[i], (int32_t)f2[i]);
    }
    return sum;
}

static uint32_t knn_calc_dist_manhattan_int32(int32_t *f1, int32_t *f2, uint8_t dim)
{
    uint32_t sum = 0;
    uint8_t i;

    for (i = 0; i < dim; i++) {
        sum += knn_abs_diff(f1[i], f2[i]);
        if (sum > 0xFFFFFFFF) {
            return 0xFFFFFFFF;
        }
    }
    return sum;
}

static uint32_t knn_calc_dist_manhattan_float(float *f1, float *f2, uint8_t dim)
{
    double sum = 0.0;
    uint8_t i;

    for (i = 0; i < dim; i++) {
        sum += (double)((f1[i] > f2[i]) ? (f1[i] - f2[i]) : (f2[i] - f1[i]));
    }
    return (uint32_t)(sum > 0xFFFFFFFF ? 0xFFFFFFFF : (uint32_t)sum);
}

static uint32_t knn_calc_dist_minkowski_int16(int16_t *f1, int16_t *f2, uint8_t dim)
{
    double sum = 0.0;
    uint8_t i;
    int32_t diff;
    double p = 3.0;  /* Minkowski p=3 */

    for (i = 0; i < dim; i++) {
        diff = (int32_t)f1[i] - (int32_t)f2[i];
        sum += pow((double)diff, p);
    }
    return (uint32_t)(pow(sum, 1.0/p));
}

static uint32_t knn_calc_dist_minkowski_int32(int32_t *f1, int32_t *f2, uint8_t dim)
{
    double sum = 0.0;
    uint8_t i;
    int64_t diff;
    double p = 3.0;

    for (i = 0; i < dim; i++) {
        diff = (int64_t)f1[i] - (int64_t)f2[i];
        sum += pow((double)diff, p);
    }
    return (uint32_t)(pow(sum, 1.0/p));
}

static uint32_t knn_calc_dist_minkowski_float(float *f1, float *f2, uint8_t dim)
{
    double sum = 0.0;
    uint8_t i;
    double diff;
    double p = 3.0;

    for (i = 0; i < dim; i++) {
        diff = (double)f1[i] - (double)f2[i];
        sum += pow(diff > 0 ? diff : -diff, p);
    }
    return (uint32_t)(pow(sum, 1.0/p));
}

/**
 * \}
 */

/**
 * \addtogroup KNN_Exported_Functions
 * \{
 */

int knn_init(knn_config_t *config)
{
    if (config == NULL) {
        return -1;
    }

    if (config->k == 0 || config->k > KNN_MAX_K) {
        return -2;
    }

    if (config->feature_dim == 0 || config->feature_dim > KNN_MAX_FEATURES) {
        return -3;
    }

    if (config->num_classes == 0 || config->num_classes > KNN_MAX_CLASSES) {
        return -4;
    }

    g_knn_config = *config;
    g_knn_num_samples = 0;
    g_knn_initialized = 1;

    return 0;
}

int knn_train(knn_sample_t *samples, uint16_t num_samples)
{
    uint16_t i;

    if (!g_knn_initialized) {
        return -1;
    }

    if (samples == NULL || num_samples == 0) {
        return -2;
    }

    if (num_samples > KNN_MAX_SAMPLES) {
        return -3;
    }

    /* Validate labels and copy samples */
    for (i = 0; i < num_samples; i++) {
        if (samples[i].label < 0 || samples[i].label >= g_knn_config.num_classes) {
            return -4;  /* Invalid label */
        }
        if (samples[i].features == NULL) {
            return -5;  /* NULL features pointer */
        }
        g_knn_samples[i] = samples[i];
    }

    g_knn_num_samples = num_samples;

    return 0;
}

int knn_predict(void *query_features, knn_result_t *result)
{
    knn_dist_label_t dist_records[KNN_MAX_SAMPLES];
    uint16_t i, j;
    uint32_t dist;
    uint8_t k;
    uint8_t vote_count[KNN_MAX_CLASSES];
    int16_t predicted_label = -1;
    uint8_t max_votes = 0;

    if (!g_knn_initialized) {
        return -1;
    }

    if (query_features == NULL || result == NULL) {
        return -2;
    }

    if (g_knn_num_samples == 0) {
        return -3;  /* No training data */
    }

    k = (g_knn_config.k > g_knn_num_samples) ? g_knn_num_samples : g_knn_config.k;

    /* Calculate distance from query to all training samples */
    for (i = 0; i < g_knn_num_samples; i++) {
        dist = knn_calc_distance(query_features, g_knn_samples[i].features, &g_knn_config);
        dist_records[i].distance = dist;
        dist_records[i].label = g_knn_samples[i].label;
    }

    /* Sort by distance (quick sort) */
    knn_quick_sort(dist_records, 0, g_knn_num_samples - 1);

    /* Count votes from K nearest neighbors */
    for (i = 0; i < KNN_MAX_CLASSES; i++) {
        vote_count[i] = 0;
    }

    for (i = 0; i < k; i++) {
        if (dist_records[i].label >= 0 && dist_records[i].label < g_knn_config.num_classes) {
            vote_count[dist_records[i].label]++;
        }
    }

    /* Find class with most votes */
    for (i = 0; i < g_knn_config.num_classes; i++) {
        if (vote_count[i] > max_votes) {
            max_votes = vote_count[i];
            predicted_label = i;
        }
    }

    result->predicted_label = predicted_label;
    result->confidence = (uint8_t)((max_votes * 100) / k);

    return 0;
}

uint32_t knn_calc_distance(void *f1, void *f2, knn_config_t *config)
{
    uint32_t result = 0;

    if (f1 == NULL || f2 == NULL || config == NULL) {
        return 0xFFFFFFFF;
    }

    switch (config->dist_type) {
        case KNN_DISTANCE_EUCLIDEAN:
            switch (config->data_type) {
                case KNN_DATA_TYPE_INT16:
                    result = knn_calc_dist_euclidean_int16((int16_t *)f1, (int16_t *)f2, config->feature_dim);
                    break;
                case KNN_DATA_TYPE_INT32:
                    result = knn_calc_dist_euclidean_int32((int32_t *)f1, (int32_t *)f2, config->feature_dim);
                    break;
                case KNN_DATA_TYPE_FLOAT:
                    result = knn_calc_dist_euclidean_float((float *)f1, (float *)f2, config->feature_dim);
                    break;
            }
            break;

        case KNN_DISTANCE_MANHATTAN:
            switch (config->data_type) {
                case KNN_DATA_TYPE_INT16:
                    result = knn_calc_dist_manhattan_int16((int16_t *)f1, (int16_t *)f2, config->feature_dim);
                    break;
                case KNN_DATA_TYPE_INT32:
                    result = knn_calc_dist_manhattan_int32((int32_t *)f1, (int32_t *)f2, config->feature_dim);
                    break;
                case KNN_DATA_TYPE_FLOAT:
                    result = knn_calc_dist_manhattan_float((float *)f1, (float *)f2, config->feature_dim);
                    break;
            }
            break;

        case KNN_DISTANCE_MINKOWSKI:
            switch (config->data_type) {
                case KNN_DATA_TYPE_INT16:
                    result = knn_calc_dist_minkowski_int16((int16_t *)f1, (int16_t *)f2, config->feature_dim);
                    break;
                case KNN_DATA_TYPE_INT32:
                    result = knn_calc_dist_minkowski_int32((int32_t *)f1, (int32_t *)f2, config->feature_dim);
                    break;
                case KNN_DATA_TYPE_FLOAT:
                    result = knn_calc_dist_minkowski_float((float *)f1, (float *)f2, config->feature_dim);
                    break;
            }
            break;
    }

    return result;
}

void knn_reset(void)
{
    g_knn_num_samples = 0;
    g_knn_initialized = 0;
}

uint16_t knn_get_trained_count(void)
{
    return g_knn_num_samples;
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

/************************ Copyright (c) 2026 aiclaw *****END OF FILE****/
