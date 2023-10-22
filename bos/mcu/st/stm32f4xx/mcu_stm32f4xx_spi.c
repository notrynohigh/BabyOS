#include "b_config.h"
#include "hal/inc/b_hal_spi.h"

#define SPI1_BASE_ADDR (0x40013000)
#define SPI2_BASE_ADDR (0x40003800)
#define SPI3_BASE_ADDR (0x40003C00)
#define SPI4_BASE_ADDR (0x40013400)
#define SPI5_BASE_ADDR (0x40015000)
#define SPI6_BASE_ADDR (0x40015400)

typedef struct
{
    volatile uint32_t CR1;     /*!< SPI control register 1 (not used in I2S mode) */
    volatile uint32_t CR2;     /*!< SPI control register 2 */
    volatile uint32_t SR;      /*!< SPI status register */
    volatile uint32_t DR;      /*!< SPI data register */
    volatile uint32_t CRCPR;   /*!< SPI CRC polynomial register (not used in I2S mode) */
    volatile uint32_t RXCRCR;  /*!< SPI RX CRC register (not used in I2S mode) */
    volatile uint32_t TXCRCR;  /*!< SPI TX CRC register (not used in I2S mode) */
    volatile uint32_t I2SCFGR; /*!< SPI_I2S configuration register */
    volatile uint32_t I2SPR;   /*!< SPI_I2S prescaler register */
} McuSpiReg_t;

#define MCU_SPI1 ((McuSpiReg_t *)SPI1_BASE_ADDR)
#define MCU_SPI2 ((McuSpiReg_t *)SPI2_BASE_ADDR)
#define MCU_SPI3 ((McuSpiReg_t *)SPI3_BASE_ADDR)
#define MCU_SPI4 ((McuSpiReg_t *)SPI4_BASE_ADDR)
#define MCU_SPI5 ((McuSpiReg_t *)SPI5_BASE_ADDR)
#define MCU_SPI6 ((McuSpiReg_t *)SPI6_BASE_ADDR)

static McuSpiReg_t *SpiTable[6] = {MCU_SPI1, MCU_SPI2, MCU_SPI3, MCU_SPI4, MCU_SPI5, MCU_SPI6};

int bMcuSpiSetSpeed(const bHalSPIIf_t *spi_if, bHalSPISpeed_t speed)
{
    uint16_t     SpeedVal = 1;
    McuSpiReg_t *pSpi     = NULL;
    if (IS_NULL(spi_if) || (speed >= B_HAL_SPI_SPEED_INVALID))
    {
        return -1;
    }
    if (speed == B_HAL_SPI_SLOW)
    {
        SpeedVal = 6;
    }
    if (spi_if->_if.spi > B_HAL_SPI_6)
    {
        return -1;
    }
    pSpi = SpiTable[spi_if->_if.spi];
    while (pSpi->SR & 0x80)
    {
        ;
    }
    pSpi->CR1 &= ~(0x7 << 3);
    pSpi->CR1 |= (SpeedVal << 3);
    return 0;
}

uint8_t bMcuSpiTransfer(const bHalSPIIf_t *spi_if, uint8_t dat)
{
    uint8_t      tmp  = dat;
    int          i    = 0;
    McuSpiReg_t *pSpi = NULL;

    if (IS_NULL(spi_if))
    {
        return 0;
    }

    if (spi_if->_if.spi > B_HAL_SPI_6)
    {
        return 0;
    }
    pSpi = SpiTable[spi_if->_if.spi];
    
    tmp = *(volatile uint8_t *)&pSpi->DR;
    /* Check if the SPI is already enabled */
    if ((pSpi->CR1 & (0x1 << 6)) == 0)
    {
        /* Enable SPI peripheral */
        B_SET_BIT(pSpi->CR1, 0x1 << 6);
    }

    while (B_READ_BIT(pSpi->SR, (0x1 << 1)) == 0)
    {
        ;
    }
    *(volatile uint8_t *)&pSpi->DR = dat;
    while (B_READ_BIT(pSpi->SR, (0x1 << 0)) == 0)
    {
        ;
    }
    tmp = *(volatile uint8_t *)&pSpi->DR;
    for (i = 0; i < 8; i++)
    {
        ;
    }
    return tmp;
}

int bMcuSpiSend(const bHalSPIIf_t *spi_if, const uint8_t *pbuf, uint16_t len)
{
    int          i    = 0;
    McuSpiReg_t *pSpi = NULL;

    if (IS_NULL(spi_if) || IS_NULL(pbuf))
    {
        return -1;
    }

    if (spi_if->_if.spi > B_HAL_SPI_6)
    {
        return 0;
    }
    pSpi = SpiTable[spi_if->_if.spi];

    /* Check if the SPI is already enabled */
    if ((pSpi->CR1 & (0x1 << 6)) == 0)
    {
        /* Enable SPI peripheral */
        B_SET_BIT(pSpi->CR1, 0x1 << 6);
    }
    for (i = 0; i < len; i++)
    {
        while (B_READ_BIT(pSpi->SR, (0x1 << 1)) == 0)
        {
            ;
        }
        *(volatile uint8_t *)&pSpi->DR = pbuf[i];
    }
    for (i = 0; i < 8; i++)
    {
        ;
    }
    return 0;
}

int bMcuSpiReceive(const bHalSPIIf_t *spi_if, uint8_t *pbuf, uint16_t len)
{
    int i = 0;
    if (IS_NULL(spi_if) || IS_NULL(pbuf))
    {
        return -1;
    }
    for (i = 0; i < len; i++)
    {
        pbuf[i] = bMcuSpiTransfer(spi_if, 0xff);
    }
    return 0;
}
