/**
 *!
 * \file        algo_kalman_filter.c
 * \version     v0.0.1
 * \date        2026/04/13
 * \author      aiclaw
 *******************************************************************************
 * @brief Kalman Filter implementation (Linear, Discrete)
 *        Based on: x_k = F * x_{k-1} + B * u_{k-1} + w_{k-1}
 *                 z_k = H * x_k + v_k
 *******************************************************************************
 */
#include "inc/algo_kalman_filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if (defined(_ALGO_KALMAN_FILTER_ENABLE) && (_ALGO_KALMAN_FILTER_ENABLE == 1))

/**
 * \addtogroup ALGORITHM
 * \{
 */

/**
 * \addtogroup KALMAN_FILTER
 * \{
 */

/* Matrix dimension limits */
#define KF_MAX_DIM     8

/**
 * @brief Internal storage
 */
typedef struct {
    double x[KF_MAX_DIM];              /* State [state_dim] */
    double P[KF_MAX_DIM][KF_MAX_DIM];  /* Covariance [state_dim x state_dim] */
    double Q[KF_MAX_DIM][KF_MAX_DIM];  /* Process noise [state_dim x state_dim] */
    double R[KF_MAX_DIM][KF_MAX_DIM];  /* Measurement noise [meas_dim x meas_dim] */
    double F[KF_MAX_DIM][KF_MAX_DIM];  /* State transition [state_dim x state_dim] */
    double H[KF_MAX_DIM][KF_MAX_DIM];  /* Measurement matrix [meas_dim x state_dim] */
    uint8_t state_dim;
    uint8_t meas_dim;
    kf_type_t type;
} kf_internal_t;

static kf_internal_t g_kf;

/**
 * @brief Matrix multiplication: C = A * B (n x m * m x p = n x p)
 *        All matrices are stored row-major
 */
static void kf_mat_mul(double *C, const double *A, const double *B, uint8_t n, uint8_t m, uint8_t p)
{
    uint8_t i, j, k;
    for (i = 0; i < n; i++) {
        for (j = 0; j < p; j++) {
            C[i * p + j] = 0.0;
            for (k = 0; k < m; k++) {
                C[i * p + j] += A[i * m + k] * B[k * p + j];
            }
        }
    }
}

/**
 * @brief Matrix addition: C = A + B
 */
static void kf_mat_add(double *C, const double *A, const double *B, uint8_t n, uint8_t m)
{
    uint8_t i;
    for (i = 0; i < n * m; i++) {
        C[i] = A[i] + B[i];
    }
}

/**
 * @brief Matrix subtraction: C = A - B
 */
static void kf_mat_sub(double *C, const double *A, const double *B, uint8_t n, uint8_t m)
{
    uint8_t i;
    for (i = 0; i < n * m; i++) {
        C[i] = A[i] - B[i];
    }
}

/**
 * @brief Copy data to internal double format
 */
static void kf_to_double(void *src, double *dst, int n, kf_type_t type)
{
    int i;
    switch (type) {
        case KF_INT16: {
            int16_t *s = (int16_t *)src;
            for (i = 0; i < n; i++) dst[i] = (double)s[i] / 256.0;
            break;
        }
        case KF_INT32: {
            int32_t *s = (int32_t *)src;
            for (i = 0; i < n; i++) dst[i] = (double)s[i] / 65536.0;
            break;
        }
        case KF_FLOAT: {
            float *s = (float *)src;
            for (i = 0; i < n; i++) dst[i] = (double)s[i];
            break;
        }
    }
}

/**
 * @brief Copy internal double to output format
 */
static void kf_from_double(double *src, void *dst, int n, kf_type_t type)
{
    int i;
    switch (type) {
        case KF_INT16: {
            int16_t *d = (int16_t *)dst;
            for (i = 0; i < n; i++) d[i] = (int16_t)(src[i] * 256.0);
            break;
        }
        case KF_INT32: {
            int32_t *d = (int32_t *)dst;
            for (i = 0; i < n; i++) d[i] = (int32_t)(src[i] * 65536.0);
            break;
        }
        case KF_FLOAT: {
            float *d = (float *)dst;
            for (i = 0; i < n; i++) d[i] = (float)src[i];
            break;
        }
    }
}

