#include "b_config.h"
#include "hal/inc/b_hal_uart.h"

//      Register Address
#define UART1_BASE_ADDR (0x40013800)
#define UART2_BASE_ADDR (0x40004400)

#define LPUART1_BASE_ADDR (0x40004800)

typedef struct
{
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t BRR;
    volatile uint32_t GTPR;
    volatile uint32_t RTOR;
    volatile uint32_t RQR;
    volatile uint32_t ISR;
    volatile uint32_t ICR;
    volatile uint32_t RDR;
    volatile uint32_t TDR;
    volatile uint32_t PRESC;
} McuUartReg_t;

#define MCU_UART1 ((McuUartReg_t *)UART1_BASE_ADDR)
#define MCU_UART2 ((McuUartReg_t *)UART2_BASE_ADDR)

#define MCU_LPUART1 ((McuUartReg_t *)LPUART1_BASE_ADDR)

static McuUartReg_t *UartTable[2] = {MCU_UART1, MCU_UART2};

int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    int           i       = 0;
    int           timeout = 0x000B0000;
    McuUartReg_t *pUart   = NULL;
    if ((uart > B_HAL_UART_2 && uart != B_HAL_LPUART_1) || pbuf == NULL)
    {
        return -1;
    }
    if (uart == B_HAL_LPUART_1)
    {
        pUart = MCU_LPUART1;
    }
    else
    {
        pUart = UartTable[uart];
    }
    for (i = 0; i < len; i++)
    {
        timeout = 0x000B0000;
        while (timeout > 0 && ((pUart->ISR & (0x1 << 6)) == 0))
        {
            timeout--;
        }
        if (timeout <= 0)
        {
            return -2;
        }
        pUart->TDR = pbuf[i];
    }
    timeout = 0x000B0000;
    while (timeout > 0 && ((pUart->ISR & (0x1 << 6)) == 0))
    {
        timeout--;
    }
    if (timeout <= 0)
    {
        return -2;
    }
    return len;
}

int bMcuUartReceiveDma(bHalUartNumber_t uart, bHalDmaConfig_t *pconf)
{
    McuUartReg_t *pUart = NULL;
    if ((uart > B_HAL_UART_2 && uart != B_HAL_LPUART_1) || pconf == NULL)
    {
        return -1;
    }
    if (uart == B_HAL_LPUART_1)
    {
        pUart          = MCU_LPUART1;
        pconf->request = B_DMA_REQ_LPUART1_RX;
    }
    else
    {
        pUart = UartTable[uart];
        if (uart == B_HAL_UART_1)
        {
            pconf->request = B_DMA_REQ_UART1_RX;
        }
        else if (uart == B_HAL_UART_2)
        {
            pconf->request = B_DMA_REQ_UART2_RX;
        }
    }
    B_SET_BIT(pUart->CR3, 0x1 << 6);
    pconf->src = (uint32_t)(&(pUart->RDR));
    return bMcuDmaConfig(pconf);
}
