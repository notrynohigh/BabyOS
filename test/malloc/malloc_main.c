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

#if (!(defined(_UNITY_ENABLE) && (_UNITY_ENABLE == 1)))
void test()
{
    uint32_t m_size = 0;
    uint8_t *pbuf   = NULL;
    srand(bHalGetSysTick());
    m_size = rand() % 256;
    pbuf   = bMalloc(m_size);
    b_log("malloc %d %p\r\n", m_size, pbuf);
    bFree(pbuf);

    b_log("free size:::%d\r\n", bGetFreeSize());

    pbuf = bMalloc(1024);
    memcpy(pbuf, "BABYOS", strlen("BABYOS"));
    b_log("malloc 1024 %p %s\r\n", pbuf, pbuf);
    pbuf = bRealloc(pbuf, 2048);
    b_log("realloc 2048 %p %s\r\n", pbuf, pbuf);
    pbuf = bRealloc(pbuf, 0);
    b_log("free size:::%d\r\n", bGetFreeSize());

    pbuf = bCalloc(1, 1024);
    memcpy(pbuf, "BABYOS", strlen("BABYOS"));
    b_log("malloc 1024 %p %s\r\n", pbuf, pbuf);
    bFree(pbuf);
}

int main()
{
    port_init();
    bInit();

    while (1)
    {
        bExec();
        BOS_PERIODIC_TASK(test, 500);
    }
    return 0;
}
#endif
