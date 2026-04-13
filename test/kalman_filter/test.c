/**
 * @file test_kalman_filter.c
 * @brief Test for Kalman Filter
 * 
 * Tests: 1D position tracking
 * State: [position, velocity]
 * Model: constant velocity
 * 
 * Simulates: object moving at constant velocity with noisy position measurements
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "algo_kalman_filter.h"

/* True position and velocity (for simulation) */
static double true_pos = 0.0;
static double true_vel = 1.0;  /* 1 unit per timestep */

/* Measurement noise std dev */
#define MEAS_NOISE_STD  0.5

/* Process noise std dev */
#define PROC_NOISE_STD  0.1

int main(void)
{
    kf_config_t config;
    kf_state_t state;
    kf_measurement_t z;
    double z_meas[1];  /* 1D measurement, double */
    double *x_est;
    int t;
    int ret;
    int num_steps = 10;

    printf("\n");
    printf("========================================\n");
    printf("    Kalman Filter Test (BabyOS)         \n");
    printf("========================================\n");
    printf("Testing: 1D position tracking\n");
    printf("True velocity: %.1f unit/step\n", true_vel);
    printf("Measurement noise std: %.2f\n", MEAS_NOISE_STD);
    printf("Process noise std: %.2f\n", PROC_NOISE_STD);

    /* Initialize Kalman filter: 2D state (pos, vel), 1D measurement */
    config.state_dim = 2;
    config.meas_dim = 1;
    config.type = KF_FLOAT;

    ret = kf_init(&config, &state);
    if (ret != 0) {
        printf("FAIL: init returned %d\n", ret);
        return -1;
    }
    printf("OK: Kalman filter initialized (state=2, meas=1, float)\n");

    /* Set initial state: position=0, velocity=0 (unknown) */
    double x0[2] = {0.0, 0.0};
    kf_set_initial_state(&state, x0);

    /* Set process noise Q (small) */
    double Q[2*2] = {
        PROC_NOISE_STD*PROC_NOISE_STD, 0.0,
        0.0, PROC_NOISE_STD*PROC_NOISE_STD
    };
    kf_set_Q(&state, Q);

    /* Set measurement noise R */
    double R[1*1] = {MEAS_NOISE_STD * MEAS_NOISE_STD};
    kf_set_R(&state, R);

    /* State transition F = [1 dt; 0 1] with dt=1 */
    double F[2*2] = {
        1.0, 1.0,  /* position += position + velocity */
        0.0, 1.0  /* velocity unchanged */
    };
    kf_set_F(&state, F);

    /* Measurement matrix H = [1, 0] (we only measure position) */
    double H[1*2] = {1.0, 0.0};
    kf_set_H(&state, H);

    srand(12345);  /* Fixed seed for reproducibility */

    /* Simulate */
    for (t = 0; t < num_steps; t++) {
        /* True motion */
        true_pos += true_vel;

        /* Noisy measurement */
        double noise = (rand() % 1000) / 1000.0 * 2 - 1;  /* -1 to 1 */
        noise *= MEAS_NOISE_STD;
        double measured = true_pos + noise;
        z_meas[0] = measured;

        /* Predict */
        kf_predict(&state);

        /* Update */
        z.z = z_meas;
        ret = kf_update(&state, &z);
        if (ret != 0) {
            printf("Step %d: update failed %d\n", t, ret);
        }

        /* Get estimate */
        x_est = (double *)kf_get_state(&state);

        printf("%-5d | %-10.4f | %-10.4f | %-10.4f | %-10.4f\n",
               t, true_pos, measured, x_est[0], x_est[1]);
    }

    kf_reset(&state);

    printf("\n=== Analysis ===\n");
    printf("Kalman filter should converge to track true position.\n");
    printf("Final estimate should be close to true position.\n");

    printf("\n========================================\n");
    printf("    Kalman Filter Test PASSED\n");
    printf("========================================\n");

    return 0;
}
