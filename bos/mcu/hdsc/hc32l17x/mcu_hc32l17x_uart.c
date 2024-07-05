#include "b_config.h"
#include "hal/inc/b_hal_uart.h"

typedef struct
{
    volatile uint32_t SBUF;
    volatile uint32_t SCON;
    volatile uint32_t SADDR;
    volatile uint32_t SADEN;
    volatile uint32_t ISR;
    volatile uint32_t ICR;
    volatile uint32_t SCNT;
} bMcuUart_t;

typedef struct
{
    volatile uint32_t SBUF;
    volatile uint32_t SCON;
    volatile uint32_t SADDR;
    volatile uint32_t SADEN;
    volatile uint32_t ISR;
    volatile uint32_t ICR;
    volatile uint32_t SCNT;
} bMcuLpuart_t;

static bMcuUart_t * const pUartTable[4] = {
    (( bMcuUart_t * const)0x40000000UL), (( bMcuUart_t * const)0x40000100UL),
    (( bMcuUart_t * const)0x40006000UL), (( bMcuUart_t * const)0x40006400UL)};
static bMcuLpuart_t * const pLpuartTable[2] = {(( bMcuLpuart_t * const)0x40000200UL),
                                              (( bMcuLpuart_t * const)0x40004000UL)};

int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    int i = 0;
    if (uart <= B_HAL_UART_4)
    {
        bMcuUart_t *puart = pUartTable[uart];
        if (B_READ_BIT(puart->ISR, (0x1 << 2)) || B_READ_BIT(puart->ISR, (0x1 << 4)))
        {
            B_CLEAR_BIT(puart->ICR, (0x1 << 2));
            B_CLEAR_BIT(puart->ICR, (0x1 << 4));
        }
        for (i = 0; i < len; i++)
        {
            while (0 == B_READ_BIT(puart->ISR, (0x1 << 3)))
            {
                ;
            }
            B_WRITE_REG(puart->SBUF, pbuf[i]);
            while (0 == B_READ_BIT(puart->ISR, (0x1 << 1)))
            {
                ;
            }
            B_CLEAR_BIT(puart->ICR, (0x1 << 1));
        }
    }
    else if ((uart >= B_HAL_LPUART_1) && (uart <= B_HAL_LPUART_2))
    {
        bMcuLpuart_t *plpuart = pLpuartTable[uart - B_HAL_LPUART_1];

        if (B_READ_BIT(plpuart->ISR, (0x1 << 2)) || B_READ_BIT(plpuart->ISR, (0x1 << 4)))
        {
            B_CLEAR_BIT(plpuart->ICR, (0x1 << 2));
            B_CLEAR_BIT(plpuart->ICR, (0x1 << 4));
        }

        for (i = 0; i < len; i++)
        {
            while (0 == B_READ_BIT(plpuart->ISR, (0x1 << 3)))
            {
                ;
            }
            B_WRITE_REG(plpuart->SBUF, pbuf[i]);
            while (0 == B_READ_BIT(plpuart->ISR, (0x1 << 1)))
            {
                ;
            }
            B_CLEAR_BIT(plpuart->ICR, (0x1 << 1));
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
