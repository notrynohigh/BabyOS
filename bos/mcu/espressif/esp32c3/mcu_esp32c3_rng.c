#include "b_config.h"
#include "hal/inc/b_hal_rng.h"

uint32_t bMcuRNGRead()
{
    srand(bHalGetSysTick());
    return rand();
}

