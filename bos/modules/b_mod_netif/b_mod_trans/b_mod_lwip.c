/**
 *!
 * \file        b_mod_lwip.c
 * \version     v0.0.1
 * \date        2020/05/24
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

/*Includes ----------------------------------------------*/
#include "modules/inc/b_mod_netif/b_mod_trans.h"

#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))

#if (defined(_NETIF_USE_LWIP) && (_NETIF_USE_LWIP == 1))
#include "core/inc/b_task.h"
#include "core/inc/b_timer.h"
#include "hal/inc/b_hal.h"
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
#include "utils/inc/b_util_log.h"

#ifndef CONNECT_RECVBUF_MAX
#define CONNECT_RECVBUF_MAX (4096)
#endif

typedef struct
{
    void        *pcb;
    void        *cb_arg;
    pbTransCb_t  callback;
    uint8_t      readable;
    uint8_t      writeable;
    bFIFO_Info_t rx_fifo;
    uint16_t     local_port;
    uint16_t     remote_port;
    uint32_t     remote_ip;
    bTransType_t type;
} bTrans_t;

typedef struct
{
    char             remote_ip_str[REMOTE_ADDR_LEN_MAX + 1];
    uint32_t         remote_ip;
    pbTransDnsCb_t   cb;
    void            *cb_arg;
    struct list_head list;
} bDns_t;

static LIST_HEAD(DnsHead);
static bTaskId_t bTransTaskId = NULL;
B_TASK_CREATE_ATTR(bTransTaskAttr);

static err_t _bTcpRecvFn(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t _bTcpSendFn(void *arg, struct tcp_pcb *tpcb, u16_t len);

static void _bNetTransEvent(bTrans_t *ptrans, bTransEvent_t event, void *param)
{
    struct pbuf *pbuf_dat = (struct pbuf *)param;
    if (ptrans == NULL || ptrans->pcb == NULL)
    {
        return;
    }
    if (event == B_TRANS_DISCONNECT || event == B_TRANS_ERROR || event == B_TRANS_DNS_FAIL)
    {
        ptrans->readable  = 0;
        ptrans->writeable = 0;
        if (ptrans->type == B_TRANS_CONN_TCP)
        {
            tcp_arg(ptrans->pcb, NULL);
            tcp_abort(ptrans->pcb);
        }
        else if (ptrans->type == B_TRANS_CONN_UDP)
        {
            udp_remove(ptrans->pcb);
        }
        ptrans->pcb = NULL;
    }
    else if (event == B_TRANS_NEW_DATA)
    {
        do
        {
            bFIFO_Write(&ptrans->rx_fifo, pbuf_dat->payload, pbuf_dat->len);
            pbuf_dat = pbuf_dat->next;
        } while (pbuf_dat != NULL && pbuf_dat->len > 0);
        ptrans->readable = 1;
    }
    else if (event == B_TRANS_CONNECTED || event == B_TRANS_NEW_CONNECT)
    {
        if (ptrans->type == B_TRANS_CONN_TCP)
        {
            tcp_recv(ptrans->pcb, _bTcpRecvFn);
            tcp_sent(ptrans->pcb, _bTcpSendFn);
        }
        ptrans->writeable = 1;
    }
    ptrans->callback(event, ptrans, ptrans->cb_arg);
}

static err_t _bTcpRecvFn(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    bTrans_t *ptrans = (bTrans_t *)arg;
    if (ptrans == NULL)
    {
        return ERR_OK;
    }
    if (p == NULL)
    {
        _bNetTransEvent(ptrans, B_TRANS_DISCONNECT, NULL);
    }
    else
    {
        _bNetTransEvent(ptrans, B_TRANS_NEW_DATA, p);
        pbuf_free(p);
    }
    return ERR_OK;
}

static err_t _bTcpSendFn(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    bTrans_t *ptrans = (bTrans_t *)arg;
    if (ptrans == NULL)
    {
        return ERR_OK;
    }
    if (len > 0)
    {
        ptrans->writeable = 1;
    }
    return ERR_OK;
}

static void _bTcpErrorFn(void *arg, err_t err)
{
    bTrans_t *ptrans = (bTrans_t *)arg;
    if (ptrans == NULL)
    {
        return;
    }
    if (err == ERR_ISCONN)
    {
        _bNetTransEvent(ptrans, B_TRANS_CONNECTED, NULL);
    }
    else if (err == ERR_ALREADY)
    {
        ;
    }
    else
    {
        _bNetTransEvent(ptrans, B_TRANS_ERROR, NULL);
    }
}

static err_t _bTcpConnectFn(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    bTrans_t *ptrans = (bTrans_t *)arg;
    if (ptrans == NULL)
    {
        return ERR_OK;
    }
    _bNetTransEvent(ptrans, B_TRANS_CONNECTED, NULL);
    return ERR_OK;
}

static void _bUdpRecvFn(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                        u16_t port)
{
    bTrans_t *ptrans = (bTrans_t *)arg;
    if (ptrans == NULL)
    {
        return;
    }
    if (p == NULL)
    {
        _bNetTransEvent(ptrans, B_TRANS_DISCONNECT, NULL);
    }
    else
    {
        _bNetTransEvent(ptrans, B_TRANS_NEW_DATA, p);
        pbuf_free(p);
    }
}

#ifndef SERVER_MAX_CONNECTIONS
#define SERVER_MAX_CONNECTIONS (2)
#endif

static err_t _bTcpServerAccept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    int       sockfd = -1;
    bTrans_t *ptrans = (bTrans_t *)arg;
    if (ptrans == NULL)
    {
        return ERR_OK;
    }
    sockfd = bSocket(B_TRANS_CONN_TCP, ptrans->callback, ptrans->cb_arg);
    if (sockfd < 0)
    {
        tcp_abort(newpcb);
        return ERR_ABRT;
    }
    bTrans_t *pnewtrans    = (bTrans_t *)sockfd;
    pnewtrans->pcb         = newpcb;
    pnewtrans->remote_ip   = ((struct tcp_pcb *)newpcb)->remote_ip.addr;
    pnewtrans->remote_port = ((struct tcp_pcb *)newpcb)->remote_port;
    tcp_arg(pnewtrans->pcb, pnewtrans);
    tcp_err(pnewtrans->pcb, _bTcpErrorFn);
    _bNetTransEvent(pnewtrans, B_TRANS_NEW_CONNECT, ptrans);
    return ERR_OK;
}

