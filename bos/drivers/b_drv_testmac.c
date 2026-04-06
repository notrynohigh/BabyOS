/**
 *!
 * \file        b_drv_testmac.c
 * \version     v0.0.1
 * \date        2022/10/29
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2022 Bean
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "drivers/inc/b_drv_testmac.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils/inc/b_util_log.h"

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup TESTMAC
 * \{
 */

/**
 * \defgroup TESTMAC_Private_Defines
 * \{
 */

#define DRIVER_NAME TESTMAC

/**
 * \}
 */

/**
 * \defgroup TESTMAC_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TESTMAC_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TESTMAC_Private_Variables
 * \{
 */
bDRIVER_HALIF_TABLE(bTESTMAC_HalIf_t, DRIVER_NAME);

static pTcpIpCallback_t bTestMacEventCb    = NULL;
static void            *bTestMacEventCbArg = NULL;

/**
 * \}
 */

/**
 * \defgroup TESTMAC_Private_FunctionPrototypes
 * \{
 */

static int _bTestMacInit(bTcpIpNetif_t *netif)
{
    return 0;
}

static int _bTestMacSetMac(uint8_t mac[6], bTcpIpNetif_t *netif)
{
    return 0;
}

static int _bTestMacSetIp(uint32_t ip, uint32_t mask, uint32_t gateway, bTcpIpNetif_t *netif)
{
    return 0;
}

static int _bTestMacSetLinkState(uint8_t state, bTcpIpNetif_t *netif)
{
    return 0;
}

static void _bTestMacLoop(bTcpIpNetif_t *netif)
{
    ;
}

// 设置socket为非阻塞模式
static int _bSockSetNonblocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1)
    {
        perror("fcntl F_GETFL failed");
        return -1;
    }
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("fcntl F_SETFL failed");
        return -1;
    }
    return 0;
}

// 检查socket是否可读
static uint8_t _bTestMacIsReadable(void *sockfd)
{
    int            sock = (int)sockfd;
    fd_set         read_fds;
    struct timeval timeout;

    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    // 设置超时时间
    timeout.tv_sec  = 0;
    timeout.tv_usec = 0;

    // 使用select检查可读性
    int ret = select(sock + 1, &read_fds, NULL, NULL, &timeout);
    if (ret == -1)
    {
        return 0;
    }
    else if (ret == 0)
    {
        return 0;  // 超时，不可读
    }
    return 1;  // 可读
}

// 检查socket是否可写
static uint8_t _bTestMacIsWriteable(void *sockfd)
{
    int            sock = (int)sockfd;
    fd_set         write_fds;
    struct timeval timeout;

    FD_ZERO(&write_fds);
    FD_SET(sock, &write_fds);

    // 设置超时时间
    timeout.tv_sec  = 0;
    timeout.tv_usec = 0;

    // 使用select检查可写性
    b_log("check wr select\r\n");
    int ret = select(sock + 1, NULL, &write_fds, NULL, &timeout);
    b_log("wr select:%d\r\n", ret);
    if (ret == -1)
    {
        return 0;
    }
    else if (ret == 0)
    {
        return 0;  // 超时，不可写
    }
    return 1;  // 可写
}

static void *_bTestMacTcpNew(bTcpIpNetif_t *pnetif)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        b_log_e("socket create failed\n");
        return NULL;
    }
    return (void *)(intptr_t)sockfd;
}

static void *_bTestMacUdpNew(bTcpIpNetif_t *pnetif)
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
    {
        b_log_e("udp socket create failed\n");
        return NULL;
    }
    _bSockSetNonblocking(sockfd);
    return (void *)(intptr_t)sockfd;
}

static int _bTestMacBind(void *sockfd, uint16_t port)
{
    int                sock = (int)sockfd;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    return 0;
}

static void *_bTestMacListen(void *sockfd, uint16_t backlog)
{
    return 0;
}

