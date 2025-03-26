/**
 *!
 * \file        b_hal.c
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
 * The above copyright notice and this permission notice shall be included in
 *all copies or substantial portions of the Software.
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

/*Includes ----------------------------------------------*/
#include "hal/inc/b_hal.h"

#include <stdio.h>
#include <string.h>

/**
 * \addtogroup B_HAL
 * \{
 */

/**
 * \addtogroup HAL
 * \{
 */

/**
 * \defgroup HAL_Private_Variables
 * \{
 */
#if defined(USER_TICK_ENABLE) && (USER_TICK_ENABLE == 1)

#else
static volatile uint32_t bSysTickOvr = 0;
static volatile uint32_t bSysTick    = 0;
#endif
static uint32_t bUsDelayParam = 10;
/**
 * \}
 */

/**
 * \defgroup HAL_Private_Functions
 * \{
 */
static void _bHalUpdateDelayParam()
{
    uint32_t          base_value = 100000 * bUsDelayParam;
    volatile uint32_t delay      = 0;
    volatile uint32_t tick_s = 0, tick_e = 0;
    while (1)
    {
        delay  = base_value;
        tick_s = bHalGetSysTick();
        while (delay--)
            ;
        tick_e = bHalGetSysTick();
        if (tick_e > tick_s)
        {
            bUsDelayParam = (uint32_t)((base_value * 1.0) / (TICKS2MS(tick_e - tick_s)) / 1000);
            if (bUsDelayParam > 0)
            {
                break;
            }
        }
        base_value *= 10;
    }
}
/**
 * \}
 */

/**
 * \addtogroup HAL_Exported_Functions
 * \{
 */
#if defined(__WEAKDEF)
__WEAKDEF void bHalUserInit()
{
    ;
}
#else
static void (*pbHalUserInitFunc)(void) = NULL;
void bHalUserInit()
{
    if (pbHalUserInitFunc)
    {
        pbHalUserInitFunc();
    }
}

void bHalRegUserInit(void (*cb)(void))
{
    pbHalUserInitFunc = cb;
}
#endif

void bHalInit()
{
    bHalUserInit();
#if (defined(_WDT_ENABLE) && (_WDT_ENABLE == 1))
#ifndef WDT_TIMEOUT_S
#define WDT_TIMEOUT_S (60)
#endif
    bHalWdtStart(WDT_TIMEOUT_S);
#endif
    _bHalUpdateDelayParam();
}

#if defined(USER_TICK_ENABLE) && (USER_TICK_ENABLE == 1)

#if defined(__WEAKDEF)
__WEAKDEF uint32_t bHalGetSysTick()
{
    return 0;
}

__WEAKDEF uint64_t bHalGetSysTickPlus()
{
    return 0;
}

#endif

#else
void bHalIncSysTick()
{
    bSysTick += 1;
    if (bSysTick == 0)
    {
        bSysTickOvr += 1;
    }
}

uint32_t bHalGetSysTick()
{
    return bSysTick;
}

uint64_t bHalGetSysTickPlus()
{
    uint64_t tick = bSysTickOvr;
    tick          = bSysTick + (tick << (8 * sizeof(uint32_t)));
    return tick;
}

#endif

void bHalDelayMs(uint16_t xms)
{
    bHalDelayUs(1000 * xms);
}

void bHalDelayUs(uint32_t xus)
{
    volatile uint32_t delay = xus * bUsDelayParam;
    while (delay--)
        ;
}
/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
