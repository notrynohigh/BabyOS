/**
 * \file port.c
 * \brief
 * \version 0.1
 * \date 2022-10-29
 * \author notrynohigh (notrynohigh@outlook.com)
 *
 * Copyright (c) 2020 by notrynohigh. All Rights Reserved.
 */

#define _POSIX_C_SOURCE 199309L
#include <time.h>  // 包含 struct timespec 的定义
//--------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "b_os.h"
#include "port.h"

int bMcuUartSend(bHalUartNumber_t uart, const uint8_t *pbuf, uint16_t len)
{
    printf("%s", pbuf);
    return len;
}

void port_init()
{
    ;
}

uint64_t _bGetClock()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

uint32_t bHalGetSysTick()
{
    return ((uint32_t)_bGetClock());
}

uint64_t bHalGetSysTickPlus()
{
    return _bGetClock();
}
