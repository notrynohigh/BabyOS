/**
 *!
 * \file        b_sem.h
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
#ifndef __B_SEM_H__
#define __B_SEM_H__

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
 * \addtogroup SEM
 * \{
 */

/**
 * \defgroup SEM_Exported_TypesDefinitions
 * \{
 */

typedef void *bSemId_t;

typedef struct
{
    uint32_t         value;
    uint32_t         value_max;
    struct list_head list;
} bSemAttr_t;

/**
 * \}
 */

/**
 * \defgroup SEM_Exported_Definitions
 * \{
 */

#define B_SEM_CREATE_ATTR(attr_name) static bSemAttr_t attr_name = {.value = 0}

#define B_SEM_ACQUIRE(pt, sem_id, timeout) \
    PT_WAIT_UNTIL((pt), bSemAcquireNonblock((sem_id)) == 0, (timeout))

/**
 * \}
 */

/**
 * \defgroup SEM_Exported_Functions
 * \{
 */

/// 创建信号量
/// \param[in]     max_count     信号量最大数量.
/// \param[in]     initial_count 信号量初始数量
/// \param[in]     attr          通过B_SEM_CREATE_ATTR创建，再传入
/// \return 信号量ID
bSemId_t bSemCreate(uint32_t max_count, uint32_t initial_count, bSemAttr_t *attr);
int      bSemAcquireNonblock(bSemId_t id);
int      bSemRelease(bSemId_t id);
uint32_t bSemGetCount(bSemId_t id);

#define bSemAcquireBlock(pt, id, timeout) B_SEM_ACQUIRE(pt, id, timeout)

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
