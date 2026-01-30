#include "b_config.h"
#include "hal/inc/b_hal_spi.h"

/* PY32F0 系列头文件 */
#include "py32f0xx.h"

/**
 * PY32F002B 硬件资源：仅 SPI1
 * 这里的索引需要根据 b_hal_spi.h 中的枚举值对应
 */
static SPI_TypeDef *bMcuSpiTable[] = {SPI1};

/**
 * @brief SPI 全双工传输一个字节
 */
uint8_t bMcuSpiTransfer(const bHalSPIIf_t *spi_if, uint8_t dat)
{
    uint8_t rec_data = 0;
    
    /* 边界检查：PY32F002B 仅支持 SPI1 */
    if (spi_if->_if.spi == B_HAL_SPI_1)
    {
        SPI_TypeDef *SPIx = bMcuSpiTable[spi_if->_if.spi];

        /* 清除接收缓冲区：如果当前 RXNE 为 1，先读出旧数据 */
        if (SPIx->SR & SPI_SR_RXNE)
        {
            (void)SPIx->DR;
        }

        /* 等待发送缓冲区为空 (TXE) */
        while (!(SPIx->SR & SPI_SR_TXE))
        {
            ;
        }

        /* 发送数据 */
        SPIx->DR = dat;

        /* 等待接收缓冲区非空 (RXNE) */
        while (!(SPIx->SR & SPI_SR_RXNE))
        {
            ;
        }

        /* 读取接收到的数据 */
        rec_data = (uint8_t)SPIx->DR;
        return rec_data;
    }
    return 0;
}

/**
 * @brief SPI 发送数据
 */
int bMcuSpiSend(const bHalSPIIf_t *spi_if, const uint8_t *pbuf, uint16_t len)
{
    if (spi_if->_if.spi == B_HAL_SPI_1)
    {
        SPI_TypeDef *SPIx = bMcuSpiTable[spi_if->_if.spi];
        
        for (uint16_t i = 0; i < len; i++)
        {
            /* 等待 TXE */
            while (!(SPIx->SR & SPI_SR_TXE))
            {
                ;
            }
            /* 写入数据 */
            SPIx->DR = pbuf[i];
            
            /* 注意：只发送时也建议读一下 DR 清除 RXNE，防止溢出错误 OVR */
            while (!(SPIx->SR & SPI_SR_RXNE))
            {
                ;
            }
            (void)SPIx->DR; 
        }

        /* 等待发送完成及不忙状态 (BSY) */
        while (SPIx->SR & SPI_SR_BSY)
        {
            ;
        }
        return 0;
    }
    return -1;
}

/**
 * @brief SPI 接收数据 (发送 0xFF 驱动时钟)
 */
int bMcuSpiReceive(const bHalSPIIf_t *spi_if, uint8_t *pbuf, uint16_t len)
{
    if (spi_if->_if.spi == B_HAL_SPI_1)
    {
        SPI_TypeDef *SPIx = bMcuSpiTable[spi_if->_if.spi];

        /* 预读清除 RXNE */
        if (SPIx->SR & SPI_SR_RXNE)
        {
            (void)SPIx->DR;
        }

        for (uint16_t i = 0; i < len; i++)
        {
            /* 等待 TXE */
            while (!(SPIx->SR & SPI_SR_TXE))
            {
                ;
            }
            /* 发送哑数据以产生时钟 */
            SPIx->DR = 0xFF;

            /* 等待接收到数据 */
            while (!(SPIx->SR & SPI_SR_RXNE))
            {
                ;
            }
            pbuf[i] = (uint8_t)SPIx->DR;
        }
        return 0;
    }
    return -1;
}

