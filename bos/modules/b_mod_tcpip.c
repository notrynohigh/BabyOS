/**
 *!
 * \file        b_mod_tcpip.c
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

/*Includes ----------------------------------------------*/
#include "modules/inc/b_mod_tcpip.h"

#include <stdio.h>
#include <string.h>

#include "core/inc/b_core.h"
#include "core/inc/b_task.h"
#include "core/inc/b_timer.h"
#include "drivers/inc/b_driver.h"
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"

#if (defined(_TCPIP_ENABLE) && (_TCPIP_ENABLE == 1))

#if (defined(_TCPIP_STACK_LWIP_ENABLE) && (_TCPIP_STACK_LWIP_ENABLE == 1))
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
#endif

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
 * \defgroup TCPIP_Private_Variables
 * \{
 */

//-------------------------------------------------socket------------------------

#ifndef REMOTE_ADDR_LEN_MAX
#define REMOTE_ADDR_LEN_MAX 128
#endif

#define B_SOCKET_STATE_INIT (0)
#define B_SOCKET_STATE_DNS (1)
#define B_SOCKET_STATE_DNS_FAIL (2)
#define B_SOCKET_STATE_CONNECT (3)
#define B_SOCKET_STATE_WAIT_CONNECTED (4)
#define B_SOCKET_STATE_CONNECTED (5)
#define B_SOCKET_STATE_WAIT_DISCONNECT (6)
#define B_SOCKET_STATE_DISCONNECT (7)

const char *bSocketStateStr[] = {"INIT",           "DNS",       "DNS_FAIL",        "CONNECT",
                                 "WAIT_CONNECTED", "CONNECTED", "WAIT_DISCONNECT", "DISCONNECT"};

typedef struct
{
    uint8_t *pbuf;
    uint16_t len;
    void (*release)(void *);
    struct list_head node;
} bTransData_t;

typedef struct
{
    void *pcb;
    void *cb_arg;
#if (defined(_TCPIP_STACK_LWIP_ENABLE) && (_TCPIP_STACK_LWIP_ENABLE == 1))
    struct pbuf *p;
    uint16_t     read_offset;
#endif
    uint8_t      state;
    bTaskAttr_t  task_attr;
    pbTransCb_t  callback;
    uint16_t     local_port;
    uint16_t     remote_port;
    uint32_t     remote_ip;
    uint8_t      remote_url[REMOTE_ADDR_LEN_MAX + 1];
    bTransType_t type;
    void        *stack_if;
    uint8_t      stack_opt;
    void        *netif;
#if (defined(_TCPIP_RECV_BUF_ENABLE) && (_TCPIP_RECV_BUF_ENABLE == 1))
    struct list_head recv_head;
#endif
#if (defined(_TCPIP_SEND_BUF_ENABLE) && (_TCPIP_SEND_BUF_ENABLE == 1))
    struct list_head send_head;
    uint8_t          send_busy;
#endif
    struct list_head node;
} bTrans_t;

//-----------------------------------------dns----------------------------------
#define MAX_DNS_BUF_SIZE 256  ///< maximum size of DNS buffer. */
/* Header for all domain messages */
struct dhdr
{
    uint16_t id; /* Identification */
    uint8_t  qr; /* Query/Response */
#define QUERY 0
#define RESPONSE 1
    uint8_t opcode;
#define IQUERY 1
    uint8_t aa;    /* Authoratative answer */
    uint8_t tc;    /* Truncation */
    uint8_t rd;    /* Recursion desired */
    uint8_t ra;    /* Recursion available */
    uint8_t rcode; /* Response code */
#define NO_ERROR 0
#define FORMAT_ERROR 1
#define SERVER_FAIL 2
#define NAME_ERROR 3
#define NOT_IMPL 4
#define REFUSED 5
    uint16_t qdcount; /* Question count */
    uint16_t ancount; /* Answer count */
    uint16_t nscount; /* Authority (name server) count */
    uint16_t arcount; /* Additional record count */
};

typedef struct
{
    char             url[REMOTE_ADDR_LEN_MAX + 1];
    uint32_t         ip;
    struct list_head node;
} bDnsInfo_t;

typedef struct
{
    uint8_t  state;
    uint8_t  retry;
    uint8_t  dns_index;
    uint8_t  buf[MAX_DNS_BUF_SIZE];
    int      socket;
    uint16_t msgid;
    uint64_t tick;
} bDnsRunCtx_t;

static LIST_HEAD(bDnsHead);

#define MAX_DNS_RETRY 2     ///< Requery Count
#define DNS_WAIT_TIME 3000  ///< Wait response time ms
#define IPPORT_DOMAIN 53    ///< DNS server port number
#define MAX_DOMAIN_NAME 128
#define INITRTT 2000L /* Initial smoothed response time */
#define MAXCNAME (MAX_DOMAIN_NAME + (MAX_DOMAIN_NAME >> 1))

#define TYPE_A 1      /* Host address */
#define TYPE_NS 2     /* Name server */
#define TYPE_MD 3     /* Mail destination (obsolete) */
#define TYPE_MF 4     /* Mail forwarder (obsolete) */
#define TYPE_CNAME 5  /* Canonical name */
#define TYPE_SOA 6    /* Start of Authority */
#define TYPE_MB 7     /* Mailbox name (experimental) */
#define TYPE_MG 8     /* Mail group member (experimental) */
#define TYPE_MR 9     /* Mail rename name (experimental) */
#define TYPE_NULL 10  /* Null (experimental) */
#define TYPE_WKS 11   /* Well-known sockets */
#define TYPE_PTR 12   /* Pointer record */
#define TYPE_HINFO 13 /* Host information */
#define TYPE_MINFO 14 /* Mailbox information (experimental)*/
#define TYPE_MX 15    /* Mail exchanger */
#define TYPE_TXT 16   /* Text strings */
#define TYPE_ANY 255  /* Matches any type */

#define CLASS_IN 1 /* The ARPA Internet */

/* Round trip timing parameters */
#define AGAIN 8  /* Average RTT gain = 1/8 */
#define LAGAIN 3 /* Log2(AGAIN) */
#define DGAIN 4  /* Mean deviation gain = 1/4 */
#define LDGAIN 2 /* log2(DGAIN) */

#define DNS_STATE_IDLE (0)
#define DNS_STATE_REQUEST (1)
#define DNS_STATE_WAIT_RESPONSE (2)

#ifndef _TCPIP_DNS_DEBUG_ENABLE
#define DNS_MODULE_DEBUG_EN (0)
#else
#define DNS_MODULE_DEBUG_EN (_TCPIP_DNS_DEBUG_ENABLE)
#endif

#ifdef TCPIP_DNS_CACHE_NUM
#define B_TCPIP_DNS_CACHE_NUM (TCPIP_DNS_CACHE_NUM)
#else
#define B_TCPIP_DNS_CACHE_NUM (5)
#endif

typedef struct
{
    uint32_t expire_tick;
    uint32_t ip;
    char     domain[MAX_DOMAIN_NAME];
} bDNSCache_t;

// 默认DNS 114.114.114.114 8.8.8.8
// 获取 IP 后将网关IP放入最后一项
#define B_TCPIP_DNS_NUM 3
static uint32_t     bTcpIpDNS[B_TCPIP_DNS_NUM] = {0x72727272, 0x08080808};
static bDnsRunCtx_t bDnsRunCtx                 = {
                    .socket = -1,
                    .msgid  = 1,
};

static bDNSCache_t bDNSCache[B_TCPIP_DNS_CACHE_NUM];

//-----------------------------------------dhcp---------------------------------
#ifndef _TCPIP_DHCP_DEBUG_ENABLE
#define DHCP_MODULE_DEBUG_EN (0)
#else
#define DHCP_MODULE_DEBUG_EN (_TCPIP_DHCP_DEBUG_ENABLE)
#endif

#define OPT_SIZE 312                   /// Max OPT size of @ref bRequestIp_t
#define RIP_MSG_SIZE (236 + OPT_SIZE)  /// Max size of @ref bRequestIp_t

#define STATE_DHCP_INIT 0       ///< Initialize
#define STATE_DHCP_DISCOVER 1   ///< send DISCOVER and wait OFFER
#define STATE_DHCP_REQUEST 2    ///< send REQEUST and wait ACK or NACK
#define STATE_DHCP_LEASED 3     ///< ReceiveD ACK and IP leased
#define STATE_DHCP_REREQUEST 4  ///< send REQUEST for maintaining leased IP
#define STATE_DHCP_RELEASE 5    ///< No use
#define STATE_DHCP_STOP 6       ///< Stop processing DHCP

/* Retry to processing DHCP */
#define MAX_DHCP_RETRY 2   ///< Maximum retry count
#define DHCP_WAIT_TIME 10  ///< Wait Time 10s

/* UDP port numbers for DHCP */
#define DHCP_SERVER_PORT 67  ///< DHCP server port number
#define DHCP_CLIENT_PORT 68  ///< DHCP client port number

/* DHCP message OP code */
#define DHCP_BOOTREQUEST 1  ///< Request Message used in op
#define DHCP_BOOTREPLY 2    ///< Reply Message used i op

/* DHCP message type */
#define DHCP_DISCOVER 1  ///< DISCOVER message in OPT
#define DHCP_OFFER 2     ///< OFFER message in OPT
#define DHCP_REQUEST 3   ///< REQUEST message in OPT
#define DHCP_DECLINE 4   ///< DECLINE message in OPT
#define DHCP_ACK 5       ///< ACK message in OPT
#define DHCP_NAK 6       ///< NACK message in OPT
#define DHCP_RELEASE 7   ///< RELEASE message in OPT  . No use
#define DHCP_INFORM 8    ///< INFORM message in OPT  . No use

#define DHCP_HTYPE10MB 1   ///< Used in type
#define DHCP_HTYPE100MB 2  ///< Used in type

#define DHCP_HLENETHERNET 6  ///< Used in hlen
#define DHCP_HOPS 0          ///< Used in hops
#define DHCP_SECS 0          ///< Used in secs

#define DHCP_FLAGSBROADCAST 0x8000  ///< The broadcast value of flags
#define DHCP_FLAGSUNICAST 0x0000    ///< The unicast   value of flags

#define MAGIC_COOKIE 0x63825363  ///< You should not modify it number.

#define DCHP_HOST_NAME "BabyOS\0"

static uint8_t HOST_NAME[] = DCHP_HOST_NAME;

typedef struct
{
    uint8_t  op;             ///< @ref DHCP_BOOTREQUEST or @ref DHCP_BOOTREPLY
    uint8_t  htype;          ///< @ref DHCP_HTYPE10MB or @ref DHCP_HTYPE100MB
    uint8_t  hlen;           ///< @ref DHCP_HLENETHERNET
    uint8_t  hops;           ///< @ref DHCP_HOPS
    uint32_t xid;            ///< @ref DHCP_XID  This increase one every DHCP transaction.
    uint16_t secs;           ///< @ref DHCP_SECS
    uint16_t flags;          ///< @ref DHCP_FLAGSBROADCAST or @ref DHCP_FLAGSUNICAST
    uint8_t  ciaddr[4];      ///< @ref Request IP to DHCP sever
    uint8_t  yiaddr[4];      ///< @ref Offered IP from DHCP server
    uint8_t  siaddr[4];      ///< No use
    uint8_t  giaddr[4];      ///< No use
    uint8_t  chaddr[16];     ///< DHCP client 6bytes MAC address. Others is filled to zero
    uint8_t  sname[64];      ///< No use
    uint8_t  file[128];      ///< No use
    uint8_t  OPT[OPT_SIZE];  ///< Option
} bRequestIp_t;

static bRequestIp_t bRequestIp = {0};

typedef struct
{
    int          sockfd;
    uint8_t      state;
    uint8_t      retry_times;
    uint64_t     next_tick;
    bRequestIp_t rip_info;
    uint32_t     xid;
    uint8_t      svr_addr[4];
    uint8_t      sip[4];       // DHCP Server IP address
    uint8_t      real_sip[4];  // For extract my DHCP server in a few DHCP server
    uint8_t      allocated_mask[4];
    uint8_t      allocated_ip[4];
    uint8_t      allocated_gw[4];
    uint8_t      allocated_dns[4];
    uint8_t      old_allocated_ip[4];
    uint32_t     dhcp_lease_time;
    uint64_t     dhcp_lease_expire_tick;
} bDhcpCtx_t;

enum
{
    DHCP_FAILED = 0,  ///< Processing Fail
    DHCP_RUNNING,     ///< Processing DHCP protocol
    DHCP_IP_ASSIGN,   ///< First Occupy IP from DHPC server
    DHCP_IP_CHANGED,  ///< Change IP address by new ip from DHCP
    DHCP_IP_LEASED,   ///< Stand by
    DHCP_STOPPED      ///< Stop processing DHCP protocol
};

/*
 * @brief DHCP option and value (cf. RFC1533)
 */
enum
{
    padOption              = 0,
    subnetMask             = 1,
    timerOffset            = 2,
    routersOnSubnet        = 3,
    timeServer             = 4,
    nameServer             = 5,
    dns                    = 6,
    logServer              = 7,
    cookieServer           = 8,
    lprServer              = 9,
    impressServer          = 10,
    resourceLocationServer = 11,
    hostName               = 12,
    bootFileSize           = 13,
    meritDumpFile          = 14,
    domainName             = 15,
    swapServer             = 16,
    rootPath               = 17,
    extentionsPath         = 18,
    IPforwarding           = 19,
    nonLocalSourceRouting  = 20,
    policyFilter           = 21,
    maxDgramReasmSize      = 22,
    defaultIPTTL           = 23,
    pathMTUagingTimeout    = 24,
    pathMTUplateauTable    = 25,
    ifMTU                  = 26,
    allSubnetsLocal        = 27,
    broadcastAddr          = 28,
    performMaskDiscovery   = 29,
    maskSupplier           = 30,
    performRouterDiscovery = 31,
    routerSolicitationAddr = 32,
    staticRoute            = 33,
    trailerEncapsulation   = 34,
    arpCacheTimeout        = 35,
    ethernetEncapsulation  = 36,
    tcpDefaultTTL          = 37,
    tcpKeepaliveInterval   = 38,
    tcpKeepaliveGarbage    = 39,
    nisDomainName          = 40,
    nisServers             = 41,
    ntpServers             = 42,
    vendorSpecificInfo     = 43,
    netBIOSnameServer      = 44,
    netBIOSdgramDistServer = 45,
    netBIOSnodeType        = 46,
    netBIOSscope           = 47,
    xFontServer            = 48,
    xDisplayManager        = 49,
    dhcpRequestedIPaddr    = 50,
    dhcpIPaddrLeaseTime    = 51,
    dhcpOptionOverload     = 52,
    dhcpMessageType        = 53,
    dhcpServerIdentifier   = 54,
    dhcpParamRequest       = 55,
    dhcpMsg                = 56,
    dhcpMaxMsgSize         = 57,
    dhcpT1value            = 58,
    dhcpT2value            = 59,
    dhcpClassIdentifier    = 60,
    dhcpClientIdentifier   = 61,
    endOption              = 255
};

//------------------------------------------tcpipinfo----------------------------
#define TCPIP_STACK_OPT_USE_LWIP (0x1)
#define TCPIP_STACK_OPT_NO_BUFFER (0x2)
#define TCPIP_STACK_OPT_SET_USE_LWIP(opt) ((opt) = TCPIP_STACK_OPT_USE_LWIP)
#define TCPIP_STACK_OPT_SET_NO_BUFFER(opt) ((opt) = TCPIP_STACK_OPT_NO_BUFFER)
#define TCPIP_STACK_OPT_IS_USE_LWIP(opt) ((opt) == TCPIP_STACK_OPT_USE_LWIP)
#define TCPIP_STACK_OPT_IS_NO_BUFFER(opt) ((opt) == TCPIP_STACK_OPT_NO_BUFFER)
#define TCPIP_STACK_OPT_IS_USE_BUFFER(opt) \
    (((opt) != TCPIP_STACK_OPT_NO_BUFFER) && ((opt) != TCPIP_STACK_OPT_USE_LWIP))

typedef struct
{
    uint8_t         priority;
    bTcpIpNetif_t   netif;
    bTcpIpStackIf_t stack_if;
    uint8_t         stack_opt;
    bDhcpCtx_t      dhcp_ctx;
    struct
    {
        uint8_t  ignore_ip;
        uint8_t  is_dhcp;
        uint8_t  get_ip_done;
        uint32_t ipaddr;
        uint32_t netmask;
        uint32_t gateway;
    } ip_info;
    struct
    {
        pbTransCb_t cb;
        void       *arg;
    } trans_listen_cb;
} bTcpIpInfo_t;

typedef struct
{
    bTcpIpInfo_t *pinfo_table;
    bTcpIpInfo_t *pinfo;
    uint8_t       info_number;
} bTcpIpCtx_t;

static bTcpIpCtx_t bTcpIpCtx = {
    .pinfo_table = NULL,
    .pinfo       = NULL,
    .info_number = 0,
};

static LIST_HEAD(bSocketHead);
B_TASK_CREATE_ATTR(bTcpIpTaskAttr);

/**
 * \}
 */

/**
 * \defgroup TCPIP_Private_Functions
 * \{
 */
// "192.168.0.1" >> 0xc0a80001
static uint32_t _bIpStr2Uint32(const char *ip_str)
{
    uint32_t result = 0;
    uint8_t  i      = 0;
    uint32_t value  = 0;
    for (i = 0; i < 16; i++)
    {
        if ((ip_str[i] < '0' || ip_str[i] > '9') && ip_str[i] != '.' && ip_str[i] != '\0')
        {
            return 0;
        }
        if (ip_str[i] == '.' || ip_str[i] == '\0')
        {
            if (value > 255)
            {
                return 0;
            }
            result <<= 8;
            result |= value;
            value = 0;
            if (ip_str[i] == '\0')
            {
                break;
            }
        }
        else
        {
            value *= 10;
            value += ip_str[i] - '0';
        }
    }
    return result;
}

static void _bIpInt2Str(char *ip_str, uint32_t ip)
{
    int     i     = 0;
    uint8_t index = 0;
    uint8_t ch    = 0;
    if (ip_str == NULL)
    {
        return;
    }
    for (i = 0; i < 4; i++)
    {
        ch = (ip >> (8 * (3 - i))) & 0xFF;
        if (ch >= 100)
        {
            ip_str[index++] = ch / 100 + '0';
            ip_str[index++] = ch % 100 / 10 + '0';
            ip_str[index++] = ch % 10 + '0';
        }
        else if (ch >= 10)
        {
            ip_str[index++] = ch / 10 + '0';
            ip_str[index++] = ch % 10 + '0';
        }
        else
        {
            ip_str[index++] = ch + '0';
        }
        if (i != 3)
        {
            ip_str[index++] = '.';
        }
    }
    ip_str[index] = '\0';
}

#if ((defined(_TCPIP_SEND_BUF_ENABLE) && (_TCPIP_SEND_BUF_ENABLE == 1)) || \
     (defined(_TCPIP_RECV_BUF_ENABLE) && (_TCPIP_RECV_BUF_ENABLE == 1)))

