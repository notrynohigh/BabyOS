/**
 * \file        mcu_at32f403axx_spi.c
 * \version     v0.0.1
 * \date        2023-11-24
 * \author      miniminiminini (405553848@qq.com)
 * \brief
 *
 * Copyright (c) 2023 by miniminiminini. All Rights Reserved.
 */
/*Includes ----------------------------------------------*/
#include "b_config.h"
#include "hal/inc/b_hal_spi.h"

#if defined(AT32F403Axx)
#include "at32f403a_407.h"
#if defined(SPI_MODULE_ENABLED)
static spi_type *spi_buf[] = {SPI1, SPI2, SPI3, SPI4};

static int get_spi_x(const bHalSPIIf_t *spi_if, spi_type **spi_x)
{
    if ((spi_if->_if.spi >= sizeof(spi_buf) / sizeof(spi_buf[0])) ||
        (spi_if->_if.spi == B_HAL_SPI_INVALID))
    {
        return -1;
    }
    *spi_x = spi_buf[spi_if->_if.spi];
    return 0;
}

int bMcuSpiSetSpeed(const bHalSPIIf_t *spi_if, bHalSPISpeed_t speed)
{
    spi_type *spi_x = NULL;

    if (IS_NULL(spi_if) || (speed >= B_HAL_SPI_SPEED_INVALID))
    {
        return -1;
    }
    if (get_spi_x(spi_if, &spi_x) < 0)
    {
        return -1;
    }

    if (speed == B_HAL_SPI_SLOW)
    {
        spi_x->ctrl2_bit.mdiv_h = 1;
        spi_x->ctrl1_bit.mdiv_l = SPI_MCLK_DIV_8 & 0x7;
        return 0;
    }
    else if (speed == B_HAL_SPI_FAST)
    {
        spi_x->ctrl2_bit.mdiv_h = 1;
        spi_x->ctrl1_bit.mdiv_l = SPI_MCLK_DIV_4 & 0x7;
        return 0;
    }
    else
    {
        return -1;
    }
}

uint8_t bMcuSpiTransfer(const bHalSPIIf_t *spi_if, uint8_t dat)
{
    spi_type *spi_x = NULL;

    if (IS_NULL(spi_if))
    {
        return 0;
    }
    if (get_spi_x(spi_if, &spi_x) < 0)
    {
        return 0;
    }

    while (spi_i2s_flag_get(spi_x, SPI_I2S_TDBE_FLAG) == RESET)
        ;
    spi_i2s_data_transmit(spi_x, dat);

    return 0;
}

int bMcuSpiSend(const bHalSPIIf_t *spi_if, const uint8_t *pbuf, uint16_t len)
{
    spi_type *spi_x = NULL;

    if (IS_NULL(spi_if) || IS_NULL(pbuf))
    {
        return -1;
    }
    if (get_spi_x(spi_if, &spi_x) < 0)
    {
        return -1;
    }

    for (uint16_t i = 0; i < len; i++)
    {
        bMcuSpiTransfer(spi_if, pbuf[i]);
    }

    return 0;
}

int bMcuSpiReceive(const bHalSPIIf_t *spi_if, uint8_t *pbuf, uint16_t len)
{
    spi_type *spi_x = NULL;

    if (IS_NULL(spi_if) || IS_NULL(pbuf))
    {
        return -1;
    }
    if (get_spi_x(spi_if, &spi_x) < 0)
    {
        return -1;
    }

    for (uint16_t i = 0; i < len; i++)
    {
        while (spi_i2s_flag_get(spi_x, SPI_I2S_TDBE_FLAG) == RESET)
            ;
        spi_i2s_data_transmit(spi_x, 0xff);

        while (spi_i2s_flag_get(spi_x, SPI_I2S_RDBF_FLAG) == RESET)
            ;
        pbuf[i] = spi_i2s_data_receive(spi_x);
    }

    return 0;
}
#endif
#endif
