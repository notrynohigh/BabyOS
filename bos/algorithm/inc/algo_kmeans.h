/**
 *!
 * \file        algo_kmeans.h
 * \version     v0.0.1
 * \date        2026/04/13
 * \author      aiclaw
 *******************************************************************************
 * @brief K-Means clustering algorithm for embedded systems
 *******************************************************************************
 */
#ifndef __ALGO_KMEANS_H__
#define __ALGO_KMEANS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * \addtogroup ALGORITHM
 * \{
 */

/**
 * \addtogroup KMEANS
 * \{
 */

/**
 * @brief K-Means data type
 */
typedef enum {
    KMEANS_INT16 = 0,       /**< int16_t (Q8.8 fixed-point) */
    KMEANS_INT32,            /**< int32_t (Q16.16 fixed-point) */
    KMEANS_FLOAT,            /**< float */
} kmeans_type_t;

/**
 * @brief K-Means configuration
 */
typedef struct {
    uint8_t k;              /**< Number of clusters (2-16) */
    uint8_t max_iterations;  /**< Max iterations (default 50) */
    uint8_t feature_dim;     /**< Feature dimension */
    kmeans_type_t type;      /**< Data type */
    uint8_t random_seed;     /**< Random seed for initialization */
} kmeans_config_t;

/**
 * @brief Data sample
 */
typedef struct {
    void *features;         /**< Feature vector [feature_dim] */
} kmeans_sample_t;

/**
 * @brief Cluster centroid
 */
typedef struct {
    int16_t id;              /**< Cluster ID (0 to k-1) */
    void *centroid;         /**< Centroid vector [feature_dim] */
    uint16_t count;          /**< Number of samples in cluster */
} kmeans_cluster_t;

/**
 * @brief Clustering result
 */
typedef struct {
    int16_t cluster_id;      /**< Assigned cluster ID */
    uint32_t distance;       /**< Distance to cluster centroid */
} kmeans_result_t;

/**
 * \addtogroup KMEANS_Exported_Functions
 * \{
 */

/**
 * @brief Initialize K-Means
 * @param config K-Means configuration
 * @return 0 on success
 */
int kmeans_init(kmeans_config_t *config);

/**
 * @brief Fit K-Means model (train)
 * @param samples Training samples
 * @param num_samples Number of samples
 * @param clusters Output cluster centroids [k]
 * @return 0 on success
 */
int kmeans_fit(kmeans_sample_t *samples, uint16_t num_samples, kmeans_cluster_t *clusters);

/**
 * @brief Predict cluster assignment for a sample
 * @param sample Input sample
 * @param result Output result
 * @return 0 on success
 */
int kmeans_predict(kmeans_sample_t *sample, kmeans_result_t *result);

/**
 * @brief Get cluster centroids
 * @return Pointer to centroids array
 */
kmeans_cluster_t *kmeans_get_centroids(void);

/**
 * @brief Reset K-Means
 */
void kmeans_reset(void);

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

/************************ Copyright (c) 2026 aiclaw *****END OF FILE****/
