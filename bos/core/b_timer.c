/**
 *!
 * \file        b_timer.c
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

/*Includes ----------------------------------------------*/
#include "core/inc/b_timer.h"

#include "b_section.h"
#include "hal/inc/b_hal.h"
/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup CORE
 * \{
 */

/**
 * \addtogroup TIMER
 * \{
 */

/**
 * \defgroup TIMER_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TIMER_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TIMER_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TIMER_Private_Variables
 * \{
 */

static LIST_HEAD(bTimerListHead);

/**
 * \}
 */

/**
 * \defgroup TIMER_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TIMER_Private_Functions
 * \{
 */

static bTimerAttr_t *_bTimerFind(bTimerId_t id)
{
    struct list_head *pos   = NULL;
    bTimerAttr_t     *pattr = NULL;
    list_for_each(pos, &bTimerListHead)
    {
        pattr = list_entry(pos, bTimerAttr_t, list);
        if (pattr == ((bTimerAttr_t *)id))
        {
            break;
        }
        pattr = NULL;
    }
    return pattr;
}

static void _bTimerCore()
{
    struct list_head *pos   = NULL;
    bTimerAttr_t     *pattr = NULL;
    list_for_each(pos, &bTimerListHead)
    {
        pattr = list_entry(pos, bTimerAttr_t, list);
        if (pattr != NULL && pattr->func != NULL && pattr->enable == 1)
        {
            if (bHalGetSysTick() - pattr->tick > MS2TICKS(pattr->cycle))
            {
                pattr->func(pattr->arg);
                if (pattr->type == B_TIMER_ONCE)
                {
                    pattr->enable = 0;
                }
                else
                {
                    pattr->tick = bHalGetSysTick();
                }
            }
        }
    }
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section bos_polling
#endif
BOS_REG_POLLING_FUNC(_bTimerCore);
#ifdef BSECTION_NEED_PRAGMA
#pragma section 
#endif
/**
 * \}
 */

/**
 * \addtogroup TIMER_Exported_Functions
 * \{
 */

bTimerId_t bTimerCreate(bTimerFunc_t func, bTimerType_t type, void *argument, bTimerAttr_t *attr)
{
    if (attr == NULL || func == NULL)
    {
        return NULL;
    }
    if (_bTimerFind(attr) != NULL)
    {
        return attr;
    }
    attr->func   = func;
    attr->arg    = argument;
    attr->type   = type;
    attr->enable = 0;
    list_add(&attr->list, &bTimerListHead);
    return attr;
}

int bTimerStart(bTimerId_t id, uint32_t ms)
{
    bTimerAttr_t *pattr = NULL;
    if (id == NULL || ms == 0)
    {
        return -1;
    }
    pattr = _bTimerFind(id);
    if (pattr == NULL)
    {
        return -1;
    }
    pattr->cycle  = ms;
    pattr->tick   = bHalGetSysTick();
    pattr->enable = 1;
    return 0;
}

int bTimerStop(bTimerId_t id)
{
    bTimerAttr_t *pattr = NULL;
    if (id == NULL)
    {
        return -1;
    }
    pattr = _bTimerFind(id);
    if (pattr == NULL)
    {
        return -1;
    }
    pattr->enable = 0;
    return 0;
}

uint32_t bTimerIsRunning(bTimerId_t id)
{
    bTimerAttr_t *pattr = NULL;
    if (id == NULL)
    {
        return 0;
    }
    pattr = _bTimerFind(id);
    if (pattr == NULL)
    {
        return 0;
    }
    return (pattr->enable == 1);
}

int bTimerDelete(bTimerId_t id)
{
    bTimerAttr_t *pattr = NULL;
    if (id == NULL)
    {
        return -1;
    }
    pattr = _bTimerFind(id);
    if (pattr == NULL)
    {
        return -1;
    }
    __list_del(pattr->list.prev, pattr->list.next);
    return 0;
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
