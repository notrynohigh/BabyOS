/**
 *!
 * \file        b_timer.h
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
#ifndef __B_TIMER_H__
#define __B_TIMER_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>
#include <string.h>

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
 * \addtogroup TIMER
 * \{
 */

/**
 * \defgroup TIMER_Exported_TypesDefinitions
 * \{
 */

typedef void *bTimerId_t;

typedef void (*bTimerFunc_t)(void *arg);

/// Timer type.
typedef enum
{
    B_TIMER_ONCE     = 0,  ///< One-shot timer.
    B_TIMER_PERIODIC = 1   ///< Repeating timer.
} bTimerType_t;

typedef struct
{
    bTimerFunc_t     func;
    void            *arg;
    uint8_t          enable;
    uint32_t         tick;
    uint32_t         cycle;
    bTimerType_t     type;
    struct list_head list;
} bTimerAttr_t;

/**
 * \}
 */

/**
 * \defgroup TIMER_Exported_Definitions
 * \{
 */

#define B_TIMER_CREATE_ATTR(attr_name) static bTimerAttr_t attr_name = {.func = NULL, .arg = NULL}

/**
 * \}
 */

/**
 * \defgroup TIMER_Exported_Functions
 * \{
 */

/// 创建定时器
/// \param[in]     func          定时器回调函数 \ref bTimerAttr_t
/// \param[in]     type          定时器类型 \ref bTimerType_t
/// \param[in]     argument      自定义参数
/// \param[in]     attr          通过B_TIMER_CREATE_ATTR创建，再传入
/// \return 定时器ID
bTimerId_t bTimerCreate(bTimerFunc_t func, bTimerType_t type, void *argument, bTimerAttr_t *attr);

/// 启动或重新启动定时器
/// \param[in]     id      定时器id \ref bTimerId_t
/// \param[in]     ms      定时周期 ms
int bTimerStart(bTimerId_t id, uint32_t ms);

/// 停止定时器
/// \param[in]     id      定时器id \ref bTimerId_t
int bTimerStop(bTimerId_t id);

/// 查询定时器是否在运行
/// \param[in]     id      定时器id \ref bTimerId_t
/// \return 0 not running, 1 running.
uint32_t bTimerIsRunning(bTimerId_t id);

/// 删除一个定时器
/// \param[in]     id      定时器id \ref bTimerId_t
int bTimerDelete(bTimerId_t id);

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
