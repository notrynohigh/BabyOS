/**
 *!
 * \file        b_core.c
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
#include "core/inc/b_core.h"

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
 * \addtogroup CORE
 * \{
 */

/**
 * \defgroup CORE_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup CORE_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup CORE_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup CORE_Private_Variables
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup CORE_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup CORE_Private_Functions
 * \{
 */

/**
 * \}
 */

/**
 * \addtogroup CORE_Exported_Functions
 * \{
 */

/**
 * \brief Init
 * \retval Result
 *          \arg 0  OK
 *          \arg -1 ERR
 */
int bInit()
{
    bHalInit();
    return 0;
}

/**
 * \brief  Call this function inside the while(1)
 * \retval Result
 *          \arg 0  OK
 *          \arg -1 ERR
 */
int bExec()
{
    return 0;
}

#if (defined(_QUEUE_ENABLE) && (_QUEUE_ENABLE == 1))

int8_t bQueuePutNonblock(void *queue, const void *msg_ptr)
{
    bQueueAttr_t *attr = (bQueueAttr_t *)queue;
    uint8_t       i    = 0;
    uint8_t      *pbuf = NULL;
    if (queue == NULL || msg_ptr == NULL)
    {
        return -1;
    }
    if (attr->number >= attr->msg_count)
    {
        return -2;
    }
    pbuf = (uint8_t *)attr->mq_mem;
    B_MEM_COPY(&pbuf[attr->w_index * attr->msg_size], msg_ptr, attr->msg_size);
    if (attr->w_index == 0xf)
    {
        attr->w_index = 0;
    }
    else
    {
        attr->w_index += 1;
    }
    attr->number += 1;
    return 0;
}

int8_t bQueueGetNonblock(void *queue, void *msg_ptr)
{
    bQueueAttr_t *attr = (bQueueAttr_t *)queue;
    uint8_t       i    = 0;
    uint8_t      *pbuf = NULL;
    if (queue == NULL || msg_ptr == NULL)
    {
        return -1;
    }
    if (attr->number == 0)
    {
        return -3;
    }
    pbuf = (uint8_t *)attr->mq_mem;
    B_MEM_COPY(msg_ptr, &pbuf[attr->r_index * attr->msg_size], attr->msg_size);
    if (attr->r_index == 0xf)
    {
        attr->r_index = 0;
    }
    else
    {
        attr->r_index += 1;
    }
    attr->number -= 1;
    return 0;
}

#endif

#if (defined(_SEM_ENABLE) && (_SEM_ENABLE == 1))

int8_t bSemAcquireNonblock(bSemAttr_t *sem)
{
    if (sem == NULL || sem->value == 0)
    {
        return -1;
    }
    sem->value -= 1;
    return 0;
}

int8_t bSemRelease(bSemAttr_t *sem)
{
    if (sem == NULL)
    {
        return -1;
    }
    if (sem->value < sem->value_max)
    {
        sem->value += 1;
    }
    return 0;
}

uint8_t bSemGetCount(bSemAttr_t *sem)
{
    if (sem == NULL)
    {
        return 0;
    }
    return sem->value;
}

#endif

/**
 * \}
 */

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
