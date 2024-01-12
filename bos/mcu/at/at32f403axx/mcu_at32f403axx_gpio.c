/**
 * \file        mcu_at32f403axx_gpio.c
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
#include "utils/inc/b_util_log.h"

#if defined(AT32F403Axx)
static const char *TAG = "BOS_MCU_AT_AT32F403A";

void _assert(const char *str_, const char *tag, uint32_t location)
{
    b_log_e("Location: %s %d.\r\n", tag, location);
    if (str_ != NULL)
    {
        b_log_e("Assert info: %s.\r\n", str_);
    }
    while (1)
    {
    }
}

#define assert(test_)                       \
    do                                      \
    {                                       \
        if (!(test_))                       \
            _assert(#test_, TAG, __LINE__); \
    } while (0)

#pragma anon_unions   // 在使用匿名联合的地方添加这个指令
#define __IO volatile /*!< Defines 'read / write' permissions */
/**
 * @brief confirm state
 */
typedef enum
{
    FALSE = 0,
    TRUE  = !FALSE
} confirm_state;

/*************** AT32F403A_407_Firmware start ***************/
#define PERIPH_BASE ((uint32_t)0x40000000)
#define APB1PERIPH_BASE (PERIPH_BASE + 0x00000)
#define APB2PERIPH_BASE (PERIPH_BASE + 0x10000)
#define AHBPERIPH_BASE (PERIPH_BASE + 0x20000)
#define CRM_BASE (AHBPERIPH_BASE + 0x1000)

#define REG8(addr) *(volatile uint8_t *)(addr)
#define REG16(addr) *(volatile uint16_t *)(addr)
#define REG32(addr) *(volatile uint32_t *)(addr)
#define PERIPH_REG(periph_base, value) REG32((periph_base + (value >> 16)))
#define PERIPH_REG_BIT(value) (0x1U << (value & 0x1F))
#define CRM_REG(value) PERIPH_REG(CRM_BASE, value)
#define CRM_REG_BIT(value) PERIPH_REG_BIT(value)

#define MAKE_VALUE(reg_offset, bit_num) (uint32_t)(((reg_offset) << 16) | (bit_num & 0x1F))

typedef enum
{
    /* apb2 periph */
    CRM_IOMUX_PERIPH_CLOCK = MAKE_VALUE(0x18, 0), /*!< iomux periph clock */
    CRM_GPIOA_PERIPH_CLOCK = MAKE_VALUE(0x18, 2), /*!< gpioa periph clock */
    CRM_GPIOB_PERIPH_CLOCK = MAKE_VALUE(0x18, 3), /*!< gpiob periph clock */
    CRM_GPIOC_PERIPH_CLOCK = MAKE_VALUE(0x18, 4), /*!< gpioc periph clock */
    CRM_GPIOD_PERIPH_CLOCK = MAKE_VALUE(0x18, 5), /*!< gpiod periph clock */
    CRM_GPIOE_PERIPH_CLOCK = MAKE_VALUE(0x18, 6), /*!< gpioe periph clock */

    CRM_ERROR_PERIPH_CLOCK = 0XFF,

} crm_periph_clock_type;

typedef enum
{
    GPIO_OUTPUT_PUSH_PULL  = 0x00, /*!< output push-pull */
    GPIO_OUTPUT_OPEN_DRAIN = 0x04, /*!< output open-drain */

    GPIO_OUTPUT_ERROR = 0XFF
} gpio_output_type;

typedef enum
{
    GPIO_PULL_NONE = 0x0004, /*!< floating for input, no pull for output */
    GPIO_PULL_UP   = 0x0018, /*!< pull-up */
    GPIO_PULL_DOWN = 0x0028, /*!< pull-down */

    GPIO_PULL_ERROR = 0xFFFF
} gpio_pull_type;

typedef enum
{
    GPIO_MODE_INPUT  = 0x00, /*!< gpio input mode */
    GPIO_MODE_OUTPUT = 0x10, /*!< gpio output mode */
    GPIO_MODE_MUX    = 0x08, /*!< gpio mux function mode */
    GPIO_MODE_ANALOG = 0x03, /*!< gpio analog in/out mode */

    GPIO_MODE_ERROR = 0xFF
} gpio_mode_type;

