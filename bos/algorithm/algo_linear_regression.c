/**
 *!
 * \file        algo_linear_regression.c
 * \version     v0.0.1
 * \date        2026/04/13
 * \author      aiclaw
 *******************************************************************************
 * @brief Linear Regression implementation using Ordinary Least Squares
 *        No matrix inversion needed - uses cumulative sums approach
 *******************************************************************************
 */
#include "inc/algo_linear_regression.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if (defined(_ALGO_LINEAR_REGRESSION_ENABLE) && (_ALGO_LINEAR_REGRESSION_ENABLE == 1))

/**
 * \addtogroup ALGORITHM
 * \{
 */

/**
 * \addtogroup LINEAR_REGRESSION
 * \{
 */

/**
 * @brief Internal coefficient storage (float for computation, then cast)
 */
typedef struct {
    double *coef;           /**< Coefficients [num_features] + intercept */
    int16_t num_features;
    linear_reg_type_t type;
} linear_reg_internal_t;

static linear_reg_internal_t g_lr_internal;

/**
 * @brief Helper: dot product of two vectors
 */
static double linear_reg_dot(double *a, double *b, int n)
{
    double sum = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        sum += a[i] * b[i];
    }
    return sum;
}

/**
 * @brief Helper: convert sample x to double
 */
static void linear_reg_x_to_double(void *x, double *xd, int16_t n, linear_reg_type_t type)
{
    int i;
    switch (type) {
        case LINEAR_REG_INT16: {
            int16_t *xi = (int16_t *)x;
            for (i = 0; i < n; i++) xd[i] = (double)xi[i] / 256.0;  /* Q8.8 */
            break;
        }
        case LINEAR_REG_INT32: {
            int32_t *xi = (int32_t *)x;
            for (i = 0; i < n; i++) xd[i] = (double)xi[i] / 65536.0;  /* Q16.16 */
            break;
        }
        case LINEAR_REG_FLOAT: {
            float *xf = (float *)x;
            for (i = 0; i < n; i++) xd[i] = (double)xf[i];
            break;
        }
    }
}

/**
 * @brief Helper: convert y to double
 */
static double linear_reg_y_to_double(int16_t y, linear_reg_type_t type)
{
    switch (type) {
        case LINEAR_REG_INT16:  return (double)y / 256.0;
        case LINEAR_REG_INT32:  return (double)y / 65536.0;
        case LINEAR_REG_FLOAT:  return (double)y;
    }
    return 0.0;
}

int linear_reg_init(linear_reg_model_t *model, int16_t num_features, linear_reg_type_t type)
{
    if (model == NULL || num_features <= 0) {
        return -1;
    }

    model->num_features = num_features;
    model->type = type;
    model->is_fitted = 0;
    model->coefficients = NULL;

    /* Allocate internal coefficient array */
    g_lr_internal.num_features = num_features;
    g_lr_internal.type = type;
    g_lr_internal.coef = (double *)calloc(num_features + 1, sizeof(double));  /* +1 for intercept */

    if (g_lr_internal.coef == NULL) {
        return -2;
    }

    return 0;
}