int kf_init(kf_config_t *config, kf_state_t *state)
{
    uint8_t i, j;

    if (config == NULL || state == NULL) {
        return -1;
    }

    if (config->state_dim == 0 || config->state_dim > KF_MAX_DIM) {
        return -2;
    }
    if (config->meas_dim == 0 || config->meas_dim > KF_MAX_DIM) {
        return -3;
    }

    g_kf.state_dim = config->state_dim;
    g_kf.meas_dim = config->meas_dim;
    g_kf.type = config->type;

    /* Initialize F to identity */
    for (i = 0; i < KF_MAX_DIM; i++) {
        for (j = 0; j < KF_MAX_DIM; j++) {
            g_kf.F[i][j] = (i == j) ? 1.0 : 0.0;
        }
    }

    /* Initialize H to zero */
    for (i = 0; i < KF_MAX_DIM; i++) {
        for (j = 0; j < KF_MAX_DIM; j++) {
            g_kf.H[i][j] = 0.0;
        }
    }

    /* Initialize P to identity */
    for (i = 0; i < KF_MAX_DIM; i++) {
        for (j = 0; j < KF_MAX_DIM; j++) {
            g_kf.P[i][j] = (i == j) ? 1.0 : 0.0;
        }
    }

    /* Initialize x to zero */
    for (i = 0; i < KF_MAX_DIM; i++) {
        g_kf.x[i] = 0.0;
    }

    (void)state;
    return 0;
}

void kf_set_initial_state(kf_state_t *state, void *x0)
{
    uint8_t i;
    if (x0) {
        kf_to_double(x0, g_kf.x, g_kf.state_dim, g_kf.type);
        /* Initialize P to small values (high uncertainty) */
        for (i = 0; i < KF_MAX_DIM; i++) {
            g_kf.P[i][i] = 1.0;
        }
    }
    (void)state;
}

void kf_set_F(kf_state_t *state, void *F)
{
    uint8_t i, j, n = g_kf.state_dim;
    double *Fd = (double *)F;
    if (F) {
        for (i = 0; i < n; i++) {
            for (j = 0; j < n; j++) {
                g_kf.F[i][j] = Fd[i * n + j];
            }
        }
    }
    (void)state;
}

void kf_set_H(kf_state_t *state, void *H)
{
    uint8_t i, j, n = g_kf.meas_dim, m = g_kf.state_dim;
    double *Hd = (double *)H;
    if (H) {
        for (i = 0; i < n; i++) {
            for (j = 0; j < m; j++) {
                g_kf.H[i][j] = Hd[i * m + j];
            }
        }
    }
    (void)state;
}

void kf_set_Q(kf_state_t *state, void *Q)
{
    uint8_t i, j, n = g_kf.state_dim;
    double *Qd = (double *)Q;
    if (Q) {
        for (i = 0; i < n; i++) {
            for (j = 0; j < n; j++) {
                g_kf.Q[i][j] = Qd[i * n + j];
            }
        }
    }
    (void)state;
}

void kf_set_R(kf_state_t *state, void *R)
{
    uint8_t i, j, n = g_kf.meas_dim;
    double *Rd = (double *)R;
    if (R) {
        for (i = 0; i < n; i++) {
            for (j = 0; j < n; j++) {
                g_kf.R[i][j] = Rd[i * n + j];
            }
        }
    }
    (void)state;
}

int kf_predict(kf_state_t *state)
{
    uint8_t n = g_kf.state_dim;
    uint8_t i, j, k;
    double x_new[KF_MAX_DIM];
    double P_new[KF_MAX_DIM][KF_MAX_DIM];
    double Ft[KF_MAX_DIM][KF_MAX_DIM];
    double FP[KF_MAX_DIM][KF_MAX_DIM];
    double FPFt[KF_MAX_DIM][KF_MAX_DIM];

    /* x = F * x */
    for (i = 0; i < n; i++) {
        x_new[i] = 0.0;
        for (j = 0; j < n; j++) {
            x_new[i] += g_kf.F[i][j] * g_kf.x[j];
        }
    }
    for (i = 0; i < n; i++) {
        g_kf.x[i] = x_new[i];
    }

    /* Compute Ft (transpose of F) */
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            Ft[i][j] = g_kf.F[j][i];
        }
    }

    /* Compute F * P */
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            FP[i][j] = 0.0;
            for (k = 0; k < n; k++) {
                FP[i][j] += g_kf.F[i][k] * g_kf.P[k][j];
            }
        }
    }

    /* Compute F * P * Ft */
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            FPFt[i][j] = 0.0;
            for (k = 0; k < n; k++) {
                FPFt[i][j] += FP[i][k] * Ft[k][j];
            }
        }
    }

    /* P = F * P * Ft + Q */
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            g_kf.P[i][j] = FPFt[i][j] + g_kf.Q[i][j];
        }
    }

    (void)state;
    return 0;
}