typedef enum
{
    GPIO_DRIVE_STRENGTH_STRONGER = 0x01, /*!< stronger sourcing/sinking strength */
    GPIO_DRIVE_STRENGTH_MODERATE = 0x02  /*!< moderate sourcing/sinking strength */
} gpio_drive_type;

typedef struct
{
    uint32_t         gpio_pins;           /*!< pins number selection */
    gpio_output_type gpio_out_type;       /*!< output type selection */
    gpio_pull_type   gpio_pull;           /*!< pull type selection */
    gpio_mode_type   gpio_mode;           /*!< mode selection */
    gpio_drive_type  gpio_drive_strength; /*!< drive strength selection */
} gpio_init_type;

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

/**
 * @brief type define iomux register all
 */
typedef struct
{
    /**
     * @brief mux event control register, offset:0x00
     */
    union
    {
        __IO uint32_t evtout;
        struct
        {
            __IO uint32_t selpin : 4;     /* [3:0] */
            __IO uint32_t selport : 3;    /* [6:4] */
            __IO uint32_t evoen : 1;      /* [7]   */
            __IO uint32_t reserved1 : 24; /* [31:8] */
        } evtout_bit;
    };

    /**
     * @brief iomux remap register, offset:0x04
     */
    union
    {
        __IO uint32_t remap;
        struct
        {
            __IO uint32_t spi1_mux_l : 1;        /* [0] */
            __IO uint32_t i2c1_mux : 1;          /* [1] */
            __IO uint32_t usart1_mux : 1;        /* [2] */
            __IO uint32_t usart2_mux : 1;        /* [3] */
            __IO uint32_t usart3_mux : 2;        /* [5:4] */
            __IO uint32_t tmr1_mux : 2;          /* [7:6] */
            __IO uint32_t tmr2_mux : 2;          /* [9:8] */
            __IO uint32_t tmr3_mux : 2;          /* [11:10] */
            __IO uint32_t tmr4_mux : 1;          /* [12] */
            __IO uint32_t can_mux : 2;           /* [14:13] */
            __IO uint32_t pd01_mux : 1;          /* [15] */
            __IO uint32_t tmr5ch4_mux : 1;       /* [16] */
            __IO uint32_t adc1_extrgpre_mux : 1; /* [17] */
            __IO uint32_t adc1_extrgord_mux : 1; /* [18] */
            __IO uint32_t adc2_extrgpre_mux : 1; /* [19] */
            __IO uint32_t adc2_extrgord_mux : 1; /* [20] */
            __IO uint32_t emac_mux : 1;          /* [21] */
            __IO uint32_t can2_mux : 1;          /* [22] */
            __IO uint32_t mii_rmii_sel_mux : 1;  /* [23] */
            __IO uint32_t swjtag_mux : 3;        /* [26:24] */
            __IO uint32_t reserved1 : 1;         /* [27] */
            __IO uint32_t spi3_mux : 1;          /* [28] */
            __IO uint32_t tim2itr1_mux : 1;      /* [29] */
            __IO uint32_t ptp_pps_mux : 1;       /* [30] */
            __IO uint32_t spi1_mux_h : 1;        /* [31] */
        } remap_bit;
    };

    /**
     * @brief mux external interrupt configuration register 1, offset:0x08
     */
    union
    {
        __IO uint32_t exintc1;
        struct
        {
            __IO uint32_t exint0 : 4;     /* [3:0] */
            __IO uint32_t exint1 : 4;     /* [7:4] */
            __IO uint32_t exint2 : 4;     /* [11:8] */
            __IO uint32_t exint3 : 4;     /* [15:12] */
            __IO uint32_t reserved1 : 16; /* [31:16] */
        } exintc1_bit;
    };

    /**
     * @brief mux external interrupt configuration register 2, offset:0x0c
     */
    union
    {
        __IO uint32_t exintc2;
        struct
        {
            __IO uint32_t exint4 : 4;     /* [3:0] */
            __IO uint32_t exint5 : 4;     /* [7:4] */
            __IO uint32_t exint6 : 4;     /* [11:8] */
            __IO uint32_t exint7 : 4;     /* [15:12] */
            __IO uint32_t reserved1 : 16; /* [31:16] */
        } exintc2_bit;
    };

    /**
     * @brief mux external interrupt configuration register 3, offset:0x10
     */
    union
    {
        __IO uint32_t exintc3;
        struct
        {
            __IO uint32_t exint8 : 4;     /* [3:0] */
            __IO uint32_t exint9 : 4;     /* [7:4] */
            __IO uint32_t exint10 : 4;    /* [11:8] */
            __IO uint32_t exint11 : 4;    /* [15:12] */
            __IO uint32_t reserved1 : 16; /* [31:16] */
        } exintc3_bit;
    };

    /**
     * @brief mux external interrupt configuration register 4, offset:0x14
     */
    union
    {
        __IO uint32_t exintc4;
        struct
        {
            __IO uint32_t exint12 : 4;    /* [3:0] */
            __IO uint32_t exint13 : 4;    /* [7:4] */
            __IO uint32_t exint14 : 4;    /* [11:8] */
            __IO uint32_t exint15 : 4;    /* [15:12] */
            __IO uint32_t reserved1 : 16; /* [31:16] */
        } exintc4_bit;
    };

    /**
     * @brief iomux reserved1 register, offset:0x18
     */
    __IO uint32_t reserved1;

    /**
     * @brief iomux remap register 2, offset:0x1C
     */
    union
    {
        __IO uint32_t remap2;
        struct
        {
            __IO uint32_t tmr15_mux : 1;       /* [0] */
            __IO uint32_t reserved1 : 4;       /* [4:1] */
            __IO uint32_t tmr9_mux : 1;        /* [5] */
            __IO uint32_t tmr10_mux : 1;       /* [6] */
            __IO uint32_t tmr11_mux : 1;       /* [7] */
            __IO uint32_t tmr13_mux : 1;       /* [8] */
            __IO uint32_t tmr14_mux : 1;       /* [9] */
            __IO uint32_t xmc_nadv_mux : 1;    /* [10] */
            __IO uint32_t reserved2 : 6;       /* [16:11] */
            __IO uint32_t spi4_mux : 1;        /* [17] */
            __IO uint32_t i2c3_mux : 1;        /* [18] */
            __IO uint32_t sdio2_mux : 2;       /* [20:19] */
            __IO uint32_t ext_spim_en_mux : 1; /* [21] */
            __IO uint32_t reserved3 : 10;      /* [31:22] */
        } remap2_bit;
    };

    /**
     * @brief iomux remap register 3, offset:0x20
     */
    union
    {
        __IO uint32_t remap3;
        struct
        {
            __IO uint32_t tmr9_gmux : 4;  /* [3:0] */
            __IO uint32_t reserved1 : 28; /* [31:4] */
        } remap3_bit;
    };

    /**
     * @brief iomux remap register 4, offset:0x24
     */
    union
    {
        __IO uint32_t remap4;
        struct
        {
            __IO uint32_t tmr1_gmux : 4;     /* [3:0] */
            __IO uint32_t tmr2_gmux : 2;     /* [5:4] */
            __IO uint32_t tmr2itr1_gmux : 2; /* [7:6] */
            __IO uint32_t tmr3_gmux : 4;     /* [11:8] */
            __IO uint32_t tmr4_gmux : 4;     /* [15:12] */
            __IO uint32_t reserved1 : 3;     /* [18:16] */
            __IO uint32_t tmr5ch4_gmux : 1;  /* [19] */
            __IO uint32_t reserved2 : 12;    /* [31:20] */
        } remap4_bit;
    };

    /**
     * @brief iomux remap register 5, offset:0x28
     */
    union
    {
        __IO uint32_t remap5;
        struct
        {
            __IO uint32_t usart5_gmux : 4; /* [3:0] */
            __IO uint32_t i2c1_gmux : 4;   /* [7:4] */
            __IO uint32_t reserved1 : 4;   /* [11:8] */
            __IO uint32_t i2c3_gmux : 4;   /* [15:12] */
            __IO uint32_t spi1_gmux : 4;   /* [19:16] */
            __IO uint32_t spi2_gmux : 4;   /* [23:20] */
            __IO uint32_t spi3_gmux : 4;   /* [27:24] */
            __IO uint32_t spi4_gmux : 4;   /* [31:28] */
        } remap5_bit;
    };

    /**
     * @brief iomux remap register 6, offset:0x2C
     */
    union
    {
        __IO uint32_t remap6;
        struct
        {
            __IO uint32_t can1_gmux : 4;   /* [3:0] */
            __IO uint32_t can2_gmux : 4;   /* [7:4] */
            __IO uint32_t reserved1 : 4;   /* [11:8] */
            __IO uint32_t sdio2_gmux : 4;  /* [15:12] */
            __IO uint32_t usart1_gmux : 4; /* [19:16] */
            __IO uint32_t usart2_gmux : 4; /* [23:20] */
            __IO uint32_t usart3_gmux : 4; /* [27:24] */
            __IO uint32_t uart4_gmux : 4;  /* [31:28] */
        } remap6_bit;
    };

    /**
     * @brief iomux remap register 7, offset:0x30
     */
    union
    {
        __IO uint32_t remap7;
        struct
        {
            __IO uint32_t ext_spim_gmux : 3; /* [2:0] */
            __IO uint32_t ext_spim_gen : 1;  /* [3] */
            __IO uint32_t adc1_etp_gmux : 1; /* [4] */
            __IO uint32_t adc1_eto_gmux : 1; /* [5] */
            __IO uint32_t reserved1 : 2;     /* [7:6] */
            __IO uint32_t adc2_etp_gmux : 1; /* [8] */
            __IO uint32_t adc2_eto_gmux : 1; /* [9] */
            __IO uint32_t reserved2 : 6;     /* [15:10] */
            __IO uint32_t swjtag_gmux : 3;   /* [18:16] */
            __IO uint32_t reserved3 : 1;     /* [19] */
            __IO uint32_t pd01_gmux : 1;     /* [20] */
            __IO uint32_t reserved4 : 3;     /* [23:21] */
            __IO uint32_t xmc_gmux : 3;      /* [26:24] */
            __IO uint32_t xmc_nadv_gmux : 1; /* [27] */
            __IO uint32_t reserved5 : 4;     /* [31:28] */
        } remap7_bit;
    };

    /**
     * @brief iomux remap register 8, offset:0x34
     */
    union
    {
        __IO uint32_t remap8;
        struct
        {
            __IO uint32_t reserved1 : 16;        /* [15:0] */
            __IO uint32_t emac_gmux : 2;         /* [17:16] */
            __IO uint32_t mii_rmii_sel_gmux : 1; /* [18] */
            __IO uint32_t ptp_pps_gmux : 1;      /* [19] */
            __IO uint32_t usart6_gmux : 4;       /* [23:20] */
            __IO uint32_t uart7_gmux : 4;        /* [27:24] */
            __IO uint32_t uart8_gmux : 4;        /* [31:28] */
        } remap8_bit;
    };
} iomux_type;

