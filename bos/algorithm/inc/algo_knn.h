/**
 *!
 * \file        algo_knn.h
 * \version     v0.0.1
 * \date        2026/04/13
 * \author      aiclaw
 *******************************************************************************
 * @attention
 *
 * KNN (K-Nearest Neighbors) algorithm implementation for embedded systems.
 * Supports both integer (fixed-point) and floating-point data types.
 *
 *******************************************************************************
 */
#ifndef __ALGO_KNN_H__
#define __ALGO_KNN_H__

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
 * \addtogroup KNN
 * \{
 */

/**
 * \defgroup KNN_Exported_Types
 * \{
 */

/**
 * @brief KNN distance metric type
 */
typedef enum {
    KNN_DISTANCE_EUCLIDEAN = 0,   /**< Euclidean distance */
    KNN_DISTANCE_MANHATTAN,       /**< Manhattan distance */
    KNN_DISTANCE_MINKOWSKI,       /**< Minkowski distance (p=3) */
} knn_distance_type_t;

/**
 * @brief KNN data type for training/ inference
 */
typedef enum {
    KNN_DATA_TYPE_INT16 = 0,      /**< int16_t (Q8.8 fixed-point) */
    KNN_DATA_TYPE_INT32,           /**< int32_t (Q16.16 fixed-point) */
    KNN_DATA_TYPE_FLOAT,          /**< float (requires FPU) */
} knn_data_type_t;

/**
 * @brief KNN classifier configuration
 */
typedef struct {
    uint8_t k;                     /**< Number of neighbors (1-255) */
    knn_distance_type_t dist_type; /**< Distance metric */
    knn_data_type_t data_type;     /**< Data representation */
    uint8_t num_classes;           /**< Number of classes (labels 0 to num_classes-1) */
    uint8_t feature_dim;           /**< Feature dimension (number of features per sample) */
} knn_config_t;

/**
 * @brief Training sample (label + feature vector)
 */
typedef struct {
    int16_t label;                 /**< Class label (0 to num_classes-1) */
    void *features;               /**< Pointer to feature vector */
} knn_sample_t;

/**
 * @brief Inference result
 */
typedef struct {
    int16_t predicted_label;       /**< Predicted class label */
    uint8_t confidence;            /**< Confidence (0-100, percentage of votes) */
} knn_result_t;

/**
 * \}
 */

/**
 * \defgroup KNN_Exported_Constants
 * \{
 */

#define KNN_MAX_K             32   /**< Maximum K value */
#define KNN_MAX_FEATURES      64   /**< Maximum feature dimension */
#define KNN_MAX_SAMPLES       256   /**< Maximum training samples */
#define KNN_MAX_CLASSES       16   /**< Maximum number of classes */

/**
 * \}
 */

/**
 * \addtogroup KNN_Exported_Functions
 * \{
 */

/**
 * @brief Initialize KNN classifier
 * @param config KNN configuration
 * @return 0 on success, negative on error
 */
int knn_init(knn_config_t *config);

/**
 * @brief Train KNN with labeled samples
 * @param samples Array of training samples
 * @param num_samples Number of training samples
 * @return 0 on success, negative on error
 */
int knn_train(knn_sample_t *samples, uint16_t num_samples);

/**
 * @brief Predict class for a query sample (brute-force KNN)
 * @param query_features Feature vector of query sample
 * @param result Pointer to store prediction result
 * @return 0 on success, negative on error
 */
int knn_predict(void *query_features, knn_result_t *result);

/**
 * @brief Calculate distance between two feature vectors
 * @param f1 Feature vector 1
 * @param f2 Feature vector 2
 * @param config KNN configuration (contains data_type and dist_type)
 * @return Distance (squared for Euclidean to avoid sqrt)
 */
uint32_t knn_calc_distance(void *f1, void *f2, knn_config_t *config);

/**
 * @brief Clear training data (reset classifier)
 */
void knn_reset(void);

/**
 * @brief Get number of trained samples
 * @return Number of trained samples
 */
uint16_t knn_get_trained_count(void);

/**
 * \}
 */

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
