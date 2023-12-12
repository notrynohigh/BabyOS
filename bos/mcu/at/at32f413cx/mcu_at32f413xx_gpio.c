/*
 * @FilePath:
 * \EIDEd:\GitHub\activeglove_at32f413_app\BabyOS\bos\mcu\at\at32f413cx\mcu_at32f413xx_gpio.c
 * @Author: miniminiminini
 * @Date: 2023-10-11 07:23:37
 * @LastEditors: miniminiminini
 * @LastEditTime: 2023-10-30 16:48:28
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

#if defined(AT32F413Cx)
#pragma anon_unions   // 在使用匿名联合的地方添加这个指令
#define __IO volatile /*!< Defines 'read / write' permissions */

typedef struct
{
    /**
     * @brief gpio cfglr register, offset:0x00
     */
    union
    {
        __IO uint32_t cfglr;
        struct
        {
            __IO uint32_t iomc0 : 2; /* [1:0] */
            __IO uint32_t iofc0 : 2; /* [3:2] */
            __IO uint32_t iomc1 : 2; /* [5:4] */
            __IO uint32_t iofc1 : 2; /* [7:6] */
            __IO uint32_t iomc2 : 2; /* [9:8] */
            __IO uint32_t iofc2 : 2; /* [11:10] */
            __IO uint32_t iomc3 : 2; /* [13:12] */
            __IO uint32_t iofc3 : 2; /* [15:14] */
            __IO uint32_t iomc4 : 2; /* [17:16] */
            __IO uint32_t iofc4 : 2; /* [19:18] */
            __IO uint32_t iomc5 : 2; /* [21:20] */
            __IO uint32_t iofc5 : 2; /* [23:22] */
            __IO uint32_t iomc6 : 2; /* [25:24] */
            __IO uint32_t iofc6 : 2; /* [27:26] */
            __IO uint32_t iomc7 : 2; /* [29:28] */
            __IO uint32_t iofc7 : 2; /* [31:30] */
        } cfglr_bit;
    };

    /**
     * @brief gpio cfghr register, offset:0x04
     */
    union
    {
        __IO uint32_t cfghr;
        struct
        {
            __IO uint32_t iomc8 : 2;  /* [1:0] */
            __IO uint32_t iofc8 : 2;  /* [3:2] */
            __IO uint32_t iomc9 : 2;  /* [5:4] */
            __IO uint32_t iofc9 : 2;  /* [7:6] */
            __IO uint32_t iomc10 : 2; /* [9:8] */
            __IO uint32_t iofc10 : 2; /* [11:10] */
            __IO uint32_t iomc11 : 2; /* [13:12] */
            __IO uint32_t iofc11 : 2; /* [15:14] */
            __IO uint32_t iomc12 : 2; /* [17:16] */
            __IO uint32_t iofc12 : 2; /* [19:18] */
            __IO uint32_t iomc13 : 2; /* [21:20] */
            __IO uint32_t iofc13 : 2; /* [23:22] */
            __IO uint32_t iomc14 : 2; /* [25:24] */
            __IO uint32_t iofc14 : 2; /* [27:26] */
            __IO uint32_t iomc15 : 2; /* [29:28] */
            __IO uint32_t iofc15 : 2; /* [31:30] */
        } cfghr_bit;
    };

    /**
     * @brief gpio idt register, offset:0x08
     */
    union
    {
        __IO uint32_t idt;
        struct
        {
            __IO uint32_t idt0 : 1;       /* [0] */
            __IO uint32_t idt1 : 1;       /* [1] */
            __IO uint32_t idt2 : 1;       /* [2] */
            __IO uint32_t idt3 : 1;       /* [3] */
            __IO uint32_t idt4 : 1;       /* [4] */
            __IO uint32_t idt5 : 1;       /* [5] */
            __IO uint32_t idt6 : 1;       /* [6] */
            __IO uint32_t idt7 : 1;       /* [7] */
            __IO uint32_t idt8 : 1;       /* [8] */
            __IO uint32_t idt9 : 1;       /* [9] */
            __IO uint32_t idt10 : 1;      /* [10] */
            __IO uint32_t idt11 : 1;      /* [11] */
            __IO uint32_t idt12 : 1;      /* [12] */
            __IO uint32_t idt13 : 1;      /* [13] */
            __IO uint32_t idt14 : 1;      /* [14] */
            __IO uint32_t idt15 : 1;      /* [15] */
            __IO uint32_t reserved1 : 16; /* [31:16] */
        } idt_bit;
    };

    /**
     * @brief gpio odt register, offset:0x0C
     */
    union
    {
        __IO uint32_t odt;
        struct
        {
            __IO uint32_t odt0 : 1;       /* [0] */
            __IO uint32_t odt1 : 1;       /* [1] */
            __IO uint32_t odt2 : 1;       /* [2] */
            __IO uint32_t odt3 : 1;       /* [3] */
            __IO uint32_t odt4 : 1;       /* [4] */
            __IO uint32_t odt5 : 1;       /* [5] */
            __IO uint32_t odt6 : 1;       /* [6] */
            __IO uint32_t odt7 : 1;       /* [7] */
            __IO uint32_t odt8 : 1;       /* [8] */
            __IO uint32_t odt9 : 1;       /* [9] */
            __IO uint32_t odt10 : 1;      /* [10] */
            __IO uint32_t odt11 : 1;      /* [11] */
            __IO uint32_t odt12 : 1;      /* [12] */
            __IO uint32_t odt13 : 1;      /* [13] */
            __IO uint32_t odt14 : 1;      /* [14] */
            __IO uint32_t odt15 : 1;      /* [15] */
            __IO uint32_t reserved1 : 16; /* [31:16] */
        } odt_bit;
    };

    /**
     * @brief gpio scr register, offset:0x10
     */
    union
    {
        __IO uint32_t scr;
        struct
        {
            __IO uint32_t iosb0 : 1;  /* [0] */
            __IO uint32_t iosb1 : 1;  /* [1] */
            __IO uint32_t iosb2 : 1;  /* [2] */
            __IO uint32_t iosb3 : 1;  /* [3] */
            __IO uint32_t iosb4 : 1;  /* [4] */
            __IO uint32_t iosb5 : 1;  /* [5] */
            __IO uint32_t iosb6 : 1;  /* [6] */
            __IO uint32_t iosb7 : 1;  /* [7] */
            __IO uint32_t iosb8 : 1;  /* [8] */
            __IO uint32_t iosb9 : 1;  /* [9] */
            __IO uint32_t iosb10 : 1; /* [10] */
            __IO uint32_t iosb11 : 1; /* [11] */
            __IO uint32_t iosb12 : 1; /* [12] */
            __IO uint32_t iosb13 : 1; /* [13] */
            __IO uint32_t iosb14 : 1; /* [14] */
            __IO uint32_t iosb15 : 1; /* [15] */
            __IO uint32_t iocb0 : 1;  /* [16] */
            __IO uint32_t iocb1 : 1;  /* [17] */
            __IO uint32_t iocb2 : 1;  /* [18] */
            __IO uint32_t iocb3 : 1;  /* [19] */
            __IO uint32_t iocb4 : 1;  /* [20] */
            __IO uint32_t iocb5 : 1;  /* [21] */
            __IO uint32_t iocb6 : 1;  /* [22] */
            __IO uint32_t iocb7 : 1;  /* [23] */
            __IO uint32_t iocb8 : 1;  /* [24] */
            __IO uint32_t iocb9 : 1;  /* [25] */
            __IO uint32_t iocb10 : 1; /* [26] */
            __IO uint32_t iocb11 : 1; /* [27] */
            __IO uint32_t iocb12 : 1; /* [28] */
            __IO uint32_t iocb13 : 1; /* [29] */
            __IO uint32_t iocb14 : 1; /* [30] */
            __IO uint32_t iocb15 : 1; /* [31] */
        } scr_bit;
    };

    /**
     * @brief gpio clr register, offset:0x14
     */
    union
    {
        __IO uint32_t clr;
        struct
        {
            __IO uint32_t iocb0 : 1;      /* [0] */
            __IO uint32_t iocb1 : 1;      /* [1] */
            __IO uint32_t iocb2 : 1;      /* [2] */
            __IO uint32_t iocb3 : 1;      /* [3] */
            __IO uint32_t iocb4 : 1;      /* [4] */
            __IO uint32_t iocb5 : 1;      /* [5] */
            __IO uint32_t iocb6 : 1;      /* [6] */
            __IO uint32_t iocb7 : 1;      /* [7] */
            __IO uint32_t iocb8 : 1;      /* [8] */
            __IO uint32_t iocb9 : 1;      /* [9] */
            __IO uint32_t iocb10 : 1;     /* [10] */
            __IO uint32_t iocb11 : 1;     /* [11] */
            __IO uint32_t iocb12 : 1;     /* [12] */
            __IO uint32_t iocb13 : 1;     /* [13] */
            __IO uint32_t iocb14 : 1;     /* [14] */
            __IO uint32_t iocb15 : 1;     /* [15] */
            __IO uint32_t reserved1 : 16; /* [31:16] */
        } clr_bit;
    };

    /**
     * @brief gpio wpr register, offset:0x18
     */
    union
    {
        __IO uint32_t wpr;
        struct
        {
            __IO uint32_t wpen0 : 1;      /* [0] */
            __IO uint32_t wpen1 : 1;      /* [1] */
            __IO uint32_t wpen2 : 1;      /* [2] */
            __IO uint32_t wpen3 : 1;      /* [3] */
            __IO uint32_t wpen4 : 1;      /* [4] */
            __IO uint32_t wpen5 : 1;      /* [5] */
            __IO uint32_t wpen6 : 1;      /* [6] */
            __IO uint32_t wpen7 : 1;      /* [7] */
            __IO uint32_t wpen8 : 1;      /* [8] */
            __IO uint32_t wpen9 : 1;      /* [9] */
            __IO uint32_t wpen10 : 1;     /* [10] */
            __IO uint32_t wpen11 : 1;     /* [11] */
            __IO uint32_t wpen12 : 1;     /* [12] */
            __IO uint32_t wpen13 : 1;     /* [13] */
            __IO uint32_t wpen14 : 1;     /* [14] */
            __IO uint32_t wpen15 : 1;     /* [15] */
            __IO uint32_t wpseq : 1;      /* [16] */
            __IO uint32_t reserved1 : 15; /* [31:17] */
        } wpr_bit;
    };

} McuGpioReg_t;

