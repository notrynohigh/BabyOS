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

B_TASK_CREATE_ATTR(test_attr);

PT_THREAD(test_task)(struct pt *pt, void *arg)
{
    PT_BEGIN(pt);
    while (1)
    {
        b_log("hello world [%s]\r\n", bTaskGetName(NULL));
        PT_DELAY_MS(pt, 1000);
    }
    PT_END(pt);
}


int main()
{
    port_init();
    bInit();
    bTaskCreate("test task", test_task, NULL, &test_attr);
    while (1)
    {
        bExec();
    }
    return 0;
}