#define IOMUX_BASE (APB2PERIPH_BASE + 0x0000)
#define GPIOA_BASE (APB2PERIPH_BASE + 0x0800)
#define GPIOB_BASE (APB2PERIPH_BASE + 0x0C00)
#define GPIOC_BASE (APB2PERIPH_BASE + 0x1000)
#define GPIOD_BASE (APB2PERIPH_BASE + 0x1400)
#define GPIOE_BASE (APB2PERIPH_BASE + 0x1800)

#define GPIOA ((gpio_type *)GPIOA_BASE)
#define GPIOB ((gpio_type *)GPIOB_BASE)
#define GPIOC ((gpio_type *)GPIOC_BASE)
#define GPIOD ((gpio_type *)GPIOD_BASE)
#define GPIOE ((gpio_type *)GPIOE_BASE)
#define IOMUX ((iomux_type *)IOMUX_BASE)

#define IS_GPIO_TYPE(var)                                                                        \
    ((var == GPIOA_BASE) || (var == GPIOB_BASE) || (var == GPIOC_BASE) || (var == GPIOD_BASE) || \
     (var == GPIOE_BASE))

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

#define IS_GPIO_PINS_TYPE(TYPE)                                                          \
    (((TYPE) == GPIO_PINS_0) || ((TYPE) == GPIO_PINS_1) || ((TYPE) == GPIO_PINS_2) ||    \
     ((TYPE) == GPIO_PINS_3) || ((TYPE) == GPIO_PINS_4) || ((TYPE) == GPIO_PINS_5) ||    \
     ((TYPE) == GPIO_PINS_6) || ((TYPE) == GPIO_PINS_7) || ((TYPE) == GPIO_PINS_8) ||    \
     ((TYPE) == GPIO_PINS_9) || ((TYPE) == GPIO_PINS_10) || ((TYPE) == GPIO_PINS_11) ||  \
     ((TYPE) == GPIO_PINS_12) || ((TYPE) == GPIO_PINS_13) || ((TYPE) == GPIO_PINS_14) || \
     ((TYPE) == GPIO_PINS_15))

