/**
 *!
 * \file        b_core.h
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
#ifndef __B_CORE_H__
#define __B_CORE_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include "b_type.h"
#include "thirdparty/pt/pt.h"
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
 * \defgroup CORE_Exported_TypesDefinitions
 * \{
 */
/**
PT_THREAD(test_task)(struct pt *pt)
{
    PT_BEGIN(pt);
    while (1)
    {
        //.....
    }
    PT_END(pt);
}
*/
typedef char (*bTaskFunc_t)(struct pt *pt);

typedef struct
{
    bTaskFunc_t func;
    uint8_t     enable;
    struct pt   task_pt;
} bTaskAttr_t;

#if (defined(_QUEUE_ENABLE) && (_QUEUE_ENABLE == 1))
typedef struct
{
    uint8_t r_index : 4;
    uint8_t w_index : 4;
    uint8_t number;
    uint8_t msg_count;
    uint8_t msg_size;
    void   *mq_mem;
    uint8_t mq_size;
} bQueueAttr_t;

#endif

#if (defined(_SEM_ENABLE) && (_SEM_ENABLE == 1))
typedef struct
{
    uint8_t value;
    uint8_t value_max;
} bSemAttr_t;

#endif

/**
 * \}
 */

/**
 * \defgroup CORE_Exported_Defines
 * \{
 */
#define B_TASK_CREATE_ATTR(attr_name) static bTaskAttr_t attr_name
#define B_TASK_DELAY_XTICK(pt, xtick) PT_DELAY_XTICK(pt, xtick)

#if (defined(_QUEUE_ENABLE) && (_QUEUE_ENABLE == 1))

// msg_count 不能超过8
#define B_QUEUE_CREATE(q_name, pbuf, buf_size, _msg_count, _msg_size) \
    static bQueueAttr_t q_name = {0, 0, 0, _msg_count, _msg_size, pbuf, buf_size}
#endif

#if (defined(_SEM_ENABLE) && (_SEM_ENABLE == 1))
#define B_SEM_CREATE(name, max_value, init_value) static bSemAttr_t name = {init_value, max_value}
#endif

/**
 * \}
 */

/**
 * \defgroup CORE_Exported_Macros
 * \{
 */

#define bTaskCreate(attr, _func)   \
    do                             \
    {                              \
        (attr)->enable = 1;        \
        (attr)->func   = _func;    \
        PT_INIT(&(attr)->task_pt); \
    } while (0)

#define bTaskSuspend(attr)  \
    do                      \
    {                       \
        (attr)->enable = 0; \
    } while (0)

#define bTaskResume(attr)   \
    do                      \
    {                       \
        (attr)->enable = 1; \
    } while (0)

#define bTaskDelay(pt, xtick) B_TASK_DELAY_XTICK(pt, xtick)

#define bTaskRun(attr)                      \
    do                                      \
    {                                       \
        if ((attr)->enable)                 \
        {                                   \
            (attr)->func(&(attr)->task_pt); \
        }                                   \
    } while (0)

#if (defined(_QUEUE_ENABLE) && (_QUEUE_ENABLE == 1))

#define B_QUEUE_PUT(pt, queue, msg_ptr, timeout) \
    PT_WAIT_UNTIL((pt), bQueuePutNonblock((queue), (msg_ptr)) == 0, (timeout))

#define B_QUEUE_GET(pt, queue, msg_ptr, timeout) \
    PT_WAIT_UNTIL((pt), bQueueGetNonblock((queue), (msg_ptr)) == 0, (timeout))

#endif

#if (defined(_SEM_ENABLE) && (_SEM_ENABLE == 1))

#define B_SEM_ACQUIRE(pt, sem, timeout) \
    PT_WAIT_UNTIL((pt), bSemAcquireNonblock((sem)) == 0, (timeout))

#endif
/**
 * \}
 */

/**
 * \defgroup CORE_Exported_Functions
 * \{
 */

int bInit(void);
int bExec(void);

#if (defined(_QUEUE_ENABLE) && (_QUEUE_ENABLE == 1))

int8_t bQueuePutNonblock(void *queue, const void *msg_ptr);
int8_t bQueueGetNonblock(void *queue, void *msg_ptr);

#define bQueuePutBlock(pt, queue, msg_ptr, timeout) B_QUEUE_PUT(pt, queue, msg_ptr, timeout)
#define bQueueGetBlock(pt, queue, msg_ptr, timeout) B_QUEUE_GET(pt, queue, msg_ptr, timeout)

#endif

#if (defined(_SEM_ENABLE) && (_SEM_ENABLE == 1))

int8_t  bSemAcquireNonblock(bSemAttr_t *sem);
int8_t  bSemRelease(bSemAttr_t *sem);
uint8_t bSemGetCount(bSemAttr_t *sem);
#define bSemAcquireBlock(pt, sem, timeout) B_SEM_ACQUIRE(pt, sem, timeout)

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

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
