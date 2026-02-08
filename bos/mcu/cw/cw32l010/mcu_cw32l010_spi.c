#include "b_config.h"
#include "hal/inc/b_hal_spi.h"
#include "cw32l010.h"

/* CW32L010 通常只有一个 SPI 接口 (SPI1) */
static SPI_TypeDef *const bMcuSpiTable[] = {SPI1};

/**
 * @brief SPI 单字节读写 (全双工同步传输)
 * @param spi_if SPI 接口结构体
 * @param dat    发送的数据
 * @return uint8_t 接收到的数据
 */
uint8_t bMcuSpiTransfer(const bHalSPIIf_t *spi_if, uint8_t dat)
{
    uint8_t rec_data = 0;
    if (spi_if->_if.spi >= (sizeof(bMcuSpiTable) / sizeof(bMcuSpiTable[0])))
    {
        return 0;
    }

    SPI_TypeDef *pSPI = bMcuSpiTable[spi_if->_if.spi];

    // 清除接收缓冲区非空标志 (如有必要)
    (void)pSPI->DR;

    // 等待发送缓冲区空 (TXE)
    while (!(pSPI->SR & SPI_SR_TXE_Msk));
    
    // 发送数据
    pSPI->DR = dat;

    // 等待接收缓冲区非空 (RXNE)
    while (!(pSPI->SR & SPI_SR_RXNE_Msk));
    
    // 读取接收到的数据
    rec_data = (uint8_t)(pSPI->DR & 0xFF);

    return rec_data;
}

/**
 * @brief SPI 批量发送数据
 * @return int 0 成功，-1 失败
 */
int bMcuSpiSend(const bHalSPIIf_t *spi_if, const uint8_t *pbuf, uint16_t len)
{
    if (spi_if->_if.spi >= (sizeof(bMcuSpiTable) / sizeof(bMcuSpiTable[0])))
    {
        return -1;
    }

    SPI_TypeDef *pSPI = bMcuSpiTable[spi_if->_if.spi];

    for (int i = 0; i < len; i++)
    {
        // 等待发送缓冲空
        while (!(pSPI->SR & SPI_SR_TXE_Msk));
        pSPI->DR = pbuf[i];
        
        // 为了确保后续操作安全，等待接收到数据（即使不使用）以清除 RXNE
        while (!(pSPI->SR & SPI_SR_RXNE_Msk));
        (void)pSPI->DR; 
    }

    // 等待传输彻底结束 (BUSY 位清零)
    while (pSPI->SR & SPI_SR_BSY_Msk);

    return 0;
}

/**
 * @brief SPI 批量接收数据
 * @return int 实际接收长度
 */
int bMcuSpiReceive(const bHalSPIIf_t *spi_if, uint8_t *pbuf, uint16_t len)
{
    if (spi_if->_if.spi >= (sizeof(bMcuSpiTable) / sizeof(bMcuSpiTable[0])))
    {
        return -1;
    }

    SPI_TypeDef *pSPI = bMcuSpiTable[spi_if->_if.spi];

    for (int i = 0; i < len; i++)
    {
        // 发送 dummy 数据以驱动时钟
        while (!(pSPI->SR & SPI_SR_TXE_Msk));
        pSPI->DR = 0xFF;

        // 等待接收数据
        while (!(pSPI->SR & SPI_SR_RXNE_Msk));
        pbuf[i] = (uint8_t)(pSPI->DR & 0xFF);
    }

    return len;
}