/**
 * @brief  enable or disable the peripheral clock
 * @param  value
 *         this parameter can be one of the following values:
 *         - CRM_DMA1_PERIPH_CLOCK         - CRM_DMA2_PERIPH_CLOCK         -
 * CRM_CRC_PERIPH_CLOCK - CRM_XMC_PERIPH_CLOCK
 *         - CRM_SDIO1_PERIPH_CLOCK        - CRM_SDIO2_PERIPH_CLOCK        -
 * CRM_EMAC_PERIPH_CLOCK
 * - CRM_EMACTX_PERIPH_CLOCK
 *         - CRM_EMACRX_PERIPH_CLOCK       - CRM_EMACPTP_PERIPH_CLOCK      -
 * CRM_IOMUX_PERIPH_CLOCK
 * - CRM_GPIOA_PERIPH_CLOCK
 *         - CRM_GPIOB_PERIPH_CLOCK        - CRM_GPIOC_PERIPH_CLOCK        -
 * CRM_GPIOD_PERIPH_CLOCK
 * - CRM_GPIOE_PERIPH_CLOCK
 *         - CRM_ADC1_PERIPH_CLOCK         - CRM_ADC2_PERIPH_CLOCK         -
 * CRM_TMR1_PERIPH_CLOCK
 * - CRM_SPI1_PERIPH_CLOCK
 *         - CRM_TMR8_PERIPH_CLOCK         - CRM_USART1_PERIPH_CLOCK       -
 * CRM_ADC3_PERIPH_CLOCK
 * - CRM_TMR9_PERIPH_CLOCK
 *         - CRM_TMR10_PERIPH_CLOCK        - CRM_TMR11_PERIPH_CLOCK        -
 * CRM_ACC_PERIPH_CLOCK - CRM_I2C3_PERIPH_CLOCK
 *         - CRM_USART6_PERIPH_CLOCK       - CRM_UART7_PERIPH_CLOCK        -
 * CRM_UART8_PERIPH_CLOCK
 * - CRM_TMR2_PERIPH_CLOCK
 *         - CRM_TMR3_PERIPH_CLOCK         - CRM_TMR4_PERIPH_CLOCK         -
 * CRM_TMR5_PERIPH_CLOCK
 * - CRM_TMR6_PERIPH_CLOCK
 *         - CRM_TMR7_PERIPH_CLOCK         - CRM_TMR12_PERIPH_CLOCK        -
 * CRM_TMR13_PERIPH_CLOCK
 * - CRM_TMR14_PERIPH_CLOCK
 *         - CRM_WWDT_PERIPH_CLOCK         - CRM_SPI2_PERIPH_CLOCK         -
 * CRM_SPI3_PERIPH_CLOCK
 * - CRM_SPI4_PERIPH_CLOCK
 *         - CRM_USART2_PERIPH_CLOCK       - CRM_USART3_PERIPH_CLOCK       -
 * CRM_UART4_PERIPH_CLOCK
 * - CRM_UART5_PERIPH_CLOCK
 *         - CRM_I2C1_PERIPH_CLOCK         - CRM_I2C2_PERIPH_CLOCK         -
 * CRM_USB_PERIPH_CLOCK - CRM_CAN1_PERIPH_CLOCK
 *         - CRM_CAN2_PERIPH_CLOCK         - CRM_BPR_PERIPH_CLOCK          -
 * CRM_PWC_PERIPH_CLOCK - CRM_DAC_PERIPH_CLOCK
 * @param  new_state (TRUE or FALSE)
 * @retval none
 */
