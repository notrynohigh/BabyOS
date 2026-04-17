/**
 *!
 * \file        b_srv_config_web.h
 * \version     v0.0.1
 * \date        2026/05/31
 * \author      aiclaw
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2026
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
#ifndef __B_SRV_CONFIG_WEB_H__
#define __B_SRV_CONFIG_WEB_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_CONFIG_WEB_SERVICE_ENABLE) && (_CONFIG_WEB_SERVICE_ENABLE == 1))

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup SERVICES
 * \{
 */

/**
 * \addtogroup CONFIG_WEB
 * \{
 */

/**
 * \defgroup CONFIG_WEB_Exported_TypesDefinitions
 * \{
 */

/**
 * @brief 配置类型
 */
typedef enum
{
    B_CONFIG_TYPE_WIFI = 0,
    B_CONFIG_TYPE_ETH,
} bConfigType_t;

/**
 * @brief 配置结果回调
 * @param type 配置类型
 * @param result 配置结果，0成功，负值失败
 * @param user_data 用户数据
 */
typedef void (*pbConfigResultCb_t)(bConfigType_t type, int result, void *user_data);

/**
 * \}
 */

/**
 * \defgroup CONFIG_WEB_Exported_Defines
 * \{
 */

#define CONFIG_WEB_PORT 80

/**
 * \}
 */

/**
 * \defgroup CONFIG_WEB_Exported_Functions
 * \{
 */

/**
 * @brief 启动配网Web服务
 * @param port 监听端口
 * @param cb 配置结果回调（type区分WiFi/以太网）
 * @param user_data 用户数据
 * @return 0成功，负值失败
 */
int bConfigWebServiceStart(uint16_t port, pbConfigResultCb_t cb, void *user_data);

/**
 * @brief 停止配网Web服务
 */
void bConfigWebDeinit(void);

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

/************************ Copyright (c) 2026 aiclaw *****END OF FILE****/
