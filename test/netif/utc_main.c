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

int  httpfd = -1;
void ntp_test()
{
    bUTC_DateTime_t tm;
    bUTC_t          now_utc = bUTC_GetTime();

    bUTC2Struct(&tm, now_utc, 8);
    b_log("%d-%02d-%02d %02d:%02d:%02d %02d\r\n", tm.year, tm.month, tm.day, tm.hour, tm.minute,
          tm.second, tm.week);
    b_log_w(":::::%d\r\n", bGetFreeSize());
    if (now_utc > 1711855508)
    {
        bHttpRequest(httpfd, B_HTTP_GET,
                     "http://restapi.amap.com/v3/weather/"
                     "weatherInfo?city=440300&key=2ecb62606e0682a50cae3ade6b30c3b1",
                     NULL, NULL);
    }
}

void HttpCb(bHttpEvent_t event, void *param, void *arg)
{
    if (event == B_HTTP_EVENT_RECV_DATA)
    {
        bHttpRecvData_t *dat = (bHttpRecvData_t *)param;
        if (dat->pdat != NULL && dat->len > 0)
        {
            b_log("%s", dat->pdat);
            char *pstr = strstr(dat->pdat, "\r\n\r\n");
            if (pstr != NULL)
            {
                cJSON *root = cJSON_Parse(pstr);
                if (root)
                {
                    cJSON *lives = cJSON_GetObjectItem(root, "lives");
                    if (lives != NULL && lives->type == cJSON_Array)
                    {
                        lives              = cJSON_GetArrayItem(lives, 0);
                        cJSON *weather     = cJSON_GetObjectItem(lives, "weather");
                        cJSON *temperature = cJSON_GetObjectItem(lives, "temperature");
                        cJSON *humidity    = cJSON_GetObjectItem(lives, "humidity");
                        b_log("\r\n\r\nweather: %s\r\n", weather->valuestring);
                        b_log("\r\ntemperature:%s℃\r\n", temperature->valuestring);
                        b_log("\r\nhumidity:%s%%\r\n", humidity->valuestring);
                    }
                    cJSON_Delete(root);
                }
            }
        }
        if (dat)
        {
            if (dat->release)
            {
                dat->release(dat->pdat);
            }
        }
    }
}

void bMallocFailedHook()
{
    b_log_e("=========================\r\n");
}

/*
typedef struct
{
    uint32_t dev_no;
    uint8_t  priority;
    uint8_t  ignore_ip;
    struct
    {
        uint32_t ip;
        uint32_t mask;
        uint32_t gateway;
    } assigned_ip;
} bNetCardInfo_t;
 */

const static bNetCardInfo_t bNetCardInfo[] = {
    [0] =
        {
            .dev_no    = bTESTMAC,
            .priority  = 0,
            .ignore_ip = 1,
        },
};

int main()
{
    port_init();
    bInit();

    bTcpipSrvInit(&bNetCardInfo[0], 1);
    bSntpStart(60 * 5);
    httpfd = bHttpInit(HttpCb, NULL);
    while (1)
    {
        bExec();
        BOS_PERIODIC_TASK(ntp_test, 1000 * 60);
    }
    return 0;
}