static uint16_t _bTransPcbCalDataLen(struct list_head *phead)
{
    uint16_t      len   = 0;
    bTransData_t *pdata = NULL;
    list_for_each_entry(pdata, bTransData_t, phead, node)
    {
        len += pdata->len;
    }
    return len;
}

static uint16_t _bTransPcbAddData(struct list_head *phead, uint8_t *pbuf, uint16_t len,
                                  void (*release)(void *), uint16_t limit)
{
    bTransData_t *pdata = NULL;
    if ((len + _bTransPcbCalDataLen(phead)) > limit)
    {
        return 0;
    }
    pdata = (bTransData_t *)bMalloc(sizeof(bTransData_t));
    if (pdata == NULL)
    {
        return 0;
    }
    if (release)
    {
        pdata->pbuf    = pbuf;
        pdata->release = release;
    }
    else
    {
        pdata->pbuf = bMalloc(len + 1);
        if (pdata->pbuf == NULL)
        {
            bFree(pdata);
            return 0;
        }
        memset(pdata->pbuf, 0, len + 1);
        memcpy(pdata->pbuf, pbuf, len);
        pdata->release = bFree;
    }
    pdata->len = len;
    list_add_tail(&pdata->node, phead);
    b_log("[add data %d limit %d]\r\n", len, limit);
    return len;
}

static void _bTransPcbDataFree(struct list_head *phead)
{
    bTransData_t *pdata = NULL;
    while (!list_empty(phead))
    {
        pdata = list_entry(phead->next, bTransData_t, node);
        list_del(phead->next);
        if (pdata->release && pdata->pbuf)
        {
            pdata->release(pdata->pbuf);
        }
        bFree(pdata);
    }
}

static void _bTransPcbDeleteData(struct list_head *phead)
{
    bTransData_t *pdata = NULL;
    if (!list_empty(phead))
    {
        pdata = list_entry(phead->next, bTransData_t, node);
        list_del(phead->next);
        if (pdata->release && pdata->pbuf)
        {
            pdata->release(pdata->pbuf);
        }
        bFree(pdata);
    }
}

static int _bTransPcbGetPcbData(struct list_head *phead, uint8_t **ppbuf, uint16_t *plen)
{
    bTransData_t *pdata = NULL;
    if (!list_empty(phead))
    {
        pdata  = list_entry(phead->next, bTransData_t, node);
        *ppbuf = pdata->pbuf;
        *plen  = pdata->len;
        return 0;
    }
    return -1;
}

static int _bTransPcbDataShiftForward(struct list_head *phead, uint16_t len)
{
    bTransData_t *pdata = NULL;
    if (!list_empty(phead))
    {
        pdata = list_entry(phead->next, bTransData_t, node);
        if (pdata->len > len)
        {
            memmove(pdata->pbuf, pdata->pbuf + len, pdata->len - len);
            pdata->len -= len;
        }
        else
        {
            list_del(phead->next);
            if (pdata->release && pdata->pbuf)
            {
                pdata->release(pdata->pbuf);
            }
            bFree(pdata);
        }
    }
    return 0;
}

static uint16_t _bTransPcbReadData(struct list_head *phead, uint8_t *pbuf, uint16_t len)
{
    uint16_t cur_valid_len = _bTransPcbCalDataLen(phead);
    uint8_t *pdata         = NULL;
    uint16_t data_len = 0, read_len = 0;
    if (len > cur_valid_len)
    {
        len = cur_valid_len;
    }
    // 通过 _bTransPcbGetPcbData 和 _bTransPcbDataShiftForward 获取数据
    cur_valid_len = 0;
    while (cur_valid_len < len)
    {
        read_len = len - cur_valid_len;
        if (_bTransPcbGetPcbData(phead, &pdata, &data_len) == 0)
        {
            if (data_len > read_len)
            {
                memcpy(pbuf + cur_valid_len, pdata, read_len);
                cur_valid_len += read_len;
            }
            else
            {
                memcpy(pbuf + cur_valid_len, pdata, data_len);
                cur_valid_len += data_len;
            }
            _bTransPcbDataShiftForward(phead, read_len);
        }
        else
        {
            break;
        }
    }
    return cur_valid_len;
}

static void _bTransPcbClear(bTrans_t *ptrans)
{
    int i = 0;
    if (ptrans)
    {
        _bTransPcbDataFree(&ptrans->recv_head);
        _bTransPcbDataFree(&ptrans->send_head);
        ptrans->send_busy = 0;
    }
}

#endif

static void _bMainNetcardUpdate()
{
    int           i          = 0;
    bTcpIpInfo_t *pinfo      = NULL;
    bTcpIpInfo_t *pinfo_last = bTcpIpCtx.pinfo;
    if (bTcpIpCtx.pinfo_table == NULL)
    {
        return;
    }
    // M-NEW-1 注释: 当前实现里 "数值越小优先级越高" (lowest-num-wins),
    // 用 `bTcpIpCtx.pinfo->priority > pinfo->priority` 检测: 当新 candidate 的 priority
    // 更小 (数值小=优先级高), 替换当前 pinfo.
    // 与 Linux rt_priority / lwIP netif 默认顺序相反, 这里采用 macOS-style "数值小=优先",
    // 调用方注册 netcard 时按业务需要分配小数值给优先网卡 (例如 WiFi=10, ETH=20).
    // 如果将来要改语义, 同步翻转这个比较符并更新 bModTcpip.h 的 priority 文档.
    for (i = 0; i < bTcpIpCtx.info_number; i++)
    {
        pinfo = &bTcpIpCtx.pinfo_table[i];
        if (pinfo->netif.is_linked)
        {
            if (bTcpIpCtx.pinfo == NULL)
            {
                bTcpIpCtx.pinfo = pinfo;
            }
            else
            {
                // 数值小=优先级高; 当前 pinfo 数值 > 新 pinfo 数值时切换.
                if ((bTcpIpCtx.pinfo->priority > pinfo->priority) ||
                    (bTcpIpCtx.pinfo->netif.is_linked == 0))
                {
                    bTcpIpCtx.pinfo = pinfo;
                }
            }
        }
        else if (bTcpIpCtx.pinfo == NULL)
        {
            bTcpIpCtx.pinfo = pinfo;
        }
    }
    if (bTcpIpCtx.pinfo != pinfo_last && bTcpIpCtx.pinfo != NULL)
    {
        if (bTcpIpCtx.pinfo->netif.private != NULL)
        {
            // CRIT-TCP-1 fix: 之前用 `pinfo` (函数形参, 来自最后一次循环迭代),
            // 但代码本意是"当前 active pinfo". 形参 pinfo 可能 != bTcpIpCtx.pinfo
            // (后者是已更新为 active 的全局), 导致 set_default_netif 调错 netif.
            // 修正为 bTcpIpCtx.pinfo, 真正传当前 active.
            B_SAFE_INVOKE(bTcpIpCtx.pinfo->stack_if.set_default_netif,
                          &bTcpIpCtx.pinfo->netif);
        }
    }
    // HIGH-TCP-1 fix: bTcpIpCtx.pinfo 可能为 NULL (上面 if 已 guard, 但 b_log
    // 还是在 if 外, 第二次访问 bTcpIpCtx.pinfo->netif.dev_no 时 NULL 会
    // crash). 改成 defensive: NULL 则跳过 log.
    if (bTcpIpCtx.pinfo != NULL)
    {
        b_log("[update netcard][%d][%d]\r\n", bTcpIpCtx.info_number,
              bTcpIpCtx.pinfo->netif.dev_no);
    }
}

static void _bPhyLinkStateCb(uint8_t state, void *arg)
{
    bTcpIpInfo_t *pinfo = (bTcpIpInfo_t *)arg;
    if (pinfo == NULL)
    {
        return;
    }
    b_log("%d link %d->%d\r\n", pinfo->netif.dev_no, pinfo->netif.is_linked, state);
    pinfo->netif.is_linked = state;
    if ((pinfo->ip_info.ignore_ip == 0) && (pinfo->ip_info.is_dhcp))
    {
        pinfo->ip_info.get_ip_done = 0;
    }
    B_SAFE_INVOKE(pinfo->stack_if.set_link_state, state, &pinfo->netif);
    _bMainNetcardUpdate();
}

///////////////////////////////////////////////////////////////////////////////
////////////////////        DNS           ////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
static int bDnsCacheAdd(char *domain, uint32_t ip, uint32_t ttl)
{
    uint32_t ctick = bHalGetSysTick();

    if (ttl == 0)
    {
        ttl = 30;
    }

    for (int i = 0; i < B_TCPIP_DNS_CACHE_NUM; i++)
    {
        if (strcmp(bDNSCache[i].domain, domain) == 0)
        {
            bDNSCache[i].expire_tick = ctick + MS2TICKS(ttl * 1000);
            bDNSCache[i].ip          = ip;
            b_log("dns update: %s %x %d %d\r\n", domain, ip, ctick, bDNSCache[i].expire_tick);
            return 0;
        }
    }

    for (int i = 0; i < B_TCPIP_DNS_CACHE_NUM; i++)
    {
        if (bDNSCache[i].expire_tick <= ctick)
        {
            bDNSCache[i].expire_tick = ctick + MS2TICKS(ttl * 1000);
            bDNSCache[i].ip          = ip;
            memset(bDNSCache[i].domain, 0, sizeof(bDNSCache[i].domain));
            // H-NEW-12 fix: strncpy 不会自动 NUL 终止, 当 strlen(domain) >= MAX_DOMAIN_NAME
            // 时残留旧字节. 用 snprintf 安全截断 + 显式 NUL. memset 已保证尾字节为 0,
            // 这里再保险一次.
            snprintf(bDNSCache[i].domain, sizeof(bDNSCache[i].domain), "%s", domain);
            bDNSCache[i].domain[sizeof(bDNSCache[i].domain) - 1] = '\0';
            b_log("dns add: %s %x %d %d\r\n", domain, ip, ctick, bDNSCache[i].expire_tick);
            return 0;
        }
    }
    return -1;
}

static int bDnsCacheGet(const char *domain, uint32_t *ip)
{
    uint32_t ctick = bHalGetSysTick();
    for (int i = 0; i < B_TCPIP_DNS_CACHE_NUM; i++)
    {
        if (bDNSCache[i].expire_tick > ctick && strcmp(bDNSCache[i].domain, domain) == 0)
        {
            *ip = bDNSCache[i].ip;
            b_log("dns get: %s %x %d %d\r\n", domain, *ip, ctick, bDNSCache[i].expire_tick);
            return 0;
        }
    }
    return -1;
}

static int8_t bDnsCheckTimeout()
{
    if ((bHalGetSysTickPlus() - bDnsRunCtx.tick) >= MS2TICKS(DNS_WAIT_TIME))
    {
        bDnsRunCtx.tick = bHalGetSysTickPlus();
        if (bDnsRunCtx.retry >= MAX_DNS_RETRY)
        {
            bDnsRunCtx.retry = 0;
            return -1;  // timeout occurred
        }
        bDnsRunCtx.retry++;
        return 0;  // timer over, but no timeout
    }
    return 1;  // no timer over, no timeout occur
}

/* converts uint16_t from network buffer to a host byte order integer. */
static uint16_t get16(uint8_t *s)
{
    uint16_t i;
    i = *s++ << 8;
    i = i + *s;
    return i;
}

/* copies uint16_t to the network buffer with network byte order. */
static uint8_t *put16(uint8_t *s, uint16_t i)
{
    *s++ = i >> 8;
    *s++ = i;
    return s;
}

static uint32_t get32(uint8_t *s)
{
    uint32_t i;
    i = (*s++) << 24;
    i = i + ((*s++) << 16);
    i = i + ((*s++) << 8);
    i = i + *s;
    return i;
}

static int16_t _bDnsMakequery(uint16_t op, char *name, uint8_t *buf, uint16_t len)
{
    uint8_t *cp;
    char    *cp1;
    char     sname[MAXCNAME];
    char    *dname;
    uint16_t p;
    uint16_t dlen;

    cp = buf;

    bDnsRunCtx.msgid++;
    cp = put16(cp, bDnsRunCtx.msgid);
    p  = (op << 11) | 0x0100; /* Recursion desired */
    cp = put16(cp, p);
    cp = put16(cp, 1);
    cp = put16(cp, 0);
    cp = put16(cp, 0);
    cp = put16(cp, 0);

    strcpy(sname, name);
    dname = sname;
    dlen  = strlen(dname);
    for (;;)
    {
        /* Look for next dot */
        cp1 = strchr(dname, '.');

        if (cp1 != NULL)
            len = cp1 - dname; /* More to come */
        else
            len = dlen; /* Last component */

        *cp++ = len; /* Write length of component */
        if (len == 0)
            break;

        /* Copy component up to (but not including) dot */
        strncpy((char *)cp, dname, len);
        cp += len;
        if (cp1 == NULL)
        {
            *cp++ = 0; /* Last one; write null and finish */
            break;
        }
        dname += len + 1;
        dlen -= len + 1;
    }
    cp = put16(cp, 0x0001); /* type */
    cp = put16(cp, 0x0001); /* class */
    return (int16_t)((intptr_t)cp - (intptr_t)buf);
}

static int _bParseName(uint8_t *msg, uint8_t *compressed, char *buf, int16_t len)
{
    uint16_t slen; /* Length of current segment */
    uint8_t *cp;
    int      clen     = 0; /* Total length of compressed name */
    int      indirect = 0; /* Set if indirection encountered */
    int      nseg     = 0; /* Total number of segments in name */

    cp = compressed;

    for (;;)
    {
        slen = *cp++; /* Length of this segment */

        if (!indirect)
            clen++;

        if ((slen & 0xc0) == 0xc0)
        {
            if (!indirect)
                clen++;
            indirect = 1;
            /* Follow indirection */
            cp   = &msg[((slen & 0x3f) << 8) + *cp];
            slen = *cp++;
        }

        if (slen == 0) /* zero length == all done */
            break;

        len -= slen + 1;

        if (len < 0)
            return -1;

        if (!indirect)
            clen += slen;

        while (slen-- != 0)
            *buf++ = (char)*cp++;
        *buf++ = '.';
        nseg++;
    }

    if (nseg == 0)
    {
        /* Root name; represent as single dot */
        *buf++ = '.';
        len--;
    }

    *buf++ = '\0';
    len--;

    return clen; /* Length of compressed message */
}

static uint8_t *_bDnsQuestion(uint8_t *msg, uint8_t *cp)
{
    int  len;
    char name[MAXCNAME];

    len = _bParseName(msg, cp, name, MAXCNAME);

    if (len == -1)
        return 0;

    cp += len;
    cp += 2; /* type */
    cp += 2; /* class */

    return cp;
}

static uint8_t *_bDnsAnswer(uint8_t *msg, uint8_t *cp, uint8_t *ip_from_dns, uint32_t *ttl)
{
    int  len, type;
    char name[MAXCNAME] = {0};

    if (msg == NULL || cp == NULL || ip_from_dns == NULL || ttl == NULL)
    {
        return 0;
    }

    len = _bParseName(msg, cp, name, MAXCNAME);

    if (len == -1)
        return 0;

    cp += len;
    type = get16(cp);
    cp += 2; /* type */
    cp += 2; /* class */
    *ttl = get32(cp);
    cp += 4; /* ttl */
    cp += 2; /* len */

    switch (type)
    {
        case TYPE_A:
            /* Just read the address directly into the structure */
            ip_from_dns[0] = *cp++;
            ip_from_dns[1] = *cp++;
            ip_from_dns[2] = *cp++;
            ip_from_dns[3] = *cp++;
            break;
        case TYPE_CNAME:
        case TYPE_MB:
        case TYPE_MG:
        case TYPE_MR:
        case TYPE_NS:
        case TYPE_PTR:
            /* These types all consist of a single domain name */
            /* convert it to ascii format */
            len = _bParseName(msg, cp, name, MAXCNAME);
            if (len == -1)
                return 0;

            cp += len;
            break;
        case TYPE_HINFO:
            len = *cp++;
            cp += len;

            len = *cp++;
            cp += len;
            break;
        case TYPE_MX:
            cp += 2;
            /* Get domain name of exchanger */
            len = _bParseName(msg, cp, name, MAXCNAME);
            if (len == -1)
                return 0;

            cp += len;
            break;
        case TYPE_SOA:
            /* Get domain name of name server */
            len = _bParseName(msg, cp, name, MAXCNAME);
            if (len == -1)
                return 0;

            cp += len;

            /* Get domain name of responsible person */
            len = _bParseName(msg, cp, name, MAXCNAME);
            if (len == -1)
                return 0;

            cp += len;

            cp += 4;
            cp += 4;
            cp += 4;
            cp += 4;
            cp += 4;
            break;
        case TYPE_TXT:
            /* Just stash */
            break;
        default:
            /* Ignore */
            break;
    }
    return cp;
}