static void crm_periph_clock_enable(crm_periph_clock_type value, confirm_state new_state)
{
    /* enable periph clock */
    if (TRUE == new_state)
    {
        CRM_REG(value) |= CRM_REG_BIT(value);
    }
    /* disable periph clock */
    else
    {
        CRM_REG(value) &= ~(CRM_REG_BIT(value));
    }
}

/**
 * @brief  fill each gpio_init_type member with its default value.
 * @param  gpio_init_struct : pointer to a gpio_init_type structure which will be initialized.
 * @retval none
 */
static void gpio_default_para_init(gpio_init_type *gpio_init_struct)
{
    /* reset gpio init structure parameters values */
    gpio_init_struct->gpio_pins           = GPIO_PINS_ALL;
    gpio_init_struct->gpio_mode           = GPIO_MODE_INPUT;
    gpio_init_struct->gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
    gpio_init_struct->gpio_pull           = GPIO_PULL_NONE;
    gpio_init_struct->gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
}

/**
 * @brief  initialize the gpio peripheral.
 * @param  gpio_x: to select the gpio peripheral.
 *         this parameter can be one of the following values:
 *         GPIOA, GPIOB, GPIOC, GPIOD, GPIOE.
 * @param  gpio_init_struct: pointer to gpio init structure.
 * @retval none
 */
