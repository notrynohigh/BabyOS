/**
 *!
 * \file        algo_kalman_filter.h
 * \version     v0.0.1
 * \date        2026/04/13
 * \author      aiclaw
 *******************************************************************************
 * @brief Kalman Filter for sensor fusion and state estimation
 *******************************************************************************
 */
#ifndef __ALGO_KALMAN_FILTER_H__
#define __ALGO_KALMAN_FILTER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * \addtogroup ALGORITHM
 * \{
 */

/**
 * \addtogroup KALMAN_FILTER
 * \{
 */

/**
 * @brief Kalman filter data type
 */
typedef enum {
    KF_INT16 = 0,            /**< int16_t (Q8.8 fixed-point) */
    KF_INT32,                 /**< int32_t (Q16.16 fixed-point) */
    KF_FLOAT,                 /**< float (requires FPU) */
} kf_type_t;

/**
 * @brief Kalman filter state
 */
typedef struct {
    void *x;                  /**< State vector [state_dim] */
    void *P;                  /**< Error covariance matrix [state_dim x state_dim] */
    void *Q;                  /**< Process noise covariance [state_dim x state_dim] */
    void *R;                  /**< Measurement noise covariance [meas_dim x meas_dim] */
    void *K;                  /**< Kalman gain [state_dim x meas_dim] (internal) */
    void *F;                  /**< State transition matrix [state_dim x state_dim] */
    void *H;                  /**< Measurement matrix [meas_dim x state_dim] */
    void *tmp;                /**< Temp buffer for calculations */
} kf_state_t;

/**
 * @brief Kalman filter configuration
 */
typedef struct {
    uint8_t state_dim;        /**< State dimension */
    uint8_t meas_dim;         /**< Measurement dimension */
    kf_type_t type;           /**< Data type */
} kf_config_t;

/**
 * @brief Measurement input
 */
typedef struct {
    void *z;                  /**< Measurement vector [meas_dim] */
    uint32_t timestamp;        /**< Timestamp (optional) */
} kf_measurement_t;

/**
 * \addtogroup KALMAN_FILTER_Exported_Functions
 * \{
 */

/**
 * @brief Initialize Kalman filter
 * @param config Configuration
 * @param state Kalman state handle
 * @return 0 on success
 */
int kf_init(kf_config_t *config, kf_state_t *state);

/**
 * @brief Set initial state value
 * @param state Kalman state
 * @param x0 Initial state vector
 */
void kf_set_initial_state(kf_state_t *state, void *x0);

/**
 * @brief Set state transition matrix F (identity by default)
 * @param state Kalman state
 * @param F State transition matrix (row-major)
 */
void kf_set_F(kf_state_t *state, void *F);

/**
 * @brief Set measurement matrix H (identity by default)
 * @param state Kalman state
 * @param H Measurement matrix (row-major)
 */
void kf_set_H(kf_state_t *state, void *H);

/**
 * @brief Set process noise covariance Q
 * @param state Kalman state
 * @param Q Process noise covariance matrix
 */
void kf_set_Q(kf_state_t *state, void *Q);

/**
 * @brief Set measurement noise covariance R
 * @param state Kalman state
 * @param R Measurement noise covariance matrix
 */
void kf_set_R(kf_state_t *state, void *R);

/**
 * @brief Predict step (time update)
 * @param state Kalman state
 * @return 0 on success
 */
int kf_predict(kf_state_t *state);

/**
 * @brief Update step (measurement update)
 * @param state Kalman state
 * @param z Measurement vector
 * @return 0 on success
 */
int kf_update(kf_state_t *state, kf_measurement_t *z);

/**
 * @brief Get current state estimate
 * @param state Kalman state
 * @return State vector pointer
 */
void *kf_get_state(kf_state_t *state);

/**
 * @brief Reset Kalman filter
 * @param state Kalman state
 */
void kf_reset(kf_state_t *state);

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