static int8_t _bDnsParseMsg(struct dhdr *pdhdr, uint8_t *pbuf, uint8_t *ip_from_dns, uint32_t *ttl)
{
    uint16_t tmp;
    uint16_t i;
    uint8_t *msg;
    uint8_t *cp;

    if (pdhdr == NULL || pbuf == NULL || ip_from_dns == NULL || ttl == NULL)
    {
        return -1;
    }

    msg = pbuf;
    memset(pdhdr, 0, sizeof(*pdhdr));

    pdhdr->id = get16(&msg[0]);
    tmp       = get16(&msg[2]);
    if (tmp & 0x8000)
        pdhdr->qr = 1;

    pdhdr->opcode = (tmp >> 11) & 0xf;

    if (tmp & 0x0400)
        pdhdr->aa = 1;
    if (tmp & 0x0200)
        pdhdr->tc = 1;
    if (tmp & 0x0100)
        pdhdr->rd = 1;
    if (tmp & 0x0080)
        pdhdr->ra = 1;

    pdhdr->rcode   = tmp & 0xf;
    pdhdr->qdcount = get16(&msg[4]);
    pdhdr->ancount = get16(&msg[6]);
    pdhdr->nscount = get16(&msg[8]);
    pdhdr->arcount = get16(&msg[10]);

    /* Now parse the variable length sections */
    cp = &msg[12];

    /* Question section */
    for (i = 0; i < pdhdr->qdcount; i++)
    {
        cp = _bDnsQuestion(msg, cp);
        if (!cp)
            return -1;
    }

    /* Answer section */
    for (i = 0; i < pdhdr->ancount; i++)
    {
        cp = _bDnsAnswer(msg, cp, ip_from_dns, ttl);
        if (!cp)
            return -1;
    }

    /* Name server (authority) section */
    for (i = 0; i < pdhdr->nscount; i++)
    {
        ;
    }

    /* Additional section */
    for (i = 0; i < pdhdr->arcount; i++)
    {
        ;
    }

    if (pdhdr->rcode == 0)
        return 1;  // No error
    else
        return 0;
}

static void _bDnsListDeletNode(const char *url)
{
    bDnsInfo_t *pos, *n;
    list_for_each_entry_safe(pos, n, bDnsInfo_t, &bDnsHead, node)
    {
        if (strcmp(pos->url, url) == 0)
        {
            list_del(&pos->node);
            bFree(pos);
            return;
        }
    }
}

static bDnsInfo_t *_bDnsListFindNode(const char *url)
{
    bDnsInfo_t *pos, *n;
    list_for_each_entry_safe(pos, n, bDnsInfo_t, &bDnsHead, node)
    {
        if (strcmp(pos->url, url) == 0)
        {
            return pos;
        }
    }
    return NULL;
}

static bDnsResult_t _bDnsParse(const char *remote, uint32_t *ip, uint8_t create_dns)
{
    if (remote == NULL || strlen(remote) > REMOTE_ADDR_LEN_MAX || ip == NULL)
    {
        return DNS_PARSE_FAILED;
    }

    uint32_t remote_ip_tmp = _bIpStr2Uint32(remote);
    if (remote_ip_tmp != 0)
    {
        *ip = remote_ip_tmp;
        return DNS_PARSE_SUCCESS;
    }

    if (0 == bDnsCacheGet(remote, &remote_ip_tmp))
    {
        *ip = remote_ip_tmp;
        return DNS_PARSE_SUCCESS;
    }

    if (_bDnsListFindNode(remote) != NULL)
    {
        return DNS_PARSE_ONGING;
    }

    if (create_dns == 0)
    {
        return DNS_PARSE_FAILED;
    }

    bDnsInfo_t *pdns = (bDnsInfo_t *)bMalloc(sizeof(bDnsInfo_t));
    if (pdns == NULL)
    {
        return -2;
    }
    memset(pdns, 0, sizeof(bDnsInfo_t));
    pdns->ip = 0;
    memcpy(pdns->url, remote, strlen(remote));
    list_add_tail(&pdns->node, &bDnsHead);
    b_log("dns add %s\r\n", pdns->url);
    return DNS_PARSE_ONGING;
}

static void _bDNSTransCb(bTransEvent_t event, void *param, void *arg)
{
}

static void _bDnsHandler()
{
    int8_t      ret = 0;
    struct dhdr dhp;
    uint8_t     ip[4];
    uint16_t    len, port;
    int8_t      ret_check_timeout;
    uint8_t     dns_str[16];

    struct list_head *node, *n;
    bDnsInfo_t       *pinfo;
    if (list_empty(&bDnsHead))
    {
        if (!SOCKFD_IS_INVALID(bDnsRunCtx.socket))
        {
            bShutdown(bDnsRunCtx.socket);
            bDnsRunCtx.socket = -1;
        }
        return;
    }
    pinfo = list_first_entry(&bDnsHead, bDnsInfo_t, node);
    if (SOCKFD_IS_INVALID(bDnsRunCtx.socket))
    {
        bDnsRunCtx.socket = bSocket(B_TRANS_CONN_UDP, _bDNSTransCb, NULL);
        if (SOCKFD_IS_INVALID(bDnsRunCtx.socket))
        {
            return;
        }
        bDnsRunCtx.state = DNS_STATE_IDLE;
    }
    // b_log("state:%d\r\n", bDnsRunCtx.state);
    if (bDnsRunCtx.state == DNS_STATE_IDLE)
    {
        memset(dns_str, 0, sizeof(dns_str));
        _bIpInt2Str((char *)dns_str, bTcpIpDNS[bDnsRunCtx.dns_index]);
        if (bConnect(bDnsRunCtx.socket, (char *)dns_str, IPPORT_DOMAIN) < 0)
        {
            bDnsRunCtx.state = DNS_STATE_IDLE;
            return;
        }
        bDnsRunCtx.state = DNS_STATE_REQUEST;
        bDnsRunCtx.retry = 0;
    }
    else if (bDnsRunCtx.state == DNS_STATE_REQUEST)
    {
        len = _bDnsMakequery(0, (char *)pinfo->url, bDnsRunCtx.buf, MAX_DNS_BUF_SIZE);
        if (bSend(bDnsRunCtx.socket, bDnsRunCtx.buf, len, NULL) <= 0)
        {
            bDnsRunCtx.state = DNS_STATE_IDLE;
            return;
        }
        bDnsRunCtx.state = DNS_STATE_WAIT_RESPONSE;
        bDnsRunCtx.tick  = bHalGetSysTickPlus();
    }
    else if (bDnsRunCtx.state == DNS_STATE_WAIT_RESPONSE)
    {
        // b_log("dns wait response %d \r\n", bDnsRunCtx.socket);
        if (bSockIsReadable(bDnsRunCtx.socket))
        {
            // b_log("readable..\r\n");
            len = bRecv(bDnsRunCtx.socket, bDnsRunCtx.buf, MAX_DNS_BUF_SIZE, NULL);
            if (len <= 0)
            {
                bDnsRunCtx.state = DNS_STATE_IDLE;
                return;
            }
#if DNS_MODULE_DEBUG_EN
            b_log("dns rec:%d\r\n", len);
            b_log_hex(bDnsRunCtx.buf, len);
#endif
            node  = bDnsHead.next;
            pinfo = list_entry(node, bDnsInfo_t, node);
            uint8_t  parse_ip_buf[4];
            uint32_t ip_ttl = 0;
            ret             = _bDnsParseMsg(&dhp, bDnsRunCtx.buf, parse_ip_buf, &ip_ttl);
#if DNS_MODULE_DEBUG_EN
            b_log("dns parse ret:%d\r\n", ret);
#endif
            if (ret == 1)
            {
                pinfo->ip = parse_ip_buf[0];
                pinfo->ip = (pinfo->ip << 8) | parse_ip_buf[1];
                pinfo->ip = (pinfo->ip << 8) | parse_ip_buf[2];
                pinfo->ip = (pinfo->ip << 8) | parse_ip_buf[3];

                bDnsCacheAdd(pinfo->url, pinfo->ip, ip_ttl / 2);
                _bDnsListDeletNode(pinfo->url);
                bDnsRunCtx.state = DNS_STATE_REQUEST;
                return;
            }
        }
    }
    // Check Timeout
    // b_log("checktimeout..\r\n");
    ret_check_timeout = bDnsCheckTimeout();
    if (ret_check_timeout < 0)
    {
        bDnsRunCtx.dns_index = (bDnsRunCtx.dns_index + 1) % B_TCPIP_DNS_NUM;
        if (bDnsRunCtx.dns_index == 0)
        {
            _bDnsListDeletNode(pinfo->url);
        }
        bDnsRunCtx.state = DNS_STATE_IDLE;
    }
    else if (ret_check_timeout == 0)
    {
        bDnsRunCtx.state = DNS_STATE_REQUEST;
    }
}

///////////////////////////////////////////////////////////////////////////////
////////////////////        DNS  END         /////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
////////////////////       DHCP           ////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
static void _bDhcpTransCb(bTransEvent_t event, void *param, void *arg)
{
}

static void bDhcpTimeoutReset(bTcpIpInfo_t *pinfo)
{
    pinfo->dhcp_ctx.retry_times = 0;
    pinfo->dhcp_ctx.next_tick   = bHalGetSysTickPlus() + MS2TICKS(DHCP_WAIT_TIME * 1000);
}

static int _bDhcpRequestInit(bTcpIpInfo_t *pinfo)
{
    pinfo->stack_if.set_ip(0, 0, 0, &pinfo->netif);
    memset(&pinfo->dhcp_ctx, 0, sizeof(bDhcpCtx_t));
    pinfo->dhcp_ctx.sockfd = bSocket2(pinfo->netif.dev_no, B_TRANS_CONN_UDP, _bDhcpTransCb, NULL);
    if (SOCKFD_IS_INVALID(pinfo->dhcp_ctx.sockfd))
    {
        return -1;
    }
#if DHCP_MODULE_DEBUG_EN
    b_log("dhcp init ok %d \r\n", pinfo->dhcp_ctx.sockfd);
#endif
    if (bBind(pinfo->dhcp_ctx.sockfd, DHCP_CLIENT_PORT) < 0)
    {
        return DHCP_FAILED;
    }
    pinfo->dhcp_ctx.xid = 0x12345678;
    {
        pinfo->dhcp_ctx.xid += pinfo->netif.mac[3];
        pinfo->dhcp_ctx.xid += pinfo->netif.mac[4];
        pinfo->dhcp_ctx.xid += pinfo->netif.mac[5];
        pinfo->dhcp_ctx.xid += (pinfo->netif.mac[3] ^ pinfo->netif.mac[4] ^ pinfo->netif.mac[5]);
    }
    bDhcpTimeoutReset(pinfo);
    pinfo->dhcp_ctx.state = STATE_DHCP_INIT;
    return pinfo->dhcp_ctx.sockfd;
}

static int8_t bDhcpParseMsg(bTcpIpInfo_t *pinfo)
{
    uint16_t len;
    uint8_t *p;
    uint8_t *e;
    uint8_t  type = 0;
    uint8_t  opt_len;

    if (bSockIsReadable(pinfo->dhcp_ctx.sockfd))
    {
        if (bRecv(pinfo->dhcp_ctx.sockfd, (uint8_t *)&(pinfo->dhcp_ctx.rip_info),
                  sizeof(pinfo->dhcp_ctx.rip_info), &len) < 0)
        {
            return -1;
        }
#if DHCP_MODULE_DEBUG_EN
        b_log("dhcp recv %d bytes [%d]\r\n", len, sizeof(bRequestIp_t));
#endif
        if (len > 0)
        {
            // compare mac address
            if ((pinfo->dhcp_ctx.rip_info.chaddr[0] != pinfo->netif.mac[0]) ||
                (pinfo->dhcp_ctx.rip_info.chaddr[1] != pinfo->netif.mac[1]) ||
                (pinfo->dhcp_ctx.rip_info.chaddr[2] != pinfo->netif.mac[2]) ||
                (pinfo->dhcp_ctx.rip_info.chaddr[3] != pinfo->netif.mac[3]) ||
                (pinfo->dhcp_ctx.rip_info.chaddr[4] != pinfo->netif.mac[4]) ||
                (pinfo->dhcp_ctx.rip_info.chaddr[5] != pinfo->netif.mac[5]))
            {
                b_log_e("No My DHCP Message. This message is ignored.\r\n");
                return 0;
            }
            // compare DHCP server ip address
            if ((pinfo->dhcp_ctx.sip[0] != 0) || (pinfo->dhcp_ctx.sip[1] != 0) ||
                (pinfo->dhcp_ctx.sip[2] != 0) || (pinfo->dhcp_ctx.sip[3] != 0))
            {
                if (((pinfo->dhcp_ctx.svr_addr[0] != pinfo->dhcp_ctx.sip[0]) ||
                     (pinfo->dhcp_ctx.svr_addr[1] != pinfo->dhcp_ctx.sip[1]) ||
                     (pinfo->dhcp_ctx.svr_addr[2] != pinfo->dhcp_ctx.sip[2]) ||
                     (pinfo->dhcp_ctx.svr_addr[3] != pinfo->dhcp_ctx.sip[3])) &&
                    ((pinfo->dhcp_ctx.svr_addr[0] != pinfo->dhcp_ctx.real_sip[0]) ||
                     (pinfo->dhcp_ctx.svr_addr[1] != pinfo->dhcp_ctx.real_sip[1]) ||
                     (pinfo->dhcp_ctx.svr_addr[2] != pinfo->dhcp_ctx.real_sip[2]) ||
                     (pinfo->dhcp_ctx.svr_addr[3] != pinfo->dhcp_ctx.real_sip[3])))
                {
                    b_log_e("Another DHCP sever send a response message. This is ignored.\r\n");
                    return 0;
                }
            }
            p = (uint8_t *)(&pinfo->dhcp_ctx.rip_info.op);
            p = p + 240;  // 240 = sizeof(RIP_MSG) + MAGIC_COOKIE size in RIP_MSG.opt -
                          // sizeof(RIP_MSG.opt)
            e                   = p + (len - 240);
            uint32_t lease_time = 0;
            while (p < e)
            {
                switch (*p)
                {
                    case endOption:
                        p = e;  // for break while(p < e)
                        break;
                    case padOption:
                        p++;
                        break;
                    case dhcpMessageType:
                        p++;
                        opt_len = *p++;
                        if (opt_len == 1)
                        {
                            type = *p++;
                        }
                        else
                        {
                            p += opt_len;
                        }
                        break;
                    case subnetMask:
                        p++;
                        p++;
                        pinfo->dhcp_ctx.allocated_mask[0] = *p++;
                        pinfo->dhcp_ctx.allocated_mask[1] = *p++;
                        pinfo->dhcp_ctx.allocated_mask[2] = *p++;
                        pinfo->dhcp_ctx.allocated_mask[3] = *p++;
                        break;
                    case routersOnSubnet:
                        p++;
                        opt_len                         = *p++;
                        pinfo->dhcp_ctx.allocated_gw[0] = *p++;
                        pinfo->dhcp_ctx.allocated_gw[1] = *p++;
                        pinfo->dhcp_ctx.allocated_gw[2] = *p++;
                        pinfo->dhcp_ctx.allocated_gw[3] = *p++;
                        p                               = p + (opt_len - 4);
                        break;
                    case dns:
                        p++;
                        opt_len                          = *p++;
                        pinfo->dhcp_ctx.allocated_dns[0] = *p++;
                        pinfo->dhcp_ctx.allocated_dns[1] = *p++;
                        pinfo->dhcp_ctx.allocated_dns[2] = *p++;
                        pinfo->dhcp_ctx.allocated_dns[3] = *p++;
                        p                                = p + (opt_len - 4);
                        break;
                    case dhcpIPaddrLeaseTime:
                        p++;
                        opt_len    = *p++;
                        lease_time = *p++;
                        lease_time = (lease_time << 8) + *p++;
                        lease_time = (lease_time << 8) + *p++;
                        lease_time = (lease_time << 8) + *p++;
#if DHCP_MODULE_DEBUG_EN
                        b_log("dhcp lease time:%d\r\n", lease_time);
#endif
                        pinfo->dhcp_ctx.dhcp_lease_time = MS2TICKS((lease_time * 1000));
                        pinfo->dhcp_ctx.dhcp_lease_expire_tick =
                            bHalGetSysTickPlus() + MS2TICKS((lease_time * 1000));
                        break;
                    case dhcpServerIdentifier:
                        p++;
                        opt_len                     = *p++;
                        pinfo->dhcp_ctx.sip[0]      = *p++;
                        pinfo->dhcp_ctx.sip[1]      = *p++;
                        pinfo->dhcp_ctx.sip[2]      = *p++;
                        pinfo->dhcp_ctx.sip[3]      = *p++;
                        pinfo->dhcp_ctx.real_sip[0] = pinfo->dhcp_ctx.svr_addr[0];
                        pinfo->dhcp_ctx.real_sip[1] = pinfo->dhcp_ctx.svr_addr[1];
                        pinfo->dhcp_ctx.real_sip[2] = pinfo->dhcp_ctx.svr_addr[2];
                        pinfo->dhcp_ctx.real_sip[3] = pinfo->dhcp_ctx.svr_addr[3];
                        break;
                    default:
                        p++;
                        opt_len = *p++;
                        p += opt_len;
                        break;
                }  // switch
            }
        }
    }
    return type;
}

