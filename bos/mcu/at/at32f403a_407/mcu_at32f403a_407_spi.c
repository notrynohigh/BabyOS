/**
 * \file        mcu_at32f403a_407_spi.c
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

#if defined(AT32F403A_407)

int bMcuSpiSetSpeed(const bHalSPIIf_t *spi_if, bHalSPISpeed_t speed)
{
    return 0;
}

uint8_t bMcuSpiTransfer(const bHalSPIIf_t *spi_if, uint8_t dat)
{
    return 0;
}

int bMcuSpiSend(const bHalSPIIf_t *spi_if, const uint8_t *pbuf, uint16_t len)
{
    return 0;
}

int bMcuSpiReceive(const bHalSPIIf_t *spi_if, uint8_t *pbuf, uint16_t len)
{
    return 0;
}

#endif
