/**
 * \file        mcu_at32f403axx_uart.c
 * \version     v0.0.1
 * \date        2023-11-24
 * \author      miniminiminini (405553848@qq.com)
 * \brief
 *
 * Copyright (c) 2023 by miniminiminini. All Rights Reserved.
 */

#include "b_config.h"
#include "hal/inc/b_hal_uart.h"

#if defined(AT32F403Axx)

#pragma anon_unions       // 在使用匿名联合的地方添加这个指令
#define UNUSED(x) (void)x /* to avoid gcc/g++ warnings */
#define __IO volatile     /*!< Defines 'read / write' permissions */

#define PERIPH_BASE ((uint32_t)0x40000000)
#define AHBPERIPH_BASE (PERIPH_BASE + 0x20000)
#define CRM_BASE (AHBPERIPH_BASE + 0x1000)
#define APB1PERIPH_BASE (PERIPH_BASE + 0x00000)
#define APB2PERIPH_BASE (PERIPH_BASE + 0x10000)
#define USART1_BASE (APB2PERIPH_BASE + 0x3800)
#define USART2_BASE (APB1PERIPH_BASE + 0x4400)
#define USART3_BASE (APB1PERIPH_BASE + 0x4800)
#define UART4_BASE (APB1PERIPH_BASE + 0x4C00)
#define UART5_BASE (APB1PERIPH_BASE + 0x5000)
#define USART6_BASE (APB2PERIPH_BASE + 0x6000)
#define UART7_BASE (APB2PERIPH_BASE + 0x6400)
#define UART8_BASE (APB2PERIPH_BASE + 0x6800)

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
    RESET = 0,
    SET   = !RESET
} flag_status;

typedef struct
{
    /**
     * @brief usart sts register, offset:0x00
     */
    union
    {
        __IO uint32_t sts;
        struct
        {
            __IO uint32_t perr : 1;       /* [0] */
            __IO uint32_t ferr : 1;       /* [1] */
            __IO uint32_t nerr : 1;       /* [2] */
            __IO uint32_t roerr : 1;      /* [3] */
            __IO uint32_t idlef : 1;      /* [4] */
            __IO uint32_t rdbf : 1;       /* [5] */
            __IO uint32_t tdc : 1;        /* [6] */
            __IO uint32_t tdbe : 1;       /* [7] */
            __IO uint32_t bff : 1;        /* [8] */
            __IO uint32_t ctscf : 1;      /* [9] */
            __IO uint32_t reserved1 : 22; /* [31:10] */
        } sts_bit;
    };

    /**
     * @brief usart dt register, offset:0x04
     */
    union
    {
        __IO uint32_t dt;
        struct
        {
            __IO uint32_t dt : 9;         /* [8:0] */
            __IO uint32_t reserved1 : 23; /* [31:9] */
        } dt_bit;
    };

    /**
     * @brief usart baudr register, offset:0x08
     */
    union
    {
        __IO uint32_t baudr;
        struct
        {
            __IO uint32_t div : 16;       /* [15:0] */
            __IO uint32_t reserved1 : 16; /* [31:16] */
        } baudr_bit;
    };

    /**
     * @brief usart ctrl1 register, offset:0x0C
     */
    union
    {
        __IO uint32_t ctrl1;
        struct
        {
            __IO uint32_t sbf : 1;        /* [0] */
            __IO uint32_t rm : 1;         /* [1] */
            __IO uint32_t ren : 1;        /* [2] */
            __IO uint32_t ten : 1;        /* [3] */
            __IO uint32_t idleien : 1;    /* [4] */
            __IO uint32_t rdbfien : 1;    /* [5] */
            __IO uint32_t tdcien : 1;     /* [6] */
            __IO uint32_t tdbeien : 1;    /* [7] */
            __IO uint32_t perrien : 1;    /* [8] */
            __IO uint32_t psel : 1;       /* [9] */
            __IO uint32_t pen : 1;        /* [10] */
            __IO uint32_t wum : 1;        /* [11] */
            __IO uint32_t dbn : 1;        /* [12] */
            __IO uint32_t uen : 1;        /* [13] */
            __IO uint32_t reserved1 : 18; /* [31:14] */
        } ctrl1_bit;
    };

    /**
     * @brief usart ctrl2 register, offset:0x10
     */
    union
    {
        __IO uint32_t ctrl2;
        struct
        {
            __IO uint32_t id : 4;         /* [3:0] */
            __IO uint32_t reserved1 : 1;  /* [4] */
            __IO uint32_t bfbn : 1;       /* [5] */
            __IO uint32_t bfien : 1;      /* [6] */
            __IO uint32_t reserved2 : 1;  /* [7] */
            __IO uint32_t lbcp : 1;       /* [8] */
            __IO uint32_t clkpha : 1;     /* [9] */
            __IO uint32_t clkpol : 1;     /* [10] */
            __IO uint32_t clken : 1;      /* [11] */
            __IO uint32_t stopbn : 2;     /* [13:12] */
            __IO uint32_t linen : 1;      /* [14] */
            __IO uint32_t reserved3 : 17; /* [31:15] */
        } ctrl2_bit;
    };

    /**
     * @brief usart ctrl3 register, offset:0x14
     */
    union
    {
        __IO uint32_t ctrl3;
        struct
        {
            __IO uint32_t errien : 1;     /* [0] */
            __IO uint32_t irdaen : 1;     /* [1] */
            __IO uint32_t irdalp : 1;     /* [2] */
            __IO uint32_t slben : 1;      /* [3] */
            __IO uint32_t scnacken : 1;   /* [4] */
            __IO uint32_t scmen : 1;      /* [5] */
            __IO uint32_t dmaren : 1;     /* [6] */
            __IO uint32_t dmaten : 1;     /* [7] */
            __IO uint32_t rtsen : 1;      /* [8] */
            __IO uint32_t ctsen : 1;      /* [9] */
            __IO uint32_t ctscfien : 1;   /* [10] */
            __IO uint32_t reserved1 : 21; /* [31:11] */
        } ctrl3_bit;
    };

    /**
     * @brief usart gdiv register, offset:0x18
     */
    union
    {
        __IO uint32_t gdiv;
        struct
        {
            __IO uint32_t isdiv : 8;      /* [7:0] */
            __IO uint32_t scgt : 8;       /* [15:8] */
            __IO uint32_t reserved1 : 16; /* [31:16] */
        } gdiv_bit;
    };
} usart_type;

