/**
 *!
 * \file        b_queue.c
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
#include "core/inc/b_queue.h"

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
 * \addtogroup QUEUE
 * \{
 */

/**
 * \defgroup QUEUE_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup QUEUE_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup QUEUE_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup QUEUE_Private_Variables
 * \{
 */

static LIST_HEAD(bQueueListHead);

/**
 * \}
 */

/**
 * \defgroup QUEUE_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup QUEUE_Private_Functions
 * \{
 */

static bQueueAttr_t *_bQueueFind(bQueueId_t id)
{
    struct list_head *pos   = NULL;
    bQueueAttr_t     *pattr = NULL;
    list_for_each(pos, &bQueueListHead)
    {
        pattr = list_entry(pos, bQueueAttr_t, list);
        if (pattr == ((bQueueAttr_t *)id))
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
 * \addtogroup QUEUE_Exported_Functions
 * \{
 */
bQueueId_t bQueueCreate(uint32_t msg_count, uint32_t msg_size, bQueueAttr_t *attr)
{
    if (attr == NULL || attr->mq_mem == NULL || (attr->mq_size < (msg_count * msg_size)))
    {
        return NULL;
    }
    if (_bQueueFind(attr) != NULL)
    {
        return attr;
    }
    attr->number    = 0;
    attr->r_index   = 0;
    attr->w_index   = 0;
    attr->msg_count = msg_count;
    attr->msg_size  = msg_size;
    list_add(&attr->list, &bQueueListHead);
    return attr;
}

int bQueuePutNonblock(bQueueId_t id, const void *msg_ptr)
{
    bQueueAttr_t *attr = NULL;
    uint8_t      *pbuf = NULL;
    if (id == NULL || msg_ptr == NULL)
    {
        return -1;
    }
    attr = _bQueueFind(id);
    if (attr == NULL)
    {
        return -1;
    }
    if (attr->number >= attr->msg_count)
    {
        return -2;
    }
    pbuf = (uint8_t *)attr->mq_mem;
    memcpy(&pbuf[attr->w_index * attr->msg_size], msg_ptr, attr->msg_size);
    attr->w_index = (attr->w_index + 1) % attr->msg_count;
    attr->number += 1;
    return 0;
}

int bQueueGetNonblock(bQueueId_t id, void *msg_ptr)
{
    bQueueAttr_t *attr = NULL;
    uint8_t      *pbuf = NULL;
    if (id == NULL || msg_ptr == NULL)
    {
        return -1;
    }
    attr = _bQueueFind(id);
    if (attr == NULL)
    {
        return -1;
    }
    if (attr->number == 0)
    {
        return -3;
    }
    pbuf = (uint8_t *)attr->mq_mem;
    memcpy(msg_ptr, &pbuf[attr->r_index * attr->msg_size], attr->msg_size);
    attr->r_index = (attr->r_index + 1) % attr->msg_count;
    attr->number -= 1;
    return 0;
}

uint32_t bQueueGetCapacity(bQueueId_t id)
{
    bQueueAttr_t *attr = NULL;
    if (id == NULL)
    {
        return 0;
    }
    attr = _bQueueFind(id);
    if (attr == NULL)
    {
        return 0;
    }
    return attr->msg_count;
}

uint32_t bQueueGetMsgSize(bQueueId_t id)
{
    bQueueAttr_t *attr = NULL;
    if (id == NULL)
    {
        return 0;
    }
    attr = _bQueueFind(id);
    if (attr == NULL)
    {
        return 0;
    }
    return attr->msg_size;
}

uint32_t bQueueGetCount(bQueueId_t id)
{
    bQueueAttr_t *attr = NULL;
    if (id == NULL)
    {
        return 0;
    }
    attr = _bQueueFind(id);
    if (attr == NULL)
    {
        return 0;
    }
    return attr->number;
}

uint32_t bQueueGetSpace(bQueueId_t id)
{
    bQueueAttr_t *attr = NULL;
    if (id == NULL)
    {
        return 0;
    }
    attr = _bQueueFind(id);
    if (attr == NULL)
    {
        return 0;
    }
    return (attr->msg_count - attr->number);
}

int bQueueReset(bQueueId_t id)
{
    bQueueAttr_t *attr = NULL;
    if (id == NULL)
    {
        return -1;
    }
    attr = _bQueueFind(id);
    if (attr == NULL)
    {
        return -1;
    }
    attr->number  = 0;
    attr->r_index = 0;
    attr->w_index = 0;
    return 0;
}

int bQueueDelete(bQueueId_t id)
{
    bQueueAttr_t *attr = NULL;
    if (id == NULL)
    {
        return -1;
    }
    attr = _bQueueFind(id);
    if (attr == NULL)
    {
        return -1;
    }
    __list_del(attr->list.prev, attr->list.next);
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
