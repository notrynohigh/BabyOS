#include "b_config.h"
#include "driver/gpio.h"
#include "hal/inc/b_hal_gpio.h"

void bMcuGpioConfig(bHalGPIOPort_t port, bHalGPIOPin_t pin, bHalGPIODir_t dir, bHalGPIOPull_t pull)
{
    if (port >= B_HAL_GPIO_INVALID || pin >= B_HAL_PIN_INVALID)
    {
        return;
    }
    int           gpio_num = port * 16 + pin;
    gpio_config_t io_conf  = {
         .pin_bit_mask = (1ULL << gpio_num),
         .mode         = (dir == B_HAL_GPIO_INPUT) ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT,
         .pull_up_en   = (pull == B_HAL_GPIO_PULLUP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
         .pull_down_en =
            (pull == B_HAL_GPIO_PULLDOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
         .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

void bMcuGpioWritePin(bHalGPIOPort_t port, bHalGPIOPin_t pin, uint8_t s)
{
    if (port >= B_HAL_GPIO_INVALID || pin >= B_HAL_PIN_INVALID)
    {
        return;
    }
    int gpio_num = port * 16 + pin;
    gpio_set_level(gpio_num, s);
}

void bMcuGpioWritePort(bHalGPIOPort_t port, uint16_t dat)
{
    if (port >= B_HAL_GPIO_INVALID)
    {
        return;
    }
    for (int pin = 0; pin < 16; pin++)
    {
        int     gpio_num = port * 16 + pin;
        uint8_t level    = (dat & (1 << pin)) ? 1 : 0;
        gpio_set_level(gpio_num, level);
    }
}

uint8_t bMcuGpioReadPin(bHalGPIOPort_t port, bHalGPIOPin_t pin)
{
    if (port >= B_HAL_GPIO_INVALID || pin >= B_HAL_PIN_INVALID)
    {
        return 0;
    }
    int gpio_num = port * 16 + pin;
    return gpio_get_level(gpio_num);
}

uint16_t bMcuGpioReadPort(bHalGPIOPort_t port)
{
    if (port >= B_HAL_GPIO_INVALID)
    {
        return 0;
    }
    uint16_t port_value = 0;
    for (int pin = 0; pin < 16; pin++)
    {
        int     gpio_num = port * 16 + pin;
        uint8_t level    = gpio_get_level(gpio_num);
        if (level)
        {
            port_value |= (1 << pin);
        }
    }

    return port_value;
}