int kf_update(kf_state_t *state, kf_measurement_t *z)
{
    uint8_t n = g_kf.state_dim;  /* State dimension */
    uint8_t m = g_kf.meas_dim;   /* Measurement dimension */
    uint8_t i, j, k;
    double z_vec[KF_MAX_DIM];
    double y[KF_MAX_DIM];         /* Innovation */
    double S[KF_MAX_DIM][KF_MAX_DIM];
    double S_inv[KF_MAX_DIM][KF_MAX_DIM];
    double K[KF_MAX_DIM][KF_MAX_DIM];  /* Kalman gain */
    double Ht[KF_MAX_DIM][KF_MAX_DIM];
    double PHt[KF_MAX_DIM][KF_MAX_DIM];
    double KH[KF_MAX_DIM][KF_MAX_DIM];
    double I[KF_MAX_DIM][KF_MAX_DIM];
    double P_new[KF_MAX_DIM][KF_MAX_DIM];

    /* Convert measurement to double */
    kf_to_double(z->z, z_vec, m, g_kf.type);

    /* y = z - H * x (innovation) */
    for (i = 0; i < m; i++) {
        y[i] = z_vec[i];
        for (j = 0; j < n; j++) {
            y[i] -= g_kf.H[i][j] * g_kf.x[j];
        }
    }

    /* Compute Ht (transpose of H) */
    for (i = 0; i < n; i++) {
        for (j = 0; j < m; j++) {
            Ht[i][j] = g_kf.H[j][i];
        }
    }

    /* Compute P * Ht */
    for (i = 0; i < n; i++) {
        for (j = 0; j < m; j++) {
            PHt[i][j] = 0.0;
            for (k = 0; k < n; k++) {
                PHt[i][j] += g_kf.P[i][k] * Ht[k][j];
            }
        }
    }

    /* Compute S = H * P * Ht + R (Innovation covariance) */
    /* First compute H * P */
    double HP[KF_MAX_DIM][KF_MAX_DIM];
    for (i = 0; i < m; i++) {
        for (j = 0; j < n; j++) {
            HP[i][j] = 0.0;
            for (k = 0; k < n; k++) {
                HP[i][j] += g_kf.H[i][k] * g_kf.P[k][j];
            }
        }
    }
    /* Then compute H * P * Ht */
    for (i = 0; i < m; i++) {
        for (j = 0; j < m; j++) {
            S[i][j] = 0.0;
            for (k = 0; k < n; k++) {
                S[i][j] += HP[i][k] * Ht[k][j];
            }
            S[i][j] += g_kf.R[i][j];
        }
    }

    /* Invert S (simplified for small m) */
    if (m == 1) {
        S_inv[0][0] = 1.0 / (S[0][0] + 1e-9);
    } else if (m == 2) {
        double det = S[0][0] * S[1][1] - S[0][1] * S[1][0];
        if (fabs(det) < 1e-12) det = 1e-12;
        S_inv[0][0] = S[1][1] / det;
        S_inv[0][1] = -S[0][1] / det;
        S_inv[1][0] = -S[1][0] / det;
        S_inv[1][1] = S[0][0] / det;
    } else {
        for (i = 0; i < m; i++) S_inv[i][i] = 1.0;
    }

    /* K = P * Ht * S_inv (Kalman gain) */
    for (i = 0; i < n; i++) {
        for (j = 0; j < m; j++) {
            K[i][j] = 0.0;
            for (k = 0; k < m; k++) {
                K[i][j] += PHt[i][k] * S_inv[k][j];
            }
        }
    }

    /* x = x + K * y (state update) */
    for (i = 0; i < n; i++) {
        for (j = 0; j < m; j++) {
            g_kf.x[i] += K[i][j] * y[j];
        }
    }

    /* Compute KH */
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            KH[i][j] = 0.0;
            for (k = 0; k < m; k++) {
                KH[i][j] += K[i][k] * g_kf.H[k][j];
            }
        }
    }

    /* P = (I - K * H) * P */
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            I[i][j] = (i == j) ? 1.0 : 0.0;
        }
    }

    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            P_new[i][j] = 0.0;
            for (k = 0; k < n; k++) {
                P_new[i][j] += (I[i][k] - KH[i][k]) * g_kf.P[k][j];
            }
        }
    }

    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            g_kf.P[i][j] = P_new[i][j];
        }
    }

    (void)state;
    return 0;
}

void *kf_get_state(kf_state_t *state)
{
    (void)state;
    return g_kf.x;
}

void kf_reset(kf_state_t *state)
{
    memset(&g_kf, 0, sizeof(g_kf));
    (void)state;
}

/**
 * \}
 */

/**
 * \}
 */

#endif

/************************ Copyright (c) 2026 aiclaw *****END OF FILE****/