static void _bDhcpMakeMsg(bTcpIpInfo_t *pinfo)
{
    uint8_t *ptmp;
    uint8_t  i;
    pinfo->dhcp_ctx.rip_info.op    = DHCP_BOOTREQUEST;
    pinfo->dhcp_ctx.rip_info.htype = DHCP_HTYPE10MB;
    pinfo->dhcp_ctx.rip_info.hlen  = DHCP_HLENETHERNET;
    pinfo->dhcp_ctx.rip_info.hops  = DHCP_HOPS;
    ptmp                           = (uint8_t *)(&pinfo->dhcp_ctx.rip_info.xid);
    *(ptmp + 0)                    = (uint8_t)((pinfo->dhcp_ctx.xid & 0xFF000000) >> 24);
    *(ptmp + 1)                    = (uint8_t)((pinfo->dhcp_ctx.xid & 0x00FF0000) >> 16);
    *(ptmp + 2)                    = (uint8_t)((pinfo->dhcp_ctx.xid & 0x0000FF00) >> 8);
    *(ptmp + 3)                    = (uint8_t)((pinfo->dhcp_ctx.xid & 0x000000FF) >> 0);
    pinfo->dhcp_ctx.rip_info.secs  = DHCP_SECS;
    ptmp                           = (uint8_t *)(&pinfo->dhcp_ctx.rip_info.flags);
    *(ptmp + 0)                    = (uint8_t)((DHCP_FLAGSBROADCAST & 0xFF00) >> 8);
    *(ptmp + 1)                    = (uint8_t)((DHCP_FLAGSBROADCAST & 0x00FF) >> 0);

    pinfo->dhcp_ctx.rip_info.ciaddr[0] = 0;
    pinfo->dhcp_ctx.rip_info.ciaddr[1] = 0;
    pinfo->dhcp_ctx.rip_info.ciaddr[2] = 0;
    pinfo->dhcp_ctx.rip_info.ciaddr[3] = 0;

    pinfo->dhcp_ctx.rip_info.yiaddr[0] = 0;
    pinfo->dhcp_ctx.rip_info.yiaddr[1] = 0;
    pinfo->dhcp_ctx.rip_info.yiaddr[2] = 0;
    pinfo->dhcp_ctx.rip_info.yiaddr[3] = 0;

    pinfo->dhcp_ctx.rip_info.siaddr[0] = 0;
    pinfo->dhcp_ctx.rip_info.siaddr[1] = 0;
    pinfo->dhcp_ctx.rip_info.siaddr[2] = 0;
    pinfo->dhcp_ctx.rip_info.siaddr[3] = 0;

    pinfo->dhcp_ctx.rip_info.giaddr[0] = 0;
    pinfo->dhcp_ctx.rip_info.giaddr[1] = 0;
    pinfo->dhcp_ctx.rip_info.giaddr[2] = 0;
    pinfo->dhcp_ctx.rip_info.giaddr[3] = 0;

    pinfo->dhcp_ctx.rip_info.chaddr[0] = pinfo->netif.mac[0];
    pinfo->dhcp_ctx.rip_info.chaddr[1] = pinfo->netif.mac[1];
    pinfo->dhcp_ctx.rip_info.chaddr[2] = pinfo->netif.mac[2];
    pinfo->dhcp_ctx.rip_info.chaddr[3] = pinfo->netif.mac[3];
    pinfo->dhcp_ctx.rip_info.chaddr[4] = pinfo->netif.mac[4];
    pinfo->dhcp_ctx.rip_info.chaddr[5] = pinfo->netif.mac[5];

    for (i = 6; i < 16; i++)
        pinfo->dhcp_ctx.rip_info.chaddr[i] = 0;
    for (i = 0; i < 64; i++)
        pinfo->dhcp_ctx.rip_info.sname[i] = 0;
    for (i = 0; i < 128; i++)
        pinfo->dhcp_ctx.rip_info.file[i] = 0;

    // MAGIC_COOKIE
    pinfo->dhcp_ctx.rip_info.OPT[0] = (uint8_t)((MAGIC_COOKIE & 0xFF000000) >> 24);
    pinfo->dhcp_ctx.rip_info.OPT[1] = (uint8_t)((MAGIC_COOKIE & 0x00FF0000) >> 16);
    pinfo->dhcp_ctx.rip_info.OPT[2] = (uint8_t)((MAGIC_COOKIE & 0x0000FF00) >> 8);
    pinfo->dhcp_ctx.rip_info.OPT[3] = (uint8_t)(MAGIC_COOKIE & 0x000000FF) >> 0;
}

static char _bNibbleToHex(uint8_t nibble)
{
    nibble &= 0x0F;
    if (nibble <= 9)
        return nibble + '0';
    else
        return nibble + ('A' - 0x0A);
}

static void _bDhcpSendDiscover(bTcpIpInfo_t *pinfo)
{
    uint16_t i;
    uint16_t k = 0;

    _bDhcpMakeMsg(pinfo);

    pinfo->dhcp_ctx.sip[0]      = 0;
    pinfo->dhcp_ctx.sip[1]      = 0;
    pinfo->dhcp_ctx.sip[2]      = 0;
    pinfo->dhcp_ctx.sip[3]      = 0;
    pinfo->dhcp_ctx.real_sip[0] = 0;
    pinfo->dhcp_ctx.real_sip[1] = 0;
    pinfo->dhcp_ctx.real_sip[2] = 0;
    pinfo->dhcp_ctx.real_sip[3] = 0;

    k = 4;  // because MAGIC_COOKIE already made by makeDHCPMSG()

    // Option Request Param
    pinfo->dhcp_ctx.rip_info.OPT[k++] = dhcpMessageType;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = 0x01;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = DHCP_DISCOVER;

    // Client identifier
    pinfo->dhcp_ctx.rip_info.OPT[k++] = dhcpClientIdentifier;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = 0x07;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = 0x01;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->netif.mac[0];
    pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->netif.mac[1];
    pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->netif.mac[2];
    pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->netif.mac[3];
    pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->netif.mac[4];
    pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->netif.mac[5];

    // host name
    uint16_t host_name_len            = strlen((char *)HOST_NAME);
    uint16_t total_host_len           = host_name_len + 6;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = hostName;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = total_host_len;  // 先写正确长度
    for (i = 0; i < host_name_len; i++)
    {
        if (k >= OPT_SIZE)
            break;
        pinfo->dhcp_ctx.rip_info.OPT[k++] = HOST_NAME[i];
    }
    if (k + 6 <= OPT_SIZE)
    {
        pinfo->dhcp_ctx.rip_info.OPT[k++] = _bNibbleToHex(pinfo->netif.mac[3] >> 4);
        pinfo->dhcp_ctx.rip_info.OPT[k++] = _bNibbleToHex(pinfo->netif.mac[3] & 0x0F);
        pinfo->dhcp_ctx.rip_info.OPT[k++] = _bNibbleToHex(pinfo->netif.mac[4] >> 4);
        pinfo->dhcp_ctx.rip_info.OPT[k++] = _bNibbleToHex(pinfo->netif.mac[4] & 0x0F);
        pinfo->dhcp_ctx.rip_info.OPT[k++] = _bNibbleToHex(pinfo->netif.mac[5] >> 4);
        pinfo->dhcp_ctx.rip_info.OPT[k++] = _bNibbleToHex(pinfo->netif.mac[5] & 0x0F);
    }

    pinfo->dhcp_ctx.rip_info.OPT[k++] = dhcpParamRequest;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = 0x06;  // length of request
    pinfo->dhcp_ctx.rip_info.OPT[k++] = subnetMask;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = routersOnSubnet;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = dns;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = domainName;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = dhcpT1value;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = dhcpT2value;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = endOption;

    for (i = k; i < OPT_SIZE; i++)
        pinfo->dhcp_ctx.rip_info.OPT[i] = 0;
    // send broadcasting packet
    if (bConnect(pinfo->dhcp_ctx.sockfd, "255.255.255.255", DHCP_SERVER_PORT) < 0)
    {
        return;
    }
#if DHCP_MODULE_DEBUG_EN
    b_log("> Send DHCP_DISCOVER\r\n");
#endif
    if (bSend(pinfo->dhcp_ctx.sockfd, (uint8_t *)&pinfo->dhcp_ctx.rip_info, RIP_MSG_SIZE, NULL) <=
        0)
    {
        return;
    }
}