static void _bUdpServerRecvFn(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                              u16_t port)
{
    int       ret       = -1;
    bTrans_t *ptrans    = (bTrans_t *)arg;
    ptrans->remote_ip   = addr->addr;
    ptrans->remote_port = port;
    _bNetTransEvent(ptrans, B_TRANS_NEW_DATA, p);
    pbuf_free(p);
}

static void _bDnsTaskCb(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    bDns_t *pdns = (bDns_t *)callback_arg;
    if (ipaddr != NULL && pdns != NULL)
    {
        pdns->remote_ip = ipaddr->addr;
        b_log("dns >> ip: %x \r\n", pdns->remote_ip);
    }
}

PT_THREAD(bTransTaskFunc)(struct pt *pt, void *arg)
{
    static struct list_head *pos  = NULL;
    static bDns_t           *pdns = NULL;
    PT_BEGIN(pt);
    while (1)
    {
        list_for_each(pos, &DnsHead)
        {
            ip_addr_t addr;
            pdns      = (bDns_t *)list_entry(pos, bDns_t, list);
            err_t err = dns_gethostbyname(pdns->remote_ip_str, &addr, _bDnsTaskCb, pdns);
            b_log("dns: %s %d \r\n", pdns->remote_ip_str, err);
            if (err == ERR_OK)
            {
                pdns->remote_ip = addr.addr;
                b_log("dns >> ip: %x \r\n", pdns->remote_ip);
            }
            else if (err == ERR_INPROGRESS)
            {
                PT_WAIT_UNTIL(pt, pdns->remote_ip != 0, 5000);
            }
            pos = pos->prev;
            __list_del(pdns->list.prev, pdns->list.next);
            pdns->cb(pdns->remote_ip_str, pdns->remote_ip, pdns->cb_arg);
            mem_free(pdns);
            pdns = NULL;
        }
        bTaskDelayMs(pt, 100);
    }
    PT_END(pt);
}

