/**
 * \file kv_main.c
 * \brief
 * \version 0.1
 * \date 2022-10-29
 * \author notrynohigh (notrynohigh@outlook.com)
 *
 * Copyright (c) 2020 by notrynohigh. All Rights Reserved.
 */
#include <time.h>

#include "../port.h"
#include "b_os.h"

void bMallocFailedHook()
{
    b_log_e("=========================\r\n");
}

void bMqttCallback(bMqttEvent_t evt, bMqttEvtParam_t *param, void *user_data)
{
    switch (evt)
    {
        case B_MQTT_EVT_CONN:
            b_log_i("bMqttEvent_Connected\r\n");
            break;
        case B_MQTT_EVT_DISCONN:
            b_log_i("bMqttEvent_Disconnected\r\n");
            break;
        case B_MQTT_EVT_PUB:
            b_log("topic:%.*s\r\n", param->pub.topic_len, param->pub.topic);
            b_log("payload:%.*s\r\n", param->pub.payload_len, param->pub.payload);
            break;
        default:
            break;
    }
}

int main()
{
    port_init();
    bInit();
    bMqttSrvStartWithCfg(bMqttCallback, NULL);
    while (1)
    {
        bExec();
    }
    return 0;
}
