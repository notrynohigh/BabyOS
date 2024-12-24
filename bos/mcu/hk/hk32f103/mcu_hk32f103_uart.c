#include "b_config.h"
#include "hal/inc/b_hal_uart.h"
#if !defined(HK32F103C8XX) && !defined(HK32F103CBXX) && !defined(HK32F103R8XX) && \
    !defined(HK32F103RBXX) && !defined(HK32F103V8XX) && !defined(HK32F103VBXX) && \
    !defined(HK32F103RCXX) && !defined(HK32F103RDXX) && !defined(HK32F103REXX) && \
    !defined(HK32F103VCXX) && !defined(HK32F103VDXX) && !defined(HK32F103VEXX)

#error \
    "Please select first the target HK32F10x device used in your application (in HK32f10x.h file)"

#else

#include "hk32f10x.h"

static USART_TypeDef *bMcuUartTable[] = {USART1, USART2, USART3, UART4, UART5};

int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    if (uart <= B_HAL_UART_5)
    {
        for (int i = 0; i < len; i++)
        {
            while (USART_GetFlagStatus(bMcuUartTable[uart], USART_FLAG_TXE) != SET)
                ;
            USART_SendData(bMcuUartTable[uart], pbuf[i]);
        }
        return 0;
    }
    return -1;
}

int bMcuUartReceiveDma(bHalUartNumber_t uart, bHalDmaConfig_t *pconf)
{
    if ((uart > B_HAL_UART_3) || pconf == NULL)
    {
        return -1;
    }

    if (uart == B_HAL_UART_1)
    {
        pconf->request = B_DMA_REQ_UART1_RX;
    }
    else if (uart == B_HAL_UART_2)
    {
        pconf->request = B_DMA_REQ_UART2_RX;
    }
    else if (uart == B_HAL_UART_3)
    {
        pconf->request = B_DMA_REQ_UART3_RX;
    }

    USART_DMACmd(bMcuUartTable[uart], USART_DMAReq_Rx, ENABLE);

    pconf->src = (uint32_t)(&(bMcuUartTable[uart]->DR));
    return bMcuDmaConfig(pconf);
}

#endif
