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

uint8_t buf[1024];

B_QUEUE_CREATE_ATTR(q_attr, buf, 1024);

bQueueId_t gqueueId = NULL;

PT_THREAD(test_task1)(struct pt *pt, void *arg)
{
    static uint32_t tmp = 0;
    PT_BEGIN(pt);
    while (1)
    {
        bQueuePutBlock(pt, gqueueId, &tmp, 1000);
        if(PT_WAIT_IS_TIMEOUT(pt))
        {
            b_log("put timeout\r\n");
        }
        tmp += 1;
    }
    PT_END(pt);
}

PT_THREAD(test_task2)(struct pt *pt, void *arg)
{
    uint32_t tmp = 0;
    PT_BEGIN(pt);
    while (1)
    {
        bTaskDelayMs(pt, 1000);
        bQueueGetBlock(pt, gqueueId, &tmp, 0xffffffff);
        if(PT_WAIT_IS_TIMEOUT(pt))
        {
            b_log("get timeout\r\n");
        }
        else
        {
            b_log("hello [%d][%d]\r\n",bQueueGetCount(gqueueId), tmp);
        }
        
    }
    PT_END(pt);
}


int main()
{
    port_init();
    bInit();

    gqueueId = bQueueCreate(10, sizeof(uint32_t), &q_attr);
    b_log("q:%p\r\n", gqueueId);

    bTaskCreate("task1", test_task1, NULL, &task1_attr);
    bTaskCreate("task2", test_task2, NULL, &task2_attr);

    while (1)
    {
        bExec();
    }
    return 0;
}
