#include "b_config.h"
#include "hal/inc/b_hal_uart.h"

#define UART1_BASE_ADDR (0x40011000)
#define UART2_BASE_ADDR (0x40004400)
#define UART3_BASE_ADDR (0x40004800)
#define UART4_BASE_ADDR (0x40004C00)
#define UART5_BASE_ADDR (0x40005000)
#define UART6_BASE_ADDR (0x40011400)
#define UART7_BASE_ADDR (0x40007800)
#define UART8_BASE_ADDR (0x40007C00)

typedef struct
{
    volatile uint32_t SR;   /*!< USART Status register,                   Address offset: 0x00 */
    volatile uint32_t DR;   /*!< USART Data register,                     Address offset: 0x04 */
    volatile uint32_t BRR;  /*!< USART Baud rate register,                Address offset: 0x08 */
    volatile uint32_t CR1;  /*!< USART Control register 1,                Address offset: 0x0C */
    volatile uint32_t CR2;  /*!< USART Control register 2,                Address offset: 0x10 */
    volatile uint32_t CR3;  /*!< USART Control register 3,                Address offset: 0x14 */
    volatile uint32_t GTPR; /*!< USART Guard time and prescaler register, Address offset: 0x18 */
} McuUartReg_t;

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
    if (uart > B_HAL_UART_8 || pbuf == NULL)
    {
        return -1;
    }
    pUart = UartTable[uart];
    for (i = 0; i < len; i++)
    {
        timeout = 0x000B0000;
        while (timeout > 0 && ((pUart->SR & (0x1 << 6)) == 0))
        {
            timeout--;
        }
        if (timeout <= 0)
        {
            return -2;
        }
        pUart->DR = pbuf[i];
    }
    timeout = 0x000B0000;
    while (timeout > 0 && ((pUart->SR & (0x1 << 6)) == 0))
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
    return -1;
}