static void bDhcpSendRequest(bTcpIpInfo_t *pinfo)
{
    int      i;
    uint8_t  ip[4];
    uint16_t k = 0;

    _bDhcpMakeMsg(pinfo);

    if (pinfo->dhcp_ctx.state == STATE_DHCP_LEASED || pinfo->dhcp_ctx.state == STATE_DHCP_REREQUEST)
    {
        *((uint8_t *)(&pinfo->dhcp_ctx.rip_info.flags))     = ((DHCP_FLAGSUNICAST & 0xFF00) >> 8);
        *((uint8_t *)(&pinfo->dhcp_ctx.rip_info.flags) + 1) = (DHCP_FLAGSUNICAST & 0x00FF);
        pinfo->dhcp_ctx.rip_info.ciaddr[0]                  = pinfo->dhcp_ctx.allocated_ip[0];
        pinfo->dhcp_ctx.rip_info.ciaddr[1]                  = pinfo->dhcp_ctx.allocated_ip[1];
        pinfo->dhcp_ctx.rip_info.ciaddr[2]                  = pinfo->dhcp_ctx.allocated_ip[2];
        pinfo->dhcp_ctx.rip_info.ciaddr[3]                  = pinfo->dhcp_ctx.allocated_ip[3];
        ip[0]                                               = pinfo->dhcp_ctx.sip[0];
        ip[1]                                               = pinfo->dhcp_ctx.sip[1];
        ip[2]                                               = pinfo->dhcp_ctx.sip[2];
        ip[3]                                               = pinfo->dhcp_ctx.sip[3];
    }
    else
    {
        ip[0] = 255;
        ip[1] = 255;
        ip[2] = 255;
        ip[3] = 255;
    }
#if DHCP_MODULE_DEBUG_EN
    b_log("> bDhcpSendRequest\r\n");
    b_log_hex(pinfo->dhcp_ctx.rip_info.ciaddr, 4);
    b_log_hex(ip, 4);
#endif
    k = 4;  // because MAGIC_COOKIE already made by makeDHCPMSG()

    // Option Request Param.
    pinfo->dhcp_ctx.rip_info.OPT[k++] = dhcpMessageType;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = 0x01;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = DHCP_REQUEST;

    pinfo->dhcp_ctx.rip_info.OPT[k++] = dhcpClientIdentifier;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = 0x07;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = 0x01;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->netif.mac[0];
    pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->netif.mac[1];
    pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->netif.mac[2];
    pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->netif.mac[3];
    pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->netif.mac[4];
    pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->netif.mac[5];

    if (ip[3] == 255)  // if(dchp_state == STATE_DHCP_LEASED || dchp_state == DHCP_REREQUEST_STATE)
    {
        pinfo->dhcp_ctx.rip_info.OPT[k++] = dhcpRequestedIPaddr;
        pinfo->dhcp_ctx.rip_info.OPT[k++] = 0x04;
        pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->dhcp_ctx.allocated_ip[0];
        pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->dhcp_ctx.allocated_ip[1];
        pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->dhcp_ctx.allocated_ip[2];
        pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->dhcp_ctx.allocated_ip[3];

        pinfo->dhcp_ctx.rip_info.OPT[k++] = dhcpServerIdentifier;
        pinfo->dhcp_ctx.rip_info.OPT[k++] = 0x04;
        pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->dhcp_ctx.sip[0];
        pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->dhcp_ctx.sip[1];
        pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->dhcp_ctx.sip[2];
        pinfo->dhcp_ctx.rip_info.OPT[k++] = pinfo->dhcp_ctx.sip[3];
    }

    // host name
    pinfo->dhcp_ctx.rip_info.OPT[k++] = hostName;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = 0;  // length of hostname
    for (i = 0; HOST_NAME[i] != 0; i++)
        pinfo->dhcp_ctx.rip_info.OPT[k++] = HOST_NAME[i];
    pinfo->dhcp_ctx.rip_info.OPT[k++]             = _bNibbleToHex(pinfo->netif.mac[3] >> 4);
    pinfo->dhcp_ctx.rip_info.OPT[k++]             = _bNibbleToHex(pinfo->netif.mac[3]);
    pinfo->dhcp_ctx.rip_info.OPT[k++]             = _bNibbleToHex(pinfo->netif.mac[4] >> 4);
    pinfo->dhcp_ctx.rip_info.OPT[k++]             = _bNibbleToHex(pinfo->netif.mac[4]);
    pinfo->dhcp_ctx.rip_info.OPT[k++]             = _bNibbleToHex(pinfo->netif.mac[5] >> 4);
    pinfo->dhcp_ctx.rip_info.OPT[k++]             = _bNibbleToHex(pinfo->netif.mac[5]);
    pinfo->dhcp_ctx.rip_info.OPT[k - (i + 6 + 1)] = i + 6;  // length of hostname

    pinfo->dhcp_ctx.rip_info.OPT[k++] = dhcpParamRequest;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = 0x08;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = subnetMask;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = routersOnSubnet;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = dns;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = domainName;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = dhcpT1value;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = dhcpT2value;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = performRouterDiscovery;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = staticRoute;
    pinfo->dhcp_ctx.rip_info.OPT[k++] = endOption;

    for (i = k; i < OPT_SIZE; i++)
        pinfo->dhcp_ctx.rip_info.OPT[i] = 0;

    uint8_t ip_addr[16];
    memset(ip_addr, 0, sizeof(ip_addr));

    sprintf((char *)ip_addr, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
#if DHCP_MODULE_DEBUG_EN
    b_log("> Send DHCP_REQUEST %s\r\n", ip_addr);
#endif
    if (bConnect(pinfo->dhcp_ctx.sockfd, (char *)ip_addr, DHCP_SERVER_PORT) < 0)
    {
        return;
    }
    if (bSend(pinfo->dhcp_ctx.sockfd, (uint8_t *)&pinfo->dhcp_ctx.rip_info, RIP_MSG_SIZE, NULL) <=
        0)
    {
        return;
    }
}

static uint8_t _bDhcpCheckTimeout(bTcpIpInfo_t *pinfo)
{
    uint8_t ret = DHCP_RUNNING;
    if (pinfo->dhcp_ctx.retry_times < MAX_DHCP_RETRY)
    {
        if (pinfo->dhcp_ctx.next_tick < bHalGetSysTickPlus())
        {
            switch (pinfo->dhcp_ctx.state)
            {
                case STATE_DHCP_DISCOVER:
                    _bDhcpSendDiscover(pinfo);
                    break;

                case STATE_DHCP_REQUEST:
                    bDhcpSendRequest(pinfo);
                    break;

                case STATE_DHCP_REREQUEST:
                    bDhcpSendRequest(pinfo);
                    break;

                default:
                    break;
            }
            pinfo->dhcp_ctx.next_tick = bHalGetSysTickPlus() + MS2TICKS(DHCP_WAIT_TIME * 1000);
            pinfo->dhcp_ctx.retry_times++;
        }
    }
    else
    {  // timeout occurred
        switch (pinfo->dhcp_ctx.state)
        {
            case STATE_DHCP_DISCOVER:
                pinfo->dhcp_ctx.state = STATE_DHCP_INIT;
                ret                   = DHCP_FAILED;
                break;
            case STATE_DHCP_REQUEST:
            case STATE_DHCP_REREQUEST:
                _bDhcpSendDiscover(pinfo);
                pinfo->dhcp_ctx.state = STATE_DHCP_DISCOVER;
                break;
            default:
                break;
        }
        bDhcpTimeoutReset(pinfo);
    }
    return ret;
}

static int8_t _bDhcpCheckLeasedIP(bTcpIpInfo_t *pinfo)
{
    return 1;
}

static void _bDhcpSetIp(bTcpIpInfo_t *pinfo)
{
    uint32_t ip, gw, mask, dns;
    ip = pinfo->dhcp_ctx.allocated_ip[0];
    ip <<= 8;
    ip |= pinfo->dhcp_ctx.allocated_ip[1];
    ip <<= 8;
    ip |= pinfo->dhcp_ctx.allocated_ip[2];
    ip <<= 8;
    ip |= pinfo->dhcp_ctx.allocated_ip[3];

    gw = pinfo->dhcp_ctx.allocated_gw[0];
    gw <<= 8;
    gw |= pinfo->dhcp_ctx.allocated_gw[1];
    gw <<= 8;
    gw |= pinfo->dhcp_ctx.allocated_gw[2];
    gw <<= 8;
    gw |= pinfo->dhcp_ctx.allocated_gw[3];

    mask = pinfo->dhcp_ctx.allocated_mask[0];
    mask <<= 8;
    mask |= pinfo->dhcp_ctx.allocated_mask[1];
    mask <<= 8;
    mask |= pinfo->dhcp_ctx.allocated_mask[2];
    mask <<= 8;
    mask |= pinfo->dhcp_ctx.allocated_mask[3];

    dns = pinfo->dhcp_ctx.allocated_dns[0];
    dns <<= 8;
    dns |= pinfo->dhcp_ctx.allocated_dns[1];
    dns <<= 8;
    dns |= pinfo->dhcp_ctx.allocated_dns[2];
    dns <<= 8;
    dns |= pinfo->dhcp_ctx.allocated_dns[3];
    pinfo->stack_if.set_ip(ip, mask, gw, &pinfo->netif);
    // M-NEW-4 fix: DHCP 返回 0.0.0.0 表示服务器不提供 DNS, 不能直接覆盖 bTcpIpDNS[2].
    // 否则用户自定义 DNS (静态配置 / 前一次 DHCP 拿到的) 会被 0 冲掉, 后续解析全部失败.
    // 校验 dns != 0 才写入.
    if (dns != 0)
    {
        bTcpIpDNS[2] = dns;
    }
    pinfo->ip_info.ipaddr      = ip;
    pinfo->ip_info.gateway     = gw;
    pinfo->ip_info.netmask     = mask;
    pinfo->ip_info.get_ip_done = 1;
#if DHCP_MODULE_DEBUG_EN
    b_log("dhcp set ip:%x %x %x %x\r\n", ip, gw, mask, dns);
#endif
}

static uint8_t _bDhcpLoopHandle(bTcpIpInfo_t *pinfo)
{
    uint8_t type;
    uint8_t ret;
    if (pinfo->dhcp_ctx.state == STATE_DHCP_STOP)
        return DHCP_STOPPED;

    if (SOCKFD_IS_INVALID(pinfo->dhcp_ctx.sockfd))
    {
        pinfo->dhcp_ctx.sockfd =
            bSocket2(pinfo->netif.dev_no, B_TRANS_CONN_UDP, _bDhcpTransCb, NULL);
        if (!SOCKFD_IS_INVALID(pinfo->dhcp_ctx.sockfd))
        {
            if (bBind(pinfo->dhcp_ctx.sockfd, DHCP_CLIENT_PORT) < 0)
            {
                return DHCP_FAILED;
            }
        }
        else
        {
            b_log_e("dhcp socket create failed! %d \r\n", pinfo->dhcp_ctx.sockfd);
            return DHCP_FAILED;
        }
    }
    ret  = DHCP_RUNNING;
    type = bDhcpParseMsg(pinfo);

    switch (pinfo->dhcp_ctx.state)
    {
        case STATE_DHCP_INIT:
            pinfo->dhcp_ctx.allocated_ip[0] = 0;
            pinfo->dhcp_ctx.allocated_ip[1] = 0;
            pinfo->dhcp_ctx.allocated_ip[2] = 0;
            pinfo->dhcp_ctx.allocated_ip[3] = 0;
            _bDhcpSendDiscover(pinfo);
            pinfo->dhcp_ctx.state = STATE_DHCP_DISCOVER;
            break;
        case STATE_DHCP_DISCOVER:
            if (type == DHCP_OFFER)
            {
#if DHCP_MODULE_DEBUG_EN
                b_log("> Receive DHCP_OFFER\r\n");
#endif
                pinfo->dhcp_ctx.allocated_ip[0] = pinfo->dhcp_ctx.rip_info.yiaddr[0];
                pinfo->dhcp_ctx.allocated_ip[1] = pinfo->dhcp_ctx.rip_info.yiaddr[1];
                pinfo->dhcp_ctx.allocated_ip[2] = pinfo->dhcp_ctx.rip_info.yiaddr[2];
                pinfo->dhcp_ctx.allocated_ip[3] = pinfo->dhcp_ctx.rip_info.yiaddr[3];
                bDhcpSendRequest(pinfo);
                pinfo->dhcp_ctx.state = STATE_DHCP_REQUEST;
            }
            else
                ret = _bDhcpCheckTimeout(pinfo);
            break;

        case STATE_DHCP_REQUEST:
            if (type == DHCP_ACK)
            {
#if DHCP_MODULE_DEBUG_EN
                b_log("> Receive DHCP_ACK\r\n");
#endif
                if (_bDhcpCheckLeasedIP(pinfo))
                {
                    // Network info assignment from DHCP
                    _bDhcpSetIp(pinfo);
                    pinfo->dhcp_ctx.state = STATE_DHCP_LEASED;
                }
                else
                {
                    // IP address conflict occurred
                    pinfo->dhcp_ctx.state = STATE_DHCP_INIT;
                }
                bDhcpTimeoutReset(pinfo);
            }
            else if (type == DHCP_NAK)
            {
#if DHCP_MODULE_DEBUG_EN
                b_log("> Receive DHCP_NACK\r\n");
#endif
                bDhcpTimeoutReset(pinfo);
                pinfo->dhcp_ctx.state = STATE_DHCP_DISCOVER;
            }
            else
                ret = _bDhcpCheckTimeout(pinfo);
            break;

        case STATE_DHCP_LEASED:
            ret = DHCP_IP_LEASED;
            if ((pinfo->dhcp_ctx.dhcp_lease_time != 0) &&
                ((bHalGetSysTickPlus() + (pinfo->dhcp_ctx.dhcp_lease_time / 2)) >=
                 (pinfo->dhcp_ctx.dhcp_lease_expire_tick)))
            {
#if DHCP_MODULE_DEBUG_EN
                b_log("> Maintains the IP address \r\n");
#endif
                type                                = 0;
                pinfo->dhcp_ctx.old_allocated_ip[0] = pinfo->dhcp_ctx.allocated_ip[0];
                pinfo->dhcp_ctx.old_allocated_ip[1] = pinfo->dhcp_ctx.allocated_ip[1];
                pinfo->dhcp_ctx.old_allocated_ip[2] = pinfo->dhcp_ctx.allocated_ip[2];
                pinfo->dhcp_ctx.old_allocated_ip[3] = pinfo->dhcp_ctx.allocated_ip[3];
                pinfo->dhcp_ctx.xid++;
                bDhcpSendRequest(pinfo);
                bDhcpTimeoutReset(pinfo);
                pinfo->dhcp_ctx.state = STATE_DHCP_REREQUEST;
            }
            break;

        case STATE_DHCP_REREQUEST:
            ret = DHCP_IP_LEASED;
            if (type == DHCP_ACK)
            {
                pinfo->dhcp_ctx.retry_times = 0;
                if (pinfo->dhcp_ctx.old_allocated_ip[0] != pinfo->dhcp_ctx.allocated_ip[0] ||
                    pinfo->dhcp_ctx.old_allocated_ip[1] != pinfo->dhcp_ctx.allocated_ip[1] ||
                    pinfo->dhcp_ctx.old_allocated_ip[2] != pinfo->dhcp_ctx.allocated_ip[2] ||
                    pinfo->dhcp_ctx.old_allocated_ip[3] != pinfo->dhcp_ctx.allocated_ip[3])
                {
                    ret = DHCP_IP_CHANGED;
                    _bDhcpSetIp(pinfo);
#if DHCP_MODULE_DEBUG_EN
                    b_log(">IP changed.\r\n");
#endif
                }
                else
                {
#if DHCP_MODULE_DEBUG_EN
                    b_log(">IP is continued.\r\n");
#endif
                }
                bDhcpTimeoutReset(pinfo);
                pinfo->dhcp_ctx.state = STATE_DHCP_LEASED;
            }
            else if (type == DHCP_NAK)
            {
#if DHCP_MODULE_DEBUG_EN
                b_log("> Receive DHCP_NACK, Failed to maintain ip\r\n");
#endif
                bDhcpTimeoutReset(pinfo);
                memset(pinfo->dhcp_ctx.allocated_ip, 0, 4);
                memset(pinfo->dhcp_ctx.allocated_gw, 0, 4);
                memset(pinfo->dhcp_ctx.allocated_mask, 0, 4);
                memset(pinfo->dhcp_ctx.allocated_dns, 0, 4);
                pinfo->ip_info.get_ip_done = 0;
                pinfo->dhcp_ctx.state      = STATE_DHCP_DISCOVER;
            }
            else
                ret = _bDhcpCheckTimeout(pinfo);
            break;
        default:
            break;
    }
    return ret;
}

///////////////////////////////////////////////////////////////////////////////
////////////////////       DHCP  END         /////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

static void _bTcpIpEvent(bTrans_t *trans, bTransEvent_t event, void *param)
{
    if (trans == NULL || trans->callback == NULL)
    {
        return;
    }
    trans->callback(event, param, trans->cb_arg);
}

static void _bTcpIpTransState(bTrans_t *trans, int state)
{
    if (trans == NULL || trans->state == state)
    {
        return;
    }
    // HIGH-TCP-2 fix: bSocketStateStr[] 长度有限, state 越界会 OOB 读.
    // 防御: 越界 state 不打日志, 但仍更新 trans->state + dispatch event.
    if (trans->state >= 0 && trans->state < (int)(sizeof(bSocketStateStr) / sizeof(bSocketStateStr[0])) &&
        state >= 0 && state < (int)(sizeof(bSocketStateStr) / sizeof(bSocketStateStr[0])))
    {
        b_log("trans[%p] state:%s --> %s\r\n", trans, bSocketStateStr[trans->state],
              bSocketStateStr[state]);
    }
    trans->state = state;
    if (state == B_SOCKET_STATE_CONNECTED)
    {
        _bTcpIpEvent(trans, B_TRANS_CONNECTED, trans);
    }
}

static void _bDhcpHandler(bTcpIpInfo_t *pinfo)
{
    if ((pinfo->ip_info.ignore_ip == 0) && (pinfo->ip_info.is_dhcp))
    {
        if (pinfo->dhcp_ctx.state == STATE_DHCP_STOP)
        {
            _bDhcpRequestInit(pinfo);
        }
        _bDhcpLoopHandle(pinfo);
    }
}

static bTrans_t *_bTransFindNodeByPcb(void *pcb)
{
    if (list_empty(&bSocketHead))
    {
        return NULL;
    }
    struct list_head *pos = NULL, *n = NULL;
    bTrans_t         *ptrans = NULL;
    list_for_each_safe(pos, n, &bSocketHead)
    {
        ptrans = list_entry(pos, bTrans_t, node);
        if (ptrans->pcb == pcb)
        {
            return ptrans;
        }
    }
    return NULL;
}

#if (defined(_TCPIP_SEND_BUF_ENABLE) && (_TCPIP_SEND_BUF_ENABLE == 1))

static int _bTransSendData(bTrans_t *ptrans)
{
    bTcpIpStackIf_t *pstack_if = NULL;
    uint8_t         *pbuf      = NULL;
    uint16_t         len       = 0;
    int              retval    = 0;
    if (ptrans->send_busy)
    {
        return -1;
    }
    if (ptrans)
    {
        pstack_if             = (bTcpIpStackIf_t *)ptrans->stack_if;
        bTcpIpNetif_t *pnetif = (bTcpIpNetif_t *)ptrans->netif;
        if (pstack_if)
        {
            retval = _bTransPcbGetPcbData(&ptrans->send_head, &pbuf, &len);
            if (retval < 0 || pbuf == NULL || len == 0)
            {
                return -2;
            }
            if (ptrans->type == B_TRANS_CONN_TCP && pstack_if->tcp.send)
            {
                if (pstack_if->tcp.send(ptrans->pcb, pbuf, len) > 0)
                {
                    ptrans->send_busy = 1;
                }
            }
            if (ptrans->type == B_TRANS_CONN_UDP && pstack_if->udp.send)
            {
                if (pstack_if->udp.send(pnetif->private, ptrans->pcb, pbuf, len) > 0)
                {
                    ptrans->send_busy = 1;
                }
            }
        }
    }
    if (ptrans->send_busy)
    {
        return 0;
    }
    return -1;
}

#endif

static void _bSocketClearDataBuffer(bTrans_t *ptrans)
{
#if (defined(_TCPIP_STACK_LWIP_ENABLE) && (_TCPIP_STACK_LWIP_ENABLE == 1))
    if (ptrans->p)
    {
        pbuf_free(ptrans->p);
        ptrans->p           = NULL;
        ptrans->read_offset = 0;
    }
#endif

#if ((defined(_TCPIP_SEND_BUF_ENABLE) && (_TCPIP_SEND_BUF_ENABLE == 1)) || \
     (defined(_TCPIP_RECV_BUF_ENABLE) && (_TCPIP_RECV_BUF_ENABLE == 1)))
    _bTransPcbClear(ptrans);
#endif
}

static void _bSocketHandler()
{
    bTcpIpStackIf_t *pstack_if  = NULL;
    bDnsResult_t     dns_result = DNS_PARSE_FAILED;
    if (list_empty(&bSocketHead))
    {
        return;
    }
    struct list_head *pos = NULL, *n = NULL;
    bTrans_t         *ptrans = NULL;
    list_for_each_safe(pos, n, &bSocketHead)
    {
        ptrans    = list_entry(pos, bTrans_t, node);
        pstack_if = (bTcpIpStackIf_t *)ptrans->stack_if;
        if (ptrans->state == B_SOCKET_STATE_DNS)
        {
            dns_result = _bDnsParse((char *)ptrans->remote_url, &ptrans->remote_ip, 0);
            if (dns_result == DNS_PARSE_FAILED)
            {
                _bTcpIpTransState(ptrans, B_SOCKET_STATE_DNS_FAIL);
                _bTcpIpEvent(ptrans, B_TRANS_DISCONNECT, ptrans);
            }
            else if (dns_result == DNS_PARSE_SUCCESS)
            {
                _bTcpIpTransState(ptrans, B_SOCKET_STATE_CONNECT);
            }
        }
        else if (ptrans->state == B_SOCKET_STATE_CONNECT)
        {
            uint8_t ip_addr[16];
            memset(ip_addr, 0, sizeof(ip_addr));
            _bIpInt2Str((char *)ip_addr, ptrans->remote_ip);
            if (bConnect((bSocketFd_t)(intptr_t)ptrans, (char *)ip_addr, ptrans->remote_port) < 0)
            {
                return;
            }
        }
        else if (ptrans->state == B_SOCKET_STATE_WAIT_DISCONNECT)
        {
            _bTcpIpTransState(ptrans, B_SOCKET_STATE_DISCONNECT);
            if (ptrans->type == B_TRANS_CONN_UDP)
            {
                pstack_if->udp.delete(ptrans->pcb);
            }
            else
            {
                pstack_if->tcp.delete(ptrans->pcb);
            }
            list_del(&ptrans->node);
            _bTcpIpEvent(ptrans, B_TRANS_DISCONNECT, ptrans);
            _bSocketClearDataBuffer(ptrans);
            bFree(ptrans);
        }
        else if (ptrans->state == B_SOCKET_STATE_CONNECTED)
        {
#if (defined(_TCPIP_SEND_BUF_ENABLE) && (_TCPIP_SEND_BUF_ENABLE == 1))
            if (TCPIP_STACK_OPT_IS_NO_BUFFER(ptrans->stack_opt))
            {
                _bTransSendData(ptrans);
            }
#endif
        }
    }
}

#if (defined(_TCPIP_STACK_LWIP_ENABLE) && (_TCPIP_STACK_LWIP_ENABLE == 1))

static pTcpIpCallback_t bLwipEventCb    = NULL;
static void            *bLwipEventCbArg = NULL;

uint32_t sys_now(void)
{
    uint32_t tick = bHalGetSysTick();
    return TICKS2MS(tick);
}

static int _bNetifMalloc(uint16_t len, void **p)
{
    struct pbuf *tmp_buf = NULL;
    if (p == NULL || len == 0)
    {
        return -1;
    }
    tmp_buf = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (tmp_buf == NULL)
    {
        return -1;
    }
    *p = tmp_buf;
    return 0;
}

static int _bNetifFree(void *p)
{
    struct pbuf *tmp_buf = (struct pbuf *)p;
    if (tmp_buf == NULL)
    {
        return -1;
    }
    pbuf_free(tmp_buf);
    return 0;
}

static int _bNetifBufNext(void *current_p, void **p)
{
    struct pbuf *tmp_buf = (struct pbuf *)current_p;
    if (current_p == NULL || p == 0)
    {
        return -1;
    }
    *p = tmp_buf->next;
    return 0;
}

static int _bNetifBufPayload(void *p, void **payload, uint32_t *payload_len)
{
    struct pbuf *tmp_buf = (struct pbuf *)p;
    if (payload == NULL || p == 0 || payload_len == NULL)
    {
        return -1;
    }
    *payload     = tmp_buf->payload;
    *payload_len = tmp_buf->len;
    return 0;
}

static err_t _bNetifLinkoutput(struct netif *netif, struct pbuf *p)
{
    int fd = -1;
    if (netif == NULL || p == NULL)
    {
        return ERR_ARG;
    }
    bTcpIpInfo_t *pinfo = (bTcpIpInfo_t *)netif->state;
    if (pinfo == NULL)
    {
        return ERR_IF;
    }
    fd = bOpen(pinfo->netif.dev_no, BCORE_FLAG_RW);
    if (fd >= 0)
    {
        if (bWrite(fd, (uint8_t *)p, p->tot_len) < 0)
        {
            bClose(fd);
            return ERR_IF;
        }
        bClose(fd);
    }
    return ERR_OK;
}