typedef enum
{
    GPIO_OUTPUT_PUSH_PULL  = 0x00, /*!< output push-pull */
    GPIO_OUTPUT_OPEN_DRAIN = 0x04  /*!< output open-drain */
} gpio_output_type;

typedef enum
{
    GPIO_PULL_NONE = 0x0004, /*!< floating for input, no pull for output */
    GPIO_PULL_UP   = 0x0018, /*!< pull-up */
    GPIO_PULL_DOWN = 0x0028  /*!< pull-down */
} gpio_pull_type;

typedef enum
{
    GPIO_MODE_INPUT  = 0x00, /*!< gpio input mode */
    GPIO_MODE_OUTPUT = 0x10, /*!< gpio output mode */
    GPIO_MODE_MUX    = 0x08, /*!< gpio mux function mode */
    GPIO_MODE_ANALOG = 0x03  /*!< gpio analog in/out mode */
} gpio_mode_type;

typedef enum
{
    GPIO_DRIVE_STRENGTH_STRONGER = 0x01, /*!< stronger sourcing/sinking strength */
    GPIO_DRIVE_STRENGTH_MODERATE = 0x02, /*!< moderate sourcing/sinking strength */
    GPIO_DRIVE_STRENGTH_MAXIMUM  = 0x03  /*!< maximum sourcing/sinking strength */
} gpio_drive_type;

