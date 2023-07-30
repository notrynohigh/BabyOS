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
#include "b_config.h"
#include "thirdparty/pt/pt.h"
#if (defined(_TASK_ENABLE) && (_TASK_ENABLE == 1))
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

/**
 * \}
 */

/**
 * \defgroup TASK_Exported_Definitions
 * \{
 */

#define B_TASK_CREATE_ATTR(attr_name) static bTaskAttr_t attr_name

#define B_TASK_DELAY_XMS(pt, ms) PT_DELAY_MS(pt, ms)

/**
 * \}
 */

/**
 * \defgroup TASK_Exported_Functions
 * \{
 */

/// 创建任务
/// \param[in]     attr          通过B_TASK_CREATE_ATTR创建，再传入
#define bTaskCreate(attr, _func)   \
    do                             \
    {                              \
        (attr)->enable = 1;        \
        (attr)->func   = _func;    \
        PT_INIT(&(attr)->task_pt); \
    } while (0)

/// 暂停任务
#define bTaskSuspend(attr)  \
    do                      \
    {                       \
        (attr)->enable = 0; \
    } while (0)

/// 恢复任务
#define bTaskResume(attr)   \
    do                      \
    {                       \
        (attr)->enable = 1; \
    } while (0)

/// task中延时函数, 这里只是为了保证接口的名字与其他接口一致。
#define bTaskDelayMs(pt, ms) B_TASK_DELAY_XMS(pt, ms)

#define bTaskRun(attr)                      \
    do                                      \
    {                                       \
        if ((attr)->enable)                 \
        {                                   \
            (attr)->func(&(attr)->task_pt); \
        }                                   \
    } while (0)

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

#endif

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2019 Bean *****END OF FILE****/
