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
#include "thirdparty/lwip/bos_lwip/include/lwip/inet_chksum.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/init.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/ip_addr.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/mem.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/memp.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/netif.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/opt.h"
#include "thirdparty/lwip/bos_lwip/include/lwip/raw.h"
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
    B_EVENT_DNS_OK = 0,
    B_EVENT_WAIT_DNS,
    B_EVENT_WAIT_CONNECTED,
    B_EVENT_CONNECTED,
    B_EVENT_NEW_DATA,
    B_EVENT_ERR_BASE = -10,
    B_EVENT_DNS_FAIL,
    B_EVENT_CONNECTING_FAIL,
    B_EVENT_DISCONNECT,
    B_EVENT_ERROR,
    B_EVENT_INVALID = 0xff,
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
    void           *pbuf;
    uint8_t         subtype;
    uint16_t        remote_port;
    uint32_t        remote_ip;
    uint32_t        rx_cache_size;
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
    bServerState_t  state;
    pbNetifConnCb_t callback;
    bTransType_t    type;
    void           *user_data;
    void           *server_pcb;
    uint16_t        server_port;
    uint32_t        rx_cache_size;
    bNetifConn_t    conn[SERVER_MAX_CONNECTIONS];
} bNetifServer_t;

typedef void (*pbNetifPingCb_t)(int result, uint32_t ms, void *arg);
typedef struct
{
    void           *pcb;
    void           *user_data;
    void           *timer_id;
    uint32_t        s_tick;
    bConnState_t    state;
    pbNetifPingCb_t callback;
} bNetifPing_t;

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
// if cache_size == 0 , use CONNECT_RECVBUF_MAX
#define B_NETIF_CLIENT_CREATE_INSTANCE(name, trans_type, cb, arg, cache_size) \
    bNetifClient_t name = {                                                   \
        .conn.pcb           = NULL,                                           \
        .conn.type          = trans_type,                                     \
        .conn.subtype       = B_NETIF_CLIENT_FLAG,                            \
        .conn.state         = B_CONN_DEINIT,                                  \
        .conn.callback      = cb,                                             \
        .conn.cb_arg        = arg,                                            \
        .conn.rx_cache_size = cache_size,                                     \
        .conn.pbuf          = NULL,                                           \
    }

#define B_NETIF_SERVER_CREATE_INSTANCE(name, trans_type, cb, arg, cache_size) \
    bNetifServer_t name = {                                                   \
        .callback      = cb,                                                  \
        .user_data     = arg,                                                 \
        .type          = trans_type,                                          \
        .state         = B_SRV_DEINIT,                                        \
        .rx_cache_size = cache_size,                                          \
    }

#define B_NETIF_CLIENT_STRUCT_INIT(pclient, trans_type, cb, arg, cache_size) \
    do                                                                       \
    {                                                                        \
        (pclient)->conn.pcb           = NULL;                                \
        (pclient)->conn.type          = trans_type;                          \
        (pclient)->conn.subtype       = B_NETIF_CLIENT_FLAG;                 \
        (pclient)->conn.state         = B_CONN_DEINIT;                       \
        (pclient)->conn.callback      = cb;                                  \
        (pclient)->conn.cb_arg        = arg;                                 \
        (pclient)->conn.rx_cache_size = cache_size;                          \
        (pclient)->conn.pbuf          = NULL;                                \
    } while (0)

#define B_NETIF_SERVER_STRUCT_INIT(pserver, trans_type, cb, arg, cache_size) \
    do                                                                       \
    {                                                                        \
        (pserver)->callback      = cb;                                       \
        (pserver)->user_data     = arg;                                      \
        (pserver)->type          = trans_type;                               \
        (pserver)->state         = B_SRV_DEINIT;                             \
        (pserver)->rx_cache_size = cache_size;                               \
    } while (0)

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

// ping
int bNetifPing(const char *remote, uint32_t timeout_ms, pbNetifPingCb_t cb, void *arg);

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