typedef struct
{
    uint32_t         gpio_pins;           /*!< pins number selection */
    gpio_output_type gpio_out_type;       /*!< output type selection */
    gpio_pull_type   gpio_pull;           /*!< pull type selection */
    gpio_mode_type   gpio_mode;           /*!< mode selection */
    gpio_drive_type  gpio_drive_strength; /*!< drive strength selection */
} gpio_init_type;

#define GPIO_PINS_0 0x0001   /*!< gpio pins number 0 */
#define GPIO_PINS_1 0x0002   /*!< gpio pins number 1 */
#define GPIO_PINS_2 0x0004   /*!< gpio pins number 2 */
#define GPIO_PINS_3 0x0008   /*!< gpio pins number 3 */
#define GPIO_PINS_4 0x0010   /*!< gpio pins number 4 */
#define GPIO_PINS_5 0x0020   /*!< gpio pins number 5 */
#define GPIO_PINS_6 0x0040   /*!< gpio pins number 6 */
#define GPIO_PINS_7 0x0080   /*!< gpio pins number 7 */
#define GPIO_PINS_8 0x0100   /*!< gpio pins number 8 */
#define GPIO_PINS_9 0x0200   /*!< gpio pins number 9 */
#define GPIO_PINS_10 0x0400  /*!< gpio pins number 10 */
#define GPIO_PINS_11 0x0800  /*!< gpio pins number 11 */
#define GPIO_PINS_12 0x1000  /*!< gpio pins number 12 */
#define GPIO_PINS_13 0x2000  /*!< gpio pins number 13 */
#define GPIO_PINS_14 0x4000  /*!< gpio pins number 14 */
#define GPIO_PINS_15 0x8000  /*!< gpio pins number 15 */
#define GPIO_PINS_ALL 0xFFFF /*!< gpio all pins */

#define GPIO_REG_OFF (0x400UL)
#define GPIO_REG_BASE (0x40010800UL)

