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
#include <errno.h>
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

// listen socket 注册表: 0 = 空槽
// 单网卡单进程, 8 个槽够用
#define B_TESTMAC_LISTEN_MAX 8
static int s_listen_socks[B_TESTMAC_LISTEN_MAX];

// _bSockSetNonblocking 前向声明 (定义在 _bTestMacLoop 之后, loop 内部要用)
static int _bSockSetNonblocking(int sockfd);

/**
 * \}
 */

/**
 * \defgroup TESTMAC_Private_FunctionPrototypes
 * \{
 */

static int _bTestMacInit(bTcpIpNetif_t *netif)
{
    memset(s_listen_socks, 0, sizeof(s_listen_socks));
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
    (void)netif;
    // 遍历 listen socket 数组, accept 新连接并派发 B_TCPIP_E_ACCEPT
    for (int i = 0; i < B_TESTMAC_LISTEN_MAX; i++)
    {
        int listen_sock = s_listen_socks[i];
        if (listen_sock == 0)
        {
            continue;
        }
        // 非阻塞 listen, 没连接就 EAGAIN
        int client_sock = accept(listen_sock, NULL, NULL);
        if (client_sock < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;  // 没新连接, 正常
            }
            b_log_e("testmac accept() error: %s\n", strerror(errno));
            continue;
        }
        // 跟 _bTestMacConnect 行为一致, accept 出来的 fd 设非阻塞
        if (_bSockSetNonblocking(client_sock) < 0)
        {
            close(client_sock);
            continue;
        }
        // 派发新连接到 b_mod_tcpip, 复用 B_TCPIP_E_ACCEPT + bTcpIpAccetpArg_t
        // 路径: b_mod_tcpip.c:2824-2849 会建 bTrans_t 并触发 B_TRANS_ACCEPTED
        // pcb 在 testmac 路径下是 (void*)(intptr_t)sockfd (跟 _bTestMacConnect 一致)
        bTcpIpAccetpArg_t accept_arg;
        memset(&accept_arg, 0, sizeof(accept_arg));
        accept_arg.netif.private = NULL;  // testmac netif.private 保持 NULL
        accept_arg.new_pcb       = (void *)(intptr_t)client_sock;
        B_SAFE_INVOKE(bTestMacEventCb, B_TCPIP_E_ACCEPT, &accept_arg, bTestMacEventCbArg);
    }
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
    int            sock = (int)(intptr_t)sockfd;
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
    int            sock = (int)(intptr_t)sockfd;
    fd_set         write_fds;
    struct timeval timeout;

    FD_ZERO(&write_fds);
    FD_SET(sock, &write_fds);

    // 设置超时时间
    timeout.tv_sec  = 0;
    timeout.tv_usec = 0;

    // 使用select检查可写性
    int ret = select(sock + 1, NULL, &write_fds, NULL, &timeout);
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
    int                sock = (int)(intptr_t)sockfd;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);
    int ret = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
    {
        b_log_e("testmac bind() port %d failed: %s\n", port, strerror(errno));
        return -1;
    }
    return 0;
}

static void *_bTestMacListen(void *sockfd, uint16_t backlog)
{
    int listen_sock = (int)(intptr_t)sockfd;

    int yes = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    b_log("[TESTMAC] listen fd=%d\r\n", listen_sock);

    // 设为非阻塞, _bTestMacLoop 里的 accept 不会卡住
    if (_bSockSetNonblocking(listen_sock) < 0)
    {
        b_log_e("testmac listen setnonblock error: %s\n", strerror(errno));
        return NULL;
    }

    if (listen(listen_sock, backlog) < 0)
    {
        b_log_e("testmac listen() error: %s\n", strerror(errno));
        return NULL;
    }

    for (int i = 0; i < B_TESTMAC_LISTEN_MAX; i++)
    {
        if (s_listen_socks[i] == 0)
        {
            s_listen_socks[i] = listen_sock;
            return (void *)(intptr_t)listen_sock;
        }
    }
    b_log_e("testmac listen table full\n");
    return NULL;
}

static int _bTestMacConnect(void *sockfd, uint32_t ip, uint16_t port)
{
    int sock = (int)(intptr_t)sockfd;
    b_log("[%d] connect %x %d\r\n", sock, ip, port);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(port);
    serverAddr.sin_addr.s_addr = htonl(ip);
    int ret                    = connect(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (ret == -1)
    {
        b_log_e("connect error:%d\r\n", ret);
        B_SAFE_INVOKE(bTestMacEventCb, B_TCPIP_E_DISCONNECT, (void *)(intptr_t)sock,
                      bTestMacEventCbArg);
        return -1;
    }
    B_SAFE_INVOKE(bTestMacEventCb, B_TCPIP_E_CONNECTED, (void *)(intptr_t)sock, bTestMacEventCbArg);
    if (_bSockSetNonblocking((int)(intptr_t)sockfd) < 0)
    {
        close(sock);
        B_SAFE_INVOKE(bTestMacEventCb, B_TCPIP_E_DISCONNECT, (void *)(intptr_t)sock,
                      bTestMacEventCbArg);
        return -1;
    }
    return 0;
}

static int _bTestMacRecv(void *sockfd, uint8_t *pbuf, uint16_t len)
{
    if (sockfd == NULL || pbuf == NULL || len == 0)
    {
        return 0;
    }
    int sock   = (int)(intptr_t)sockfd;
    int retval = recv(sock, pbuf, len, 0);
    if (retval > 0)
    {
        ;
    }
    else if (retval == 0)
    {
        retval = -1;
    }
    else
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            retval = 0;
        }
        else
        {
            retval = -1;
        }
    }

    return retval;
}

static int _bTestMacSend(void *sockfd, uint8_t *pbuf, uint16_t len)
{
    bTcpIpSendDoneArg_t param;
    if (sockfd == NULL || pbuf == NULL || len == 0)
    {
        return 0;
    }
    int sock   = (int)(intptr_t)sockfd;
    int retval = send(sock, pbuf, len, 0);
    b_log("socket send:%d %d\r\n", len, retval);
    b_log_hex(pbuf, retval > 0 ? retval : 0);
    if (retval > 0)
    {
        param.len = len;
        param.pcb = (void *)(intptr_t)sock;
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
    int sock = (int)(intptr_t)sockfd;
    // 如果是 listen socket, 释放数组槽, 防止下次 restart 失败
    for (int i = 0; i < B_TESTMAC_LISTEN_MAX; i++)
    {
        if (s_listen_socks[i] == sock)
        {
            s_listen_socks[i] = 0;
        }
    }
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
        case bCMD_GET_DRIVER_NETIF:
        {
            if (param == NULL)
            {
                return -1;
            }
            ((bDriverNetif_t *)param)->private = NULL;
        }
        break;
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
