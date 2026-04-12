#include "b_config.h"
#include "hal/inc/b_hal_spi.h"
#include "cw32l010.h"

//#include "cw32l010_spi.h"

/* CW32L010 通常只有一个 SPI 接口 (SPI1) */
static SPI_TypeDef *const bMcuSpiTable[] = {CW_SPI};

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

	uint32_t timeout = 0xFFFF;

    // 1. 等待发送缓冲区为空 (TXE)
    // 检查 ISR 寄存器的第 0 位
    while (!(pSPI->ISR & SPI_ISR_TXE_Msk))
    {
        if (timeout-- == 0) return 0xFF; // 超时保护
    }

    // 2. 写入数据到数据寄存器 DR
    // 虽然 DR 是 16 位宽 (0xffff)，但 8 位传输只操作低 8 位
    pSPI->DR = dat;

    // 3. 等待接收缓冲区非空 (RXNE)
    // 检查 ISR 寄存器的第 1 位
    timeout = 0xFFFF;
    while (!(pSPI->ISR & SPI_ISR_RXNE_Msk))
    {
        if (timeout-- == 0) return 0xFF; // 超时保护
    }

    // 4. 读取并返回接收到的数据
    return (uint8_t)(pSPI->DR & 0xFF);
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

	uint32_t timeout;

    for (uint16_t i = 0; i < len; i++)
    {
        timeout = 0xFFFF;
        // 1. 等待发送缓冲区为空 (TXE)
        // 检查 ISR 寄存器的第 0 位
        while (!(pSPI->ISR & SPI_ISR_TXE_Msk))
        {
            if (timeout-- == 0) 
            {
                return -1; // 发送超时，返回失败
            }
        }

        // 2. 将数据写入数据寄存器 DR
        pSPI->DR = pbuf[i];
        
        // 备注：在某些应用场景下，如果需要确保数据完全从移位寄存器发出，
        // 可以在循环外检查 BUSY 标志，但批量连续发送时只需检查 TXE。
    }

    // 3. 可选：等待最后一帧数据发送完毕（BUSY位变为0）
    timeout = 0xFFFF;
    while (pSPI->ISR & SPI_ISR_BUSY_Msk)
    {
        if (timeout-- == 0) return -1;
    }

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

	uint32_t timeout;
    uint16_t rcv_cnt = 0;

    for (rcv_cnt = 0; rcv_cnt < len; rcv_cnt++)
    {
        // 1. 等待发送缓冲区为空 (TXE)
        // SPI 是全双工的，接收必须通过发送一个 dummy 字节来产生时钟
        timeout = 0xFFFF;
        while (!(pSPI->ISR & SPI_ISR_TXE_Msk))
        {
            if (timeout-- == 0) return (int)rcv_cnt;
        }

        // 2. 发送填充字节（Dummy Data）以产生接收时钟
        pSPI->DR = 0xFF;

        // 3. 等待接收缓冲区非空 (RXNE)
        // 标志位在 ISR 寄存器的第 1 位
        timeout = 0xFFFF;
        while (!(pSPI->ISR & SPI_ISR_RXNE_Msk))
        {
            if (timeout-- == 0) return (int)rcv_cnt;
        }

        // 4. 读取数据寄存器并存入缓冲区
        pbuf[rcv_cnt] = (uint8_t)(pSPI->DR & 0xFF);
    }

    return (int)rcv_cnt;
}