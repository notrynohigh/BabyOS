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

bQRCODE_INSTANCE(qrInstance, QRCODE_21X21, QRCODE_ECC_HIGH);

int main()
{
    port_init();
    bInit();
    
    bQRCodeCreate(&qrInstance, "BabyOS", strlen("BabyOS"));
    
    int i = 0, j = 0;
    for(i = 0;i < qrInstance.qr.size;i++)
    {
    	for(j = 0;j < qrInstance.qr.size;j++)
    	{
    		printf("%s", bQRCodeGetValue(&qrInstance, j, i) == 0 ? "  " : "**");
    	}
    	printf("\n");
    }

    while (1)
    {
        bExec();
        
    }
    return 0;
}
