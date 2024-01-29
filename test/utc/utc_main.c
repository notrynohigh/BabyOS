/**
 * \file kv_main.c
 * \brief
 * \version 0.1
 * \date 2022-10-29
 * \author notrynohigh (notrynohigh@outlook.com)
 *
 * Copyright (c) 2020 by notrynohigh. All Rights Reserved.
 */
#include "../port.h"
#include "b_os.h"
#include <time.h>

static void time_print()
{
    bUTC_DateTime_t tm;
    bUTC_t utc = bUTC_GetTime();
    b_log("utc:%ld\r\n", utc);
    bUTC2Struct(&tm, utc, 8);
    b_log("%d-%d-%d %d:%d:%d [%d]\r\n", tm.year, tm.month, tm.day, tm.hour, tm.minute, tm.second, tm.week);
    bUTC2Struct(&tm, utc, 5.5);
    b_log("%d-%d-%d %d:%d:%d [%d]\r\n", tm.year, tm.month, tm.day, tm.hour, tm.minute, tm.second, tm.week);
    b_log("utc:%ld\r\n", bStruct2UTC(tm, 5.5));
}

int main()
{
    port_init();
    bInit();

    time_t ctm = time(NULL);
    b_log("ctm:%d\r\n", ctm);

    bUTC_SetTime(ctm);

    time_print();

    while (1)
    {
        bExec();
        BOS_PERIODIC_TASK(time_print, 3000);
    }
    return 0;
}
