#include "b_config.h"
#include "hal/inc/b_hal_flash.h"

#if !defined(HK32F103C8XX) && !defined(HK32F103CBXX) && !defined(HK32F103R8XX) && \
    !defined(HK32F103RBXX) && !defined(HK32F103V8XX) && !defined(HK32F103VBXX) && \
    !defined(HK32F103RCXX) && !defined(HK32F103RDXX) && !defined(HK32F103REXX) && \
    !defined(HK32F103VCXX) && !defined(HK32F103VDXX) && !defined(HK32F103VEXX)

#error \
    "Please select first the target HK32F10x device used in your application (in HK32f10x.h file)"

#else

#include "hk32f10x.h"

#define FLASH_PAGE_SIZE (2048)
#define FLASH_BASE_ADDR (0x8000000UL)

static uint32_t sMcuFlashSize = 0;

int bMcuFlashInit()
{
    sMcuFlashSize = (*((volatile uint16_t *)0x1FFFF7E0)) * 1024;
    return 0;
}

int bMcuFlashUnlock()
{
    FLASH_Unlock();
    return 0;
}

int bMcuFlashLock()
{
    FLASH_Lock();
    return 0;
}

int bMcuFlashErase(uint32_t raddr, uint32_t pages)
{
    raddr = FLASH_BASE_ADDR + raddr;
    raddr = raddr / FLASH_PAGE_SIZE * FLASH_PAGE_SIZE;
    if (sMcuFlashSize == 0)
    {
        bMcuFlashInit();
    }
    if ((raddr + (pages * FLASH_PAGE_SIZE)) > (FLASH_BASE_ADDR + sMcuFlashSize))
    {
        return -1;
    }
    for (int i = 0; i < pages; i++)
    {
        FLASH_ErasePage(raddr);
        raddr += FLASH_PAGE_SIZE;
    }
    return 0;
}

int bMcuFlashWrite(uint32_t raddr, const uint8_t *pbuf, uint32_t len)
{
    uint16_t wdata   = 0;
    uint32_t wlen = (len + 1) / 2, i = 0;
    if (sMcuFlashSize == 0)
    {
        bMcuFlashInit();
    }
    raddr = FLASH_BASE_ADDR + raddr;
    if (pbuf == NULL || (raddr & 0x1) || (raddr + len) > (sMcuFlashSize + FLASH_BASE_ADDR))
    {
        return -1;
    }
    FLASH_Status status;
    for (i = 0; i < wlen; i++)
    {
        wdata = (wdata << 8) | pbuf[i * 2 + 1];
        wdata = (wdata << 8) | pbuf[i * 2 + 0];

        status = FLASH_ProgramHalfWord(raddr, wdata);
        if (status != FLASH_COMPLETE)
        {
            return -2;
        }
        raddr += 2;
    }
    return (wlen * 2);
}

int bMcuFlashRead(uint32_t raddr, uint8_t *pbuf, uint32_t len)
{
    if (sMcuFlashSize == 0)
    {
        bMcuFlashInit();
    }
    if (pbuf == NULL || (raddr + FLASH_BASE_ADDR + len) > (sMcuFlashSize + FLASH_BASE_ADDR))
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
    if (sMcuFlashSize == 0)
    {
        bMcuFlashInit();
    }
    return sMcuFlashSize;
}

int bMcuFlashReadUID(uint8_t *pbuf, uint8_t buf_size, uint8_t *rlen)
{
    uint8_t           uid_len = 12;
    uint8_t *puid    = (uint8_t *)0x1FFFF7E8;
    if (pbuf == NULL || buf_size == 0)
    {
        return -1;
    }
    if (buf_size < uid_len)
    {
        uid_len = buf_size;
    }
    memcpy(pbuf, puid, uid_len);
    if (rlen)
    {
        *rlen = uid_len;
    }
    return 0;
}

#endif
