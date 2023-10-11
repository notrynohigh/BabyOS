/*
 * @FilePath: \EIDEd:\GitHub\activeglove_at32f413_app\BabyOS\bos\mcu\at\at32f413xx\mcu_at32f413xx_gpio.c
 * @Author: miniminiminini
 * @Date: 2023-10-11 07:23:37
 * @LastEditors: miniminiminini
 * @LastEditTime: 2023-10-11 09:47:37
 * @Description: file content
 */
/**
 *!
 * \file        mcu_stm32f10x_gpio.c
 * \version     v0.0.1
 * \date        2020/03/25
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2020 Bean
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SGPIOL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "b_config.h"
#include "hal/inc/b_hal_gpio.h"

#if defined(AT32F413)
#include "at32f413.h"

static int transfer_at32_gpioport(bHalGPIOPort_t port, gpio_type **at32gpioport_x)
{
    int retval = 0;
    switch (port)
    {
    case B_HAL_GPIOA:
    {
        *at32gpioport_x = GPIOA;
    }
    break;
    case B_HAL_GPIOB:
    {
        *at32gpioport_x = GPIOB;
    }
    break;
    case B_HAL_GPIOC:
    {
        *at32gpioport_x = GPIOC;
    }
    break;
    case B_HAL_GPIOD:
    {
        *at32gpioport_x = GPIOD;
    }
    break;
    case B_HAL_GPIOF:
    {
        *at32gpioport_x = GPIOF;
    }
    break;

    default:
        retval = -1;
        break;
    }

    return retval;
}

static int transfer_at32_gpiopin(bHalGPIOPin_t pin, uint32_t *at32gpiopin_x)
{
    int retval = 0;

    switch (pin)
    {
    case B_HAL_PIN0:
    {
        *at32gpiopin_x = GPIO_PINS_0;
    }
    break;
    case B_HAL_PIN1:
    {
        *at32gpiopin_x = GPIO_PINS_1;
    }
    break;
    case B_HAL_PIN2:
    {
        *at32gpiopin_x = GPIO_PINS_2;
    }
    break;
    case B_HAL_PIN3:
    {
        *at32gpiopin_x = GPIO_PINS_3;
    }
    break;
    case B_HAL_PIN4:
    {
        *at32gpiopin_x = GPIO_PINS_4;
    }
    break;
    case B_HAL_PIN5:
    {
        *at32gpiopin_x = GPIO_PINS_5;
    }
    break;
    case B_HAL_PIN6:
    {
        *at32gpiopin_x = GPIO_PINS_6;
    }
    break;
    case B_HAL_PIN7:
    {
        *at32gpiopin_x = GPIO_PINS_7;
    }
    break;
    case B_HAL_PIN8:
    {
        *at32gpiopin_x = GPIO_PINS_8;
    }
    break;
    case B_HAL_PIN9:
    {
        *at32gpiopin_x = GPIO_PINS_9;
    }
    break;
    case B_HAL_PIN10:
    {
        *at32gpiopin_x = GPIO_PINS_10;
    }
    break;
    case B_HAL_PIN11:
    {
        *at32gpiopin_x = GPIO_PINS_11;
    }
    break;
    case B_HAL_PIN12:
    {
        *at32gpiopin_x = GPIO_PINS_12;
    }
    break;
    case B_HAL_PIN13:
    {
        *at32gpiopin_x = GPIO_PINS_13;
    }
    break;
    case B_HAL_PIN14:
    {
        *at32gpiopin_x = GPIO_PINS_14;
    }
    break;
    case B_HAL_PIN15:
    {
        *at32gpiopin_x = GPIO_PINS_15;
    }
    break;
    case B_HAL_PINAll:
    {
        *at32gpiopin_x = GPIO_PINS_ALL;
    }
    break;

    default:
        retval = -1;
        break;
    }
    return retval;
}

static int transfer_at32_gpiodir(bHalGPIODir_t dir, gpio_mode_type *at32gpio_mode)
{
    int retval = 0;
    switch (dir)
    {
    case B_HAL_GPIO_INPUT:
    {
        *at32gpio_mode = GPIO_MODE_INPUT;
    }
    break;
    case B_HAL_GPIO_OUTPUT:
    {
        *at32gpio_mode = GPIO_MODE_OUTPUT;
    }
    break;

    default:
        retval = -1;
        break;
    }

    return retval;
}

static int transfer_at32_gpiopull(bHalGPIOPull_t pull, gpio_pull_type *at32gpio_pull)
{
    int retval = 0;
    switch (pull)
    {
    case B_HAL_GPIO_NOPULL:
    {
        *at32gpio_pull = GPIO_PULL_NONE;
    }
    break;
    case B_HAL_GPIO_PULLUP:
    {
        *at32gpio_pull = GPIO_PULL_UP;
    }
    break;
    case B_HAL_GPIO_PULLDOWN:
    {
        *at32gpio_pull = GPIO_PULL_DOWN;
    }
    break;

    default:
        retval = -1;
        break;
    }

    return retval;
}

void bMcuGpioConfig(bHalGPIOPort_t port, bHalGPIOPin_t pin, bHalGPIODir_t dir, bHalGPIOPull_t pull)
{
    gpio_type *gpio_x;
    if (transfer_at32_gpioport(port, &gpio_x))
    {
        return;
    }

    uint32_t gpio_pin;
    if (transfer_at32_gpiopin(pin, &gpio_pin))
    {
        return;
    }

    gpio_mode_type gpio_mode;
    if (transfer_at32_gpiodir(dir, &gpio_mode))
    {
        return;
    }

    gpio_pull_type gpio_pull;
    if (transfer_at32_gpiopull(pull, &gpio_pull))
    {
        return;
    }

    gpio_init_type gpio_init_struct;

    gpio_default_para_init(&gpio_init_struct);

    gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
    gpio_init_struct.gpio_mode = gpio_mode;
    gpio_init_struct.gpio_pull = gpio_pull;
    gpio_init_struct.gpio_pins = gpio_pin;
    gpio_init(gpio_x, &gpio_init_struct);
}

void bMcuGpioWritePin(bHalGPIOPort_t port, bHalGPIOPin_t pin, uint8_t s)
{
    gpio_type *gpio_x;
    if (transfer_at32_gpioport(port, &gpio_x))
    {
        return;
    }

    uint32_t gpio_pin;
    if (transfer_at32_gpiopin(pin, &gpio_pin))
    {
        return;
    }

    if (s)
    {
        gpio_bits_write(gpio_x, gpio_pin, TRUE);
    }
    else
    {
        gpio_bits_write(gpio_x, gpio_pin, FALSE);
    }
}

uint8_t bMcuGpioReadPin(bHalGPIOPort_t port, bHalGPIOPin_t pin)
{
    gpio_type *gpio_x;
    transfer_at32_gpioport(port, &gpio_x);

    uint32_t gpio_pin;
    transfer_at32_gpiopin(pin, &gpio_pin);

    return (uint8_t)gpio_input_data_bit_read(gpio_x, gpio_pin);
}

// void bMcuGpioWritePort(bHalGPIOPort_t port, uint16_t dat)
// {
//     gpio_type *gpio_x;
//     if (transfer_at32_gpioport(port, &gpio_x))
//     {
//         return;
//     }
// }

// uint16_t bMcuGpioReadPort(bHalGPIOPort_t port)
// {
//     gpio_type *gpio_x;
//     transfer_at32_gpioport(port, &gpio_x);
// }

#endif
/************************ Copyright (c) 2020 Bean *****END OF FILE****/
