/**
 *!
 * \file        b_mod_netif.h
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
#ifndef __B_MOD_NETIF_H__
#define __B_MOD_NETIF_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))
#include "thirdparty/lwip/bos_lwip/include/lwip/dhcp.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/dns.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/init.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/ip_addr.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/mem.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/memp.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/netif.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/opt.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/tcp.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/timeouts.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/udp.h"
#include "thirdparty/lwip/bos_lwip/include/netif/etharp.h"
#include "utils/inc/b_util_fifo.h"
#include "utils/inc/b_util_list.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup NETIF
 * \{
 */

/**
 * \defgroup NETIF_Exported_TypesDefinitions
 * \{
 */

typedef struct
{
    struct netif     lwip_netif;
    uint32_t         mac_dev;
    uint32_t         dhcp_en;
    int              fd;
    struct list_head list;
} bNetif_t;

typedef enum
{
    B_EVENT_DNS_FAIL,
    B_EVENT_DNS_OK,
    B_EVENT_WAIT_DNS,
    B_EVENT_CONNECTING_FAIL,
    B_EVENT_WAIT_CONNECTED,
    B_EVENT_CONNECTED,
    B_EVENT_DISCONNECT,
    B_EVENT_NEW_DATA,
    B_EVENT_ERROR,
    B_EVENT_INVALID,
} bNetifConnEvent_t;

typedef void (*pbNetifConnCb_t)(bNetifConnEvent_t event, void *param, void *arg);

typedef enum
{
    B_TRANS_UDP,
    B_TRANS_TCP,
} bTransType_t;
#define IS_VALID_TRANS_TYPE(t) ((t) == B_TRANS_UDP || (t) == B_TRANS_TCP)

typedef enum
{
    B_CONN_INIT,
    B_CONN_DNS,
    B_CONN_CONNECTING,
    B_CONN_WAIT_CONNECTED,
    B_CONN_CONNECTED,
    B_CONN_DEINIT,
} bConnState_t;

typedef enum
{
    B_SRV_LISTEN,
    B_SRV_DEINIT,
} bServerState_t;
typedef struct
{
    void           *pcb;
    int             err_code;
    int             reserved;
    uint8_t         readable;
    uint8_t         writeable;
    uint8_t         mem_index;
    uint8_t         subtype;
    uint16_t        remote_port;
    uint32_t        remote_ip;
    bFIFO_Info_t    recv_buf;
    bConnState_t    state;
    bTransType_t    type;
    pbNetifConnCb_t callback;
    void           *cb_arg;
} bNetifConn_t;

typedef struct
{
    bNetifConn_t     conn;
    char             remote_addr[REMOTE_ADDR_LEN_MAX + 1];
    struct list_head list;
} bNetifClient_t;

typedef struct
{
    bServerState_t   state;
    pbNetifConnCb_t  callback;
    bTransType_t     type;
    void            *user_data;
    void            *server_pcb;
    uint16_t         server_port;
    bNetifConn_t     conn[SERVER_MAX_CONNECTIONS];
    struct list_head list;
} bNetifServer_t;

/**
 * \}
 */

/**
 * \defgroup NETIF_Exported_Defines
 * \{
 */

#define B_NETIF_CLIENT_FLAG (0x1)
#define B_NETIF_SERVER_FLAG (0x2)

#define bNETIF_INSTANCE(name, dev_no) \
    bNetif_t name = {                 \
        .mac_dev = dev_no,            \
    }

// trans_type: \ref bTransType_t  B_TRANS_UDP/B_TRANS_TCP
#define B_NETIF_CLIENT_CREATE_INSTANCE(name, trans_type, cb, arg) \
    bNetifClient_t name = {                                       \
        .conn.pcb      = NULL,                                    \
        .conn.type     = trans_type,                              \
        .conn.subtype  = B_NETIF_CLIENT_FLAG,                     \
        .conn.state    = B_CONN_DEINIT,                           \
        .conn.callback = cb,                                      \
        .conn.cb_arg   = arg,                                     \
    }

#define B_NETIF_SERVER_CREATE_INSTANCE(name, trans_type, cb, arg) \
    bNetifServer_t name = {                                       \
        .callback  = cb,                                          \
        .user_data = arg,                                         \
        .type      = trans_type,                                  \
        .state     = B_SRV_DEINIT,                                \
    }

#define bNETIF_IP_U32(a, b, c, d)                                      \
    (((uint32_t)((a) & 0xff) << 24) | ((uint32_t)((b) & 0xff) << 16) | \
     ((uint32_t)((c) & 0xff) << 8) | (uint32_t)((d) & 0xff))
/**
 * \}
 */

/**
 * \defgroup NETIF_Exported_Functions
 * \{
 */

// ip eg. 192.168.1.4  bNETIF_IP_U32(192,168,1,4)
int bNetifAdd(bNetif_t *pInstance, uint32_t ip, uint32_t gw, uint32_t mask);

// tcp/udp client api
bNetifConn_t *bNetifConnect(bNetifClient_t *client, char *remote, uint16_t port);
// tcp/udp server api
int bNetifSrvListen(bNetifServer_t *server, uint16_t port);

uint8_t bNetifConnIsReadable(bNetifConn_t *pconn);
uint8_t bNetifConnIsWriteable(bNetifConn_t *pconn);
int     bNetifConnReadData(bNetifConn_t *pconn, uint8_t *pbuf, uint16_t buf_len, uint16_t *rlen);
int     bNetifConnWriteData(bNetifConn_t *pconn, uint8_t *pbuf, uint16_t buf_len, uint16_t *wlen);
int     bNetifConnDeinit(bNetifConn_t *pconn);

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

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