static void _bSocketDnsCb(const char *name, uint32_t ipaddr, void *arg)
{
    bTrans_t *ptrans = (bTrans_t *)arg;
    ip_addr_t remote_ip;
    remote_ip.addr = ipaddr;
    if (ipaddr == 0)
    {
        b_log_w("%s dns fail..\r\n", name);
        _bNetTransEvent(ptrans, B_TRANS_DNS_FAIL, NULL);
        return;
    }
    if (ptrans->type == B_TRANS_CONN_TCP)
    {
        if (ERR_OK == tcp_connect(ptrans->pcb, &remote_ip, ptrans->remote_port, _bTcpConnectFn))
        {
            _bNetTransEvent(ptrans, B_TRANS_WAIT_CONNECTED, NULL);
        }
        else
        {
            _bNetTransEvent(ptrans, B_TRANS_ERROR, NULL);
        }
    }
    else if (ptrans->type == B_TRANS_CONN_UDP)
    {
        if (ERR_OK == udp_connect(ptrans->pcb, &remote_ip, ptrans->remote_port))
        {
            _bNetTransEvent(ptrans, B_TRANS_CONNECTED, NULL);
        }
        else
        {
            _bNetTransEvent(ptrans, B_TRANS_ERROR, NULL);
        }
    }
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
int bSocket(bTransType_t type, pbTransCb_t cb, void *user_data)
{
    if (bTransTaskId == NULL)
    {
        bTransTaskId = bTaskCreate("trans_task", bTransTaskFunc, NULL, &bTransTaskAttr);
        if (bTransTaskId == NULL)
        {
            return -1;
        }
    }
    if (cb == NULL || (type != B_TRANS_CONN_TCP && type != B_TRANS_CONN_UDP))
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)mem_malloc(sizeof(bTrans_t));
    if (ptrans == NULL)
    {
        return -2;
    }
    memset(ptrans, 0, sizeof(bTrans_t));
    uint8_t *pbuf = (uint8_t *)mem_malloc(CONNECT_RECVBUF_MAX);
    if (pbuf == NULL)
    {
        mem_free(ptrans);
        ptrans = NULL;
        return -2;
    }
    bFIFO_Init(&ptrans->rx_fifo, pbuf, CONNECT_RECVBUF_MAX);
    ptrans->type      = type;
    ptrans->callback  = cb;
    ptrans->cb_arg    = user_data;
    ptrans->readable  = 0;
    ptrans->writeable = 0;
    return (int)ptrans;
}

int bConnect(int sockfd, char *remote, uint16_t port)
{
    if (sockfd < 0 || remote == NULL || strlen(remote) > REMOTE_ADDR_LEN_MAX)
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    if (ptrans->pcb != NULL)
    {
        b_log_e("pcb != NULL ...\r\n");
        return 0;
    }
    ptrans->remote_port = port;
    if (ptrans->type == B_TRANS_CONN_TCP)
    {
        ptrans->pcb = tcp_new();
        if (ptrans->pcb == NULL)
        {
            b_log_e("pcb new fail...\r\n");
            return -1;
        }
        tcp_arg(ptrans->pcb, ptrans);
        tcp_err(ptrans->pcb, _bTcpErrorFn);
    }
    else if (ptrans->type == B_TRANS_CONN_UDP)
    {
        ptrans->pcb = udp_new();
        if (ptrans->pcb == NULL)
        {
            b_log_e("pcb new fail...\r\n");
            return -1;
        }
        udp_bind(ptrans->pcb, IP_ADDR_ANY, 0);
        udp_recv(ptrans->pcb, _bUdpRecvFn, ptrans);
    }
    return bDnsParse(remote, _bSocketDnsCb, ptrans);
}

int bBind(int sockfd, uint16_t port)
{
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    if (sockfd < 0)
    {
        return -1;
    }
    ptrans->local_port = port;
    return 0;
}

int bListen(int sockfd, int backlog)
{
    if (sockfd < 0)
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    if (ptrans->pcb != NULL)
    {
        return 0;
    }
    if (ptrans->type == B_TRANS_CONN_TCP)
    {
        ptrans->pcb = tcp_new();
        if (ptrans->pcb == NULL)
        {
            return -1;
        }
        tcp_bind(ptrans->pcb, IP_ADDR_ANY, ptrans->local_port);
        ptrans->pcb = tcp_listen(ptrans->pcb);
        tcp_arg(ptrans->pcb, ptrans);
        tcp_accept(ptrans->pcb, _bTcpServerAccept);
    }
    else if (ptrans->type == B_TRANS_CONN_UDP)
    {
        ptrans->pcb = udp_new();
        if (ptrans->pcb == NULL)
        {
            return -1;
        }
        ip_addr_t ipaddr;
        IP4_ADDR(&ipaddr, 0, 0, 0, 0);
        udp_bind(ptrans->pcb, &ipaddr, ptrans->local_port);
        udp_recv(ptrans->pcb, _bUdpServerRecvFn, ptrans);
    }
    return 0;
}