static err_t _bNetifInit(struct netif *netif)
{
    if (netif == NULL)
    {
        return ERR_ARG;
    }
    bTcpIpInfo_t *pinfo = (bTcpIpInfo_t *)netif->state;
    if (pinfo == NULL)
    {
        return ERR_IF;
    }
    /* Initialize interface hostname */
#if LWIP_NETIF_HOSTNAME
    netif->hostname = "babyos";
#endif
    netif->name[0] = (pinfo->netif.dev_no % 10) + '0';
    netif->name[1] = ((pinfo->netif.dev_no % 100) / 10) + '0';

    netif->output     = etharp_output;
    netif->linkoutput = _bNetifLinkoutput;

    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    netif->hwaddr[0]  = pinfo->netif.mac[0];
    netif->hwaddr[1]  = pinfo->netif.mac[1];
    netif->hwaddr[2]  = pinfo->netif.mac[2];
    netif->hwaddr[3]  = pinfo->netif.mac[3];
    netif->hwaddr[4]  = pinfo->netif.mac[4];
    netif->hwaddr[5]  = pinfo->netif.mac[5];

    uint8_t link_state = 0;
    int     fd         = -1;
    fd                 = bOpen(pinfo->netif.dev_no, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return ERR_IF;
    }
    if (bCtl(fd, bCMD_GET_LINK_STATE, &link_state) != 0)
    {
        bClose(fd);
        return ERR_IF;
    }
    bClose(fd);
    if (link_state)
    {
        netif->flags |= NETIF_FLAG_LINK_UP;
    }
    netif->mtu = 1500;
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    return ERR_OK;
}

static int _bStackLwipSetIp(uint32_t ip, uint32_t mask, uint32_t gateway, bTcpIpNetif_t *pnetif)
{
    ip4_addr_t    ip_addr, gw_addr, mask_addr;
    struct netif *real_netif = (struct netif *)pnetif->private;
    if (real_netif == NULL)
    {
        return -1;
    }
    ip4_addr_set_u32(&ip_addr, PP_HTONL(ip));
    ip4_addr_set_u32(&mask_addr, PP_HTONL(mask));
    ip4_addr_set_u32(&gw_addr, PP_HTONL(gateway));
    netif_set_addr(real_netif, &ip_addr, &mask_addr, &gw_addr);
    return 0;
}

static int _bStackLwipSetMac(uint8_t mac[6], bTcpIpNetif_t *pnetif)
{
    struct netif *real_netif = (struct netif *)pnetif->private;
    if (real_netif == NULL)
    {
        return -1;
    }
    memcpy(real_netif->hwaddr, mac, 6);
    real_netif->hwaddr_len = 6;
    return 0;
}

static int _bStackLwipSetDefaultNetif(bTcpIpNetif_t *pnetif)
{
    struct netif *real_netif = (struct netif *)pnetif->private;
    if (real_netif == NULL)
    {
        return -1;
    }
    netif_set_default(real_netif);
    return 0;
}

static int _bLwipTcpBind(void *pcb, uint16_t port)
{
    return tcp_bind(pcb, IP4_ADDR_ANY, port);
}

static void _bTcpErrorFn(void *arg, err_t err)
{
    b_log_e("tcp error %d\r\n", err);
    if (err == ERR_ISCONN)
    {
        B_SAFE_INVOKE(bLwipEventCb, B_TCPIP_E_CONNECTED, arg, bLwipEventCbArg);
    }
    else if (err == ERR_ALREADY)
    {
        ;
    }
    else
    {
        B_SAFE_INVOKE(bLwipEventCb, B_TCPIP_E_DISCONNECT, arg, bLwipEventCbArg);
    }
}

static err_t _bTcpConnectFn(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    B_SAFE_INVOKE(bLwipEventCb, B_TCPIP_E_CONNECTED, tpcb, bLwipEventCbArg);
    return ERR_OK;
}

static err_t _bTcpSendFn(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    bTcpIpSendDoneArg_t sendone_arg;
    sendone_arg.pcb = tpcb;
    sendone_arg.len = len;
    B_SAFE_INVOKE(bLwipEventCb, B_TCPIP_E_SEND_DONE, &sendone_arg, bLwipEventCbArg);
    return ERR_OK;
}

static err_t _bTcpRecvFn(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    bTcpIpNewDataArg_t newdata;
    if (p == NULL)
    {
        B_SAFE_INVOKE(bLwipEventCb, B_TCPIP_E_DISCONNECT, tpcb, bLwipEventCbArg);
    }
    else
    {
        newdata.pcb     = tpcb;
        newdata.pbuf    = (uint8_t *)p;
        newdata.len     = p->tot_len;
        newdata.release = NULL;
        B_SAFE_INVOKE(bLwipEventCb, B_TCPIP_E_NEW_DATA, &newdata, bLwipEventCbArg);
    }
    return ERR_OK;
}

static int _bLwipTcpConnect(void *pcb, uint32_t addr, uint16_t port)
{
    ip_addr_t ipaddr;
    err_t     err;

    if (pcb == NULL)
    {
        return -1;
    }

    tcp_arg(pcb, pcb);
    tcp_err(pcb, _bTcpErrorFn);
    tcp_sent(pcb, _bTcpSendFn);
    tcp_recv(pcb, _bTcpRecvFn);

    ip4_addr_set_u32(&ipaddr, PP_HTONL(addr));
    err = tcp_connect(pcb, &ipaddr, port, _bTcpConnectFn);

    if (err == ERR_OK)
    {
        // SYN 已入队，状态进入 SYN_SENT；连接结果由 _bTcpConnectFn / _bTcpErrorFn 回调。
        B_SAFE_INVOKE(bLwipEventCb, B_TCPIP_E_CONNECTING, pcb, bLwipEventCbArg);
        return 0;
    }

    // 同步失败 (ERR_MEM / ERR_RTE / ERR_USE / ERR_ISCONN / ...):
    // lwIP 不会触发 connected/err 回调，必须靠返回值让调用方感知。
    // 这里不主动 dispatch DISCONNECT，避免与调用方后续 bShutdown() 产生的 DISCONNECT
    // 重复回调；调用方在 bConnect() 返回 -1 后应走正常 bShutdown() 清理路径。
    return -1;
}

static int _bLwipTcpDelete(void *pcb)
{
    tcp_err(pcb, NULL);
    tcp_arg(pcb, NULL);
    tcp_close(pcb);
    return 0;
}

static int _bLwipTcpSend(void *pcb, const uint8_t *pbuf, uint16_t len)
{
    err_t    err;
    uint16_t writeable_len = tcp_sndbuf((struct tcp_pcb *)pcb);
    writeable_len          = (writeable_len > len) ? len : writeable_len;
    err                    = tcp_write(pcb, pbuf, writeable_len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK)
    {
        // H-NEW-11 fix: tcp_write 仅把数据塞入发送队列, 必须显式调 tcp_output
        // 才能立即触发 lwIP 把 segment 发出去. 否则远程会在 sndbuf 满前都收不到
        // 数据 (PC/NB 上的 TCP 延迟 ack 60ms+ 触发, 用户体验差).
        // 仅在 writeable_len > 0 时调用, 避免空帧 flush.
        if (writeable_len > 0)
        {
            tcp_output(pcb);
        }
        return writeable_len;
    }
    return 0;
}

static int _bLwipUdpBind(void *pcb, uint16_t port)
{
    err_t err = udp_bind(pcb, IP4_ADDR_ANY, port);
    return (err == ERR_OK) ? 0 : -1;
}

static void _bUdpRecvFn(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                        u16_t port)
{
    bTcpIpNewDataArg_t newdata;
    if (p == NULL)
    {
        B_SAFE_INVOKE(bLwipEventCb, B_TCPIP_E_DISCONNECT, pcb, bLwipEventCbArg);
    }
    else
    {
        newdata.pcb     = arg;
        newdata.pbuf    = (uint8_t *)p;
        newdata.len     = p->tot_len;
        newdata.release = NULL;
        B_SAFE_INVOKE(bLwipEventCb, B_TCPIP_E_NEW_DATA, &newdata, bLwipEventCbArg);
    }
}

static int _bLwipUdpConnect(void *pcb, uint32_t ip, uint16_t port)
{
    ip_addr_t ip_addr;
    ip_addr_set_ip4_u32(&ip_addr, PP_HTONL(ip));
    if (pcb == NULL)
    {
        return -1;
    }
    if (ip == 0 || ip == 0xffffffff)
    {
        ip_set_option((struct udp_pcb *)pcb, SOF_BROADCAST);
        ip_addr_set_ip4_u32(&ip_addr, 0);
    }
    udp_recv(pcb, _bUdpRecvFn, pcb);
    err_t err = udp_connect(pcb, &ip_addr, port);
    if (err == ERR_OK)
    {
        B_SAFE_INVOKE(bLwipEventCb, B_TCPIP_E_CONNECTED, pcb, bLwipEventCbArg);
    }
    return (err == ERR_OK) ? 0 : -1;
}

static int _bLwipUdpDelete(void *pcb)
{
    udp_disconnect(pcb);
    udp_remove(pcb);
    return 0;
}

static int _bLwipUdpSend(void *netif, void *pcb, const uint8_t *pbuf, uint16_t len)
{
    if (pcb == NULL || netif == NULL)
    {
        return -1;
    }
    struct netif   *pnetif       = (struct netif *)netif;
    struct udp_pcb *real_udp_pcb = (struct udp_pcb *)pcb;
    struct pbuf    *p            = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    err_t           err;
    if (p == NULL)
    {
        // H-NEW-13 fix: pbuf_alloc 失败时返回 -1 (而不是 0=success-zero), 让调用方
        // 知道本次发送未完成. 此前返 0 会被上层解释为 "成功发了 0 字节",
        // 触发 SEND_DONE, 丢失真实失败信号.
        b_log_e("[lwip udp] pbuf_alloc fail, len=%u\r\n", (unsigned)len);
        return -1;
    }
    memcpy(p->payload, pbuf, len);

    if (ip4_addr_isbroadcast(&real_udp_pcb->remote_ip, pnetif))
    {
        err = udp_sendto_if_src(real_udp_pcb, p, IP_ADDR_BROADCAST, real_udp_pcb->remote_port,
                                pnetif, &real_udp_pcb->local_ip);
    }
    else
    {
        err = udp_send(pcb, p);
    }
    pbuf_free(p);
    if (err == ERR_OK)
    {
        bTcpIpSendDoneArg_t sendone_arg;
        sendone_arg.pcb = pcb;
        sendone_arg.len = len;
        B_SAFE_INVOKE(bLwipEventCb, B_TCPIP_E_SEND_DONE, &sendone_arg, bLwipEventCbArg);
        return (int)len;
    }
    b_log_e("[lwip udp] send err:%d\r\n", err);
    return -1;
}

static int _bLwipUdpRecv(void *pcb, uint8_t *pbuf, uint16_t len)
{
    uint16_t  rlen   = 0;
    bTrans_t *ptrans = NULL;
    if (pcb == NULL)
    {
        return -1;
    }
    ptrans = _bTransFindNodeByPcb(pcb);
    if (ptrans == NULL)
    {
        return -1;
    }
    struct pbuf *tmp_buf = ptrans->p;
    if (tmp_buf)
    {
        rlen = pbuf_copy_partial(tmp_buf, pbuf, len, ptrans->read_offset);
        if (rlen > 0)
        {
            ptrans->read_offset += rlen;
        }
        if (rlen <= 0 || ptrans->read_offset >= tmp_buf->tot_len)
        {
            pbuf_free(tmp_buf);
            ptrans->p           = NULL;
            ptrans->read_offset = 0;
        }
    }
    return rlen;
}

void *_bLwipTcpNew(bTcpIpNetif_t *pnetif)
{
    B_UNUSED(pnetif);
    return tcp_new();
}

void *_bLwipUdpNew(bTcpIpNetif_t *pnetif)
{
    B_UNUSED(pnetif);
    return udp_new();
}

int _bStackLwipSetLinkState(uint8_t state, bTcpIpNetif_t *pnetif)
{
    struct netif *real_netif = (struct netif *)pnetif->private;
    if (real_netif == NULL)
    {
        return -1;
    }
    if (state)
    {
        netif_set_link_up(real_netif);
        netif_set_up(real_netif);
    }
    else
    {
        netif_set_link_down(real_netif);
        netif_set_down(real_netif);
    }
    return 0;
}

void _bStackLwipLoop(bTcpIpNetif_t *netif)
{
    struct pbuf  *p           = NULL;
    err_t         err         = ERR_ARG;
    struct netif *preal_netif = NULL;
    int           fd          = -1;
    if (netif == NULL)
    {
        return;
    }
    if (netif->is_linked)
    {
        p  = NULL;
        fd = bOpen(netif->dev_no, BCORE_FLAG_RW);
        if (fd >= 0)
        {
            if (bRead(fd, (uint8_t *)&p, CONNECT_RECVBUF_MAX) < 0)
            {
                bClose(fd);
                return;
            }
            bClose(fd);
        }
        if (p != NULL)
        {
            // b_log("lwip mac rec:\r\n");
            // b_log_hex((uint8_t *)p->payload, p->len);
            err         = ERR_ARG;
            preal_netif = (struct netif *)netif->private;
            if (preal_netif)
            {
                err = netif_input(p, preal_netif);
            }
            if (err != ERR_OK)
            {
                pbuf_free(p);
                p = NULL;
            }
        }
    }
    sys_check_timeouts();
}

static err_t _bTcpServerAccept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    int               sockfd = -1;
    bTcpIpAccetpArg_t accept_arg;
    struct tcp_pcb   *pcb = (struct tcp_pcb *)arg;
    tcp_arg(newpcb, newpcb);
    tcp_err(newpcb, _bTcpErrorFn);
    tcp_recv(newpcb, _bTcpRecvFn);
    tcp_sent(newpcb, _bTcpSendFn);
    accept_arg.new_pcb       = newpcb;
    accept_arg.netif.private = netif_get_by_index(pcb->netif_idx);
    B_SAFE_INVOKE(bLwipEventCb, B_TCPIP_E_ACCEPT, &accept_arg, bLwipEventCbArg);
    return ERR_OK;
}

void *_bLwipTcpListen(void *pcb, uint16_t backlog)
{
    void *listen_pcb = NULL;
    B_UNUSED(backlog);
    if (pcb == NULL)
    {
        return NULL;
    }
    listen_pcb = tcp_listen(pcb);
    tcp_arg(listen_pcb, listen_pcb);
    tcp_accept(listen_pcb, _bTcpServerAccept);
    return listen_pcb;
}

void *_bLwipUdpListen(void *pcb, uint16_t backlog)
{
    B_UNUSED(backlog);
    if (pcb == NULL)
    {
        return NULL;
    }
    udp_recv(pcb, _bUdpRecvFn, pcb);
    return pcb;
}

static uint8_t _bLwipIsWriteable(void *pcb)
{
    if (pcb == NULL)
    {
        return -1;
    }
    uint16_t writeable_len = tcp_sndbuf((struct tcp_pcb *)pcb);
    return (writeable_len > 0);
}

static void _bLwipCallback(pTcpIpCallback_t cb, void *arg, bTcpIpNetif_t *netif)
{
    B_UNUSED(netif);
    bLwipEventCb    = cb;
    bLwipEventCbArg = arg;
}

#endif

static bTcpIpInfo_t *_bFindNetcard(uint32_t dev_no)
{
    int           i     = 0;
    bTcpIpInfo_t *pinfo = NULL;
    if (bTcpIpCtx.pinfo_table == NULL)
    {
        return NULL;
    }
    for (i = 0; i < bTcpIpCtx.info_number; i++)
    {
        if (bTcpIpCtx.pinfo_table[i].netif.dev_no == dev_no)
        {
            return &bTcpIpCtx.pinfo_table[i];
        }
    }
    return NULL;
}

static bTcpIpInfo_t *_bFindNetcardByPriv(void *priv)
{
    int           i     = 0;
    bTcpIpInfo_t *pinfo = NULL;
    if (bTcpIpCtx.pinfo_table == NULL)
    {
        return NULL;
    }
    for (i = 0; i < bTcpIpCtx.info_number; i++)
    {
        if (bTcpIpCtx.pinfo_table[i].netif.private == priv)
        {
            return &bTcpIpCtx.pinfo_table[i];
        }
    }
    return NULL;
}