static void gpio_init(gpio_type *gpio_x, gpio_init_type *gpio_init_struct)
{
    uint32_t temp;
    uint16_t pinx_value, pin_index;

    pin_index = (uint16_t)gpio_init_struct->gpio_pins;

    /* pinx_value indecate pin grounp bit[3:0] from modey[1:0] confy[1:0] */

    /* pin input analog config */
    if (gpio_init_struct->gpio_mode == GPIO_MODE_ANALOG)
    {
        pinx_value = 0x00;
    }
    /* pin input config */
    else if (gpio_init_struct->gpio_mode == GPIO_MODE_INPUT)
    {
        pinx_value = gpio_init_struct->gpio_pull & 0x0F;

        if (gpio_init_struct->gpio_pull == GPIO_PULL_UP)
        {
            gpio_x->scr = pin_index;
        }
        else if (gpio_init_struct->gpio_pull == GPIO_PULL_DOWN)
        {
            gpio_x->clr = pin_index;
        }
    }
    else
    {
        pinx_value = (gpio_init_struct->gpio_mode & 0x08) |
                     (gpio_init_struct->gpio_out_type & 0x04) |
                     (gpio_init_struct->gpio_drive_strength & 0x03);
    }

    /* pin 0~7 config */
    if (((uint32_t)pin_index & ((uint32_t)0x00FF)) != 0x00)
    {
        for (temp = 0; temp < 0x08; temp++)
        {
            if ((1 << temp) & pin_index)
            {
                gpio_x->cfglr &= (uint32_t) ~(0x0F << (temp * 4));
                gpio_x->cfglr |= (uint32_t)(pinx_value << (temp * 4));
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
                gpio_x->cfghr &= (uint32_t) ~(0xf << (temp * 4));
                gpio_x->cfghr |= (uint32_t)(pinx_value << (temp * 4));
            }
        }
    }
}

/*************** AT32F403A_407_Firmware end ***************/
#define IS_CRM_PERIPH_CLOCK_TYPE(TYPE)                                           \
    (((TYPE) == CRM_GPIOA_PERIPH_CLOCK) || ((TYPE) == CRM_GPIOB_PERIPH_CLOCK) || \
     ((TYPE) == CRM_GPIOC_PERIPH_CLOCK) || ((TYPE) == CRM_GPIOD_PERIPH_CLOCK) || \
     ((TYPE) == CRM_GPIOE_PERIPH_CLOCK))

#define IS_GPIO_OUTPUT_TYPE(TYPE) \
    (((TYPE) == GPIO_OUTPUT_PUSH_PULL) || ((TYPE) == GPIO_OUTPUT_OPEN_DRAIN))

#define IS_GPIO_PULL_TYPE(TYPE) \
    (((TYPE) == GPIO_PULL_NONE) || ((TYPE) == GPIO_PULL_UP) || ((TYPE) == GPIO_PULL_DOWN))

