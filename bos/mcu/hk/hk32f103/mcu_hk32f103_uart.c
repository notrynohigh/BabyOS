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

#endif
