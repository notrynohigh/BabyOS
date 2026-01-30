#include "b_config.h"
#include "hal/inc/b_hal_flash.h"

/* 适配 PY32F002B 系列 */
#if !defined(PY32F002Bx5)
// #error "Please select the target PY32F002B device"
#endif

#include "py32f0xx.h"
#include "utils/inc/b_util_log.h"
#include <string.h>

/* PY32F002B 特性：Page Size 为 128 Bytes */
#define FLASH_PAGE_SIZE (128)
#define FLASH_BASE_ADDR (0x08000000UL)

static uint32_t sMcuFlashSize = 0;

int bMcuFlashInit()
{
    /* PY32F002B Flash Size 寄存器地址为 0x1FFF0E00，单位 KB */
    sMcuFlashSize = (*((volatile uint16_t *)0x1FFF0E00)) * 1024;
    if (sMcuFlashSize == 0 || sMcuFlashSize > 0xFFFF) 
    {
        sMcuFlashSize = 24 * 1024; // 默认 24KB (PY32F002B 典型值)
    }
    return 0;
}

int bMcuFlashUnlock()
{
    /* 使用 PY32 标准外设库或直接操作寄存器 */
    HAL_FLASH_Unlock(); 
    return 0;
}

int bMcuFlashLock()
{
    HAL_FLASH_Lock();
    return 0;
}

int bMcuFlashErase(uint32_t raddr, uint32_t pages)
{
    raddr = FLASH_BASE_ADDR + raddr;
    /* 页面对齐 */
    raddr = (raddr / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
    
    if (sMcuFlashSize == 0)
    {
        bMcuFlashInit();
    }
    
    if ((raddr + (pages * FLASH_PAGE_SIZE)) > (FLASH_BASE_ADDR + sMcuFlashSize))
    {
        return -1;
    }

    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error = 0;
    
    erase_init.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase_init.PageAddress = raddr;
    erase_init.NbPages     = pages;

    if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK)
    {
        b_log_e("erase error at: 0x%x\r\n", page_error);
        return -1;
    }
    return 0;
}

int bMcuFlashWrite(uint32_t raddr, const uint8_t *pbuf, uint32_t len)
{
    if (sMcuFlashSize == 0)
    {
        bMcuFlashInit();
    }
    
    uint32_t abs_addr = FLASH_BASE_ADDR + raddr;
    
    /* PY32F002B 建议按字 (32-bit) 写入 */
    if (pbuf == NULL || (abs_addr & 0x3) || (abs_addr + len) > (sMcuFlashSize + FLASH_BASE_ADDR))
    {
        return -1;
    }

    uint32_t i = 0;
    uint32_t data = 0;
    
    for (i = 0; i < len; i += 4)
    {
        /* 组装 32 位数据 */
        data = *((uint32_t *)(&pbuf[i]));
        
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_PAGE, abs_addr + i, &data) != HAL_OK)
        {
            b_log_e("write error: 0x%x\r\n", abs_addr + i);
            return -2;
        }
    }
    return len;
}

int bMcuFlashRead(uint32_t raddr, uint8_t *pbuf, uint32_t len)
{
    if (sMcuFlashSize == 0)
    {
        bMcuFlashInit();
    }
    
    uint32_t abs_addr = FLASH_BASE_ADDR + raddr;
    
    if (pbuf == NULL || (abs_addr + len) > (sMcuFlashSize + FLASH_BASE_ADDR))
    {
        return -1;
    }
    
    memcpy(pbuf, (const uint8_t *)abs_addr, len);
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
    /* PY32F002B UID 起始地址通常为 0x1FFF0E90，长度 12 字节 (96 bits) */
    uint8_t  uid_len = 12;
    uint8_t *puid    = (uint8_t *)0x1FFF0E90;
    
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