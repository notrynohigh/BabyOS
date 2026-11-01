/**
 * \file port.c
 * \brief
 * \version 0.1
 * \date 2022-10-29
 * \author notrynohigh (notrynohigh@outlook.com)
 *
 * Copyright (c) 2020 by notrynohigh. All Rights Reserved.
 */

#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "b_os.h"
#include "port.h"
#include "b_config.h"

int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    printf("%s", pbuf);
    fflush(stdout);
    return len;
}

void port_init()
{
    bMockFlash_Init();
}

uint64_t _bGetClock()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

uint32_t bHalGetSysTick()
{
    return ((uint32_t)_bGetClock());
}

uint64_t bHalGetSysTickPlus()
{
    return _bGetClock();
}

/* ================================================================ */
/* ============== HAL Mock Infrastructure ======================== */
/* ================================================================ */

#define MOCK_FLASH_SIZE (512 * 1024)

static uint8_t s_mock_flash[MOCK_FLASH_SIZE];
static uint8_t s_mock_flash_init = 0;
static uint32_t s_mock_flash_sector_size = 4096;

/* ----- Mock GPIO (overrides weak bMcuGpioReadPin) ----- */
static uint8_t (*s_mock_gpio_read)(void) = NULL;
static void *s_mock_gpio_arg = NULL;

uint8_t bMcuGpioReadPin(bHalGPIOPort_t port, bHalGPIOPin_t pin)
{
    if (s_mock_gpio_read != NULL)
    {
        return s_mock_gpio_read();
    }
    return 0;
}

void bMcuGpioConfig(bHalGPIOPort_t port, bHalGPIOPin_t pin,
                   bHalGPIODir_t dir, bHalGPIOPull_t pull)
{
    (void)port; (void)pin; (void)dir; (void)pull;
}

void bMcuGpioWritePin(bHalGPIOPort_t port, bHalGPIOPin_t pin, uint8_t s)
{
    (void)port; (void)pin; (void)s;
}

void bMcuGpioWritePort(bHalGPIOPort_t port, uint16_t dat)
{
    (void)port; (void)dat;
}

uint16_t bMcuGpioReadPort(bHalGPIOPort_t port)
{
    (void)port;
    return 0;
}

/* ----- Mock Flash (overrides weak bMcuFlash*) ----- */
int bMcuFlashInit(void)
{
    return 0;
}

int bMcuFlashUnlock(void)
{
    return 0;
}

int bMcuFlashLock(void)
{
    return 0;
}

#ifdef MCUFLASH_BASE_ADDR
static uint32_t _mock_flash_abs(uint32_t raddr)
{
    return (uint32_t)(MCUFLASH_BASE_ADDR + raddr);
}
#endif

int bMcuFlashErase(uint32_t raddr, uint32_t pages)
{
    if (!s_mock_flash_init)
        bMockFlash_Init();
#ifdef MCUFLASH_BASE_ADDR
    uint32_t abs = _mock_flash_abs(raddr);
    uint32_t base = abs - MCUFLASH_BASE_ADDR;
#else
    uint32_t base = raddr;
#endif
    if (base >= MOCK_FLASH_SIZE)
        return -1;
    for (uint32_t i = 0; i < pages * s_mock_flash_sector_size; i++)
    {
        if (base + i < MOCK_FLASH_SIZE)
            s_mock_flash[base + i] = 0xFF;
    }
    return 0;
}

int bMcuFlashWrite(uint32_t raddr, const uint8_t *pbuf, uint32_t len)
{
    if (!s_mock_flash_init)
        bMockFlash_Init();
#ifdef MCUFLASH_BASE_ADDR
    uint32_t abs = _mock_flash_abs(raddr);
    uint32_t base = abs - MCUFLASH_BASE_ADDR;
#else
    uint32_t base = raddr;
#endif
    if (base >= MOCK_FLASH_SIZE)
        return -1;
    for (uint32_t i = 0; i < len; i++)
    {
        if (base + i < MOCK_FLASH_SIZE)
            s_mock_flash[base + i] = pbuf[i];
    }
    return (int)len;
}

int bMcuFlashRead(uint32_t raddr, uint8_t *pbuf, uint32_t len)
{
    if (!s_mock_flash_init)
        bMockFlash_Init();
#ifdef MCUFLASH_BASE_ADDR
    uint32_t abs = _mock_flash_abs(raddr);
    uint32_t base = abs - MCUFLASH_BASE_ADDR;
#else
    uint32_t base = raddr;
#endif
    if (base >= MOCK_FLASH_SIZE)
        return -1;
    for (uint32_t i = 0; i < len; i++)
    {
        if (base + i < MOCK_FLASH_SIZE)
            pbuf[i] = s_mock_flash[base + i];
    }
    return (int)len;
}

