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

bTimerId_t tid1;
bTimerId_t tid2;

B_TIMER_CREATE_ATTR(timer1_attr);
B_TIMER_CREATE_ATTR(timer2_attr);

void Timer1Handler(void *arg)
{
    static uint8_t count = 0;
    count += 1;
    b_log("babyos[%s]\r\n", arg);
    if (count > 10)
    {
        count = 0;
        bTimerStart(tid2, 200);
    }
}

void Timer2Handler(void *arg)
{
    b_log("babyos[%s]\r\n", arg);
}

int main()
{
    port_init();
    bInit();
    tid1 = bTimerCreate(Timer1Handler, B_TIMER_PERIODIC, "timer1", &timer1_attr);
    tid2 = bTimerCreate(Timer2Handler, B_TIMER_ONCE, "timer2", &timer2_attr);
    bTimerStart(tid1, 100);
    bTimerStart(tid2, 200);
    while (1)
    {
        bExec();
    }
    return 0;
}
