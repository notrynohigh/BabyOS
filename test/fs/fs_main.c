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

void bTestFs()
{
    static uint8_t   f = 0;
    static bFSFile_t file;
    int              fd = -1;
    uint8_t          tmp[128];
    uint32_t         tsize = 0, fsize = 0;
    if (f == 0)
    {
        fd = bFSOpen(&file, "0:hello", BFS_O_RDWR | BFS_O_CREAT | BFS_O_APPEND);
        if (BFS_FD_IS_VALID(fd))
        {
            bFSFileGetInfo(fd, &fsize);
            b_log("file-0-size:%d(B)\r\n", fsize);
            bFSWrite(fd, "baby", strlen("baby"));
            bFSClose(fd);
            fd = -1;
        }

        fd = bFSOpen(&file, "1:hello", BFS_O_RDWR | BFS_O_CREAT | BFS_O_APPEND);
        if (BFS_FD_IS_VALID(fd))
        {
            bFSFileGetInfo(fd, &fsize);
            b_log("file-1-size:%d(B)\r\n", fsize);
            bFSWrite(fd, "os", strlen("os"));
            bFSClose(fd);
            fd = -1;
        }
        f = 1;
    }
    else
    {
        bFSGetInfo(0, &tsize, &fsize);
        b_log("disk[0]-total:%d(KB) free:%d(KB)\r\n", tsize / 1024, fsize / 1024);
        bFSGetInfo(1, &tsize, &fsize);
        b_log("disk[1]-total:%d(KB) free:%d(KB)\r\n", tsize / 1024, fsize / 1024);

        memset(tmp, 0, sizeof(tmp));

        fd = bFSOpen(&file, "0:hello", BFS_O_RD);
        if (BFS_FD_IS_VALID(fd))
        {
            bFSLseek(fd, 0 - strlen("baby"), BFS_SEEK_END);
            bFSRead(fd, tmp, sizeof(tmp));
            bFSClose(fd);
            fd = -1;
        }

        fd = bFSOpen(&file, "1:hello", BFS_O_RD);
        if (BFS_FD_IS_VALID(fd))
        {
            bFSLseek(fd, 0 - strlen("os"), BFS_SEEK_END);
            bFSRead(fd, &tmp[strlen(tmp)], strlen("os"));
            bFSClose(fd);
            fd = -1;
        }

        b_log("%s\r\n", tmp);

        f = 0;
    }
}

int main()
{
    port_init();
    bInit();

    bFSInit(&bPartitionTable[0], 2);

    bFSMount(0, 1);  // mount partition 0
    bFSMount(1, 1);  // mount partition 1

    b_log("mount ok...\r\n");

    while (1)
    {
        bExec();
        BOS_PERIODIC_TASK(bTestFs, 1000);
    }
    return 0;
}
