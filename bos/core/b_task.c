/**
 *!
 * \file        b_task.c
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
#include "core/inc/b_task.h"

#include "b_section.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup CORE
 * \{
 */

/**
 * \addtogroup TASK
 * \{
 */

/**
 * \defgroup TASK_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TASK_Private_Defines
 * \{
 */
#define B_TASK_DEFAULT_NAME "NULL"
/**
 * \}
 */

/**
 * \defgroup TASK_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TASK_Private_Variables
 * \{
 */

static LIST_HEAD(bTaskListHead);
static bTaskAttr_t *pCurrentTaskAttr = NULL;
/**
 * \}
 */

/**
 * \defgroup TASK_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TASK_Private_Functions
 * \{
 */

static bTaskAttr_t *_bTaskFind(bTaskId_t id)
{
    struct list_head *pos   = NULL;
    bTaskAttr_t      *pattr = NULL;
    list_for_each(pos, &bTaskListHead)
    {
        pattr = list_entry(pos, bTaskAttr_t, list);
        if (pattr == ((bTaskAttr_t *)id))
        {
            break;
        }
        pattr = NULL;
    }
    return pattr;
}

static void _bTaskCore()
{
    struct list_head *pos   = NULL;
    bTaskAttr_t      *pattr = NULL;
    list_for_each(pos, &bTaskListHead)
    {
        pattr = list_entry(pos, bTaskAttr_t, list);
        if (pattr != NULL && pattr->func != NULL && pattr->enable == 1)
        {
            pCurrentTaskAttr = pattr;
            pattr->func(&pattr->task_pt, pattr->arg);
        }
    }
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section bos_polling
#endif
BOS_REG_POLLING_FUNC(_bTaskCore);
#ifdef BSECTION_NEED_PRAGMA
#pragma section
#endif
/**
 * \}
 */

/**
 * \addtogroup TASK_Exported_Functions
 * \{
 */

bTaskId_t bTaskCreate(const char *name, bTaskFunc_t func, void *argument, bTaskAttr_t *attr)
{
    if (attr == NULL || func == NULL)
    {
        return NULL;
    }
    if (_bTaskFind(attr) != NULL)
    {
        return attr;
    }
    attr->name         = (name == NULL) ? B_TASK_DEFAULT_NAME : name;
    attr->func         = func;
    attr->arg          = argument;
    attr->enable       = 1;
    attr->task_pt.init = 0;
    PT_INIT(&attr->task_pt);
    list_add(&attr->list, &bTaskListHead);
    return attr;
}

/// 销毁任务
void bTaskRemove(bTaskId_t id)
{
    bTaskAttr_t *pattr = NULL;
    if (id == NULL)
    {
        return;
    }
    pattr = _bTaskFind(id);
    if (pattr != NULL)
    {
        __list_del(pattr->list.prev, pattr->list.next);
        pattr->arg    = NULL;
        pattr->enable = 0;
        pattr->func   = NULL;
    }
}

/// 暂停任务
void bTaskSuspend(bTaskId_t id)
{
    bTaskAttr_t *pattr = NULL;
    if (id == NULL)
    {
        return;
    }
    pattr = _bTaskFind(id);
    if (pattr != NULL)
    {
        pattr->enable = 0;
    }
}

/// 恢复任务
void bTaskResume(bTaskId_t id)
{
    bTaskAttr_t *pattr = NULL;
    if (id == NULL)
    {
        return;
    }
    pattr = _bTaskFind(id);
    if (pattr != NULL)
    {
        pattr->enable = 1;
    }
}

/// 获取id对应的任务名。id为NULL表示获取当前任务
const char *bTaskGetName(bTaskId_t id)
{
    bTaskAttr_t *pattr = NULL;
    if (id == NULL && pCurrentTaskAttr != NULL)
    {
        return pCurrentTaskAttr->name;
    }
    pattr = _bTaskFind(id);
    if (pattr == NULL)
    {
        return NULL;
    }
    return pattr->name;
}

/// 获取当前运行任务的id
bTaskId_t bTaskGetId()
{
    return pCurrentTaskAttr;
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
