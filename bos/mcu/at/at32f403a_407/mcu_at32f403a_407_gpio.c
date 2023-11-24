/**
 * \file        mcu_at32f403a_407_gpio.c
 * \version     v0.0.1
 * \date        2023-11-24
 * \author      miniminiminini (405553848@qq.com)
 * \brief
 *
 * Copyright (c) 2023 by miniminiminini. All Rights Reserved.
 */
/*Includes ----------------------------------------------*/
#include "b_config.h"
#include "hal/inc/b_hal_gpio.h"

#if defined(AT32F403A_407)
#pragma anon_unions   // 在使用匿名联合的地方添加这个指令
#define __IO volatile /*!< Defines 'read / write' permissions */
/**
 * @brief type define gpio register all
 */
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

    /**
     * @brief gpio reserved1 register, offset:0x1C~0x38
     */
    __IO uint32_t reserved1[8];

    /**
     * @brief gpio hdrv register, offset:0x3C
     */
    union
    {
        __IO uint32_t hdrv;
        struct
        {
            __IO uint32_t hdrv0 : 1;      /* [0] */
            __IO uint32_t hdrv1 : 1;      /* [1] */
            __IO uint32_t hdrv2 : 1;      /* [2] */
            __IO uint32_t hdrv3 : 1;      /* [3] */
            __IO uint32_t hdrv4 : 1;      /* [4] */
            __IO uint32_t hdrv5 : 1;      /* [5] */
            __IO uint32_t hdrv6 : 1;      /* [6] */
            __IO uint32_t hdrv7 : 1;      /* [7] */
            __IO uint32_t hdrv8 : 1;      /* [8] */
            __IO uint32_t hdrv9 : 1;      /* [9] */
            __IO uint32_t hdrv10 : 1;     /* [10] */
            __IO uint32_t hdrv11 : 1;     /* [11] */
            __IO uint32_t hdrv12 : 1;     /* [12] */
            __IO uint32_t hdrv13 : 1;     /* [13] */
            __IO uint32_t hdrv14 : 1;     /* [14] */
            __IO uint32_t hdrv15 : 1;     /* [15] */
            __IO uint32_t reserved1 : 16; /* [31:16] */
        } hdrv_bit;
    };
} gpio_type;

void bMcuGpioConfig(bHalGPIOPort_t port, bHalGPIOPin_t pin, bHalGPIODir_t dir, bHalGPIOPull_t pull)
{
}

void bMcuGpioWritePin(bHalGPIOPort_t port, bHalGPIOPin_t pin, uint8_t s)
{
}

uint8_t bMcuGpioReadPin(bHalGPIOPort_t port, bHalGPIOPin_t pin)
{
}
#endif
