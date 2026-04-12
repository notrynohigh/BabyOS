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

static bTaskAttr_t sTaskAttrTcp;

const static bNetCardInfo_t bNetCardInfo[] = {
    [0] =
        {
            .dev_no    = bTESTMAC,
            .priority  = 0,
            .ignore_ip = 1,
        },
};

static void _MainMonitor()
{
    uint32_t        free_memory = 0;
    bUTC_DateTime_t tm;
    bUTC2Struct(&tm, bUTC_GetTime(), 8.0);
    free_memory = bGetFreeSize();
    b_log("[%d:%d:%d]i am alive ..%d Bytes \r\n", tm.hour, tm.minute, tm.second, free_memory);
}

void bTcpCallback(bTransEvent_t event, void *param, void *arg)
{
    b_log("trans event:%d param:%p arg:%p \r\n", event, param, arg);
}

PT_THREAD(bWifiTestTask666)(struct pt *pt, void *arg)
{
    static int sockfd = -1;
    B_TASK_INIT_BEGIN();
    // ...
    B_TASK_INIT_END();

    PT_BEGIN(pt);
    while (1)
    {
        sockfd = bSocket(B_TRANS_CONN_TCP, bTcpCallback, NULL);
        if (sockfd >= 0)
        {
            b_log("sockfd:%d \r\n", sockfd);
            b_log("connecting... \r\n");
            bConnect(sockfd, "192.168.3.50", 666);
            PT_WAIT_UNTIL(pt, bSocketIsConnected(sockfd), 2000);
            b_log("connected???%d\r\n", bSocketIsConnected(sockfd));
            if (bSocketIsConnected(sockfd))
            {
                bSend(sockfd, (uint8_t *)"[linux]hello world666\r\n", strlen("[linux]hello world666\r\n"), NULL);
                bTaskDelayMs(pt, 5000);
            }
            bShutdown(sockfd);
            sockfd = -1;
        }
        bTaskDelayMs(pt, 5000);
    }
    PT_END(pt);
}


int main()
{
    port_init();
    bInit();

    bTcpipSrvInit(&bNetCardInfo[0], 1);
    bSntpStart(300);
    bTaskCreate("tcp", bWifiTestTask666, NULL, &sTaskAttrTcp);
    while (1)
    {
        bExec();
        BOS_PERIODIC_TASK(_MainMonitor, 5000);
    }
    return 0;
}