#define IS_GPIO_MODE_TYPE(TYPE) (((TYPE) == GPIO_MODE_INPUT) || ((TYPE) == GPIO_MODE_OUTPUT))

#define IS_GPIO_DRIVE_TYPE(TYPE) \
    (((TYPE) == GPIO_DRIVE_STRENGTH_STRONGER) || ((TYPE) == GPIO_DRIVE_STRENGTH_MODERATE))

static gpio_type *transform_bos_port(bHalGPIOPort_t port)
{
    gpio_type *gpio_x = NULL;

    switch (port)
    {
        case B_HAL_GPIOA:
            gpio_x = GPIOA;
            break;
        case B_HAL_GPIOB:
            gpio_x = GPIOB;
            break;
        case B_HAL_GPIOC:
            gpio_x = GPIOC;
            break;
        case B_HAL_GPIOD:
            gpio_x = GPIOD;
            break;
        case B_HAL_GPIOE:
            gpio_x = GPIOE;
            break;
        default:
            break;
    }

    return gpio_x;
}

// pin转换
static uint32_t transform_bos_pin(bHalGPIOPin_t pin)
{
    uint32_t gpio_pins_X = 0;

    switch (pin)
    {
        case B_HAL_PIN0:
            gpio_pins_X = GPIO_PINS_0;
            break;
        case B_HAL_PIN1:
            gpio_pins_X = GPIO_PINS_1;
            break;
        case B_HAL_PIN2:
            gpio_pins_X = GPIO_PINS_2;
            break;
        case B_HAL_PIN3:
            gpio_pins_X = GPIO_PINS_3;
            break;
        case B_HAL_PIN4:
            gpio_pins_X = GPIO_PINS_4;
            break;
        case B_HAL_PIN5:
            gpio_pins_X = GPIO_PINS_5;
            break;
        case B_HAL_PIN6:
            gpio_pins_X = GPIO_PINS_6;
            break;
        case B_HAL_PIN7:
            gpio_pins_X = GPIO_PINS_7;
            break;
        case B_HAL_PIN8:
            gpio_pins_X = GPIO_PINS_8;
            break;
        case B_HAL_PIN9:
            gpio_pins_X = GPIO_PINS_9;
            break;
        case B_HAL_PIN10:
            gpio_pins_X = GPIO_PINS_10;
            break;
        case B_HAL_PIN11:
            gpio_pins_X = GPIO_PINS_11;
            break;
        case B_HAL_PIN12:
            gpio_pins_X = GPIO_PINS_12;
            break;
        case B_HAL_PIN13:
            gpio_pins_X = GPIO_PINS_13;
            break;
        case B_HAL_PIN14:
            gpio_pins_X = GPIO_PINS_14;
            break;
        case B_HAL_PIN15:
            gpio_pins_X = GPIO_PINS_15;
            break;
        default:
            break;
    }

    return gpio_pins_X;
}

// dir转换
static gpio_mode_type transform_bos_dir(bHalGPIODir_t dir)
{
    gpio_mode_type gpio_mode = GPIO_MODE_ERROR;

    switch (dir)
    {
        case B_HAL_GPIO_INPUT:
            gpio_mode = GPIO_MODE_INPUT;
            break;
        case B_HAL_GPIO_OUTPUT:
            gpio_mode = GPIO_MODE_OUTPUT;
            break;

        default:
            break;
    }

    return gpio_mode;
}

// pull转换
static gpio_pull_type transform_bos_pull(bHalGPIOPull_t pull)
{
    gpio_pull_type gpio_pull = GPIO_PULL_ERROR;

    switch (pull)
    {
        case B_HAL_GPIO_NOPULL:
            gpio_pull = GPIO_PULL_NONE;
            break;
        case B_HAL_GPIO_PULLUP:
            gpio_pull = GPIO_PULL_UP;
            break;
        case B_HAL_GPIO_PULLDOWN:
            gpio_pull = GPIO_PULL_DOWN;
            break;

        default:
            break;
    }

    return gpio_pull;
}

