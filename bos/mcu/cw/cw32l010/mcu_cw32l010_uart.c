#include "b_config.h"
#include "hal/inc/b_hal_uart.h"
#include "cw32l010.h"

/* CW32L010 UART 寄存器基地址映射 */
static UART_TypeDef *const bMcuUartTable[] = {UART1, UART2};

/**
 * @brief UART 发送数据函数
 * @param uart UART 编号 (B_HAL_UART_1, B_HAL_UART_2)
 * @param pbuf 数据缓冲区指针
 * @param len  数据长度
 * @return int 成功返回 0，失败返回 -1
 */
int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    // CW32L010 仅支持 UART1 和 UART2
    if (uart > B_HAL_UART_2)
    {
        return -1;
    }

    UART_TypeDef *pUART = bMcuUartTable[uart];

    for (int i = 0; i < len; i++)
    {
        // 等待发送缓冲区空中断标志 (TXE)
        // 在 CW32L010 中，ISR 寄存器的 TXE 位表示发送缓冲空
        while (!(pUART->ISR & UARTx_ISR_TXE_Msk));
        
        // 向 TDR 寄存器写入数据
        pUART->TDR = pbuf[i];
    }

    // 等待发送完成 (TC)
    while (!(pUART->ISR & UARTx_ISR_TC_Msk));

    return 0;
}

/**
 * @brief UART 接收数据函数 (示例)
 * @param uart UART 编号
 * @param pbuf 接收缓冲区
 * @param len  期望接收长度
 * @return int 实际接收长度
 */
int bMcuUartReceive(bHalUartNumber_t uart, uint8_t *pbuf, uint16_t len)
{
    if (uart > B_HAL_UART_2)
    {
        return -1;
    }

    UART_TypeDef *pUART = bMcuUartTable[uart];
    uint16_t count = 0;

    for (int i = 0; i < len; i++)
    {
        // 检查接收缓冲非空标志 (RXNE)
        if (pUART->ISR & UARTx_ISR_RXNE_Msk)
        {
            pbuf[count++] = (uint8_t)(pUART->RDR & 0xFF);
        }
        else
        {
            break;
        }
    }
    return count;
}