static void _bTcpIpCallback(bTcpIpEvent_t event, void *param, void *arg)
{
    bTcpIpInfo_t *tcpip_info = NULL;
    bTrans_t     *ptrans     = NULL;
    if (param == NULL)
    {
        return;
    }
    b_log("tcpip callback: %d\r\n", event);
    if (event == B_TCPIP_E_ACCEPT)
    {
        bTcpIpAccetpArg_t *pinfo     = (bTcpIpAccetpArg_t *)param;
        bTrans_t          *pnewtrans = (bTrans_t *)bCalloc(1, sizeof(bTrans_t));
        b_assert_log(pnewtrans != NULL);
        tcpip_info = _bFindNetcardByPriv(pinfo->netif.private);
        if (tcpip_info == NULL)
        {
            // H-NEW-14 fix: 此前 netif 查找失败直接 return, pnewtrans 泄漏 (1 个 sizeof(bTrans_t)).
            // 找到 netcard 后再填字段并挂链表, 找不到则释放 pcb 和 pnewtrans.
            // 注意: 这里不能直接 tcp_close(new_pcb) — pcb 仍归 lwIP accept 状态机,
            // 等下一轮 sys_check_timeouts/recv 时自然关闭. 先把 bTrans 释放掉.
            bFree(pnewtrans);
            return;
        }
        pnewtrans->pcb       = pinfo->new_pcb;
        pnewtrans->type      = B_TRANS_CONN_TCP;
        pnewtrans->callback  = tcpip_info->trans_listen_cb.cb;
        pnewtrans->cb_arg    = tcpip_info->trans_listen_cb.arg;
        pnewtrans->stack_if  = &tcpip_info->stack_if;
        pnewtrans->stack_opt = tcpip_info->stack_opt;
        pnewtrans->netif     = &tcpip_info->netif;
#if (defined(_TCPIP_RECV_BUF_ENABLE) && (_TCPIP_RECV_BUF_ENABLE == 1))
        INIT_LIST_HEAD(&pnewtrans->recv_head);
#endif
#if (defined(_TCPIP_SEND_BUF_ENABLE) && (_TCPIP_SEND_BUF_ENABLE == 1))
        INIT_LIST_HEAD(&pnewtrans->send_head);
#endif
        _bTcpIpTransState(pnewtrans, B_SOCKET_STATE_CONNECTED);
        list_add_tail(&pnewtrans->node, &bSocketHead);
        B_SAFE_INVOKE(pnewtrans->callback, B_TRANS_ACCEPTED, pnewtrans, pnewtrans->cb_arg);
    }
    else if (event == B_TCPIP_E_NEW_DATA)
    {
        bTcpIpNewDataArg_t *pinfo = (bTcpIpNewDataArg_t *)param;
        if (pinfo)
        {
            b_log("new data:%p %p %d\r\n", pinfo->pcb, pinfo->pbuf, pinfo->len);
        }
        ptrans = _bTransFindNodeByPcb(pinfo->pcb);
#if (defined(_TCPIP_STACK_LWIP_ENABLE) && (_TCPIP_STACK_LWIP_ENABLE == 1))
        // C-NEW-5 Site A/B: ptrans 可能为 NULL (pcb 在 bShutdown 路径已释放).
        // 把外层 if 改成 NULL-aware, 之前 else 分支还 deref ptrans->type (Site A.2) 也修了.
        if (ptrans != NULL && TCPIP_STACK_OPT_IS_USE_LWIP(ptrans->stack_opt))
        {
            if (ptrans->p == NULL)
            {
                ptrans->p           = (struct pbuf *)pinfo->pbuf;
                ptrans->read_offset = 0;
            }
            else
            {
                pbuf_cat(ptrans->p, (struct pbuf *)pinfo->pbuf);
            }
        }
        else if (ptrans == NULL)
        {
            // 没找到匹配的 bTrans, 释放 lwIP 已排队的 pbuf (否则泄漏).
            // 仅 TCP 需要告知 lwIP 已消费 (滑动窗口), UDP 不需要.
            // 这里无法从 pcb 类型直接判断 TCP/UDP (param 只有 pbuf+len),
            // 但 tcp_recved 对非 TCP pcb 是无害的; lwIP 在 2.x 也是宏, 编译期会过滤.
            tcp_recved(pinfo->pcb, pinfo->len);
            pbuf_free((struct pbuf *)(pinfo->pbuf));
            return;
        }
#endif

#if (defined(_TCPIP_RECV_BUF_ENABLE) && (_TCPIP_RECV_BUF_ENABLE == 1))
        // C-NEW-5 Site C: NO_BUFFER 路径同样要先 NULL 校验.
        if (ptrans != NULL && TCPIP_STACK_OPT_IS_NO_BUFFER(ptrans->stack_opt))
        {
            _bTransPcbAddData(&ptrans->recv_head, pinfo->pbuf, pinfo->len, pinfo->release,
                              TCPIP_RECV_BUF_LEN_MAX);
        }
#endif
        if (ptrans != NULL)
        {
            _bTcpIpEvent(ptrans, B_TRANS_NEW_DATA, ptrans);
        }
    }
    else if (event == B_TCPIP_E_SEND_DONE)
    {
        bTcpIpSendDoneArg_t *pinfo = (bTcpIpSendDoneArg_t *)param;
        ptrans                     = _bTransFindNodeByPcb(pinfo->pcb);
#if (defined(_TCPIP_SEND_BUF_ENABLE) && (_TCPIP_SEND_BUF_ENABLE == 1))
        // C-NEW-5 Site D: ptrans 可能是 NULL (pcb 已 bShutdown 释放), 必须先校验.
        if (ptrans != NULL && TCPIP_STACK_OPT_IS_NO_BUFFER(ptrans->stack_opt))
        {
            if (param)
            {
                if (pinfo->len > 0)
                {
                    _bTransPcbDataShiftForward(&ptrans->send_head, pinfo->len);
                }
            }
            ptrans->send_busy = 0;
        }
#endif
        if (ptrans != NULL)
        {
            _bTcpIpEvent(ptrans, B_TRANS_SEND_DONE, ptrans);
        }
    }
    // C-NEW-5 Site E: 之前 "if (ptrans != NULL) return;" 会无差别短路掉
    // CONNECTED / DISCONNECT / CONNECTING 处理 (那些事件本身 ptrans 就还未找到).
    // 改成事件感知的分支: 只有当 event 已是 NEW_DATA / SEND_DONE (在上面处理过)
    // 才提前 return; 否则继续走下面对 CONNECTED 等事件的处理.
    if (event == B_TCPIP_E_NEW_DATA || event == B_TCPIP_E_SEND_DONE)
    {
        return;
    }
    ptrans = _bTransFindNodeByPcb(param);
    if (ptrans == NULL)
    {
        return;
    }
    if (event == B_TCPIP_E_CONNECTED)
    {
        _bTcpIpTransState(ptrans, B_SOCKET_STATE_CONNECTED);
    }
    else if (event == B_TCPIP_E_DISCONNECT)
    {
        _bSocketClearDataBuffer(ptrans);
        _bTcpIpTransState(ptrans, B_SOCKET_STATE_DISCONNECT);
        _bTcpIpEvent(ptrans, B_TRANS_DISCONNECT, ptrans);
    }
    else if (event == B_TCPIP_E_CONNECTING)
    {
        _bTcpIpTransState(ptrans, B_SOCKET_STATE_WAIT_CONNECTED);
    }
}

static void _bNetCardHandler()
{
    int           fd             = -1;
    int           i              = 0;
    int           netcard_update = 0;
    bTcpIpInfo_t *pinfo          = NULL;
    if (bTcpIpCtx.pinfo_table == NULL)
    {
        return;
    }
    netcard_update = 0;
    for (i = 0; i < bTcpIpCtx.info_number; i++)
    {
        pinfo = &bTcpIpCtx.pinfo_table[i];
        if (pinfo->stack_if.tcp.new == NULL && pinfo->stack_if.udp.new == NULL)
        {
            fd = bOpen(pinfo->netif.dev_no, BCORE_FLAG_RW);
            if (fd < 0)
            {
                continue;
            }
#if (defined(_TCPIP_STACK_LWIP_ENABLE) && (_TCPIP_STACK_LWIP_ENABLE == 1))
            bHalBufList_t buf_list;
            buf_list.m_create  = _bNetifMalloc;
            buf_list.m_next    = _bNetifBufNext;
            buf_list.m_payload = _bNetifBufPayload;
            buf_list.m_free    = _bNetifFree;
            if (bCtl(fd, bCMD_REG_BUF_LIST, &buf_list) != 0)
            {
                bClose(fd);
                continue;
            }
#endif
            uint8_t link_state = 0;
            if (0 == bCtl(fd, bCMD_GET_LINK_STATE, &link_state))
            {
                pinfo->netif.is_linked = link_state;
            }
            bMacAddress_t mac_addr;
            if (0 == bCtl(fd, bCMD_GET_MAC_ADDRESS, &mac_addr))
            {
                memcpy(pinfo->netif.mac, mac_addr.address, 6);
            }
            bDriverNetif_t drv_info;
            if (0 == bCtl(fd, bCMD_GET_DRIVER_NETIF, &drv_info))
            {
                pinfo->netif.private = drv_info.private;
            }
            bLinkStateCb_t link_cb;
            link_cb.cb  = _bPhyLinkStateCb;
            link_cb.arg = pinfo;
            if (bCtl(fd, bCMD_REG_LINK_CALLBACK, &link_cb) != 0)
            {
                bClose(fd);
                continue;
            }
            if (bCtl(fd, bCMD_GET_STACK_IF, &pinfo->stack_if) != 0)
            {
                bClose(fd);
                continue;
            }
            bClose(fd);
            fd = -1;
            if (pinfo->stack_if.tcp.new == NULL && pinfo->stack_if.udp.new == NULL)
            {
#if (defined(_TCPIP_STACK_LWIP_ENABLE) && (_TCPIP_STACK_LWIP_ENABLE == 1))
                TCPIP_STACK_OPT_SET_USE_LWIP(pinfo->stack_opt);

                pinfo->stack_if.init         = NULL;
                pinfo->stack_if.loop         = _bStackLwipLoop;
                pinfo->stack_if.reg_callback = _bLwipCallback;

                pinfo->stack_if.set_ip            = _bStackLwipSetIp;
                pinfo->stack_if.set_mac           = _bStackLwipSetMac;
                pinfo->stack_if.set_link_state    = _bStackLwipSetLinkState;
                pinfo->stack_if.set_default_netif = _bStackLwipSetDefaultNetif;

                pinfo->stack_if.tcp.new     = _bLwipTcpNew;
                pinfo->stack_if.tcp.bind    = _bLwipTcpBind;
                pinfo->stack_if.tcp.connect = _bLwipTcpConnect;
                pinfo->stack_if.tcp.delete  = _bLwipTcpDelete;
                pinfo->stack_if.tcp.recv    = NULL;
                pinfo->stack_if.tcp.send    = _bLwipTcpSend;
                pinfo->stack_if.tcp.listen  = _bLwipTcpListen;

                pinfo->stack_if.udp.new     = _bLwipUdpNew;
                pinfo->stack_if.udp.bind    = _bLwipUdpBind;
                pinfo->stack_if.udp.connect = _bLwipUdpConnect;
                pinfo->stack_if.udp.delete  = _bLwipUdpDelete;
                pinfo->stack_if.udp.send    = _bLwipUdpSend;
                pinfo->stack_if.udp.recv    = NULL;
                pinfo->stack_if.udp.listen  = _bLwipUdpListen;

                pinfo->stack_if.is_readable  = NULL;
                pinfo->stack_if.is_writeable = _bLwipIsWriteable;

                pinfo->netif.private = bMalloc(sizeof(struct netif));
                b_assert_log(pinfo->netif.private != NULL);
                ip4_addr_t addr_ip, addr_mask, addr_gw;
                if (pinfo->ip_info.ignore_ip == 0)
                {
                    ip4_addr_set_u32(&addr_ip, PP_HTONL(pinfo->ip_info.ipaddr));
                    ip4_addr_set_u32(&addr_mask, PP_HTONL(pinfo->ip_info.netmask));
                    ip4_addr_set_u32(&addr_gw, PP_HTONL(pinfo->ip_info.gateway));
                }
                netif_add((struct netif *)pinfo->netif.private, &addr_ip, &addr_mask, &addr_gw,
                          pinfo, _bNetifInit, ethernet_input);
#endif
            }
            else
            {
                if (pinfo->stack_if.is_readable == NULL || pinfo->stack_if.is_writeable == NULL)
                {
                    TCPIP_STACK_OPT_SET_NO_BUFFER(pinfo->stack_opt);
                }
            }
            B_SAFE_INVOKE(pinfo->stack_if.init, &pinfo->netif);
            B_SAFE_INVOKE(pinfo->stack_if.reg_callback, _bTcpIpCallback, pinfo, &pinfo->netif);
            netcard_update = 1;
        }
        else
        {
            _bDhcpHandler(pinfo);
            B_SAFE_INVOKE(pinfo->stack_if.loop, &pinfo->netif);
        }
    }
    if (netcard_update)
    {
        _bMainNetcardUpdate();
    }
}

PT_THREAD(_bTcpIpTask)(struct pt *pt, void *arg)
{
    B_TASK_INIT_BEGIN();
    b_log("TCP/IP Task Start\r\n");
#if (defined(_TCPIP_STACK_LWIP_ENABLE) && (_TCPIP_STACK_LWIP_ENABLE == 1))
    lwip_init();
#endif
    B_TASK_INIT_END();
    PT_BEGIN(pt);
    while (1)
    {
        _bNetCardHandler();
        _bSocketHandler();
        _bDnsHandler();
        bTaskDelayMs(pt, 100);
    }
    PT_END(pt);
}

static int _bSocket(bTcpIpInfo_t *pinfo, bTransType_t type, pbTransCb_t cb, void *user_data)
{
    if (cb == NULL || (type != B_TRANS_CONN_TCP && type != B_TRANS_CONN_UDP) || pinfo == NULL)
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)bMalloc(sizeof(bTrans_t));
    if (ptrans == NULL)
    {
        return -2;
    }
    memset(ptrans, 0, sizeof(bTrans_t));
    if (type == B_TRANS_CONN_TCP)
    {
        ptrans->pcb = pinfo->stack_if.tcp.new(&pinfo->netif);
    }
    else
    {
        ptrans->pcb = pinfo->stack_if.udp.new(&pinfo->netif);
    }
    if (ptrans->pcb == NULL)
    {
        bFree(ptrans);
        return -3;
    }
    b_log("socket[%p->%p][%d(l:%d)]:%s ...\r\n", ptrans, ptrans->pcb, pinfo->netif.dev_no,
          pinfo->netif.is_linked, type == B_TRANS_CONN_TCP ? "tcp" : "udp");
    ptrans->type      = type;
    ptrans->callback  = cb;
    ptrans->cb_arg    = user_data;
    ptrans->stack_if  = &pinfo->stack_if;
    ptrans->stack_opt = pinfo->stack_opt;
    ptrans->netif     = &pinfo->netif;
#if (defined(_TCPIP_RECV_BUF_ENABLE) && (_TCPIP_RECV_BUF_ENABLE == 1))
    INIT_LIST_HEAD(&ptrans->recv_head);
#endif
#if (defined(_TCPIP_SEND_BUF_ENABLE) && (_TCPIP_SEND_BUF_ENABLE == 1))
    INIT_LIST_HEAD(&ptrans->send_head);
#endif
    _bTcpIpTransState(ptrans, B_SOCKET_STATE_INIT);
    list_add_tail(&ptrans->node, &bSocketHead);
    return (bSocketFd_t)(intptr_t)ptrans;
}

static uint8_t _bTcpIpTransIsEnable(bTrans_t *ptrans)
{
    if (ptrans == NULL)
    {
        return 0;
    }
    if (ptrans->type == B_TRANS_CONN_UDP && ptrans->local_port == DHCP_CLIENT_PORT)
    {
        return 1;
    }
    bTcpIpNetif_t *pnetif = (bTcpIpNetif_t *)ptrans->netif;
    if (pnetif == NULL || pnetif->is_linked == 0)
    {
        return 0;
    }
    bTcpIpInfo_t *pinfo = _bFindNetcard(pnetif->dev_no);
    if (pinfo == NULL)
    {
        return 0;
    }
    if ((pinfo->ip_info.ipaddr == 0) && (pinfo->ip_info.ignore_ip == 0))
    {
        return 0;
    }
    return 1;
}

/**
 * \}
 */

/**
 * \addtogroup TCPIP_Exported_Functions
 * \{
 */

int bTcpIpInit(const bNetCardInfo_t *pnetcard, uint8_t number)
{
    int i = 0;
    if (pnetcard == NULL || number == 0)
    {
        return -1;
    }
    if (bTcpIpCtx.pinfo_table)
    {
        return -2;
    }
    bTcpIpCtx.pinfo_table = (bTcpIpInfo_t *)bMalloc(number * sizeof(bTcpIpInfo_t));
    b_assert_log(bTcpIpCtx.pinfo_table != NULL);
    bTcpIpCtx.info_number = number;
    for (i = 0; i < number; i++)
    {
        memset(&bTcpIpCtx.pinfo_table[i], 0, sizeof(bTcpIpInfo_t));

        bTcpIpCtx.pinfo_table[i].priority          = pnetcard[i].priority;
        bTcpIpCtx.pinfo_table[i].netif.dev_no      = pnetcard[i].dev_no;
        bTcpIpCtx.pinfo_table[i].ip_info.ignore_ip = pnetcard[i].ignore_ip;
        bTcpIpCtx.pinfo_table[i].ip_info.ipaddr    = pnetcard[i].assigned_ip.ip;
        bTcpIpCtx.pinfo_table[i].ip_info.netmask   = pnetcard[i].assigned_ip.mask;
        bTcpIpCtx.pinfo_table[i].ip_info.gateway   = pnetcard[i].assigned_ip.gateway;
        if (pnetcard[i].assigned_ip.ip == 0 || pnetcard[i].assigned_ip.mask == 0 ||
            pnetcard[i].assigned_ip.gateway == 0)
        {
            bTcpIpCtx.pinfo_table[i].ip_info.is_dhcp = 1;
        }
        bTcpIpCtx.pinfo_table[i].dhcp_ctx.sockfd = -1;
        bTcpIpCtx.pinfo_table[i].dhcp_ctx.state  = STATE_DHCP_STOP;
    }
    memset(bDNSCache, 0, sizeof(bDNSCache));
    bTaskCreate("tcpip", _bTcpIpTask, NULL, &bTcpIpTaskAttr);
    return 0;
}