#define USART1 ((usart_type *)USART1_BASE)
#define USART2 ((usart_type *)USART2_BASE)
#define USART3 ((usart_type *)USART3_BASE)
#define UART4 ((usart_type *)UART4_BASE)
#define UART5 ((usart_type *)UART5_BASE)
#define USART6 ((usart_type *)USART6_BASE)
#define UART7 ((usart_type *)UART7_BASE)
#define UART8 ((usart_type *)UART8_BASE)

#define USART_PERR_FLAG ((uint32_t)0x00000001)  /*!< usart parity error flag */
#define USART_FERR_FLAG ((uint32_t)0x00000002)  /*!< usart framing error flag */
#define USART_NERR_FLAG ((uint32_t)0x00000004)  /*!< usart noise error flag */
#define USART_ROERR_FLAG ((uint32_t)0x00000008) /*!< usart receiver overflow error flag */
#define USART_IDLEF_FLAG ((uint32_t)0x00000010) /*!< usart idle flag */
#define USART_RDBF_FLAG ((uint32_t)0x00000020)  /*!< usart receive data buffer full flag */
#define USART_TDC_FLAG ((uint32_t)0x00000040)   /*!< usart transmit data complete flag */
#define USART_TDBE_FLAG ((uint32_t)0x00000080)  /*!< usart transmit data buffer empty flag */
#define USART_BFF_FLAG ((uint32_t)0x00000100)   /*!< usart break frame flag */
#define USART_CTSCF_FLAG ((uint32_t)0x00000200) /*!< usart cts change flag */

static usart_type *UartTable[] = {USART1, USART2, USART3, UART4, UART5, USART6, UART7, UART8};

static flag_status usart_flag_get(usart_type *usart_x, uint32_t flag)
{
    if (usart_x->sts & flag)
    {
        return SET;
    }
    else
    {
        return RESET;
    }
}

static void usart_data_transmit(usart_type *usart_x, uint16_t data)
{
    usart_x->dt = (data & 0x01FF);
}

int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    if (uart > (sizeof(UartTable) / sizeof(usart_type *)) - 1 || pbuf == NULL)
    {
        return -1;
    }

    usart_type *pUart = UartTable[uart];

    for (uint16_t i = 0; i < len; i++)
    {
        while (usart_flag_get(pUart, USART_TDBE_FLAG) == RESET)
            ;
        usart_data_transmit(pUart, pbuf[i]);
    }

    while (usart_flag_get(pUart, USART_TDC_FLAG) == RESET)
        ;

    return len;
}

int bMcuReceive(bHalUartNumber_t uart, uint8_t *pbuf, uint16_t len)
{
    return len;
}

#endif
