/**
 *!
 * \file        b_mod_tcpip.h
 * \version     v0.0.1
 * \date        2019/06/05
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2019 Bean
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
#ifndef __B_MOD_TCPIP_H__
#define __B_MOD_TCPIP_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"
#include "utils/inc/b_util_list.h"

#if (defined(_TCPIP_ENABLE) && (_TCPIP_ENABLE == 1))
/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup TCPIP
 * \{
 */

/**
 * \defgroup TCPIP_Exported_TypesDefinitions
 * \{
 */

/*
   ip,mask,gateway 有为0的值，表示使用DHCP;
   ip格式："192.168.0.1" >> 0xc0a80001 或者使用bIPStr2Uint32辅助函数转换
   priority 表示优先级，数值越小优先级越高
   当 ignore_ip 为 1时，assiged_ip内容被忽略，不会执行DHCP或者相关IP操作
*/
typedef struct
{
    uint32_t dev_no;
    uint8_t  priority;
    uint8_t  ignore_ip;
    struct
    {
        uint32_t ip;
        uint32_t mask;
        uint32_t gateway;
    } assigned_ip;
} bNetCardInfo_t;

typedef enum
{
    B_TRANS_CONN_TCP,
    B_TRANS_CONN_UDP
} bTransType_t;

typedef enum
{
    B_TRANS_DNS_SUCCESS = 0,
    B_TRANS_CONNECTED,
    B_TRANS_ACCEPTED,
    B_TRANS_NEW_DATA,
    B_TRANS_SEND_DONE,
    B_TRANS_ERR_BASE = -128,
    B_TRANS_DISCONNECT,  // 收到此事件后，不要再对socket做任何操作
    B_TRANS_ERROR,
    B_TRANS_INVALID = 0xff,
} bTransEvent_t;

typedef void (*pbTransCb_t)(bTransEvent_t event, void *param, void *arg);
typedef void (*pbTransPingCb_t)(int result, uint32_t ms, void *arg);

typedef enum
{
    DNS_PARSE_ONGING  = 1,
    DNS_PARSE_SUCCESS = 0,
    DNS_PARSE_FAILED  = -1,
} bDnsResult_t;

/**
 * \brief Portable socket descriptor type for TCP/IP operations.
 * On 64-bit platforms, this must be large enough to hold a pointer.
 */
typedef intptr_t bSocketFd_t;

/**
 * \}
 */

/**
 * \defgroup TCPIP_Exported_Defines
 * \{
 */

#define SOCKFD_IS_INVALID(sockfd) ((sockfd) <= 0)
#define SOCKET_SHUTDOWN(pt, sockfd) \
    do { \
        if (!SOCKFD_IS_INVALID((sockfd))) { \
            PT_WAIT_UNTIL_FOREVER((pt), bShutdown((sockfd)) >= 0); \
        } \
    } while (0)

/**
 * \}
 */

/**
 * \defgroup TCPIP_Exported_Functions
 * \{
 */

int      bTcpIpInit(const bNetCardInfo_t *pnetcard, uint8_t number);
int      bTcpIpSetIp(const char *ip_addr, const char *netmask, const char *gateway);
int      bTcpIpGetIp(char *ipaddr, char *netmask, char *gateway);
int      bTcpIpSetMac(const uint8_t mac[6]);
int      bTcpIpGetMac(uint8_t mac[6]);
uint8_t  bTcpIpPhyIsLinked(void);
uint32_t bTcpIpGetCurrentDevNo(void);

bSocketFd_t bSocket(bTransType_t type, pbTransCb_t cb, void *user_data);
bSocketFd_t bSocket2(uint32_t dev_no, bTransType_t type, pbTransCb_t cb, void *user_data);
int         bConnect(bSocketFd_t sockfd, char *remote, uint16_t port);
int         bBind(bSocketFd_t sockfd, uint16_t port);
int         bListen(bSocketFd_t sockfd, int backlog);
int         bRecv(bSocketFd_t sockfd, uint8_t *pbuf, uint16_t buf_len, uint16_t *rlen);
int         bSend(bSocketFd_t sockfd, uint8_t *pbuf, uint16_t buf_len, uint16_t *wlen);
int         bShutdown(bSocketFd_t sockfd);
uint8_t     bSockIsReadable(bSocketFd_t sockfd);
uint8_t     bSockIsWriteable(bSocketFd_t sockfd);
uint8_t     bSocketIsConnected(bSocketFd_t sockfd);

uint32_t bIPStr2Uint32(const char *ip);

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

/**
 * \}
 */

#endif

#ifdef __cplusplus
}
#endif

#endif
/************************ Copyright (c) 2019 Bean *****END OF FILE****/
