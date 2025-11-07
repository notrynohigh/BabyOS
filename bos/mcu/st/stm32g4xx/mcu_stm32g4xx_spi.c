/**
 *!
 * \file        mcu_stm32g0x0_spi.c
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SSPIL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "b_config.h"
#include "hal/inc/b_hal_spi.h"

#if (defined(STM32G4XX))

//         Register Address

#define SPI1_BASE_ADDR (0x40013000)
#define SPI2_BASE_ADDR (0x40003800)
#define SPI3_BASE_ADDR (0x40003C00)
#define SPI4_BASE_ADDR (0x40013C00)

typedef struct
{
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t CRCPR;
    volatile uint32_t RXCRCR;
    volatile uint32_t TXCRCR;
    volatile uint32_t I2SCFGR;
    volatile uint32_t I2SPR;
} McuSpiReg_t;

#define MCU_SPI1 ((McuSpiReg_t *)SPI1_BASE_ADDR)
#define MCU_SPI2 ((McuSpiReg_t *)SPI2_BASE_ADDR)
#define MCU_SPI3 ((McuSpiReg_t *)SPI3_BASE_ADDR)
#define MCU_SPI4 ((McuSpiReg_t *)SPI4_BASE_ADDR)

static McuSpiReg_t *SpiTable[4] = {MCU_SPI1, MCU_SPI2, MCU_SPI3, MCU_SPI4};

int bMcuSpiSetSpeed(const bHalSPIIf_t *spi_if, bHalSPISpeed_t speed)
{
    McuSpiReg_t *pSpi = NULL;
    uint8_t prescaler;

    // 参数校验
    if (IS_NULL(spi_if) || speed >= B_HAL_SPI_SPEED_INVALID || spi_if->_if.spi >= 4)
    {
        return -1;
    }

    pSpi = SpiTable[spi_if->_if.spi];

    // 等待SPI空闲
    while (B_READ_BIT(pSpi->SR, (1 << 7))) {} // 等待BSY位清零

    // 根据速度设置分频系数（CR1[5:3] = BR[2:0]）
    switch(speed)
    {
        case B_HAL_SPI_SLOW:
            prescaler = 6; // 64分频 (0110)
            break;
        case B_HAL_SPI_MEDIUM:
            prescaler = 3; // 8分频  (0011)
            break;
        case B_HAL_SPI_FAST:
            prescaler = 0; // 2分频  (0000)
            break;
        default:
            return -1;
    }

    // 更新波特率配置
    B_CLEAR_BIT(pSpi->CR1, (0x7 << 3)); // 清除原有配置
    B_SET_BIT(pSpi->CR1, (prescaler << 3)); // 设置新分频

    return 0;
}

uint8_t bMcuSpiTransfer(const bHalSPIIf_t *spi_if, uint8_t dat)
{
    McuSpiReg_t *pSpi = NULL;
    uint32_t timeout = 0xFFFF; // 超时保护

    // 参数校验
    if (IS_NULL(spi_if) || spi_if->_if.spi >= 4)
    {
        return 0;
    }

    pSpi = SpiTable[spi_if->_if.spi];

    // 确保SPI已使能
    if (!B_READ_BIT(pSpi->CR1, (1 << 6)))
    {
        B_SET_BIT(pSpi->CR1, (1 << 6)); // 置位SPE使能SPI
    }

    // 等待发送缓冲区为空 (TXE)
    while (!B_READ_BIT(pSpi->SR, (1 << 1)) && timeout-- > 0) {}
    if (timeout == 0) return 0; // 超时处理

    // 发送数据（8位模式）
    *(volatile uint8_t *)&pSpi->DR = dat;

    // 等待接收缓冲区非空 (RXNE)
    timeout = 0xFFFF;
    while (!B_READ_BIT(pSpi->SR, (1 << 0)) && timeout-- > 0) {}
    if (timeout == 0) return 0; // 超时处理

    // 返回接收数据
    return *(volatile uint8_t *)&pSpi->DR;
}

int bMcuSpiSend(const bHalSPIIf_t *spi_if, const uint8_t *pbuf, uint16_t len)
{
    uint16_t i;

    // 参数校验
    if (IS_NULL(spi_if) || IS_NULL(pbuf) || len == 0 || spi_if->_if.spi >= 4)
    {
        return -1;
    }

    // 循环发送每个字节
    for (i = 0; i < len; i++)
    {
        bMcuSpiTransfer(spi_if, pbuf[i]);
    }

    // 等待发送完成
    while (B_READ_BIT(SpiTable[spi_if->_if.spi]->SR, (1 << 7))) {} // 等待BSY位清零

    return 0;
}

int bMcuSpiReceive(const bHalSPIIf_t *spi_if, uint8_t *pbuf, uint16_t len)
{
    uint16_t i;

    // 参数校验
    if (IS_NULL(spi_if) || IS_NULL(pbuf) || len == 0 || spi_if->_if.spi >= 4)
    {
        return -1;
    }

    // 循环接收每个字节（发送0xFF作为dummy数据）
    for (i = 0; i < len; i++)
    {
        pbuf[i] = bMcuSpiTransfer(spi_if, 0xFF);
    }

    return 0;
}

#endif

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