// REVIEW-V3 #3 fix: 抽出核心 set-IP 逻辑, 接受显式 pinfo.
// 旧 bTcpIpSetIp 只改 bTcpIpCtx.pinfo (活动网卡), 在多网卡设备上调用 /api/eth
// 会改错网卡 (改到 WiFi 而不是 ETH). 新增 ByDevNo 版本给 config-web 用.
static int _bTcpIpSetIpPinfo(bTcpIpInfo_t *pinfo, const char *ip_addr,
                             const char *netmask, const char *gateway)
{
    if (pinfo == NULL)
    {
        return -1;
    }
    if (pinfo->stack_if.set_ip == NULL)
    {
        return -1;
    }
    if (ip_addr == NULL || netmask == NULL || gateway == NULL)
    {
        pinfo->ip_info.is_dhcp     = 1;
        pinfo->ip_info.get_ip_done = 0;
    }
    else
    {
        pinfo->ip_info.is_dhcp = 0;
        pinfo->ip_info.ipaddr  = _bIpStr2Uint32(ip_addr);
        pinfo->ip_info.netmask = _bIpStr2Uint32(netmask);
        pinfo->ip_info.gateway = _bIpStr2Uint32(gateway);
        if (pinfo->ip_info.ipaddr == 0 || pinfo->ip_info.netmask == 0 ||
            pinfo->ip_info.gateway == 0)
        {
            pinfo->ip_info.is_dhcp = 1;
            return -1;
        }
        pinfo->ip_info.get_ip_done = 1;
        bTcpIpDNS[2]               = pinfo->ip_info.gateway;
        pinfo->stack_if.set_ip(pinfo->ip_info.ipaddr, pinfo->ip_info.netmask,
                               pinfo->ip_info.gateway, &pinfo->netif);
    }
    return 0;
}

int bTcpIpSetIp(const char *ip_addr, const char *netmask, const char *gateway)
{
    return _bTcpIpSetIpPinfo(bTcpIpCtx.pinfo, ip_addr, netmask, gateway);
}

int bTcpIpGetIp(char *ipaddr, char *netmask, char *gateway)
{
    bTcpIpInfo_t *pinfo = bTcpIpCtx.pinfo;
    if (pinfo == NULL)
    {
        return -1;
    }
    if (pinfo->ip_info.get_ip_done == 0)
    {
        return -1;
    }
    if (ipaddr != NULL)
    {
        _bIpInt2Str(ipaddr, pinfo->ip_info.ipaddr);
    }
    if (netmask != NULL)
    {
        _bIpInt2Str(netmask, pinfo->ip_info.netmask);
    }
    if (gateway != NULL)
    {
        _bIpInt2Str(gateway, pinfo->ip_info.gateway);
    }
    return 0;
}

int bTcpIpSetMac(const uint8_t mac[6])
{
    int           ret = 0;
    bMacAddress_t mac_addr;
    bTcpIpInfo_t *pinfo = bTcpIpCtx.pinfo;
    if (pinfo == NULL)
    {
        return -1;
    }
    if (pinfo->stack_if.set_mac == NULL)
    {
        return -1;
    }
    memcpy(mac_addr.address, mac, 6);
    int fd = bOpen(pinfo->netif.dev_no, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return -1;
    }
    ret = bCtl(fd, bCMD_SET_MAC_ADDRESS, &mac_addr);
    bClose(fd);
    if (ret == 0)
    {
        memcpy(pinfo->netif.mac, mac, 6);
        pinfo->stack_if.set_mac((uint8_t *)mac, &pinfo->netif);
    }
    return ret;
}

int bTcpIpGetMac(uint8_t mac[6])
{
    bTcpIpInfo_t *pinfo = bTcpIpCtx.pinfo;
    if (pinfo == NULL)
    {
        return -1;
    }
    memcpy(mac, pinfo->netif.mac, 6);
    return 0;
}

uint8_t bTcpIpPhyIsLinked(void)
{
    bTcpIpInfo_t *pinfo = bTcpIpCtx.pinfo;
    if (pinfo == NULL)
    {
        return -1;
    }
    return pinfo->netif.is_linked;
}

bSocketFd_t bSocket2(uint32_t dev_no, bTransType_t type, pbTransCb_t cb, void *user_data)
{
    bTcpIpInfo_t *pinfo = _bFindNetcard(dev_no);
    if (cb == NULL || (type != B_TRANS_CONN_TCP && type != B_TRANS_CONN_UDP) || pinfo == NULL)
    {
        return -1;
    }
    return _bSocket(pinfo, type, cb, user_data);
}

bSocketFd_t bSocket(bTransType_t type, pbTransCb_t cb, void *user_data)
{
    bTcpIpInfo_t *pinfo = bTcpIpCtx.pinfo;
    if (cb == NULL || (type != B_TRANS_CONN_TCP && type != B_TRANS_CONN_UDP) || pinfo == NULL)
    {
        return -1;
    }
    return _bSocket(pinfo, type, cb, user_data);
}

int bConnect(bSocketFd_t sockfd, char *remote, uint16_t port)
{
    bTrans_t *ptrans = (bTrans_t *)(intptr_t)sockfd;
    if (SOCKFD_IS_INVALID(sockfd) || remote == NULL || strlen(remote) > REMOTE_ADDR_LEN_MAX ||
        (_bTcpIpTransIsEnable(ptrans) == 0))
    {
        return -1;
    }
    uint32_t         remote_ip_tmp = 0;
    bDnsResult_t     dns_result    = DNS_PARSE_FAILED;
    bTcpIpStackIf_t *pstack_if     = (bTcpIpStackIf_t *)ptrans->stack_if;
    if (ptrans->pcb == NULL || pstack_if == NULL)
    {
        b_log_e("pcb == NULL ...\r\n");
        return -1;
    }
    b_log("[%p]connect:%s:%d ...\r\n", ptrans, remote, port);
    ptrans->remote_port = port;
    if ((dns_result = _bDnsParse(remote, &remote_ip_tmp, 1)) == DNS_PARSE_FAILED)
    {
        return -1;
    }
    if (dns_result == DNS_PARSE_ONGING)
    {
        b_log("dns:%s\r\n", remote);
        memset(ptrans->remote_url, 0, REMOTE_ADDR_LEN_MAX);
        memcpy(ptrans->remote_url, remote, strlen(remote));
        _bTcpIpTransState(ptrans, B_SOCKET_STATE_DNS);
        return 0;
    }
    // b_log("connect %s %x %d ...\r\n", remote, remote_ip_tmp, port);
    ptrans->remote_ip = remote_ip_tmp;
    if (ptrans->type == B_TRANS_CONN_TCP)
    {
        if (pstack_if->tcp.connect(ptrans->pcb, ptrans->remote_ip, ptrans->remote_port) < 0)
        {
            return -1;
        }
    }
    else if (ptrans->type == B_TRANS_CONN_UDP)
    {
        if (pstack_if->udp.connect(ptrans->pcb, ptrans->remote_ip, ptrans->remote_port) < 0)
        {
            return -1;
        }
    }
    return 0;
}

int bBind(bSocketFd_t sockfd, uint16_t port)
{
    bTrans_t *ptrans = (bTrans_t *)(intptr_t)sockfd;
    if (SOCKFD_IS_INVALID(sockfd))
    {
        return -1;
    }
    bTcpIpStackIf_t *pstack_if = (bTcpIpStackIf_t *)ptrans->stack_if;
    if (pstack_if == NULL)
    {
        return -1;
    }
    ptrans->local_port = port;
    if (ptrans->type == B_TRANS_CONN_UDP)
    {
        if (pstack_if->udp.bind(ptrans->pcb, port) < 0)
        {
            return -1;
        }
    }
    else
    {
        if (pstack_if->tcp.bind(ptrans->pcb, port) < 0)
        {
            return -1;
        }
    }
    return 0;
}

int bListen(bSocketFd_t sockfd, int backlog)
{
    void *listen_pcb = NULL;
    B_UNUSED(backlog);
    bTrans_t *ptrans = (bTrans_t *)(intptr_t)sockfd;
    if (SOCKFD_IS_INVALID(sockfd))
    {
        return -1;
    }
    bTcpIpStackIf_t *pstack_if = (bTcpIpStackIf_t *)ptrans->stack_if;
    if (pstack_if == NULL)
    {
        return -1;
    }
    if (ptrans->type == B_TRANS_CONN_TCP)
    {
        listen_pcb = pstack_if->tcp.listen(ptrans->pcb, backlog);
        if (listen_pcb == NULL)
        {
            return -1;
        }
        ptrans->pcb = listen_pcb;
    }
    else if (ptrans->type == B_TRANS_CONN_UDP)
    {
        pstack_if->udp.listen(ptrans->pcb, backlog);
    }
    bTcpIpInfo_t *pinfo        = list_entry(ptrans->netif, bTcpIpInfo_t, netif);
    pinfo->trans_listen_cb.cb  = ptrans->callback;
    pinfo->trans_listen_cb.arg = ptrans->cb_arg;
    return 0;
}

int bRecv(bSocketFd_t sockfd, uint8_t *pbuf, uint16_t len, uint16_t *real_len)
{
    int       rlen   = 0;
    bTrans_t *ptrans = (bTrans_t *)(intptr_t)sockfd;
    if (SOCKFD_IS_INVALID(sockfd) || pbuf == NULL || len == 0 ||
        (_bTcpIpTransIsEnable(ptrans) == 0))
    {
        return -1;
    }
    bTcpIpStackIf_t *pstack_if = (bTcpIpStackIf_t *)ptrans->stack_if;
    if (pstack_if == NULL)
    {
        return -1;
    }
#if (defined(_TCPIP_STACK_LWIP_ENABLE) && (_TCPIP_STACK_LWIP_ENABLE == 1))
    struct pbuf *tmp_buf = ptrans->p;
    if (tmp_buf)
    {
        rlen = pbuf_copy_partial(tmp_buf, pbuf, len, ptrans->read_offset);
        if (rlen > 0)
        {
            ptrans->read_offset += rlen;
            if (ptrans->type == B_TRANS_CONN_TCP)
            {
                tcp_recved(ptrans->pcb, rlen);
            }
        }
        if (rlen <= 0 || ptrans->read_offset >= tmp_buf->tot_len)
        {
            pbuf_free(tmp_buf);
            ptrans->p           = NULL;
            ptrans->read_offset = 0;
        }
    }
#endif

#if (defined(_TCPIP_RECV_BUF_ENABLE) && (_TCPIP_RECV_BUF_ENABLE == 1))
    if (TCPIP_STACK_OPT_IS_NO_BUFFER(ptrans->stack_opt))
    {
        rlen = _bTransPcbReadData(&ptrans->recv_head, pbuf, len);
    }
#endif
    if (TCPIP_STACK_OPT_IS_USE_BUFFER(ptrans->stack_opt))
    {
        if (ptrans->type == B_TRANS_CONN_UDP)
        {
            rlen = pstack_if->udp.recv(ptrans->pcb, pbuf, len);
        }
        else if (ptrans->type == B_TRANS_CONN_TCP)
        {
            rlen = pstack_if->tcp.recv(ptrans->pcb, pbuf, len);
        }
    }
    if (real_len)
    {
        *real_len = (uint16_t)(rlen & 0xffff);
    }
    return rlen;
}

int bSend(bSocketFd_t sockfd, uint8_t *pbuf, uint16_t buf_len, uint16_t *wlen)
{
    int       retval = 0;
    bTrans_t *ptrans = (bTrans_t *)(intptr_t)sockfd;
    if (SOCKFD_IS_INVALID(sockfd) || pbuf == NULL || buf_len == 0 ||
        (_bTcpIpTransIsEnable(ptrans) == 0))
    {
        return -1;
    }
    bTcpIpNetif_t   *pnetif    = (bTcpIpNetif_t *)ptrans->netif;
    bTcpIpStackIf_t *pstack_if = (bTcpIpStackIf_t *)ptrans->stack_if;
    if (pstack_if == NULL)
    {
        return -1;
    }

#if (defined(_TCPIP_SEND_BUF_ENABLE) && (_TCPIP_SEND_BUF_ENABLE == 1))
    if (TCPIP_STACK_OPT_IS_NO_BUFFER(ptrans->stack_opt))
    {
        retval = _bTransPcbAddData(&ptrans->send_head, pbuf, buf_len, NULL, TCPIP_SEND_BUF_LEN_MAX);
    }
#endif

    if (TCPIP_STACK_OPT_IS_USE_BUFFER(ptrans->stack_opt) ||
        TCPIP_STACK_OPT_IS_USE_LWIP(ptrans->stack_opt))
    {
        if (ptrans->type == B_TRANS_CONN_TCP)
        {
            retval = pstack_if->tcp.send(ptrans->pcb, pbuf, buf_len);
        }
        else if (ptrans->type == B_TRANS_CONN_UDP)
        {
            retval = pstack_if->udp.send(pnetif->private, ptrans->pcb, pbuf, buf_len);
        }
    }
    if (wlen && retval >= 0)
    {
        *wlen = (uint16_t)(retval & 0xffff);
    }
    return retval;
}

uint8_t bSockIsReadable(bSocketFd_t sockfd)
{
    bTrans_t *ptrans = (bTrans_t *)(intptr_t)sockfd;
    if (SOCKFD_IS_INVALID(sockfd) || (_bTcpIpTransIsEnable(ptrans) == 0))
    {
        return 0;
    }
#if ((defined(_TCPIP_STACK_LWIP_ENABLE)) && (_TCPIP_STACK_LWIP_ENABLE == 1))
    if (TCPIP_STACK_OPT_IS_USE_LWIP(ptrans->stack_opt))
    {
        return (ptrans->p != NULL);
    }
#endif

#if (defined(_TCPIP_RECV_BUF_ENABLE) && (_TCPIP_RECV_BUF_ENABLE == 1))
    if (TCPIP_STACK_OPT_IS_NO_BUFFER(ptrans->stack_opt))
    {
        // b_log("list head:%p\r\n", &ptrans->recv_head);
        return (!list_empty(&ptrans->recv_head));
    }
#endif

    if (TCPIP_STACK_OPT_IS_USE_BUFFER(ptrans->stack_opt))
    {
        bTcpIpStackIf_t *pstack_if = (bTcpIpStackIf_t *)ptrans->stack_if;
        if (pstack_if)
        {
            return pstack_if->is_readable(ptrans->pcb);
        }
    }
    return 0;
}

uint8_t bSockIsWriteable(bSocketFd_t sockfd)
{
    bTrans_t *ptrans = (bTrans_t *)(intptr_t)sockfd;
    if (SOCKFD_IS_INVALID(sockfd) || (_bTcpIpTransIsEnable(ptrans) == 0))
    {
        return 0;
    }
    bTcpIpStackIf_t *pstack_if = (bTcpIpStackIf_t *)ptrans->stack_if;
    if (pstack_if == NULL)
    {
        return -1;
    }
    if (ptrans->type == B_TRANS_CONN_UDP)
    {
        return 1;
    }
#if ((defined(_TCPIP_STACK_LWIP_ENABLE)) && (_TCPIP_STACK_LWIP_ENABLE == 1))
    if (TCPIP_STACK_OPT_IS_USE_LWIP(ptrans->stack_opt))
    {
        if (pstack_if->is_writeable)
        {
            return pstack_if->is_writeable(ptrans->pcb);
        }
    }
#endif

#if (defined(_TCPIP_SEND_BUF_ENABLE) && (_TCPIP_SEND_BUF_ENABLE == 1))
    if (TCPIP_STACK_OPT_IS_NO_BUFFER(ptrans->stack_opt))
    {
        return (_bTransPcbCalDataLen(&ptrans->send_head) < TCPIP_SEND_BUF_LEN_MAX);
    }
#endif

    if (TCPIP_STACK_OPT_IS_USE_BUFFER(ptrans->stack_opt))
    {
        if (pstack_if->is_writeable)
        {
            return pstack_if->is_writeable(ptrans->pcb);
        }
    }
    return 0;
}

uint8_t bSocketIsConnected(bSocketFd_t sockfd)
{
    if (SOCKFD_IS_INVALID(sockfd))
    {
        return 0;
    }
    bTrans_t *ptrans = (bTrans_t *)(intptr_t)sockfd;
    return (ptrans->state == B_SOCKET_STATE_CONNECTED);
}

int bShutdown(bSocketFd_t sockfd)
{
    if (SOCKFD_IS_INVALID(sockfd))
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)(intptr_t)sockfd;
    _bTcpIpTransState(ptrans, B_SOCKET_STATE_WAIT_DISCONNECT);
    return 0;
}

uint32_t bIPStr2Uint32(const char *ip)
{
    if (ip == NULL)
    {
        return 0;
    }
    return _bIpStr2Uint32(ip);
}

uint32_t bTcpIpGetCurrentDevNo()
{
    if (bTcpIpCtx.pinfo != NULL)
    {
        return bTcpIpCtx.pinfo->netif.dev_no;
    }
    return 0;
}

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
/************************ Copyright (c) 2019 Bean *****END OF FILE****/