int bRecv(int sockfd, uint8_t *pbuf, uint16_t buf_len, uint16_t *rlen)
{
    int      read_len = 0;
    uint16_t fifo_len = 0;
    if (sockfd < 0 || pbuf == NULL || buf_len == 0)
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    read_len         = bFIFO_Read(&ptrans->rx_fifo, pbuf, buf_len);
    if (read_len < 0)
    {
        return -2;
    }
    else
    {
        if (ptrans->type == B_TRANS_CONN_TCP)
        {
            tcp_recved(ptrans->pcb, read_len);
        }
    }
    if (rlen)
    {
        *rlen = (uint16_t)(read_len & 0xffff);
    }
    bFIFO_Length(&ptrans->rx_fifo, &fifo_len);
    if (fifo_len == 0)
    {
        ptrans->readable = 0;
    }
    return 0;
}

int bSend(int sockfd, uint8_t *pbuf, uint16_t buf_len, uint16_t *wlen)
{
    uint16_t writeable_len = 0;
    if (sockfd < 0 || pbuf == NULL || buf_len == 0)
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    if (ptrans->type == B_TRANS_CONN_TCP)
    {
        writeable_len = tcp_sndbuf((struct tcp_pcb *)ptrans->pcb);
        if (writeable_len <= buf_len)
        {
            ptrans->writeable = 0;
        }
        writeable_len = (writeable_len > buf_len) ? buf_len : writeable_len;
        tcp_write(ptrans->pcb, pbuf, buf_len, TCP_WRITE_FLAG_COPY);
        if (wlen)
        {
            *wlen = writeable_len;
        }
    }
    else if (ptrans->type == B_TRANS_CONN_UDP)
    {
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, buf_len, PBUF_RAM);
        if (p)
        {
            memcpy(p->payload, pbuf, buf_len);
            udp_send(ptrans->pcb, p);
            pbuf_free(p);
            if (wlen)
            {
                *wlen = buf_len;
            }
        }
        else
        {
            if (wlen)
            {
                *wlen = 0;
            }
        }
    }
    return 0;
}

uint8_t bSockIsReadable(int sockfd)
{
    if (sockfd < 0)
    {
        return 0;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    return ptrans->readable;
}

uint8_t bSockIsWriteable(int sockfd)
{
    if (sockfd < 0)
    {
        return 0;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    return ptrans->writeable;
}

int bShutdown(int sockfd)
{
    if (sockfd < 0)
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    _bNetTransEvent(ptrans, B_TRANS_DISCONNECT, NULL);
    mem_free(ptrans->rx_fifo.pbuf);
    bFIFO_Deinit(&ptrans->rx_fifo);
    mem_free(ptrans);
    ptrans = NULL;
    return 0;
}

//---------------------------------------------------------------------------------------------------
// dns
//---------------------------------------------------------------------------------------------------
int bDnsParse(char *remote, pbTransDnsCb_t cb, void *user_data)
{
    if (remote == NULL || cb == NULL || strlen(remote) > REMOTE_ADDR_LEN_MAX)
    {
        return -1;
    }
    bDns_t *pdns = (bDns_t *)mem_malloc(sizeof(bDns_t));
    if (pdns == NULL)
    {
        return -2;
    }
    memset(pdns, 0, sizeof(bDns_t));
    pdns->cb        = cb;
    pdns->cb_arg    = user_data;
    pdns->remote_ip = 0;
    memcpy(pdns->remote_ip_str, remote, strlen(remote));
    list_add(&pdns->list, &DnsHead);
    return 0;
}

//---------------------------------------------------------------------------------------------------
// ping
//---------------------------------------------------------------------------------------------------
typedef struct
{
    uint8_t         busy;
    void           *pcb;
    void           *timer_id;
    uint32_t        s_tick;
    pbTransPingCb_t callback;
    void           *cb_arg;
} bPing_t;

B_TIMER_CREATE_ATTR(bPingTimerAttr);

static bPing_t bPingHd = {
    .busy     = 0,
    .pcb      = NULL,
    .timer_id = NULL,
    .cb_arg   = NULL,
};

static void _bPingResult(int result, uint32_t ms)
{
    if (bPingHd.timer_id != NULL)
    {
        bTimerStop(bPingHd.timer_id);
    }
    if (bPingHd.pcb != NULL)
    {
        raw_remove(bPingHd.pcb);
        bPingHd.pcb = NULL;
    }
    bPingHd.busy = 0;
    if (bPingHd.callback != NULL)
    {
        bPingHd.callback(result, ms, bPingHd.cb_arg);
    }
}

static void _bSendPing(uint32_t ip)
{
    struct ip_hdr        *ip_pack = NULL;
    struct icmp_echo_hdr *icmp_pack;
    struct pbuf          *p     = NULL;
    static uint16_t       seqno = 0;

    p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr) + 32, PBUF_RAM);
    if (p == NULL)
    {
        _bPingResult(-1, 0);
        return;
    }
    memset(p->payload, 0, sizeof(struct icmp_echo_hdr) + 32);
    icmp_pack         = (struct icmp_echo_hdr *)p->payload;
    icmp_pack->code   = 0;
    icmp_pack->type   = ICMP_ECHO;
    icmp_pack->id     = PP_HTONS(0x1);
    icmp_pack->seqno  = PP_HTONS(seqno);
    icmp_pack->chksum = 0;
    seqno += 1;
    for (int i = 0; i < 26; i++)
    {
        ((char *)icmp_pack)[sizeof(struct icmp_echo_hdr) + i] = 'a' + i;
    }
    ((char *)icmp_pack)[sizeof(struct icmp_echo_hdr) + 26] = 'B';
    ((char *)icmp_pack)[sizeof(struct icmp_echo_hdr) + 27] = 'a';
    ((char *)icmp_pack)[sizeof(struct icmp_echo_hdr) + 28] = 'b';
    ((char *)icmp_pack)[sizeof(struct icmp_echo_hdr) + 29] = 'y';
    ((char *)icmp_pack)[sizeof(struct icmp_echo_hdr) + 30] = 'O';
    ((char *)icmp_pack)[sizeof(struct icmp_echo_hdr) + 31] = 'S';
#if CHECKSUM_CHECK_ICMP
    icmp_pack->chksum = inet_chksum(icmp_pack, p->len);
#endif
    if (bPingHd.pcb != NULL)
    {
        ip_addr_t ipaddr;
        ipaddr.addr = ip;
        raw_sendto(bPingHd.pcb, p, &ipaddr);
        bPingHd.s_tick = bHalGetSysTick();
    }
    pbuf_free(p);
    p = NULL;
}