// clock转换
static crm_periph_clock_type transform_bos_clock(bHalGPIOPort_t port)
{
    crm_periph_clock_type CRM_GPIOX_PERIPH_CLOCK = CRM_ERROR_PERIPH_CLOCK;

    switch (port)
    {
        case B_HAL_GPIOA:
            CRM_GPIOX_PERIPH_CLOCK = CRM_GPIOA_PERIPH_CLOCK;
            break;
        case B_HAL_GPIOB:
            CRM_GPIOX_PERIPH_CLOCK = CRM_GPIOB_PERIPH_CLOCK;
            break;
        case B_HAL_GPIOC:
            CRM_GPIOX_PERIPH_CLOCK = CRM_GPIOC_PERIPH_CLOCK;
            break;
        case B_HAL_GPIOD:
            CRM_GPIOX_PERIPH_CLOCK = CRM_GPIOD_PERIPH_CLOCK;
            break;
        case B_HAL_GPIOE:
            CRM_GPIOX_PERIPH_CLOCK = CRM_GPIOE_PERIPH_CLOCK;
            break;

        default:
            break;
    }

    assert(CRM_GPIOX_PERIPH_CLOCK == CRM_GPIOA_PERIPH_CLOCK ||
           CRM_GPIOX_PERIPH_CLOCK == CRM_GPIOB_PERIPH_CLOCK ||
           CRM_GPIOX_PERIPH_CLOCK == CRM_GPIOC_PERIPH_CLOCK ||
           CRM_GPIOX_PERIPH_CLOCK == CRM_GPIOD_PERIPH_CLOCK ||
           CRM_GPIOX_PERIPH_CLOCK == CRM_GPIOE_PERIPH_CLOCK);

    return CRM_GPIOX_PERIPH_CLOCK;
}

void bMcuGpioConfig(bHalGPIOPort_t port, bHalGPIOPin_t pin, bHalGPIODir_t dir, bHalGPIOPull_t pull)
{
    uint32_t              gpio_pins_X            = transform_bos_pin(pin);
    gpio_type            *gpio_x                 = transform_bos_port(port);
    crm_periph_clock_type CRM_GPIOX_PERIPH_CLOCK = transform_bos_clock(port);
    gpio_init_type        gpio_init_struct       = {0};

    assert(IS_GPIO_PINS_TYPE(gpio_pins_X));
    assert(IS_CRM_PERIPH_CLOCK_TYPE((uint32_t)gpio_x));
    assert(IS_CRM_PERIPH_CLOCK_TYPE(CRM_GPIOX_PERIPH_CLOCK));

    crm_periph_clock_enable(CRM_GPIOX_PERIPH_CLOCK, TRUE);

    gpio_default_para_init(&gpio_init_struct);

    gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
    gpio_init_struct.gpio_mode           = transform_bos_dir(dir);
    gpio_init_struct.gpio_pins           = gpio_pins_X;
    gpio_init_struct.gpio_pull           = transform_bos_pull(pull);

    assert(IS_GPIO_MODE_TYPE(gpio_init_struct.gpio_mode));
    assert(IS_GPIO_PULL_TYPE(gpio_init_struct.gpio_pull));

    gpio_init(gpio_x, &gpio_init_struct);
}

void bMcuGpioWritePin(bHalGPIOPort_t port, bHalGPIOPin_t pin, uint8_t s)
{
    uint32_t   gpio_pins_X = transform_bos_pin(pin);
    gpio_type *gpio_x      = transform_bos_port(port);

    assert(IS_GPIO_PINS_TYPE(gpio_pins_X));
    assert(IS_GPIO_TYPE((uint32_t)gpio_x));

    if (s)
    {
        gpio_x->scr = gpio_pins_X;
    }
    else
    {
        gpio_x->clr = gpio_pins_X;
    }
}

uint8_t bMcuGpioReadPin(bHalGPIOPort_t port, bHalGPIOPin_t pin)
{
    uint32_t   gpio_pins_X = transform_bos_pin(pin);
    gpio_type *gpio_x      = transform_bos_port(port);

    assert(IS_GPIO_PINS_TYPE(gpio_pins_X));
    assert(IS_CRM_PERIPH_CLOCK_TYPE((uint32_t)gpio_x));

    if (gpio_pins_X != (gpio_pins_X & gpio_x->idt))
    {
        return 0;
    }
    else
    {
        return 1;
    }
}
#endif
