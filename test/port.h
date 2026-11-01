/**
 * \file port.h
 * \brief
 * \version 0.1
 * \date 2022-10-29
 * \author notrynohigh (notrynohigh@outlook.com)
 *
 * Copyright (c) 2020 by notrynohigh. All Rights Reserved.
 */
#ifndef __PORT_H__
#define __PORT_H__

#include <stdint.h>

void port_init(void);

/* ============== HAL Mock API (common) ============== */

void bMockFlash_Init(void);
void bMockFlash_Reset(void);
void bMockFlash_WriteAt(uint32_t abs_addr, uint8_t *data, uint32_t len);
void bMockFlash_ReadAt(uint32_t abs_addr, uint8_t *data, uint32_t len);
void bMockFlash_EraseAt(uint32_t abs_addr, uint32_t pages);

void bMockGpio_SetIntercept(uint8_t (*read_fn)(void), void *arg);
void bMockGpio_ClearIntercept(void);

/* ============== Selftest-only API ============== */

#ifdef _SELFTEST_BUILD_

/* IAP jump stubs: safe no-ops on x86_64 (APP_START_ADDR is out of range) */
void bIapJump2App(void);
void bIapJump2Boot(void);

#endif

#endif