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
    bHttpRequest(httpfd, B_HTTP_GET,
                 "http://restapi.amap.com/v3/weather/"
                 "weatherInfo?city=440300&key=",
                 NULL, NULL);
}

void HttpCb(bHttpEvent_t event, void *param, void *arg)
{
    if (event == B_HTTP_EVENT_RECV_DATA)
    {
        bHttpRecvData_t *dat = (bHttpRecvData_t *)param;
        if (dat->pdat != NULL && dat->len > 0)
        {
            char *pstr = strstr(dat->pdat, "\r\n\r\n");
            if (pstr != NULL)
            {
                cJSON *root = cJSON_Parse(pstr);
                if (root)
                {
                    cJSON *lives = cJSON_GetObjectItem(root, "lives");
                    if (lives != NULL && lives->type == cJSON_Array)
                    {
                        lives         = cJSON_GetArrayItem(lives, 0);
                        cJSON *wether = cJSON_GetObjectItem(lives, "weather");
                        cJSON *temp   = cJSON_GetObjectItem(lives, "temperature");
                        cJSON *hum    = cJSON_GetObjectItem(lives, "humidity");
                        char  *we     = "Cloudy";
                        if (strstr(wether->valuestring, "晴") != NULL)
                        {
                            we = "Sunny";
                        }
                        if (strstr(wether->valuestring, "雨") != NULL)
                        {
                            we = "Rainy";
                        }
                        if (strstr(wether->valuestring, "雪") != NULL)
                        {
                            we = "Snowy";
                        }
                        b_log("w:%s\r\n", we);
                        b_log("t:%s\r\n", temp->valuestring);
                        b_log("h:%s\r\n", hum->valuestring);
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

int main()
{
    port_init();
    bInit();

    bTcpipSrvInit();
    bSntpStart(60 * 60);
    httpfd = bHttpInit(HttpCb, NULL);
    while (1)
    {
        bExec();
        BOS_PERIODIC_TASK(ntp_test, 1000 * 60);
    }
    return 0;
}
