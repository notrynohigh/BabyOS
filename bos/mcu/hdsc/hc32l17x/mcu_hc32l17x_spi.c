#include "b_config.h"
#include "hal/inc/b_hal_spi.h"

typedef struct
{
    volatile uint32_t CR;
    volatile uint32_t SSN;
    volatile uint32_t STAT;
    volatile uint32_t DATA;
    volatile uint32_t CR2;
    volatile uint32_t ICLR;
} bMcuSpi_t;

#define MCU_SPI0_BASE ((bMcuSpi_t *)0x40000800UL)
#define MCU_SPI1_BASE ((bMcuSpi_t *)0x40004800UL)

static bMcuSpi_t *bMcuSpiTable[] = {MCU_SPI0_BASE, MCU_SPI1_BASE};

int bMcuSpiSetSpeed(const bHalSPIIf_t *spi_if, bHalSPISpeed_t speed)
{
    return -1;
}

uint8_t bMcuSpiTransfer(const bHalSPIIf_t *spi_if, uint8_t dat)
{
    if (IS_NULL(spi_if) || (spi_if->_if.spi > B_HAL_SPI_2))
    {
        return 0;
    }

    while ((bMcuSpiTable[spi_if->_if.spi]->STAT & (0x1 << 2)) == 0)
    {
        ;
    }
    bMcuSpiTable[spi_if->_if.spi]->DATA = dat;
    while ((bMcuSpiTable[spi_if->_if.spi]->STAT & (0x1 << 1)) == 0)
    {
        ;
    }
    return bMcuSpiTable[spi_if->_if.spi]->DATA;
}

int bMcuSpiSend(const bHalSPIIf_t *spi_if, const uint8_t *pbuf, uint16_t len)
{
    if (IS_NULL(spi_if) || IS_NULL(pbuf) || (len == 0) || (spi_if->_if.spi > B_HAL_SPI_2))
    {
        return -1;
    }
    uint32_t u32Index = 0;
    uint8_t  tmp      = 0;
    for (u32Index = 0; u32Index < len; u32Index++)
    {
        while ((bMcuSpiTable[spi_if->_if.spi]->STAT & (0x1 << 2)) == 0)
        {
            ;
        }
        bMcuSpiTable[spi_if->_if.spi]->DATA = pbuf[u32Index];
        while ((bMcuSpiTable[spi_if->_if.spi]->STAT & (0x1 << 1)) == 0)
        {
            ;
        }
        tmp = bMcuSpiTable[spi_if->_if.spi]->DATA;
    }

    while ((bMcuSpiTable[spi_if->_if.spi]->STAT & (0x1 << 2)) == 0)
    {
        ;
    }
    while ((bMcuSpiTable[spi_if->_if.spi]->STAT & (0x1 << 3)))
    {
        ;
    }

    return len;
}

int bMcuSpiReceive(const bHalSPIIf_t *spi_if, uint8_t *pbuf, uint16_t len)
{
    if (IS_NULL(spi_if) || IS_NULL(pbuf) || (len == 0) || (spi_if->_if.spi > B_HAL_SPI_2))
    {
        return -1;
    }
    uint32_t u32Index = 0;

    for (u32Index = 0; u32Index < len; u32Index++)
    {
        while ((bMcuSpiTable[spi_if->_if.spi]->STAT & (0x1 << 2)) == 0)
        {
            ;
        }
        bMcuSpiTable[spi_if->_if.spi]->DATA = 0x0;
        while ((bMcuSpiTable[spi_if->_if.spi]->STAT & (0x1 << 1)) == 0)
        {
            ;
        }
        pbuf[u32Index] = bMcuSpiTable[spi_if->_if.spi]->DATA;
    }

    while ((bMcuSpiTable[spi_if->_if.spi]->STAT & (0x1 << 3)))
    {
        ;
    }

    return len;
}
