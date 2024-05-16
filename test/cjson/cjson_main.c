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

int main()
{
    port_init();
    bInit();

    cJSON *root = cJSON_CreateObject();
    if (root)
    {
        cJSON_AddStringToObject(root, "os", "babyos");
        char *root_str = cJSON_PrintUnformatted(root);
        if (root_str)
        {
            b_log("%s\r\n", root_str);
        }
    }

    while (1)
    {
        bExec();
    }
    return 0;
}
