#include "b_config.h"
#include "hal/inc/b_hal_uart.h"
#include "lpuart.h"
#include "uart.h"

static M0P_UART_TypeDef   *pUartTable[4]   = {M0P_UART0, M0P_UART1, M0P_UART2, M0P_UART3};
static M0P_LPUART_TypeDef *pLpuartTable[2] = {M0P_LPUART0, M0P_LPUART1};

int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    int i = 0;
    if (uart <= B_HAL_UART_4)
    {
        M0P_UART_TypeDef *puart = pUartTable[uart];
        if ((Uart_GetStatus(puart, UartFE)) || (Uart_GetStatus(puart, UartPE)))
        {
            Uart_ClrStatus(puart, UartFE);
            Uart_ClrStatus(puart, UartPE);
        }
        for (i = 0; i < len; i++)
        {
            Uart_SendDataPoll(puart, pbuf[i]);
        }
    }
    else if ((uart >= B_HAL_LPUART_1) && (uart <= B_HAL_LPUART_2))
    {
        M0P_LPUART_TypeDef *plpuart = pLpuartTable[uart - B_HAL_LPUART_1];
        if ((LPUart_GetStatus(plpuart, LPUartFE)) || (LPUart_GetStatus(plpuart, LPUartPE)))
        {
            LPUart_ClrStatus(plpuart, LPUartFE);
            LPUart_ClrStatus(plpuart, LPUartPE);
        }
        for (i = 0; i < len; i++)
        {
            LPUart_SendData(plpuart, pbuf[i]);
        }
    }
    else
    {
        return -1;
    }
    return len;
}

int bMcuReceive(bHalUartNumber_t uart, uint8_t *pbuf, uint16_t len)
{
    return 0;
}
