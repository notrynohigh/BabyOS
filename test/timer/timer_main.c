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

B_TIMER_CREATE_ATTR(timer1_attr);
B_TIMER_CREATE_ATTR(timer2_attr);

void Timer1Handler(void *arg)
{
    b_log("babyos[%s]\r\n", arg);
}

void Timer2Handler(void *arg)
{
    b_log("babyos[%s]\r\n", arg);
}

int main()
{
    port_init();
    bInit();
    bTimerId_t tid1 = bTimerCreate(Timer1Handler, B_TIMER_PERIODIC, "timer1", &timer1_attr);
    bTimerId_t tid2 = bTimerCreate(Timer2Handler, B_TIMER_ONCE, "timer2", &timer2_attr);
    bTimerStart(tid1, 1000);
    bTimerStart(tid2, 2000);
    while (1)
    {
        bExec();
    }
    return 0;
}
