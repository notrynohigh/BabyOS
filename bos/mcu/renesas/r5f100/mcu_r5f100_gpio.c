#include "b_config.h"
#include "hal/inc/b_hal_gpio.h"

void bMcuGpioConfig(bHalGPIOPort_t port, bHalGPIOPin_t pin, bHalGPIODir_t dir,
                              bHalGPIOPull_t pull)
{
    ;
}

void bMcuGpioWritePin(bHalGPIOPort_t port, bHalGPIOPin_t pin, uint8_t s)
{
    ;
}

void bMcuGpioWritePort(bHalGPIOPort_t port, uint16_t dat)
{
    ;
}

uint8_t bMcuGpioReadPin(bHalGPIOPort_t port, bHalGPIOPin_t pin)
{
    return 0;
}

uint16_t bMcuGpioReadPort(bHalGPIOPort_t port)
{
    return 0;
}