static int _bTestMacConnect(void *sockfd, uint32_t ip, uint16_t port)
{
    int sock = (int)sockfd;
    b_log("[%d] connect %x %d\r\n", sock, ip, port);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(port);
    serverAddr.sin_addr.s_addr = htonl(ip);
    int ret                    = connect(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (ret == -1)
    {
        b_log_e("connect error:%d\r\n", ret);
        B_SAFE_INVOKE(bTestMacEventCb, B_TCPIP_E_DISCONNECT, sock, bTestMacEventCbArg);
        return -1;
    }
    B_SAFE_INVOKE(bTestMacEventCb, B_TCPIP_E_CONNECTED, sock, bTestMacEventCbArg);
    if (_bSockSetNonblocking(sockfd) < 0)
    {
        close(sockfd);
        B_SAFE_INVOKE(bTestMacEventCb, B_TCPIP_E_DISCONNECT, sock, bTestMacEventCbArg);
        return NULL;
    }
    return 0;
}

static int _bTestMacRecv(void *sockfd, uint8_t *pbuf, uint16_t len)
{
    if (sockfd == NULL || pbuf == NULL || len == 0)
    {
        return 0;
    }
    int sock   = (int)sockfd;
    int retval = recv(sock, pbuf, len, 0);
    return retval;
}

static int _bTestMacSend(void *sockfd, uint8_t *pbuf, uint16_t len)
{
    bTcpIpSendDoneArg_t param;
    if (sockfd == NULL || pbuf == NULL || len == 0)
    {
        return 0;
    }
    int sock   = (int)sockfd;
    int retval = send(sock, pbuf, len, 0);
    b_log("socket send:%d %d\r\n", len, retval);
    b_log_hex(pbuf, len);
    if (retval > 0)
    {
        param.len = len;
        param.pcb = sock;
        B_SAFE_INVOKE(bTestMacEventCb, B_TCPIP_E_SEND_DONE, &param, bTestMacEventCbArg);
    }
    return retval;
}

static int _bTestMacUdpSend(void *netif, void *sockfd, uint8_t *pbuf, uint16_t len)
{
    return _bTestMacSend(sockfd, pbuf, len);
}

static int _bTestMacDelete(void *sockfd)
{
    if (sockfd == NULL)
    {
        return 0;
    }
    int sock = (int)sockfd;
    close(sock);
    return 0;
}

static void _bTestMacCallback(pTcpIpCallback_t cb, void *arg, bTcpIpNetif_t *netif)
{
    bTestMacEventCb    = cb;
    bTestMacEventCbArg = arg;
}

const bTcpIpStackIf_t bTestMacIf = {
    .init         = _bTestMacInit,
    .loop         = _bTestMacLoop,
    .reg_callback = _bTestMacCallback,

    .set_mac           = _bTestMacSetMac,
    .set_ip            = _bTestMacSetIp,
    .set_link_state    = _bTestMacSetLinkState,
    .set_default_netif = NULL,

    .tcp =
        {
            .new     = _bTestMacTcpNew,
            .delete  = _bTestMacDelete,
            .send    = _bTestMacSend,
            .recv    = _bTestMacRecv,
            .bind    = _bTestMacBind,
            .listen  = _bTestMacListen,
            .connect = _bTestMacConnect,
        },
    .udp =
        {
            .new     = _bTestMacUdpNew,
            .delete  = _bTestMacDelete,
            .send    = _bTestMacUdpSend,
            .recv    = _bTestMacRecv,
            .bind    = _bTestMacBind,
            .listen  = _bTestMacListen,
            .connect = _bTestMacConnect,
        },
    .is_readable  = _bTestMacIsReadable,
    .is_writeable = _bTestMacIsWriteable,

};

/**
 * \}
 */

/**
 * \defgroup TESTMAC_Private_Functions
 * \{
 */

static uint8_t sg_private_mac[6] = {0x00, 0x28, 0x00, 0x02, 0x04, 0x08};

static int _bTESTMACCtl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    int            retval = -1;
    bMacAddress_t *pmac   = (bMacAddress_t *)param;
    switch (cmd)
    {
        case bCMD_GET_MAC_ADDRESS:
        {
            if (param == NULL)
            {
                return -1;
            }
            memcpy(pmac->address, sg_private_mac, sizeof(sg_private_mac));
            retval = 0;
        }
        break;
        case bCMD_SET_MAC_ADDRESS:
        {
            if (param == NULL)
            {
                return -1;
            }
            memcpy(sg_private_mac, pmac->address, sizeof(sg_private_mac));
            retval = 0;
        }
        break;
        case bCMD_GET_LINK_STATE:
        {
            if (param == NULL)
            {
                return -1;
            }
            *(uint8_t *)param = 1;
            retval            = 0;
        }
        break;
        case bCMD_REG_BUF_LIST:
        case bCMD_REG_LINK_CALLBACK:
        {
            retval = 0;
        }
        break;
        case bCMD_GET_STACK_IF:
        {
            if (param == NULL)
            {
                return -1;
            }
            memcpy(param, &bTestMacIf, sizeof(bTestMacIf));
            retval = 0;
        }
        break;
    }
    return retval;
}

/**
 * \}
 */

/**
 * \addtogroup TESTMAC_Exported_Functions
 * \{
 */
int bTESTMAC_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bTESTMAC_Init);
    pdrv->ctl = _bTESTMACCtl;
    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_TESTMAC, bTESTMAC_Init);
#ifdef BSECTION_NEED_PRAGMA
#pragma section
#endif
/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

/************************ Copyright (c) 2022 Bean *****END OF FILE****/
