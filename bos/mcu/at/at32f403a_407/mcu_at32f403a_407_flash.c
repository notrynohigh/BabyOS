/**
 * \file        mcu_at32f403a_407_flash.c
 * \version     v0.0.1
 * \date        2023-11-24
 * \author      miniminiminini (405553848@qq.com)
 * \brief
 *
 * Copyright (c) 2023 by miniminiminini. All Rights Reserved.
 */
/*Includes ----------------------------------------------*/
#include <string.h>

#include "b_config.h"
#include "hal/inc/b_hal_flash.h"

#if defined(AT32F403A_407)
#pragma anon_unions       // 在使用匿名联合的地方添加这个指令
#define UNUSED(x) (void)x /* to avoid gcc/g++ warnings */
#define __IO volatile     /*!< Defines 'read / write' permissions */

//----------------------------------------------------------------
int bMcuFlashInit()
{
    return 0;
}

int bMcuFlashUnlock()
{
    int retval = 0;
    return retval;
}

int bMcuFlashLock()
{
    int retval = 0;
    return retval;
}

int bMcuFlashErase(uint32_t raddr, uint32_t pages)
{
    int retval = 0;

    return retval;
}

int bMcuFlashWrite(uint32_t raddr, const uint8_t *pbuf, uint32_t len)
{
    return len;
}

int bMcuFlashRead(uint32_t raddr, uint8_t *pbuf, uint32_t len)
{
    return len;
}

uint32_t bMcuFlashSectorSize()
{
    return FLASH_PAGE_SIZE;
}

uint32_t bMcuFlashChipSize()
{
    return FLASH_MAX_SIZE;
}

#endif
