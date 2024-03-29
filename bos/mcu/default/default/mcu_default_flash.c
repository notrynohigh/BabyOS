#include "b_config.h"
#include "hal/inc/b_hal_flash.h"

int bMcuFlashInit()
{
    return -1;
}

int bMcuFlashUnlock()
{
    return -1;
}

int bMcuFlashLock()
{
    return -1;
}

int bMcuFlashErase(uint32_t raddr, uint32_t pages)
{
    return -1;
}

int bMcuFlashWrite(uint32_t raddr, const uint8_t *pbuf, uint32_t len)
{
    return -1;
}

int bMcuFlashRead(uint32_t raddr, uint8_t *pbuf, uint32_t len)
{
    return -1;
}

uint32_t bMcuFlashSectorSize()
{
    return 0;
}

uint32_t bMcuFlashChipSize()
{
    return 0;
}

