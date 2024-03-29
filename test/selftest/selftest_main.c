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
#include "b_config.h"
#include "b_os.h"

int main()
{

    int a = 1;

    port_init();

    b_log("hello world\r\n");
    b_log_i("hello world\r\n");
    b_log_w("hello world\r\n");
    b_log_e("hello world\r\n");

    b_assert_log(a == 1);

    bInit();
    while (1)
    {
        bExec();
    }
    return 0;
}
