/**
 *!
 * \file        mcu_stm32g4xx_uart.c
 * \version     v0.0.1
 * \date        2020/03/25
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2020 Bean
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SUARTL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "b_config.h"
#include "hal/inc/b_hal_uart.h"

#if (defined(STM32G4XX))



//      Register Address
#define UART1_BASE_ADDR 		(0x40013800)
#define UART2_BASE_ADDR 		(0x40004400)
#define UART3_BASE_ADDR 		(0x40004800)
#define UART4_BASE_ADDR 		(0x40004C00)

#define LPUART1_BASE_ADDR 	(0x40008000)


// ref stm32g431xx.h
typedef struct
{
  volatile uint32_t CR1;         /*!< USART Control register 1,                 Address offset: 0x00  */
  volatile uint32_t CR2;         /*!< USART Control register 2,                 Address offset: 0x04  */
  volatile uint32_t CR3;         /*!< USART Control register 3,                 Address offset: 0x08  */
  volatile uint32_t BRR;         /*!< USART Baud rate register,                 Address offset: 0x0C  */
  volatile uint32_t GTPR;        /*!< USART Guard time and prescaler register,  Address offset: 0x10  */
  volatile uint32_t RTOR;        /*!< USART Receiver Timeout register,          Address offset: 0x14  */
  volatile uint32_t RQR;         /*!< USART Request register,                   Address offset: 0x18  */
  volatile uint32_t ISR;         /*!< USART Interrupt and status register,      Address offset: 0x1C  */
  volatile uint32_t ICR;         /*!< USART Interrupt flag Clear register,      Address offset: 0x20  */
  volatile uint32_t RDR;         /*!< USART Receive Data register,              Address offset: 0x24  */
  volatile uint32_t TDR;         /*!< USART Transmit Data register,             Address offset: 0x28  */
  volatile uint32_t PRESC;       /*!< USART Prescaler register,                 Address offset: 0x2C  */
} McuUartReg_t;




#define MCU_UART1 	((McuUartReg_t *)UART1_BASE_ADDR)
#define MCU_UART2 	((McuUartReg_t *)UART2_BASE_ADDR)
#define MCU_UART3 	((McuUartReg_t *)UART3_BASE_ADDR)
#define MCU_UART4 	((McuUartReg_t *)UART4_BASE_ADDR)
#define MCU_UART5 	((McuUartReg_t *)UART4_BASE_ADDR)
#define MCU_UART6 	((McuUartReg_t *)UART4_BASE_ADDR)
#define MCU_UART7 	((McuUartReg_t *)UART4_BASE_ADDR)
#define MCU_UART8 	((McuUartReg_t *)UART4_BASE_ADDR)
#define MCU_LPUART1 ((McuUartReg_t *)LPUART1_BASE_ADDR)


static McuUartReg_t *UartTable[9] = {MCU_UART1, MCU_UART2, MCU_UART3,
                                     MCU_UART4, MCU_UART5, MCU_UART6,MCU_UART7,MCU_UART8, MCU_LPUART1};

int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    int           i       = 0;
    int           timeout = 0x000B0000;
    McuUartReg_t *pUart   = NULL;
    if (uart > B_HAL_UART_NUMBER || pbuf == NULL)
    {
        return -1;
    }
    pUart = UartTable[uart];
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

int bMcuReceive(bHalUartNumber_t uart, uint8_t *pbuf, uint16_t len)
{
    int           i       = 0;
    int           timeout = 0x000B0000;
    McuUartReg_t *pUart   = NULL;
    if (uart > B_HAL_UART_5 || pbuf == NULL)
    {
        return -1;
    }
    pUart = UartTable[uart];
    for (i = 0; i < len; i++)
    {
        timeout = 0x000B0000;
        while (timeout > 0 && ((pUart->ISR & (0x1 << 5)) == 0))
        {
            timeout--;
        }
        if (timeout <= 0)
        {
            return -2;
        }
        pbuf[i] = pUart->RDR;
    }
    return len;
}

#endif

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
