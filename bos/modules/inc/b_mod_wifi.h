/**
 *!
 * \file        b_mod_wifi.h
 * \version     v0.0.1
 * \date        2020/05/16
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2020 Bean
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
#ifndef __B_MOD_WIFI_H__
#define __B_MOD_WIFI_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_WIFI_ENABLE) && (_WIFI_ENABLE == 1))

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup WIFI
 * \{
 */

/**
 * \defgroup WIFI_Exported_TypesDefinitions
 * \{
 */
#ifndef WIFI_SSID_LEN_MAX
#define WIFI_SSID_LEN_MAX (32)
#endif

#ifndef WIFI_PASSWD_LEN_MAX
#define WIFI_PASSWD_LEN_MAX (64)
#endif

typedef struct
{
    char ssid[WIFI_SSID_LEN_MAX + 1];
    char passwd[WIFI_PASSWD_LEN_MAX + 1];
} bWifiApInfo_t;

typedef struct
{
    uint8_t *pbuf;
    uint16_t len;
} bWifiNewData_t;

typedef enum
{
    B_WIFI_EVT_APCFGNET,  // arg：bWifiApInfo_t
    B_WIFI_EVT_CONN_AP,   // arg: uint8_t 1:成功 0:失败
    B_WIFI_EVT_CONN_TCP,
    B_WIFI_EVT_CONN_UDP,
    B_WIFI_EVT_DISCONN,
    B_WIFI_EVT_PING,
    B_WIFI_EVT_SEND,
    B_WIFI_EVT_RECV,  // bWifiNewData_t
} bWifiEvent_t;

typedef void (*pWifiEvtCb_t)(bWifiEvent_t evt, void *arg, void (*release)(void *), void *user_data);

/**
 * \}
 */

/**
 * \defgroup WIFI_Exported_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup WIFI_Exported_Functions
 * \{
 */
int bWifiInit(uint32_t dev_no, pWifiEvtCb_t cb, void *user_data);
int bWifiDeinit(void);
/*
 * \brief      AP配置网络
 端口号 666
 配网数据：
 {
    "ssid": "xxxxx",
    "passwd": "xxxxx"
 }
*/
int bWifiApConfigNet(const char *ssid, const char *passwd);
int bWifiJoinAp(const char *ssid, const char *passwd);

int bWifiConnTcp(const char *remote, uint16_t port);
int bWifiConnUdp(const char *remote, uint16_t port);
int bWifiDisconn(const char *remote, uint16_t port);
int bWifiPing(const char *remote);
int bWifiSend(const char *remote, uint16_t port, uint8_t *pbuf, uint16_t len);

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

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
