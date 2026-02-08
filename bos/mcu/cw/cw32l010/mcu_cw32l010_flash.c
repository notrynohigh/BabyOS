#include "b_config.h"
#include "hal/inc/b_hal_flash.h"
#include "cw32l010.h"
#include "utils/inc/b_util_log.h"
#include <string.h>

/* CW32L010 Flash 配置 */
#define FLASH_PAGE_SIZE      (512)           // 每页 512 字节
#define FLASH_BASE_ADDR      (0x00000000UL)  // 存储起始物理地址
#define CW32_UID_ADDR        (0x001007E4UL)  // UID 存放起始地址

static uint32_t sMcuFlashSize = 0;

/**
 * @brief Flash 解锁
 */
void bMcuFlashUnlock(void)
{
    FLASH->KEY = 0x5A5A;
    FLASH->KEY = 0xA5A5;
}

/**
 * @brief Flash 上锁
 */
void bMcuFlashLock(void)
{
    FLASH->CR1_f.OP = 0x00; // 恢复读取模式
    FLASH->KEY = 0x0000;    // 锁定
}

/**
 * @brief 初始化 Flash 信息
 */
int bMcuFlashInit(void)
{
    // CW32L010 通常为 64KB
    sMcuFlashSize = 64 * 1024;
    return 0;
}

/**
 * @brief 获取 Flash 总大小 (字节)
 */
uint32_t bMcuFlashChipSize(void)
{
    if (sMcuFlashSize == 0)
    {
        bMcuFlashInit();
    }
    return sMcuFlashSize;
}

/**
 * @brief 获取扇区大小
 */
uint32_t bMcuFlashSectorSize(void)
{
    return FLASH_PAGE_SIZE;
}

/**
 * @brief 擦除 Flash
 * @param addr 相对偏移地址
 * @param num  要擦除的页数
 */
int bMcuFlashErase(uint32_t addr, uint32_t num)
{
    uint32_t i;
    uint32_t page_addr = FLASH_BASE_ADDR + addr;

    for (i = 0; i < num; i++)
    {
        // 1. 配置为页擦除模式
        FLASH->CR1_f.OP = 0x02;
        
        // 2. 触发擦除 (向页内任一地址写数据)
        *((volatile uint32_t *)page_addr) = 0xFFFFFFFF;

        // 3. 等待 BUSY 清零
        while (FLASH->ISR_f.BUSY);
        
        // 4. 清除操作完成标志
        FLASH->ICR_f.PC = 0;

        page_addr += FLASH_PAGE_SIZE;
    }
    return 0;
}

/**
 * @brief 写入 Flash
 * @param addr 相对偏移地址
 * @param pbuf 数据缓冲区
 * @param len  写入长度 (字节)
 */
int bMcuFlashWrite(uint32_t addr, const uint8_t *pbuf, uint32_t len)
{
    uint32_t i;
    uint32_t raddr = FLASH_BASE_ADDR + addr;

    if (sMcuFlashSize == 0) bMcuFlashInit();
    if (pbuf == NULL || (addr + len) > sMcuFlashSize)
    {
        return -1;
    }

    // 设置为编程模式
    FLASH->CR1_f.OP = 0x01;

    for (i = 0; i < len; i++)
    {
        // CW32 支持直接字节编程
        *((volatile uint8_t *)(raddr + i)) = pbuf[i];
        
        // 等待操作完成
        while (FLASH->ISR_f.BUSY);
        
        // 检查编程错误
        if (FLASH->ISR_f.PROGERR)
        {
            FLASH->ICR_f.PROGERR = 0;
            return -2;
        }
    }
    return (int)len;
}

/**
 * @brief 读取 Flash
 */
int bMcuFlashRead(uint32_t raddr, uint8_t *pbuf, uint32_t len)
{
    if (sMcuFlashSize == 0) bMcuFlashInit();
    
    if (pbuf == NULL || (raddr + len) > sMcuFlashSize)
    {
        return -1;
    }

    // Flash 直接映射，使用内存拷贝
    memcpy(pbuf, (const uint8_t *)(FLASH_BASE_ADDR + raddr), len);
    return (int)len;
}

/**
 * @brief 读取芯片唯一标识 (UID)
 */
int bMcuFlashReadUID(uint8_t *pbuf, uint8_t buf_size, uint8_t *rlen)
{
    uint8_t  uid_len = 12; // CW32L010 UID 为 12 字节
    uint8_t *puid    = (uint8_t *)CW32_UID_ADDR;

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

/* 保持兼容性的旧接口封装 */
uint32_t bMcuFlashGetSize(void)
{
    return bMcuFlashChipSize();
}



