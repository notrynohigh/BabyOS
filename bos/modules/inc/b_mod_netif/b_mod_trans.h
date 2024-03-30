/**
 *!
 * \file        b_mod_trans.h
 * \version     v0.0.1
 * \date        2020/01/02
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2020 Bean
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO PARAM SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
#ifndef __B_MOD_TRANS_H__
#define __B_MOD_TRANS_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))
#include "utils/inc/b_util_fifo.h"
#include "utils/inc/b_util_list.h"

typedef enum
{
    B_TRANS_CONN_TCP,
    B_TRANS_CONN_UDP
} bTransType_t;

typedef enum
{
    B_TRANS_WAIT_CONNECTED = 0,
    B_TRANS_CONNECTED,
    B_TRANS_NEW_CONNECT,
    B_TRANS_NEW_DATA,
    B_TRANS_ERR_BASE = -128,
    B_TRANS_CONNECTING_FAIL,
    B_TRANS_DISCONNECT,
    B_TRANS_DNS_FAIL,
    B_TRANS_ERROR,
    B_TRANS_INVALID = 0xff,
} bTransEvent_t;

typedef void (*pbTransCb_t)(bTransEvent_t event, void *param, void *arg);
typedef void (*pbTransDnsCb_t)(const char *name, uint32_t ipaddr, void *arg);
typedef void (*pbTransPingCb_t)(int result, uint32_t ms, void *arg);



int     bSocket(bTransType_t type, pbTransCb_t cb, void *user_data);
int     bConnect(int sockfd, char *remote, uint16_t port);
int     bBind(int sockfd, uint16_t port);
int     bListen(int sockfd, int backlog);
int     bRecv(int sockfd, uint8_t *pbuf, uint16_t buf_len, uint16_t *rlen);
int     bSend(int sockfd, uint8_t *pbuf, uint16_t buf_len, uint16_t *wlen);
int     bShutdown(int sockfd);
uint8_t bSockIsReadable(int sockfd);
uint8_t bSockIsWriteable(int sockfd);
int     bDnsParse(char *remote, pbTransDnsCb_t cb, void *user_data);
int     bPing(char *remote, uint32_t timeout_ms, pbTransPingCb_t cb, void *user_data);

#endif

#ifdef __cplusplus
}
#endif

#endif
