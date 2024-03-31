/**
 *!
 * \file        b_srv_tcpip.h
 * \version     v0.0.1
 * \date        2023/08/27
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 Bean
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
#ifndef __B_SRV_TCPIP_H__
#define __B_SRV_TCPIP_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_TCPIP_SERVICE_ENABLE) && (_TCPIP_SERVICE_ENABLE == 1))

#include "modules/inc/b_mod_netif/b_mod_link.h"
#include "modules/inc/b_mod_netif/b_mod_trans.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup SERVICES
 * \{
 */

/**
 * \addtogroup TCPIP
 * \{
 */

/**
 * \defgroup TCPIP_Exported_TypesDefinitions
 * \{
 */

typedef enum
{
    B_HTTP_GET,
    B_HTTP_POST
} bHttpReqType_t;
#define HTTPREQ_TYPE_IS_VALID(t) ((t) == B_HTTP_GET || (t) == B_HTTP_POST)

typedef struct
{
    uint8_t *pdat;
    uint16_t len;
    void (*release)(void *);
} bHttpRecvData_t;

typedef enum
{
    B_HTTP_EVENT_CONNECTED = 0,
    B_HTTP_EVENT_RECV_DATA,  // callback param : bHttpRecvData_t
    B_HTTP_EVENT_DESTROY,
    B_HTTP_EVENT_ERR_BASE = -100,
    B_HTTP_EVENT_ERROR,
} bHttpEvent_t;

typedef void (*pHttpCb_t)(bHttpEvent_t event, void *param, void *arg);

/**
 * \}
 */

/**
 * \defgroup TCPIP_Exported_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TCPIP_Exported_Functions
 * \{
 */

int bTcpipSrvInit(void);

int bSntpStart(uint32_t interval_s);

int bHttpInit(pHttpCb_t cb, void *user_data);
int bHttpDeInit(int httpfd);
// 默认头部有Content-Length; head为自定义头部，以\r\n结尾
int bHttpRequest(int httpfd, bHttpReqType_t type, const char *url, const char *head,
                 const char *body);
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

/************************ Copyright (c) 2023 Bean *****END OF FILE****/
