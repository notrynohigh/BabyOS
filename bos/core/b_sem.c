/**
 *!
 * \file        b_sem.c
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
#include "core/inc/b_sem.h"

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
 * \addtogroup SEM
 * \{
 */

/**
 * \defgroup SEM_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SEM_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SEM_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SEM_Private_Variables
 * \{
 */

static LIST_HEAD(bSemListHead);

/**
 * \}
 */

/**
 * \defgroup SEM_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SEM_Private_Functions
 * \{
 */

static bSemAttr_t *_bSemFind(bSemId_t id)
{
    struct list_head *pos   = NULL;
    bSemAttr_t       *pattr = NULL;
    list_for_each(pos, &bSemListHead)
    {
        pattr = list_entry(pos, bSemAttr_t, list);
        if (pattr == ((bSemAttr_t *)id))
        {
            break;
        }
        pattr = NULL;
    }
    return pattr;
}

/**
 * \}
 */

/**
 * \addtogroup SEM_Exported_Functions
 * \{
 */

bSemId_t bSemCreate(uint32_t max_count, uint32_t initial_count, bSemAttr_t *attr)
{
    if (attr == NULL)
    {
        return NULL;
    }
    if (_bSemFind(attr) != NULL)
    {
        return attr;
    }
    attr->value     = initial_count;
    attr->value_max = max_count;
    list_add(&attr->list, &bSemListHead);
    return attr;
}

int bSemAcquireNonblock(bSemId_t id)
{
    bSemAttr_t *attr = NULL;
    if (id == NULL)
    {
        return -1;
    }
    attr = _bSemFind(id);
    if (attr == NULL || attr->value == 0)
    {
        return -1;
    }
    attr->value -= 1;
    return 0;
}

int bSemRelease(bSemId_t id)
{
    bSemAttr_t *attr = NULL;
    if (id == NULL)
    {
        return -1;
    }
    attr = _bSemFind(id);
    if (attr == NULL)
    {
        return -1;
    }
    if (attr->value < attr->value_max)
    {
        attr->value += 1;
    }
    return 0;
}

uint32_t bSemGetCount(bSemId_t id)
{
    bSemAttr_t *attr = NULL;
    if (id == NULL)
    {
        return 0;
    }
    attr = _bSemFind(id);
    if (attr == NULL)
    {
        return 0;
    }
    return attr->value;
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
