/**
 *!
 * \file        b_queue.h
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
#ifndef __B_QUEUE_H__
#define __B_QUEUE_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>
#include <string.h>

#include "thirdparty/pt/pt.h"
#include "utils/inc/b_util_list.h"

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
 * \defgroup QUEUE_Exported_TypesDefinitions
 * \{
 */

typedef void *bQueueId_t;

typedef struct
{
    uint32_t         number;
    uint32_t         r_index;
    uint32_t         w_index;
    uint32_t         msg_count;
    uint32_t         msg_size;
    void            *mq_mem;
    uint32_t         mq_size;
    struct list_head list;
} bQueueAttr_t;

/**
 * \}
 */

/**
 * \defgroup QUEUE_Exported_Definitions
 * \{
 */

#define B_QUEUE_CREATE_ATTR(attr_name, pbuf, buf_size) \
    static bQueueAttr_t attr_name = {.mq_mem = pbuf, .mq_size = buf_size}

#define B_QUEUE_PUT(pt, queue_id, msg_ptr, timeout) \
    PT_WAIT_UNTIL((pt), bQueuePutNonblock((queue_id), (msg_ptr)) == 0, (timeout))

#define B_QUEUE_GET(pt, queue_id, msg_ptr, timeout) \
    PT_WAIT_UNTIL((pt), bQueueGetNonblock((queue_id), (msg_ptr)) == 0, (timeout))

/**
 * \}
 */

/**
 * \defgroup QUEUE_Exported_Functions
 * \{
 */

/// 创建消息队列
/// \param[in]     msg_count     队列里消息的最大数量
/// \param[in]     msg_size      队列消息的大小，单位字节
/// \param[in]     attr          通过B_QUEUE_CREATE_ATTR创建，再传入
/// \return 消息队列ID
bQueueId_t bQueueCreate(uint32_t msg_count, uint32_t msg_size, bQueueAttr_t *attr);

int bQueuePutNonblock(bQueueId_t id, const void *msg_ptr);
int bQueueGetNonblock(bQueueId_t id, void *msg_ptr);

uint32_t bQueueGetCapacity(bQueueId_t id);
uint32_t bQueueGetMsgSize(bQueueId_t id);
uint32_t bQueueGetCount(bQueueId_t id);
uint32_t bQueueGetSpace(bQueueId_t id);

int bQueueReset(bQueueId_t id);
int bQueueDelete(bQueueId_t id);

#define bQueuePutBlock(pt, id, msg_ptr, timeout) B_QUEUE_PUT(pt, id, msg_ptr, timeout)
#define bQueueGetBlock(pt, id, msg_ptr, timeout) B_QUEUE_GET(pt, id, msg_ptr, timeout)

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