void bMcuGpioConfig(bHalGPIOPort_t port, bHalGPIOPin_t pin, bHalGPIODir_t dir, bHalGPIOPull_t pull)
{
    uint32_t      temp;
    uint16_t      pinx_value, pin_index;
    McuGpioReg_t *pGpio     = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    uint32_t      gpio_pin  = 0x00000001 << pin;
    uint32_t      gpio_mode = (dir == B_HAL_GPIO_INPUT) ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT;
    uint32_t      gpio_pull = (pull <= B_HAL_GPIO_PULLUP) ? pull * 20 + 4 : GPIO_PULL_DOWN;

    if (port >= B_HAL_GPIO_INVALID || pin >= B_HAL_PIN_INVALID)
    {
        return;
    }
    if (dir >= B_HAL_GPIO_DIR_INVALID)
    {
        return;
    }
    if (pull >= B_HAL_GPIO_PULL_INVALID)
    {
        return;
    }

    gpio_init_type gpio_init_struct;

    /* reset gpio init structure parameters values */
    gpio_init_struct.gpio_pins           = GPIO_PINS_ALL;
    gpio_init_struct.gpio_mode           = GPIO_MODE_INPUT;
    gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
    gpio_init_struct.gpio_pull           = GPIO_PULL_NONE;
    gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;

    gpio_init_struct.gpio_mode = (gpio_mode_type)gpio_mode;
    gpio_init_struct.gpio_pull = (gpio_pull_type)gpio_pull;
    gpio_init_struct.gpio_pins = gpio_pin;

    pin_index = (uint16_t)gpio_init_struct.gpio_pins;

    /* pinx_value indecate pin grounp bit[3:0] from modey[1:0] confy[1:0] */

    /* pin input analog config */
    if (gpio_init_struct.gpio_mode == GPIO_MODE_ANALOG)
    {
        pinx_value = 0x00;
    }
    /* pin input config */
    else if (gpio_init_struct.gpio_mode == GPIO_MODE_INPUT)
    {
        pinx_value = gpio_init_struct.gpio_pull & 0x0F;

        if (gpio_init_struct.gpio_pull == GPIO_PULL_UP)
        {
            pGpio->scr = pin_index;
        }
        else if (gpio_init_struct.gpio_pull == GPIO_PULL_DOWN)
        {
            pGpio->clr = pin_index;
        }
    }
    else
    {
        pinx_value = (gpio_init_struct.gpio_mode & 0x08) | (gpio_init_struct.gpio_out_type & 0x04) |
                     (gpio_init_struct.gpio_drive_strength & 0x03);
    }

    /* pin 0~7 config */
    if (((uint32_t)pin_index & ((uint32_t)0x00FF)) != 0x00)
    {
        for (temp = 0; temp < 0x08; temp++)
        {
            if ((1 << temp) & pin_index)
            {
                pGpio->cfglr &= (uint32_t) ~(0x0F << (temp * 4));
                pGpio->cfglr |= (uint32_t)(pinx_value << (temp * 4));
            }
        }
    }

    /* pin 8~15 config */
    if (pin_index > 0x00ff)
    {
        pin_index = pin_index >> 8;

        for (temp = 0; temp < 0x8; temp++)
        {
            if ((1 << temp) & pin_index)
            {
                pGpio->cfghr &= (uint32_t) ~(0xf << (temp * 4));
                pGpio->cfghr |= (uint32_t)(pinx_value << (temp * 4));
            }
        }
    }
}

void bMcuGpioWritePin(bHalGPIOPort_t port, bHalGPIOPin_t pin, uint8_t s)
{
    McuGpioReg_t *pGpio     = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    uint32_t      GPIO_PINS = 0x00000001 << pin;

    if (port >= B_HAL_GPIO_INVALID || pin >= B_HAL_PIN_INVALID || pin == B_HAL_PINAll)
    {
        return;
    }
    if (s)
    {
        pGpio->scr = GPIO_PINS;
    }
    else
    {
        pGpio->clr = GPIO_PINS;
    }
}

uint8_t bMcuGpioReadPin(bHalGPIOPort_t port, bHalGPIOPin_t pin)
{
    McuGpioReg_t *pGpio     = (McuGpioReg_t *)(GPIO_REG_BASE + port * GPIO_REG_OFF);
    uint32_t      GPIO_PINS = 0x00000001 << pin;

    if (port >= B_HAL_GPIO_INVALID || pin >= B_HAL_PIN_INVALID || pin == B_HAL_PINAll)
    {
        return 0;
    }

    if (GPIO_PINS != (GPIO_PINS & pGpio->idt))
    {
        return 0;
    }
    else
    {
        return 1;
    }
}
#endif
/************************ Copyright (c) 2020 Bean *****END OF FILE****/
