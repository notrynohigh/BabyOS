/**
 *!
 * \file        b_task.h
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
#ifndef __B_TASK_H__
#define __B_TASK_H__

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
 * \addtogroup TASK
 * \{
 */

/**
 * \defgroup TASK_Exported_TypesDefinitions
 * \{
 */

typedef void *bTaskId_t;

#define B_TASK_INIT_BEGIN() \
    if (pt->init == 0)      \
    {                       \
        pt->init = 1;

#define B_TASK_INIT_END() }

/**
PT_THREAD(test_task)(struct pt *pt, void *arg)
{
    B_TASK_INIT_BEGIN();
    // ...
    B_TASK_INIT_END();

    PT_BEGIN(pt);
    while (1)
    {
        //.....
    }
    PT_END(pt);
}
*/
typedef char (*bTaskFunc_t)(struct pt *pt, void *arg);

typedef struct
{
    const char      *name;
    bTaskFunc_t      func;
    void            *arg;
    uint8_t          enable;
    struct pt        task_pt;
    struct list_head list;
} bTaskAttr_t;

/**
 * \}
 */

/**
 * \defgroup TASK_Exported_Definitions
 * \{
 */

#define B_TASK_CREATE_ATTR(attr_name) \
    static bTaskAttr_t attr_name = {.name = NULL, .func = NULL, .arg = NULL}

#define B_TASK_DELAY_XMS(pt, ms) PT_DELAY_MS(pt, ms)

/**
 * \}
 */

/**
 * \defgroup TASK_Exported_Functions
 * \{
 */

/// 创建任务
/// \param[in]     name          任务名
/// \param[in]     func          任务执行函数 \ref bTaskFunc_t
/// \param[in]     argument      自定义参数
/// \param[in]     attr          通过B_TASK_CREATE_ATTR创建，再传入
/// \return 任务ID
bTaskId_t bTaskCreate(const char *name, bTaskFunc_t func, void *argument, bTaskAttr_t *attr);

/// 销毁任务
void bTaskRemove(bTaskId_t id);

/// 暂停任务
void bTaskSuspend(bTaskId_t id);

/// 恢复任务
void bTaskResume(bTaskId_t id);

/// 获取id对应的任务名。id为NULL表示获取当前任务
const char *bTaskGetName(bTaskId_t id);

/// 获取当前运行任务的id
bTaskId_t bTaskGetId(void);

/// task中延时函数, 这里只是为了保证接口的名字与其他接口一致。
#define bTaskDelayMs(pt, ms) B_TASK_DELAY_XMS(pt, ms)

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
