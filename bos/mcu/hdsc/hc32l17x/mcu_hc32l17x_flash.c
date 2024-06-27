#include "b_config.h"
#include "ddl.h"
#include "flash.h"
#include "hal/inc/b_hal_flash.h"

#define FLASH_BASE_ADDR (0x0000000UL)
#define FLASH_PAGE_SIZE (512)
#define FLASH_MAX_SIZE (*((volatile uint32_t *)0x00100C70))

int bMcuFlashInit()
{
    en_result_t ret = Flash_Init(1, TRUE);
    B_ASSERT(ret == Ok);
    return 0;
}

int bMcuFlashUnlock()
{
    en_result_t ret = Flash_LockSet(FlashLock1, 0x80000000);
    return (ret == Ok) ? 0 : -1;
}

int bMcuFlashLock()
{
    while (Ok != Flash_OpModeConfig(FlashReadMode))
        ;
    while (Ok != Flash_LockAll())
        ;
    return 0;
}

int bMcuFlashErase(uint32_t raddr, uint32_t pages)
{
    int         i   = 0;
    en_result_t ret = Ok;
    while (Ok != Flash_OpModeConfig(FlashSectorEraseMode))
        ;
    for (i = 0; i < pages; i++)
    {
        ret = Flash_SectorErase(raddr);
        B_ASSERT(ret == Ok);
        raddr += FLASH_PAGE_SIZE;
    }
    return 0;
}

int bMcuFlashWrite(uint32_t raddr, const uint8_t *pbuf, uint32_t len)
{
    if (pbuf == NULL)
    {
        return -1;
    }
    en_result_t ret = Ok;
    while (Ok != Flash_OpModeConfig(FlashWriteMode))
        ;
    ret = Flash_Write8(raddr, (uint8_t *)pbuf, len);
    B_ASSERT(ret == Ok);
    return len;
}

int bMcuFlashRead(uint32_t raddr, uint8_t *pbuf, uint32_t len)
{
    if (pbuf == NULL || (raddr + FLASH_BASE_ADDR + len) > (FLASH_MAX_SIZE + FLASH_BASE_ADDR))
    {
        return -1;
    }
    raddr = FLASH_BASE_ADDR + raddr;
    memcpy(pbuf, (const uint8_t *)raddr, len);
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
