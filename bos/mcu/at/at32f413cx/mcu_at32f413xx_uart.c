/*
 * @FilePath:
 * \EIDEd:\GitHub\activeglove_at32f413_app\BabyOS\bos\mcu\at\at32f413cx\mcu_at32f413xx_uart.c
 * @Author: miniminiminini
 * @Date: 2023-10-09 14:05:19
 * @LastEditors: miniminiminini
 * @LastEditTime: 2023-10-27 18:13:53
 * @Description: file content
 */
#include "b_config.h"
#include "hal/inc/b_hal_uart.h"

#if defined(AT32F413Cx)

#pragma anon_unions       // 在使用匿名联合的地方添加这个指令
#define UNUSED(x) (void)x /* to avoid gcc/g++ warnings */
#define __IO volatile     /*!< Defines 'read / write' permissions */

#define USART1_BASE_ADDR (0x40013800)
#define USART2_BASE_ADDR (0x40004400)
#define USART3_BASE_ADDR (0x40004800)

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

#define MCU_UART1 ((McuUartReg_t *)USART1_BASE_ADDR)
#define MCU_UART2 ((McuUartReg_t *)USART2_BASE_ADDR)
#define MCU_UART3 ((McuUartReg_t *)USART3_BASE_ADDR)

#define USART_TDBE_FLAG ((uint32_t)0x00000080) /*!< usart transmit data buffer empty flag */
#define USART_TDC_FLAG ((uint32_t)0x00000040)  /*!< usart transmit data complete flag */

static McuUartReg_t *UartTable[] = {MCU_UART1, MCU_UART2,
                                    MCU_UART3};  // 注意UartTable内串口必须连续

/**
 * @description: 注意UartTable内串口必须连续
 * @param {bHalUartNumber_t} uart
 * @param {uint8_t} *pbuf
 * @param {uint16_t} len
 * @return {*}
 */
int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    uint16_t      t_len = len;
    McuUartReg_t *pUart = NULL;
    if (uart > (sizeof(UartTable) / sizeof(McuUartReg_t *) - 1) || pbuf == NULL)
    {
        return -1;
    }
    pUart = UartTable[uart];

    while (t_len--)
    {
        while ((pUart->sts & USART_TDBE_FLAG) == 0)
            ;
        pUart->dt = (*pbuf++ & 0x01FF);
    }
    while ((pUart->sts & USART_TDC_FLAG) == 0)
        ;

    return len;
}

int bMcuReceive(bHalUartNumber_t uart, uint8_t *pbuf, uint16_t len)
{
    return len;
}

#endif
