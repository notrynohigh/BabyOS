/**
 *!
 * \file        mcu_at32f403a_uart.c
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SUARTL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "b_config.h"
#include "hal/inc/b_hal_uart.h"

#if (defined(AT32F403A))
#include "at32f403a_407_usart.h"

//      Register Address

#define UART1_BASE_ADDR (USART1_BASE)
#define UART2_BASE_ADDR (USART2_BASE)
#define UART3_BASE_ADDR (USART3_BASE)
#define UART4_BASE_ADDR (UART4_BASE)
#define UART5_BASE_ADDR (UART5_BASE)
#define UART6_BASE_ADDR (USART6_BASE)
#define UART7_BASE_ADDR (UART7_BASE)
#define UART8_BASE_ADDR (UART8_BASE)

/**
 * @brief type define usart register all
 */
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
} McuUartReg_t;

#define McuUartReg_t usart_type

#define MCU_UART1 ((McuUartReg_t *)UART1_BASE_ADDR)
#define MCU_UART2 ((McuUartReg_t *)UART2_BASE_ADDR)
#define MCU_UART3 ((McuUartReg_t *)UART3_BASE_ADDR)
#define MCU_UART4 ((McuUartReg_t *)UART4_BASE_ADDR)
#define MCU_UART5 ((McuUartReg_t *)UART5_BASE_ADDR)
#define MCU_UART6 ((McuUartReg_t *)UART6_BASE_ADDR)
#define MCU_UART7 ((McuUartReg_t *)UART7_BASE_ADDR)
#define MCU_UART8 ((McuUartReg_t *)UART8_BASE_ADDR)

static McuUartReg_t *UartTable[8] = {MCU_UART1, MCU_UART2, MCU_UART3, MCU_UART4,
                                     MCU_UART5, MCU_UART6, MCU_UART7, MCU_UART8};

int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    int           i       = 0;
    int           timeout = 0x000B0000;
    McuUartReg_t *pUart   = NULL;
    if (uart > B_HAL_UART_5 || pbuf == NULL)
    {
        return -1;
    }
    pUart = UartTable[uart];
    for (i = 0; i < len; i++)
    {
        timeout = 0x000B0000;
        while (timeout > 0 && ((pUart->sts & (0x1 << 7)) == 0))
        {
            timeout--;
        }
        if (timeout <= 0)
        {
            return -2;
        }
        pUart->dt = pbuf[i];
    }
    timeout = 0x000B0000;
    while (timeout > 0 && ((pUart->sts & (0x1 << 7)) == 0))
    {
        timeout--;
    }
    if (timeout <= 0)
    {
        return -2;
    }
    return len;
}

int bMcuReceive(bHalUartNumber_t uart, uint8_t *pbuf, uint16_t len)
{
    int           i       = 0;
    int           timeout = 0x000B0000;
    McuUartReg_t *pUart   = NULL;
    if (uart > B_HAL_UART_5 || pbuf == NULL)
    {
        return -1;
    }
    pUart = UartTable[uart];
    for (i = 0; i < len; i++)
    {
        timeout = 0x000B0000;
        while (timeout > 0 && ((pUart->sts & (0x1 << 5)) == 0))
        {
            timeout--;
        }
        if (timeout <= 0)
        {
            return -2;
        }
        pbuf[i] = pUart->dt;
    }
    return len;
}

#endif

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
