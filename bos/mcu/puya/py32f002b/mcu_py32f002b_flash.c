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
//#define FLASH_PAGE_SIZE (128)
#define FLASH_BASE_ADDR (0x08000000UL)

static uint32_t sMcuFlashSize = 0;

int bMcuFlashInit()
{
    /* PY32F002B Flash Size 寄存器地址为 0x1FFF0E00，单位 KB */
    sMcuFlashSize = (*((volatile uint16_t *)FLASHSIZE_BASE)) * 1024;
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
    
    erase_init.TypeErase   = FLASH_TYPEERASE_PAGEERASE;
    erase_init.PageAddress = raddr;
    erase_init.NbPages     = pages;

    if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK)
    {
        b_log_e("erase error at: 0x%x\r\n", page_error);
        return -1;
    }
    return 0;
}

// 这里的 FLASH_PAGE_SIZE 必须是 128
static uint8_t s_page_buffer[128]; 

int bMcuFlashWrite(uint32_t raddr, const uint8_t *pbuf, uint32_t len)
{
    uint32_t abs_addr = FLASH_BASE_ADDR + raddr;
    uint32_t page_addr = (abs_addr / 128) * 128; // 获取当前物理页起始地址
    uint32_t offset = abs_addr % 128;            // 计算在页内的偏移
    
    if (pbuf == NULL || (abs_addr + len) > (sMcuFlashSize + FLASH_BASE_ADDR))
    {
        return -1;
    }

    // 针对 BabyOS KV 常见的 4/8 字节写入，我们采用 Read-Modify-Write
    // 1. 先读出整页数据到缓存
    memcpy(s_page_buffer, (const uint8_t *)page_addr, 128);
    
    // 2. 将要写入的数据覆盖到缓存的对应位置
    // 注意：如果 len + offset > 128，说明跨页了，这里简单处理不跨页的情况
    // BabyOS KV 的 sector size 设为 128 时通常不会跨页写入单条记录
    uint32_t write_len = ((offset + len) > 128) ? (128 - offset) : len;
    memcpy(&s_page_buffer[offset], pbuf, write_len);
    
    // 3. 擦除该页 (PY32 写入前必须擦除，除非该位置本就是 0xFF)
    // 注意：如果 KV 逻辑已经处理了擦除，这里可以直接写。
    // 但页编程通常要求目标区域为全 0xFF。
//    FLASH_EraseInitTypeDef erase_init;
//    uint32_t page_error = 0;
//    erase_init.TypeErase   = FLASH_TYPEERASE_PAGEERASE; // 根据你之前的修正
//    erase_init.PageAddress = page_addr;
//    erase_init.NbPages     = 1;
//    HAL_FLASHEx_Erase(&erase_init, &page_error);

    // 4. 调用唯一的 PAGE 编程接口，写入整页 (128字节)
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_PAGE, page_addr, (uint32_t *)s_page_buffer) != HAL_OK)
    {
        return -2;
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
    /* PY32F002B UID 起始地址通常为 0x1FFF0000UL，长度 12 字节 (96 bits) */
    uint8_t  uid_len = 12;
    uint8_t *puid    = (uint8_t *)UID_BASE;
    
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

