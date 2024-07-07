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

const bFSPartition_t bPartitionTable[] = {{0, bTESTFLASH, 0, 2 * 1024 * 1024, 4096},
                                          {1, bTESTFLASH, 2 * 1024 * 1024, 2 * 1024 * 1024, 4096}};

static bFS_t *pFS  = NULL;
static bFS_t *pFS2 = NULL;

void bTestFs()
{
    static uint8_t   f = 0;
    static bFSFile_t file;
    uint8_t          tmp[128];
    if (f == 0)
    {
        bFSOpen(pFS, &file, "hello", BFS_O_RDWR | BFS_O_CREAT | BFS_O_APPEND);
        bFSWrite(pFS, &file, "hello", strlen("hello"), NULL);
        bFSClose(pFS, &file);
        f = 1;
    }
    else
    {
        memset(tmp, 0, sizeof(tmp));
        bFSOpen(pFS, &file, "hello", BFS_O_RDWR | BFS_O_CREAT);
        bFSLseek(pFS, &file, 0 - strlen("hello"), BFS_SEEK_END);
        bFSRead(pFS, &file, tmp, 128, NULL);
        bFSClose(pFS, &file);
        b_log("r %s\r\n", tmp);

        uint32_t tsize = 0, fsize = 0;

        bFSGetInfo(pFS, &tsize, &fsize);
        b_log("disk-total:%d(B) free:%d(B)\r\n", tsize, fsize);
        bFSFileGetInfo(pFS, &file, &fsize);
        b_log("file-size:%d(B)\r\n", fsize);
        f = 0;
    }
}

int main()
{
    port_init();
    bInit();

    bFSInit(&bPartitionTable[0], 2);

    bFSMount(&pFS, 0, 1);   // 挂载1
    bFSMount(&pFS2, 1, 1);  // 挂载2

    b_log("mount ok...\r\n");

    while (1)
    {
        bExec();
        BOS_PERIODIC_TASK(bTestFs, 1000);
    }
    return 0;
}
