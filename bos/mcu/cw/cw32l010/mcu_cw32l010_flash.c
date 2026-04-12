#include "b_config.h"
#include "hal/inc/b_hal_flash.h"
#include "cw32l010.h"
#include "utils/inc/b_util_log.h"
#include <string.h>

#include "cw32l010_flash.h"
#include "cw32l010_digitalsign.h"

/* CW32L010 Flash 配置 */
#define FLASH_PAGE_SIZE      (512)           // 每页 512 字节
#define FLASH_BASE_ADDR      (0x00000000UL)  // 存储起始物理地址
#define CW32_UID_ADDR        (0x001007B0)  // UID 存放起始地址

static volatile uint32_t sMcuFlashSize = 0;
static volatile uint8_t  sFlashInited = 0;

/**
 * @brief 初始化 Flash 信息（线程安全）
 */
int bMcuFlashInit(void)
{
    if (!sFlashInited)
    {
        sMcuFlashSize = DIGITALSIGN_GetFlashSize();
        sFlashInited = 1;
    }
    return 0;
}

/**
 * @brief Flash 解锁
 */
int bMcuFlashUnlock(void)
{
	FLASH_UnlockAllPages();
	return 0;
}

/**
 * @brief Flash 上锁
 */
int bMcuFlashLock(void)
{
	FLASH_LockAllPages();
	return 0;
}



/**
 * @brief 擦除 Flash
 * @param addr 相对偏移地址
 * @param num  要擦除的页数
 */
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
	uint32_t EndAddr = raddr + (pages-1) * FLASH_PAGE_SIZE;
	uint8_t status = FLASH_ErasePages(raddr, EndAddr);
	b_log_w("erase status:%x,%x, %d, %d\r\n", raddr,EndAddr,pages,status);
    return 0;	
}


/**
 * @brief 写入 Flash
 * @param addr 相对偏移地址
 * @param pbuf 数据缓冲区
 * @param len  写入长度 (字节)
 */
int bMcuFlashWrite(uint32_t raddr, const uint8_t *pbuf, uint32_t len)
{
	uint16_t wdata = 0;
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
	uint8_t status;
	
    for (i = 0; i < wlen; i++)
    {
        wdata = (wdata << 8) | pbuf[i * 2 + 1];
        wdata = (wdata << 8) | pbuf[i * 2 + 0];
		status = FLASH_WriteHalfWords(raddr, &wdata,1);
        if ((status != 0x00)&&(status != 0x10))
//		if ((status != 0x00))
        {
            b_log_e("write error:%x %d\r\n", raddr, status);
            b_log_hex(pbuf, len);
            return -2;
        }
        raddr += 2;
    }
	
	return (wlen * 2);	
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
 * @brief 获取扇区大小
 */
uint32_t bMcuFlashSectorSize(void)
{
    return FLASH_PAGE_SIZE;
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
 * @brief 读取芯片唯一标识 (UID)
 */
int bMcuFlashReadUID(uint8_t *pbuf, uint8_t buf_size, uint8_t *rlen)
{
    uint8_t  uid_len = 10; // CW32L010 UID 为 10 字节
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



