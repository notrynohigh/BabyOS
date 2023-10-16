#include "b_config.h"
#include "hal/inc/b_hal_uart.h"

#if defined(AT32F413xB)
#include "at32f413.h"

static uint16_t rs485_send_data(usart_type *usart_x, const uint8_t *pbuf, uint16_t len)
{
    uint16_t i = 0;
    uint16_t retval = len;

    gpio_bits_write(GPIOA, GPIO_PINS_1, TRUE);
    while (len--)
    {
        while (usart_flag_get(USART3, USART_TDBE_FLAG) == RESET)
            ;
        usart_data_transmit(usart_x, pbuf[i++]);
    }
    while (usart_flag_get(USART3, USART_TDC_FLAG) == RESET)
        ;
    gpio_bits_write(GPIOA, GPIO_PINS_1, FALSE);

    return retval;
}

static uint16_t usart_Tx_len(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    uint16_t retval = 0;

    switch (uart)
    {
    case B_HAL_UART_1:
        break;
    case B_HAL_UART_2:
        break;
    case B_HAL_UART_3:
        retval = rs485_send_data(USART3, pbuf, len);
        break;
    case B_HAL_UART_4:
        break;
    case B_HAL_UART_5:
        break;
    case B_HAL_UART_6:
        break;
    case B_HAL_UART_7:
        break;
    case B_HAL_UART_8:
        break;
    default:
        break;
    }

    return retval;
}

int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    usart_Tx_len(uart, pbuf, len);
    return len;
}

int bMcuReceive(bHalUartNumber_t uart, uint8_t *pbuf, uint16_t len)
{
    return len;
}

#endif
