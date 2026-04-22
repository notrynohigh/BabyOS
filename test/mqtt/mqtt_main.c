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
#include <string.h>

#include "../port.h"
#include "b_os.h"

const static bNetCardInfo_t bNetCardInfo[] = {
    [0] =
        {
            .dev_no    = bTESTMAC,
            .priority  = 0,
            .ignore_ip = 1,
        },
};

void bMallocFailedHook()
{
    b_log_e("=========================\r\n");
}

// Counter for publishing test messages
static int g_publish_count = 0;

void bMqttCallback(bMqttEvent_t evt, bMqttEvtParam_t *param, void *user_data)
{
    switch (evt)
    {
        case B_MQTT_EVT_CONN:
            b_log_i("bMqttEvent_Connected\r\n");
            // Try to publish a test message when connected
            {
                char msg[128];
                snprintf(msg, sizeof(msg), "Hello from BabyOS! Count: %d", g_publish_count++);
                int ret = bMqttSrvPublish("test_topic", (uint8_t *)msg, strlen(msg), 0);
                b_log_i("Publish test message: %s, ret=%d\r\n", msg, ret);
            }
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
    bTcpipSrvInit(&bNetCardInfo[0], 1);
    
    // Initialize and start MQTT service
    int ret = bMqttSrvStartWithCfg(bMqttCallback, NULL);
    b_log_i("bMqttSrvStartWithCfg returned: %d\r\n", ret);
    
    // Try to subscribe to an additional topic
    ret = bMqttSrvSubscribe("custom_topic", 0);
    b_log_i("bMqttSrvSubscribe(custom_topic) returned: %d\r\n", ret);
    
    // Try to publish immediately (should fail since not connected yet)
    ret = bMqttSrvPublish("test_topic", (uint8_t *)"early message", 13, 0);
    b_log_i("bMqttSrvPublish (early) returned: %d\r\n", ret);
    
    // Check MQTT status
    int status = bMqttSrvGetStatus();
    b_log_i("bMqttSrvGetStatus returned: %d\r\n", status);
    
    // Show available API functions
    b_log_i("\r\n=== MQTT API Functions ===\r\n");
    b_log_i("bMqttSrvStartWithCfg(cb, arg) - Start MQTT service with callback\r\n");
    b_log_i("bMqttSrvPublish(topic, payload, len, qos) - Publish message\r\n");
    b_log_i("bMqttSrvSubscribe(topic, qos) - Subscribe to topic\r\n");
    b_log_i("bMqttSrvUnsubscribe(topic) - Unsubscribe from topic\r\n");
    b_log_i("bMqttSrvGetStatus() - Get connection status\r\n");
    b_log_i("bMqttSrvDestroy() - Destroy MQTT service\r\n");
    b_log_i("===========================\r\n\r\n");
    
    while (1)
    {
        bExec();
    }
    return 0;
}
