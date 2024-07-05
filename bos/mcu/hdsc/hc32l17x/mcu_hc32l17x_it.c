#include "b_config.h"
#include "cmsis_compiler.h"
#include "hal/inc/b_hal_it.h"


void bMcuIntEnable()
{
    __enable_irq();
}

void bMcuIntDisable()
{
    __disable_irq();
}