int linear_reg_fit(linear_reg_model_t *model, linear_reg_sample_t *samples, uint16_t num_samples)
{
    double *X;        /* Design matrix [num_samples][num_features+1], last col = 1 */
    double *y;       /* Target vector [num_samples] */
    double *XTX;     /* X^T * X [num_features+1][num_features+1] */
    double *XTy;     /* X^T * y [num_features+1] */
    double *coef;    /* Final coefficients */
    int i, j, k;
    double sum;

    if (model == NULL || samples == NULL || num_samples == 0) {
        return -1;
    }

    if (g_lr_internal.coef == NULL) {
        return -2;
    }

    int n = num_samples;
    int d = model->num_features;
    int m = d + 1;  /* including intercept */

    /* Allocate matrices */
    X = (double *)calloc(n * m, sizeof(double));
    y = (double *)calloc(n, sizeof(double));
    XTX = (double *)calloc(m * m, sizeof(double));
    XTy = (double *)calloc(m, sizeof(double));
    coef = (double *)calloc(m, sizeof(double));

    if (!X || !y || !XTX || !XTy || !coef) {
        goto cleanup;
    }

    /* Build X matrix and y vector */
    for (i = 0; i < n; i++) {
        linear_reg_x_to_double(samples[i].x, &X[i * m], d, model->type);
        X[i * m + d] = 1.0;  /* Intercept term */
        y[i] = linear_reg_y_to_double(samples[i].y, model->type);
    }

    /* Compute X^T * X (Gram matrix) */
    for (i = 0; i < m; i++) {
        for (j = 0; j < m; j++) {
            sum = 0.0;
            for (k = 0; k < n; k++) {
                sum += X[k * m + i] * X[k * m + j];
            }
            XTX[i * m + j] = sum;
        }
    }

    /* Compute X^T * y */
    for (i = 0; i < m; i++) {
        sum = 0.0;
        for (k = 0; k < n; k++) {
            sum += X[k * m + i] * y[k];
        }
        XTy[i] = sum;
    }

    /* Solve XTX * coef = XTy using Gaussian elimination */
    for (i = 0; i < m; i++) {
        /* Partial pivoting */
        int max_row = i;
        double max_val = fabs(XTX[i * m + i]);
        for (k = i + 1; k < m; k++) {
            if (fabs(XTX[k * m + i]) > max_val) {
                max_val = fabs(XTX[k * m + i]);
                max_row = k;
            }
        }

        /* Swap rows */
        if (max_row != i) {
            for (j = 0; j < m; j++) {
                double tmp = XTX[i * m + j];
                XTX[i * m + j] = XTX[max_row * m + j];
                XTX[max_row * m + j] = tmp;
            }
            double tmp = XTy[i];
            XTy[i] = XTy[max_row];
            XTy[max_row] = tmp;
        }

        /* Elimination */
        for (k = i + 1; k < m; k++) {
            if (fabs(XTX[i * m + i]) < 1e-12) continue;
            double factor = XTX[k * m + i] / XTX[i * m + i];
            for (j = i; j < m; j++) {
                XTX[k * m + j] -= factor * XTX[i * m + j];
            }
            XTy[k] -= factor * XTy[i];
        }
    }

    /* Back substitution */
    for (i = m - 1; i >= 0; i--) {
        coef[i] = XTy[i];
        for (j = i + 1; j < m; j++) {
            coef[i] -= XTX[i * m + j] * coef[j];
        }
        if (fabs(XTX[i * m + i]) > 1e-12) {
            coef[i] /= XTX[i * m + i];
        }
    }

    /* Copy coefficients */
    for (i = 0; i < m; i++) {
        g_lr_internal.coef[i] = coef[i];
    }

    /* Store in model */
    model->coefficients = g_lr_internal.coef;
    model->is_fitted = 1;

cleanup:
    free(X);
    free(y);
    free(XTX);
    free(XTy);
    free(coef);

    return 0;
}

int linear_reg_predict(linear_reg_model_t *model, void *x, linear_reg_result_t *result)
{
    double xd[64];  /* Max 64 features */
    double pred;
    int i;

    if (model == NULL || x == NULL || result == NULL) {
        return -1;
    }

    if (!model->is_fitted) {
        return -2;
    }

    if (model->num_features > 64) {
        return -3;
    }

    /* Convert x to double */
    linear_reg_x_to_double(x, xd, model->num_features, model->type);

    /* Compute prediction: coef[0]*x[0] + ... + coef[d-1]*x[d-1] + coef[d] (intercept) */
    pred = 0.0;
    for (i = 0; i < model->num_features; i++) {
        pred += g_lr_internal.coef[i] * xd[i];
    }
    pred += g_lr_internal.coef[model->num_features];  /* intercept */

    /* Store result based on type */
    switch (model->type) {
        case LINEAR_REG_INT16:
            result->prediction = (int16_t)(pred * 256.0);
            break;
        case LINEAR_REG_INT32:
            result->prediction = (int16_t)(pred * 65536.0);
            break;
        case LINEAR_REG_FLOAT:
            result->prediction = (int16_t)pred;
            break;
    }

    result->confidence = 100;  /* Placeholder: could compute R² or variance */

    return 0;
}

void linear_reg_reset(linear_reg_model_t *model)
{
    if (model) {
        model->is_fitted = 0;
        model->coefficients = NULL;
    }
    if (g_lr_internal.coef) {
        free(g_lr_internal.coef);
        g_lr_internal.coef = NULL;
    }
}

double linear_reg_get_coef(linear_reg_model_t *model, uint8_t index)
{
    if (model == NULL || !model->is_fitted || g_lr_internal.coef == NULL) {
        return 0.0;
    }
    if (index > (uint8_t)model->num_features) {
        return 0.0;
    }
    return g_lr_internal.coef[index];
}

/**
 * \}
 */

/**
 * \}
 */

#endif

/************************ Copyright (c) 2026 aiclaw *****END OF FILE****/
