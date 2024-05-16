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
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/md.h"

void _md_test()
{
    mbedtls_md_context_t ctx;
    mbedtls_md_info_t   *pinfo;
    uint8_t             *pdigist = NULL;
    mbedtls_md_init(&ctx);
    pinfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (pinfo != NULL)
    {
        int olen = mbedtls_md_get_size(pinfo);
        if (olen > 0)
        {
            pdigist = (uint8_t *)bMalloc(olen);
        }
        mbedtls_md_setup(&ctx, pinfo, 0);
        mbedtls_md_starts(&ctx);
        mbedtls_md_update(&ctx, "hello world", strlen("hello world"));
        mbedtls_md_finish(&ctx, pdigist);
        b_log_hex(pdigist, olen);
        bFree(pdigist);
    }
    mbedtls_md_free(&ctx);
}

int main()
{
    int len = 0;
    port_init();
    bInit();
    _md_test();
    while (1)
    {
        bExec();
    }
    return 0;
}
