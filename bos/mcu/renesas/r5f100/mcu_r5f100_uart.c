#include "b_config.h"
#include "hal/inc/b_hal_uart.h"
#include "r_cg_serial.h"

int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    int wlen = len;
    STMK1    = 1U; /* disable INTSTx interrupt */
    while (wlen--)
    {
        TXD1 = *pbuf++;
        while (STIF1 == 0)
            ;
        STIF1 = 0;  // clear INTSTx flag
    }
    STMK1 = 0U; /* add enable INTST1 interrupt */
    return len;
}

int bMcuReceive(bHalUartNumber_t uart, uint8_t *pbuf, uint16_t len)
{
    return -1;
}
