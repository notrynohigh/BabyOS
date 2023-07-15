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

B_TASK_CREATE_ATTR(task1_attr);
B_TASK_CREATE_ATTR(task2_attr);
B_SEM_CREATE_ATTR(sem_attr);

bSemId_t gsemId = NULL;

PT_THREAD(test_task1)(struct pt *pt, void *arg)
{
    PT_BEGIN(pt);
    while (1)
    {
        bTaskDelayMs(pt, 5000);
        bSemRelease(gsemId);
    }
    PT_END(pt);
}

PT_THREAD(test_task2)(struct pt *pt, void *arg)
{
    PT_BEGIN(pt);
    while (1)
    {
        bSemAcquireBlock(pt, gsemId, 1000);
        if(PT_WAIT_IS_TIMEOUT(pt))
        {
            b_log("timeout\r\n");
        }
        else
        {
            b_log("hello [%d]\r\n", bSemGetCount(gsemId));
        }
        
    }
    PT_END(pt);
}


int main()
{
    port_init();
    bInit();

    gsemId = bSemCreate(10, 0, &sem_attr);

    bTaskCreate("task1", test_task1, NULL, &task1_attr);
    bTaskCreate("task2", test_task2, NULL, &task2_attr);

    while (1)
    {
        bExec();
    }
    return 0;
}
