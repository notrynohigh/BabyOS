/**
 *!
 * \file        algo_linear_regression.h
 * \version     v0.0.1
 * \date        2026/04/13
 * \author      aiclaw
 *******************************************************************************
 * @brief Linear Regression for embedded systems (OLS Ordinary Least Squares)
 *******************************************************************************
 */
#ifndef __ALGO_LINEAR_REGRESSION_H__
#define __ALGO_LINEAR_REGRESSION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * \addtogroup ALGORITHM
 * \{
 */

/**
 * \addtogroup LINEAR_REGRESSION
 * \{
 */

/**
 * @brief Linear regression data type
 */
typedef enum {
    LINEAR_REG_INT16 = 0,   /**< int16_t (Q8.8 fixed-point) */
    LINEAR_REG_INT32,        /**< int32_t (Q16.16 fixed-point) */
    LINEAR_REG_FLOAT,        /**< float (requires FPU) */
} linear_reg_type_t;

/**
 * @brief Linear regression model
 */
typedef struct {
    int16_t num_features;    /**< Number of features (X dimensions) */
    linear_reg_type_t type; /**< Data type */
    void *coefficients;     /**< Coefficients [num_features] + intercept */
    int8_t is_fitted;       /**< 1 if model is fitted */
} linear_reg_model_t;

/**
 * @brief Training sample
 */
typedef struct {
    void *x;                /**< Feature vector [num_features] */
    int16_t y;              /**< Target value */
} linear_reg_sample_t;

/**
 * @brief Inference result
 */
typedef struct {
    int16_t prediction;      /**< Predicted value */
    int32_t confidence;    /**< Confidence (variance inverse, higher=better) */
} linear_reg_result_t;

/**
 * \addtogroup LINEAR_REGRESSION_Exported_Functions
 * \{
 */

/**
 * @brief Initialize linear regression model
 * @param model Model handle
 * @param num_features Number of input features
 * @param type Data type for computation
 * @return 0 on success
 */
int linear_reg_init(linear_reg_model_t *model, int16_t num_features, linear_reg_type_t type);

/**
 * @brief Fit model using Ordinary Least Squares (batch training)
 * @param model Model handle
 * @param samples Training samples [num_samples]
 * @param num_samples Number of training samples
 * @return 0 on success, negative on error
 */
int linear_reg_fit(linear_reg_model_t *model, linear_reg_sample_t *samples, uint16_t num_samples);

/**
 * @brief Predict using fitted model
 * @param model Fitted model
 * @param x Input features [num_features]
 * @param result Output result
 * @return 0 on success
 */
int linear_reg_predict(linear_reg_model_t *model, void *x, linear_reg_result_t *result);

/**
 * @brief Reset/free model
 * @param model Model handle
 */
void linear_reg_reset(linear_reg_model_t *model);

/**
 * @brief Get coefficient at index
 * @param model Fitted model
 * @param index Coefficient index (0 to num_features-1 = intercept)
 * @return Coefficient value as double
 */
double linear_reg_get_coef(linear_reg_model_t *model, uint8_t index);

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