static void _bPingTimerCb(void *arg)
{
    _bPingResult(-1, 0);
}

static u8_t _bRawRecvFn(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
    struct ip_hdr        *ip_pack   = NULL;
    struct icmp_echo_hdr *icmp_pack = NULL;
    uint8_t               ip_hlen   = 0;
    uint32_t              r_tick    = 0;

    if (bPingHd.busy && p != NULL)
    {
        r_tick  = bHalGetSysTick();
        ip_pack = (struct ip_hdr *)p->payload;
        ip_hlen = IPH_HL(ip_pack) * 4;
        pbuf_header(p, -ip_hlen);
        icmp_pack = (struct icmp_echo_hdr *)p->payload;
        if (icmp_pack->code == 0 && icmp_pack->type == 0)
        {
            _bPingResult(0, TICKS2MS((r_tick - bPingHd.s_tick)));
            pbuf_free(p);
            return 1;
        }
    }
    return 0;
}

static void _bPingDnsCb(const char *name, uint32_t ipaddr, void *arg)
{
    if (ipaddr == NULL)
    {
        _bPingResult(-1, 0);
    }
    else
    {
        _bSendPing(ipaddr);
    }
}

int bPing(char *remote, uint32_t timeout_ms, pbTransPingCb_t cb, void *user_data)
{
    if (remote == NULL || cb == NULL || timeout_ms == 0)
    {
        return -1;
    }
    if (bPingHd.busy)
    {
        b_log_e("ping busy...\r\n");
        return -2;
    }
    if (bPingHd.timer_id == NULL)
    {
        bPingHd.timer_id = bTimerCreate(_bPingTimerCb, B_TIMER_ONCE, NULL, &bPingTimerAttr);
    }
    if (bPingHd.pcb == NULL)
    {
        bPingHd.pcb = raw_new(IP_PROTO_ICMP);
        if (bPingHd.pcb == NULL)
        {
            b_log_e("raw pcb new fail ...\r\n");
            return -3;
        }
        raw_bind(bPingHd.pcb, IP4_ADDR_ANY);
        raw_recv(bPingHd.pcb, _bRawRecvFn, NULL);
    }
    bPingHd.callback = cb;
    bPingHd.cb_arg   = user_data;
    bPingHd.busy     = 1;
    bTimerStart(bPingHd.timer_id, timeout_ms);

    if (bDnsParse(remote, _bPingDnsCb, &bPingHd) < 0)
    {
        _bPingResult(-1, 0);
    }
    return 0;
}

#endif

#endif
