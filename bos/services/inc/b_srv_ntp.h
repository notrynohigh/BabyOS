/**
 *!
 * \file        b_srv_ntp.h
 * \version     v0.0.1
 * \date        2026/05/31
 * \author      BabyOS Team
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 BabyOS
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
#ifndef __B_SRV_NTP_H__
#define __B_SRV_NTP_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_NTP_SERVICE_ENABLE) && (_NTP_SERVICE_ENABLE == 1))

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup SERVICES
 * \{
 */

/**
 * \addtogroup NTP
 * \{
 */

/**
 * \defgroup NTP_Exported_Functions
 * \{
 */

/**
 * @brief 启动NTP服务（自动同步网络时间）
 * @param interval_s 同步间隔（秒），默认3600
 * @return 0成功，负值失败
 */
int bSntpStart(uint32_t interval_s);

/**
 * @brief 停止NTP服务 (H-NEW-4 fix).
 *
 * 清除 running 标志并等待 task 自报停. 之后所有 bSend/bRecv/bConnect
 * 都不再被调度. 多次调用安全, 重复调用返 0.
 *
 * @return 0成功, 负值失败
 */
int bSntpStop(void);

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

/************************ Copyright (c) 2026 BabyOS Team *****END OF FILE****/