uint32_t bMcuFlashSectorSize(void)
{
    return s_mock_flash_sector_size;
}

uint32_t bMcuFlashChipSize(void)
{
    return MOCK_FLASH_SIZE;
}

int bMcuFlashReadUID(uint8_t *pbuf, uint8_t buf_size, uint8_t *rlen)
{
    if (pbuf == NULL || rlen == NULL)
        return -1;
    *rlen = 0;
    (void)buf_size;
    return 0;
}

/* ----- Public mock API (exposed via port.h) ----- */
void bMockFlash_Init(void)
{
    if (s_mock_flash_init)
        return;
    memset(s_mock_flash, 0xFF, sizeof(s_mock_flash));
    s_mock_flash_init = 1;
}

void bMockFlash_Reset(void)
{
    memset(s_mock_flash, 0xFF, sizeof(s_mock_flash));
    s_mock_flash_init = 1;
}

void bMockFlash_WriteAt(uint32_t abs_addr, uint8_t *data, uint32_t len)
{
    if (!s_mock_flash_init)
        bMockFlash_Init();
#ifdef MCUFLASH_BASE_ADDR
    uint32_t base = abs_addr - MCUFLASH_BASE_ADDR;
#else
    uint32_t base = abs_addr;
#endif
    if (base >= MOCK_FLASH_SIZE)
        return;
    if (len > MOCK_FLASH_SIZE - base)
        len = MOCK_FLASH_SIZE - base;
    memcpy(&s_mock_flash[base], data, len);
}

void bMockFlash_ReadAt(uint32_t abs_addr, uint8_t *data, uint32_t len)
{
    if (!s_mock_flash_init)
        bMockFlash_Init();
#ifdef MCUFLASH_BASE_ADDR
    uint32_t base = abs_addr - MCUFLASH_BASE_ADDR;
#else
    uint32_t base = abs_addr;
#endif
    if (base >= MOCK_FLASH_SIZE)
        return;
    if (len > MOCK_FLASH_SIZE - base)
        len = MOCK_FLASH_SIZE - base;
    memcpy(data, &s_mock_flash[base], len);
}

void bMockFlash_EraseAt(uint32_t abs_addr, uint32_t pages)
{
    if (!s_mock_flash_init)
        bMockFlash_Init();
#ifdef MCUFLASH_BASE_ADDR
    uint32_t base = abs_addr - MCUFLASH_BASE_ADDR;
#else
    uint32_t base = abs_addr;
#endif
    if (base >= MOCK_FLASH_SIZE)
        return;
    if (pages * s_mock_flash_sector_size > MOCK_FLASH_SIZE - base)
        pages = (MOCK_FLASH_SIZE - base) / s_mock_flash_sector_size;
    memset(&s_mock_flash[base], 0xFF, pages * s_mock_flash_sector_size);
}

void bMockGpio_SetIntercept(uint8_t (*read_fn)(void), void *arg)
{
    s_mock_gpio_read = read_fn;
    s_mock_gpio_arg = arg;
}

void bMockGpio_ClearIntercept(void)
{
    s_mock_gpio_read = NULL;
    s_mock_gpio_arg = NULL;
}

/* ================================================================ */
/* ============== Selftest-only overrides ======================= */
/* ================================================================ */

#ifdef _SELFTEST_BUILD_

/* Override bIapJump2App/Boot: prevents segfault when tests call bIapInit()
 * in boot mode. The original weak implementation reads from APP_START_ADDR,
 * which is an invalid address in the x86_64 test environment. */
void bIapJump2App(void) { }
void bIapJump2Boot(void) { }

#endif /* _SELFTEST_BUILD_ */

/* ================================================================ */
/* ============== Fake KEY device driver ======================== */
/* ================================================================ */

#ifdef _SELFTEST_BUILD_

static int _bKeyRead(bDriverInterface_t *pdrv, uint32_t off, uint8_t *pbuf, uint32_t len)
{
    if (len == 0)
        return -1;
    (void)pdrv; (void)off;
    uint8_t level = bHalGpioReadPin(0, 0);
    pbuf[0] = level;
    return 1;
}

static int _bKeyInit(bDriverInterface_t *pdrv)
{
    pdrv->status = 0;
    pdrv->read = _bKeyRead;
    return 0;
}

/* Override bKEY_Init: make static so it doesn't conflict with b_drv_key.c's weak definition */
#define bKEY_Init _bKeyInit

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_KEY, bKEY_Init);
#ifdef BSECTION_NEED_PRAGMA
#pragma section
#endif

#endif /* _SELFTEST_BUILD_ */