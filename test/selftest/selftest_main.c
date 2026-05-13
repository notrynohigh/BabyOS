/**
 * \file selftest_main.c
 * \brief BabyOS 自测入口
 */
#include "b_config.h"
#include "b_os.h"
#include "../port.h"

#include <stdio.h>
#include <stdlib.h>

extern int test_drivers_runner(void);
extern int test_algo_runner(void);
extern int test_core_runner(void);
extern int test_utils_runner(void);
extern int test_modules_runner(void);
extern int test_service_runner(void);

static int total_failed = 0;

int main(void)
{
    port_init();
    b_log("\n========================================\n");
    b_log("  BabyOS Self-Test Suite\n");
    b_log("========================================\n\n");

    bInit();
    bShellInit();  /* needed for shell and param module tests */

    b_log("\n[MAIN] ====== Batch 0: Drivers ======\n");
    total_failed += test_drivers_runner();

    b_log("\n[MAIN] ====== Batch 1: Algorithms ======\n");
    total_failed += test_algo_runner();

    b_log("\n[MAIN] ====== Batch 2: Core ======\n");
    total_failed += test_core_runner();

    b_log("\n[MAIN] ====== Batch 3: Utils ======\n");
    total_failed += test_utils_runner();

    b_log("\n[MAIN] ====== Batch 4: Modules ======\n");
    total_failed += test_modules_runner();

    b_log("\n[MAIN] ====== Batch 5: Services ======\n");
    total_failed += test_service_runner();

    b_log("\n========================================\n");
    b_log("  Result: %s (failed=%d)\n", total_failed == 0 ? "ALL PASSED" : "SOME FAILED", total_failed);
    b_log("========================================\n");

    return total_failed > 0 ? 1 : 0;
}