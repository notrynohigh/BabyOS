/**
 *!
 * \file        b_hal.h
 * \version     v0.0.1
 * \date        2019/06/05
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2019 Bean
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
#ifndef __B_HAL_H__
#define __B_HAL_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include "b_config.h"
#include "b_hal_display.h"
#include "b_hal_dma.h"
#include "b_hal_eth.h"
#include "b_hal_flash.h"
#include "b_hal_gpio.h"
#include "b_hal_i2c.h"
#include "b_hal_if.h"
#include "b_hal_it.h"
#include "b_hal_qspi.h"
#include "b_hal_rng.h"
#include "b_hal_sdio.h"
#include "b_hal_spi.h"
#include "b_hal_uart.h"
#include "b_hal_wdt.h"

/**
 * \addtogroup B_HAL
 * \{
 */

/**
 * \addtogroup HAL
 * \{
 */

/**
 * \defgroup HAL_Exported_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup HAL_Exported_Defines
 * \{
 */
#define MS2TICKS(m) ((m) / (1000 / TICK_FRQ_HZ))
#define TICKS2MS(t) ((t) * (1000 / TICK_FRQ_HZ))

#define TICK_MAX_UINT32 0xFFFFFFFF
#define TICK_DIFF_BIT32(start, end) \
    ((uint32_t)(((end) > (start)) ? ((end) - (start)) : (TICK_MAX_UINT32 - (start) + (end) + 1)))

#if (defined(_HALIF_VARIABLE_ENABLE) && (_HALIF_VARIABLE_ENABLE == 1))
#define HALIF_KEYWORD static
#else
#define HALIF_KEYWORD const static
#endif

/**
 * \}
 */

/**
 * \defgroup HAL_Exported_Functions
 * \{
 */

// 移植时调用，TICK中断服务函数调用bHalIncSysTick
void bHalIncSysTick(void);
#if defined(__WEAKDEF)
// 弱函数，用户可重新实现此函数。bInit->bHalInit->bHalUserInit
void bHalUserInit(void);
#else
// 若编译器不支持弱函数，调用此接口注册
void bHalRegUserInit(void (*cb)(void));
#endif

void     bHalInit(void);
void     bHalDelayMs(uint16_t xms);
void     bHalDelayUs(uint32_t xus);
uint32_t bHalGetSysTick(void);
uint64_t bHalGetSysTickPlus(void);

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
