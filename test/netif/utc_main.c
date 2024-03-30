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

void ntp_test()
{
    bUTC_DateTime_t tm;
    bUTC_t          now_utc = bUTC_GetTime();

    bUTC2Struct(&tm, now_utc, 8);
    b_log("%d-%02d-%02d %02d:%02d:%02d %02d\r\n", tm.year, tm.month, tm.day, tm.hour, tm.minute,
          tm.second, tm.week);
}

int main()
{
    port_init();
    bInit();

    bTcpipSrvInit();
    bSntpStart(600);
    while (1)
    {
        bExec();
        BOS_PERIODIC_TASK(ntp_test, 10000);
    }
    return 0;
}
