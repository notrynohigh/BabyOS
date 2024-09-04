/**
 *!
 * \file        b_srv_mqtt.h
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
#ifndef __B_SRV_MQTT_H__
#define __B_SRV_MQTT_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_MQTT_SERVICE_ENABLE) && (_MQTT_SERVICE_ENABLE == 1))

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
 * \addtogroup MQTT
 * \{
 */

/**
 * \defgroup MQTT_Exported_TypesDefinitions
 * \{
 */

typedef struct
{
    uint8_t  dup;
    uint8_t  retained;
    uint16_t pack_id;
    int      qos;
    char    *topic;
    uint16_t topic_len;
    char    *payload;
    uint32_t payload_len;
} bMqttPubParam_t;

typedef union
{
    bMqttPubParam_t pub;
} bMqttEvtParam_t;

typedef enum
{
    B_MQTT_EVT_CONN,
    B_MQTT_EVT_DISCONN,
    B_MQTT_EVT_PUB,
    B_MQTT_EVT_INVALID,
} bMqttEvent_t;

typedef struct
{
    char    *host;
    char    *username;
    char    *passwd;
    uint16_t keep_alive;
} bMqttConnParam_t;

typedef void (*pbMqttCallback_t)(bMqttEvent_t evt, bMqttEvtParam_t *param, void *user_data);

/**
 * \}
 */

/**
 * \defgroup MQTT_Exported_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup MQTT_Exported_Functions
 * \{
 */

int bMqttSrvStartWithCfg(pbMqttCallback_t cb, void *arg);

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
