#include "b_config.h"
#include "hal/inc/b_hal_flash.h"

#if !defined(STM32G4XX)
#error "Please select first the target STM32G473 device used in your application (in stm32g473xx.h file)"
#else

#include "stm32g4xx_hal.h"
#include "utils/inc/b_util_log.h"

#define FLASH_BASE_ADDR      (0x08000000UL)
#define G4_PROG_UNIT         (8)  // 硬件强制要求 8 字节

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

int bMcuFlashErase(uint32_t raddr, uint32_t pages)
{
    uint32_t abs_addr = 0x08000000UL + raddr;
    uint32_t page_error = 0;
    FLASH_EraseInitTypeDef erase_init;
    HAL_StatusTypeDef status;

    // 1. 确定擦除区域（G4 支持单银行或双银行擦除，这里使用标准的页面擦除）
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    
    // 2. 计算起始页码
    // 对于 STM32G473，通常页大小为 4KB (0x1000)
    // 注意：请确认 FLASH_PAGE_SIZE 在您的 stm32g4xx_hal_flash.h 中定义正确
    erase_init.Page = (abs_addr - 0x08000000UL) / FLASH_PAGE_SIZE;
    erase_init.NbPages = pages;
    
    // 3. 处理 Banks
    // G473 具有双 Bank 结构。如果地址超过了第一个 Bank 的范围，需要指定 Bank 2
    // 大部分 HAL 库会自动处理，但为了严谨，我们根据页码简单判断
    if (erase_init.Page < 128) // 假设每 Bank 128 页，具体取决于型号
    {
        erase_init.Banks = FLASH_BANK_1;
    }
    else
    {
        erase_init.Banks = FLASH_BANK_2;
    }

    // 4. 执行擦除
    status = HAL_FLASHEx_Erase(&erase_init, &page_error);

    if (status != HAL_OK)
    {
        return -1;
    }

    return 0;
}
int bMcuFlashWrite(uint32_t raddr, const uint8_t *pbuf, uint32_t len)
{
    uint32_t abs_addr = FLASH_BASE_ADDR + raddr;
    uint32_t i = 0;
    uint64_t prog_data = 0;
    HAL_StatusTypeDef status;

    while (i < len)
    {
        // 1. 计算 8 字节对齐的起始地址
        uint32_t align_addr = (abs_addr / G4_PROG_UNIT) * G4_PROG_UNIT;
        uint32_t offset = abs_addr % G4_PROG_UNIT;
        
        // 2. 准备 8 字节编程数据 (Double-Word)
        // 即使 BabyOS 只写 4 字节，我们也必须读出相邻的 8 字节来补齐
        // 否则无法满足硬件的 Double-word 写入要求
        prog_data = *(volatile uint64_t *)align_addr; 
        
        // 计算本次能写入多少字节（不能超过当前 8 字节块的边界）
        uint32_t chunk = (G4_PROG_UNIT - offset);
        if (chunk > (len - i)) 
        {
            chunk = len - i;
        }

        // 将新数据覆盖到 8 字节缓存中
        memcpy((uint8_t *)&prog_data + offset, &pbuf[i], chunk);

        // 3. 执行硬件编程 (Double-word)
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, align_addr, prog_data);
        
        if (status != HAL_OK)
        {
            return -2;
        }

        i += chunk;
        abs_addr += chunk;
    }

    return len;
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



