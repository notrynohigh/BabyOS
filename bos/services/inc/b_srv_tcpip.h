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

#include "modules/inc/b_mod_netif.h"

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

typedef enum
{
    B_HTTP_STA_INIT,
    B_HTTP_STA_CONNECTED,
    B_HTTP_STA_DISCONNECT,
    B_HTTP_STA_DEINIT
} bHttpState_t;

typedef enum
{
    B_HTTP_EVENT_CONNECTED,
    B_HTTP_EVENT_DISCONNECT,
    B_HTTP_RECV_DATA
} bHttpEvent_t;

typedef void (*pHttpCb_t)(bHttpEvent_t event, uint8_t *dat, uint32_t len, void *arg);

typedef struct
{
    uint8_t        is_https;
    bNetifClient_t client;
    bHttpState_t   state;
    char           host[_HTTP_HOST_LEN_MAX + 1];
    char           path[_HTTP_PATH_LEN_MAX + 1];
    char          *head;
    char          *body;
    uint16_t       port;
    pHttpCb_t      callback;
    void          *user_data;
} bHttpStruct_t;

/**
 * \}
 */

/**
 * \defgroup TCPIP_Exported_Defines
 * \{
 */

#define B_HTTP_CLIENT_CREATE_INSTANCE(name, cb, arg) \
    bHttpStruct_t name = {                           \
        .is_https  = 0,                              \
        .state     = B_HTTP_STA_DEINIT,              \
        .callback  = cb,                             \
        .user_data = arg,                            \
    }

/**
 * \}
 */

/**
 * \defgroup TCPIP_Exported_Functions
 * \{
 */

int bSntpStart(uint32_t interval_s);

int bHttpRequest(bHttpStruct_t *http, bHttpReqType_t type, char *url, char *head, char *body);

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
