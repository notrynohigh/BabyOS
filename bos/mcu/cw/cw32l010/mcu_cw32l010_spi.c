#include "b_config.h"
#include "hal/inc/b_hal_spi.h"
#include "cw32l010.h"
#include "utils/inc/b_util_time.h"

//#include "cw32l010_spi.h"

/* CW32L010 通常只有一个 SPI 接口 (SPI1) */
static SPI_TypeDef *const bMcuSpiTable[] = {CW_SPI};

/* SPI timeout configuration (ms) */
#ifndef SPI_TIMEOUT_MS
#define SPI_TIMEOUT_MS  100
#endif

/**
 * @brief SPI 单字节读写 (全双工同步传输)
 * @param spi_if SPI 接口结构体
 * @param dat    发送的数据
 * @return uint8_t 接收到的数据
 */

uint8_t bMcuSpiTransfer(const bHalSPIIf_t *spi_if, uint8_t dat)
{
    uint8_t rec_data = 0;
    uint32_t start_tick;
    
    if (spi_if->_if.spi >= (sizeof(bMcuSpiTable) / sizeof(bMcuSpiTable[0])))
    {
        return 0;
    }

    SPI_TypeDef *pSPI = bMcuSpiTable[spi_if->_if.spi];

    start_tick = bHalGetSysTick();

    // Wait for TXE (Transmit Buffer Empty)
    while (!(pSPI->ISR & SPI_ISR_TXE_Msk))
    {
        if (TICK_DIFF_BIT32(start_tick, bHalGetSysTick()) > MS2TICKS(SPI_TIMEOUT_MS))
        {
            return 0xFF; // timeout
        }
    }

    // Write data to DR
    pSPI->DR = dat;

    // Wait for RXNE (Receive Buffer Not Empty)
    start_tick = bHalGetSysTick();
    while (!(pSPI->ISR & SPI_ISR_RXNE_Msk))
    {
        if (TICK_DIFF_BIT32(start_tick, bHalGetSysTick()) > MS2TICKS(SPI_TIMEOUT_MS))
        {
            return 0xFF; // timeout
        }
    }

    // Read and return received data
    return (uint8_t)(pSPI->DR & 0xFF);
}

/**
 * @brief SPI bulk send data
 * @return int 0 success, -1 failure
 */
int bMcuSpiSend(const bHalSPIIf_t *spi_if, const uint8_t *pbuf, uint16_t len)
{
    if (spi_if->_if.spi >= (sizeof(bMcuSpiTable) / sizeof(bMcuSpiTable[0])))
    {
        return -1;
    }

    SPI_TypeDef *pSPI = bMcuSpiTable[spi_if->_if.spi];
    uint32_t start_tick;

    for (uint16_t i = 0; i < len; i++)
    {
        start_tick = bHalGetSysTick();
        // Wait for TXE
        while (!(pSPI->ISR & SPI_ISR_TXE_Msk))
        {
            if (TICK_DIFF_BIT32(start_tick, bHalGetSysTick()) > MS2TICKS(SPI_TIMEOUT_MS))
            {
                return -1; // send timeout
            }
        }

        // Write data to DR
        pSPI->DR = pbuf[i];
    }

    // Wait for BUSY bit to clear
    start_tick = bHalGetSysTick();
    while (pSPI->ISR & SPI_ISR_BUSY_Msk)
    {
        if (TICK_DIFF_BIT32(start_tick, bHalGetSysTick()) > MS2TICKS(SPI_TIMEOUT_MS))
        {
            return -1;
        }
    }

    return 0;
}

/**
 * @brief SPI bulk receive data
 * @return int actual received length
 */
int bMcuSpiReceive(const bHalSPIIf_t *spi_if, uint8_t *pbuf, uint16_t len)
{
    if (spi_if->_if.spi >= (sizeof(bMcuSpiTable) / sizeof(bMcuSpiTable[0])))
    {
        return -1;
    }

    SPI_TypeDef *pSPI = bMcuSpiTable[spi_if->_if.spi];
    uint32_t start_tick;
    uint16_t rcv_cnt = 0;

    for (rcv_cnt = 0; rcv_cnt < len; rcv_cnt++)
    {
        // Wait for TXE (Full-duplex: must send dummy byte to generate clock)
        start_tick = bHalGetSysTick();
        while (!(pSPI->ISR & SPI_ISR_TXE_Msk))
        {
            if (TICK_DIFF_BIT32(start_tick, bHalGetSysTick()) > MS2TICKS(SPI_TIMEOUT_MS))
            {
                return (int)rcv_cnt;
            }
        }

        // Send dummy data to generate receive clock
        pSPI->DR = 0xFF;

        // Wait for RXNE
        start_tick = bHalGetSysTick();
        while (!(pSPI->ISR & SPI_ISR_RXNE_Msk))
        {
            if (TICK_DIFF_BIT32(start_tick, bHalGetSysTick()) > MS2TICKS(SPI_TIMEOUT_MS))
            {
                return (int)rcv_cnt;
            }
        }

        // Read data from DR to buffer
        pbuf[rcv_cnt] = (uint8_t)(pSPI->DR & 0xFF);
    }

    return (int)rcv_cnt;
}