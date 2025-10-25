#include "b_config.h"
#include "hal/inc/b_hal_flash.h"

#if !defined(STM32G4XX)
#error "Please select first the target STM32G473 device used in your application (in stm32g473xx.h file)"
#else

#include "stm32g4xx_hal.h"
#include "utils/inc/b_util_log.h"

static uint32_t sMcuFlashSize = 0;

int bMcuFlashInit()
{
    sMcuFlashSize = FLASH_SIZE;
    return 0;
}

int bMcuFlashUnlock()
{
    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        b_log_e("Flash unlock failed\r\n");
        return -1;
    }
    return 0;
}

int bMcuFlashLock()
{
    if (HAL_FLASH_Lock() != HAL_OK)
    {
        b_log_e("Flash lock failed\r\n");
        return -1;
    }
    return 0;
}

static uint32_t getPage(uint32_t addr)
{
    /* G473 每页 2 KB，0x0800 0000 起 */
    return (addr - FLASH_BASE) / FLASH_PAGE_SIZE;
}

/* -------------- 页擦除 -------------- */
int bMcuFlashErase(uint32_t raddr, uint32_t pages)
{
	raddr = FLASH_BASE + raddr;	
    if (raddr < FLASH_BASE ||
        raddr + pages * FLASH_PAGE_SIZE > FLASH_BASE + FLASH_SIZE ||
        pages == 0) return -1;

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {0};
    uint32_t             pageErr = 0;

    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.Banks       = FLASH_BANK_2;          // 如跨 Bank 需自行拆分
    erase.Page        = getPage(raddr);
    erase.NbPages     = pages;

    if (HAL_FLASHEx_Erase(&erase, &pageErr) != HAL_OK) {
        HAL_FLASH_Lock();
        return -2;          // 擦除失败
    }

    HAL_FLASH_Lock();
    return 0;
}
int bMcuFlashWrite(uint32_t raddr, const uint8_t *pbuf, uint32_t len)
{
	raddr = FLASH_BASE + raddr;
    /* 基本合法性检查：只保留地址范围和对齐检查 */
    if (raddr < FLASH_BASE || raddr + len > FLASH_BASE + FLASH_SIZE || (raddr & 0x7))
        return 0;

    HAL_FLASH_Unlock();

    uint32_t written = 0;

    while (written < len) {
        uint32_t rem = len - written;          /* 剩余字节 */
        uint64_t data64 = 0xFFFFFFFFFFFFFFFFULL;/* 默认全 FF，与擦除状态一致 */

        /* 拷贝有效数据，不足 8 B 时高位保持 0xFF */
        if (rem >= 8)
            memcpy(&data64, &pbuf[written], 8);
        else
            memcpy(&data64, &pbuf[written], rem); /* 剩余<8B，自动补FF */

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              raddr + written,
                              data64) != HAL_OK)
            break;              /* 出错立即停 */

        written += (rem >= 8) ? 8 : rem;        /* 更新已写字节数 */
    }

    HAL_FLASH_Lock();
    return (int)written;
}

int bMcuFlashRead(uint32_t raddr, uint8_t *pbuf, uint32_t len)
{
    if (pbuf == NULL || len == 0)
    {
        return -1;
    }

    if (sMcuFlashSize == 0)
    {
        bMcuFlashInit();
    }

    uint32_t read_addr = FLASH_BASE + raddr;
    if ((read_addr + len) > (FLASH_BASE + sMcuFlashSize))
    {
        b_log_e("Read address out of range\r\n");
        return -1;
    }

    memcpy(pbuf, (const void *)read_addr, len);
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
    uint8_t uid_len = 12;  // STM32G473的UID为12字节
    uint32_t *uid_base = (uint32_t *)0x1FFF7590;  // UID起始地址

    if (pbuf == NULL || buf_size == 0)
    {
        return -1;
    }

    if (buf_size < uid_len)
    {
        uid_len = buf_size;
    }

    memcpy(pbuf, (const uint8_t *)uid_base, uid_len);
    if (rlen != NULL)
    {
        *rlen = uid_len;
    }

    return 0;
}

#endif



