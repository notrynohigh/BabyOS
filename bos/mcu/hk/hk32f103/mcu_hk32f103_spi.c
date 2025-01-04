#include "b_config.h"
#include "hal/inc/b_hal_spi.h"
#if !defined(HK32F103C8XX) && !defined(HK32F103CBXX) && !defined(HK32F103R8XX) && \
    !defined(HK32F103RBXX) && !defined(HK32F103V8XX) && !defined(HK32F103VBXX) && \
    !defined(HK32F103RCXX) && !defined(HK32F103RDXX) && !defined(HK32F103REXX) && \
    !defined(HK32F103VCXX) && !defined(HK32F103VDXX) && !defined(HK32F103VEXX)

#error \
    "Please select first the target HK32F10x device used in your application (in HK32f10x.h file)"

#else

#include "hk32f10x.h"

static SPI_TypeDef *bMcuSpiTable[] = {SPI1, SPI2, SPI3};

uint8_t bMcuSpiTransfer(const bHalSPIIf_t *spi_if, uint8_t dat)
{
    uint8_t rec_data = 0;
    if (SPI_I2S_GetFlagStatus(bMcuSpiTable[spi_if->_if.spi], SPI_I2S_FLAG_RXNE) == SET)
    {
        SPI_I2S_ReceiveData(bMcuSpiTable[spi_if->_if.spi]);
    }
    if (spi_if->_if.spi <= B_HAL_SPI_3)
    {
        while (SPI_I2S_GetFlagStatus(bMcuSpiTable[spi_if->_if.spi], SPI_I2S_FLAG_TXE) != SET)
        {
            ;
        }
        SPI_I2S_SendData(bMcuSpiTable[spi_if->_if.spi], dat);
        while (SPI_I2S_GetFlagStatus(bMcuSpiTable[spi_if->_if.spi], SPI_I2S_FLAG_RXNE) != SET)
        {
            ;
        }
        rec_data = SPI_I2S_ReceiveData(bMcuSpiTable[spi_if->_if.spi]);
        return rec_data;
    }
    return 0;
}

int bMcuSpiSend(const bHalSPIIf_t *spi_if, const uint8_t *pbuf, uint16_t len)
{
    if (spi_if->_if.spi <= B_HAL_SPI_3)
    {
        for (int i = 0; i < len; i++)
        {
            while (SPI_I2S_GetFlagStatus(bMcuSpiTable[spi_if->_if.spi], SPI_I2S_FLAG_TXE) != SET)
            {
                ;
            }
            SPI_I2S_SendData(bMcuSpiTable[spi_if->_if.spi], pbuf[i]);
        }
        while (SPI_I2S_GetFlagStatus(bMcuSpiTable[spi_if->_if.spi], SPI_I2S_FLAG_TXE) != SET)
        {
            ;
        }
        while (SPI_I2S_GetFlagStatus(bMcuSpiTable[spi_if->_if.spi], SPI_I2S_FLAG_BSY) == SET)
        {
            ;
        }
        return 0;
    }
    return -1;
}

int bMcuSpiReceive(const bHalSPIIf_t *spi_if, uint8_t *pbuf, uint16_t len)
{
    if (spi_if->_if.spi <= B_HAL_SPI_3)
    {
        if (SPI_I2S_GetFlagStatus(bMcuSpiTable[spi_if->_if.spi], SPI_I2S_FLAG_RXNE) == SET)
        {
            SPI_I2S_ReceiveData(bMcuSpiTable[spi_if->_if.spi]);
        }
        for (int i = 0; i < len; i++)
        {
            while (SPI_I2S_GetFlagStatus(bMcuSpiTable[spi_if->_if.spi], SPI_I2S_FLAG_TXE) != SET)
            {
                ;
            }
            SPI_I2S_SendData(bMcuSpiTable[spi_if->_if.spi], 0xff);
            while (SPI_I2S_GetFlagStatus(bMcuSpiTable[spi_if->_if.spi], SPI_I2S_FLAG_RXNE) != SET)
            {
                ;
            }
            pbuf[i] = SPI_I2S_ReceiveData(bMcuSpiTable[spi_if->_if.spi]);
        }
        return 0;
    }
    return -1;
}

#endif
