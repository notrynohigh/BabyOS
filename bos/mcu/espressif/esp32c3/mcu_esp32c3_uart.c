#include "b_config.h"
#include "driver/uart.h"
#include "hal/inc/b_hal_uart.h"

static int _bHalUartNo(bHalUartNumber_t uart)
{
    int uart_no = -1;
    if (uart == B_HAL_UART_1)
    {
        uart_no = UART_NUM_0;
    }
    else if (uart == B_HAL_UART_2)
    {
        uart_no = UART_NUM_1;
    }
    return uart_no;
}

int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    if (_bHalUartNo(uart) < 0)
    {
        return -1;
    }
    uart_write_bytes(_bHalUartNo(uart), pbuf, len);
    return len;
}

int bMcuUartReceiveDma(bHalUartNumber_t uart, bHalDmaConfig_t *pconf)
{
    return -1;
}
