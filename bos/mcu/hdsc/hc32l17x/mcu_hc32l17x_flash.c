#include "b_config.h"
#include "hal/inc/b_hal_flash.h"

typedef struct
{
    volatile uint32_t TNVS;
    volatile uint32_t TPGS;
    volatile uint32_t TPROG;
    volatile uint32_t TSERASE;
    volatile uint32_t TMERASE;
    volatile uint32_t TPRCV;
    volatile uint32_t TSRCV;
    volatile uint32_t TMRCV;
    volatile uint32_t CR;
    volatile uint32_t IFR;
    volatile uint32_t ICLR;
    volatile uint32_t BYPASS;
    volatile uint32_t SLOCK0;
    volatile uint32_t SLOCK1;
} bMcuFlash_t;

#define B_MCU_FLASH ((bMcuFlash_t *)0x40020000UL)

#define FLASH_BYPASS()                \
    do                                \
    {                                 \
        B_MCU_FLASH->BYPASS = 0x5A5A; \
        B_MCU_FLASH->BYPASS = 0xA5A5; \
    } while (0);

#define FLASH_TIMEOUT_PGM (0xFFFFFFu)
#define FLASH_TIMEOUT_ERASE (0xFFFFFFu)

#define FLASH_LOCK_ALL (0u)
#define FLASH_UNLOCK_ALL (0xFFFFFFFFu)

#define FLASH_BASE_ADDR (0x0000000UL)
#define FLASH_PAGE_SIZE (512)
#define FLASH_MAX_SIZE (*((volatile uint32_t *)0x00100C70))
#define FLASH_END_ADDR ((FLASH_BASE_ADDR) + FLASH_MAX_SIZE)

int bMcuFlashInit()
{
    return 0;
}

int bMcuFlashUnlock()
{
    FLASH_BYPASS();
    B_MCU_FLASH->SLOCK0 = FLASH_UNLOCK_ALL;
    FLASH_BYPASS();
    B_MCU_FLASH->SLOCK1 = FLASH_UNLOCK_ALL;

    if ((FLASH_UNLOCK_ALL == B_MCU_FLASH->SLOCK0) && (FLASH_UNLOCK_ALL == B_MCU_FLASH->SLOCK1))
    {
        return 0;
    }
    return -1;
}

int bMcuFlashLock()
{
    do
    {
        FLASH_BYPASS();
        B_MODIFY_REG(B_MCU_FLASH->CR, 0x3, 0x0);
    } while (B_READ_BIT(B_MCU_FLASH->CR, 0x3) != 0);

    do
    {
        FLASH_BYPASS();
        B_MCU_FLASH->SLOCK0 = FLASH_LOCK_ALL;
        FLASH_BYPASS();
        B_MCU_FLASH->SLOCK1 = FLASH_LOCK_ALL;
    } while (!((FLASH_LOCK_ALL == B_MCU_FLASH->SLOCK0) && (FLASH_LOCK_ALL == B_MCU_FLASH->SLOCK1)));

    return 0;
}

int bMcuFlashErase(uint32_t raddr, uint32_t pages)
{
    int               i          = 0;
    volatile uint32_t u32TimeOut = FLASH_TIMEOUT_ERASE;
    do
    {
        FLASH_BYPASS();
        B_MODIFY_REG(B_MCU_FLASH->CR, 0x3, 0x2);
    } while (B_READ_BIT(B_MCU_FLASH->CR, 0x3) != 2);

    for (i = 0; i < pages; i++)
    {
        if (FLASH_END_ADDR < raddr)
        {
            return -1;
        }

        // busy?
        u32TimeOut = FLASH_TIMEOUT_ERASE;
        while (B_READ_BIT(B_MCU_FLASH->CR, (0x1 << 4)))
        {
            if (0 == u32TimeOut--)
            {
                return -2;
            }
        }

        // write data
        *((volatile uint32_t *)raddr) = 0;

        // busy?
        u32TimeOut = FLASH_TIMEOUT_ERASE;
        while (B_READ_BIT(B_MCU_FLASH->CR, (0x1 << 4)))
        {
            if (0 == u32TimeOut--)
            {
                return -2;
            }
        }
        raddr += FLASH_PAGE_SIZE;
    }
    return 0;
}

int bMcuFlashWrite(uint32_t raddr, const uint8_t *pbuf, uint32_t len)
{
    uint32_t          u32Index   = 0;
    volatile uint32_t u32TimeOut = FLASH_TIMEOUT_PGM;
    if (pbuf == NULL)
    {
        return -1;
    }

    do
    {
        FLASH_BYPASS();
        B_MODIFY_REG(B_MCU_FLASH->CR, 0x3, 0x1);
    } while (B_READ_BIT(B_MCU_FLASH->CR, 0x3) != 1);

    if (FLASH_END_ADDR < (raddr + len - 1))
    {
        return -1;
    }

    // busy?
    u32TimeOut = FLASH_TIMEOUT_PGM;
    while (B_READ_BIT(B_MCU_FLASH->CR, (0x1 << 4)))
    {
        if (0 == u32TimeOut--)
        {
            return -2;
        }
    }

    // write data byte
    for (u32Index = 0; u32Index < len; u32Index++)
    {
        *((volatile uint8_t *)raddr) = pbuf[u32Index];

        // busy?
        u32TimeOut = FLASH_TIMEOUT_PGM;
        while (B_READ_BIT(B_MCU_FLASH->CR, (0x1 << 4)))
        {
            if (0 == u32TimeOut--)
            {
                return -2;
            }
        }

        if (pbuf[u32Index] != *((volatile uint8_t *)raddr))
        {
            return -3;
        }
        raddr++;
    }
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
