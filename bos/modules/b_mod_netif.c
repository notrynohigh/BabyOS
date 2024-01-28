/**
 *!
 * \file        b_mod_netif.c
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
#include "modules/inc/b_mod_netif.h"
#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))

#include "b_section.h"
#include "core/inc/b_core.h"
#include "core/inc/b_device.h"
#include "core/inc/b_timer.h"
#include "drivers/inc/b_driver_cmd.h"
#include "hal/inc/b_hal.h"
#include "utils/inc/b_util_log.h"

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
 * \defgroup NETIF_Private_TypesDefinitions
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup NETIF_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup NETIF_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup NETIF_Private_Variables
 * \{
 */
static LIST_HEAD(bNetifListHead);

static LIST_HEAD(bNetifClientListHead);

/**
 * \}
 */

/**
 * \defgroup NETIF_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup NETIF_Private_Functions
 * \{
 */

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
    if (netif == NULL || p == NULL)
    {
        return ERR_ARG;
    }
    bNetif_t *pbnetif = (bNetif_t *)netif->state;

    bWrite(pbnetif->fd, (uint8_t *)p, p->tot_len);

    return ERR_OK;
}

static err_t _bNetifInit(struct netif *netif)
{
    int       fd      = -1;
    bNetif_t *pbnetif = list_entry(netif, bNetif_t, lwip_netif);
    /* Initialize interface hostname */
    netif->hostname = bDeviceDescription(pbnetif->mac_dev);
    netif->name[0]  = pbnetif->mac_dev % 10 + '0';
    netif->name[1]  = (pbnetif->mac_dev % 100) / 10 + '0';

    netif->output     = etharp_output;
    netif->linkoutput = _bNetifLinkoutput;

    fd = bOpen(pbnetif->mac_dev, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return ERR_RTE;
    }
    pbnetif->fd = fd;

    bMacAddress_t mac_address;
    bCtl(fd, bCMD_MAC_ADDRESS, &mac_address);
    netif->state      = (void *)pbnetif;
    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    netif->hwaddr[0]  = mac_address.address[0];
    netif->hwaddr[1]  = mac_address.address[1];
    netif->hwaddr[2]  = mac_address.address[2];
    netif->hwaddr[3]  = mac_address.address[3];
    netif->hwaddr[4]  = mac_address.address[4];
    netif->hwaddr[5]  = mac_address.address[5];

    uint8_t link_state = 0;
    bCtl(fd, bCMD_GET_LINK_STATE, &link_state);

    if (link_state)
    {
        netif->flags |= NETIF_FLAG_LINK_UP;
    }
    netif->mtu = 1500;
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

    bHalBufList_t buf_list;
    buf_list.m_create  = _bNetifMalloc;
    buf_list.m_next    = _bNetifBufNext;
    buf_list.m_payload = _bNetifBufPayload;
    bCtl(fd, bCMD_REG_BUF_LIST, &buf_list);

    return ERR_OK;
}

static void _bNetifLinkUpdate(struct netif *netif)
{
    bNetif_t *pbnetif    = list_entry(netif, bNetif_t, lwip_netif);
    uint8_t   link_state = netif_is_link_up(&pbnetif->lwip_netif);
    bCtl(pbnetif->fd, bCMD_LINK_STATE_CHANGE, &link_state);
}

static void _bNetifDhcpStart(struct netif *netif)
{
    struct dhcp *dhcp = netif_dhcp_data(netif);
    if (dhcp == NULL)
    {
        dhcp_start(netif);
    }
}

static void _bNetifCore()
{
    struct list_head *pos        = NULL;
    bNetif_t         *pbnetif    = NULL;
    struct pbuf      *p          = NULL;
    uint32_t          len        = 0;
    uint8_t           link_state = 0;
    static uint32_t   tick       = 0;
    err_t             err;
    list_for_each(pos, &bNetifListHead)
    {
        pbnetif = list_entry(pos, bNetif_t, list);
        if (bHalGetSysTick() - tick > MS2TICKS(NETIF_LINK_CHECK_INTERVAL))
        {
            tick = bHalGetSysTick();
            bCtl(pbnetif->fd, bCMD_GET_LINK_STATE, &link_state);
            if (!netif_is_link_up(&pbnetif->lwip_netif) && (link_state))
            {
                /* network cable is connected */
                netif_set_link_up(&pbnetif->lwip_netif);
                netif_set_up(&pbnetif->lwip_netif);
                if (pbnetif->dhcp_en)
                {
                    _bNetifDhcpStart(&pbnetif->lwip_netif);
                }
                b_log("link up..\r\n");
            }
            else if (netif_is_link_up(&pbnetif->lwip_netif) && (!link_state))
            {
                /* network cable is disconnected */
                netif_set_link_down(&pbnetif->lwip_netif);
                netif_set_down(&pbnetif->lwip_netif);
                b_log("link down..\r\n");
            }
        }

        if (netif_is_link_up(&pbnetif->lwip_netif))
        {
            p = NULL;
            bRead(pbnetif->fd, (uint8_t *)&p, 0);
            if (p != NULL)
            {
                err = pbnetif->lwip_netif.input(p, &pbnetif->lwip_netif);
                if (err != ERR_OK)
                {
                    pbuf_free(p);
                    p = NULL;
                }
            }
        }
    }
    sys_check_timeouts();
}

BOS_REG_POLLING_FUNC(_bNetifCore);

//------------------------------------------------------------------------
//    上面是网卡对接LWIP，保证LWIP能跑起来
//    下面是发起连接，对接lwip的接口
//------------------------------------------------------------------------
static err_t _bTcpRecvFn(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t _bTcpSendFn(void *arg, struct tcp_pcb *tpcb, u16_t len);
static void  _bTcpErrorFn(void *arg, err_t err);
static void  _bUdpRecvFn(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                         u16_t port);
static void  _bNetifTrigEvent(bNetifConn_t *pconn, bNetifConnEvent_t event, void *param);

static int _bNetifConnAllocBuf(bNetifConn_t *pconn)
{
    uint32_t     rx_size = (pconn->rx_cache_size == 0) ? CONNECT_RECVBUF_MAX : pconn->rx_cache_size;
    struct pbuf *p       = pbuf_alloc(PBUF_RAW, rx_size, PBUF_RAM);
    if (p == NULL)
    {
        b_log_e("malloc error...\r\n");
        return -1;
    }
    pconn->pbuf = p;
    bFIFO_Init(&pconn->recv_buf, p->payload, p->tot_len);
    return 0;
}

static int _bNetifConnFreeBuf(bNetifConn_t *pconn)
{
    if (pconn->pbuf == NULL)
    {
        return 0;
    }
    pbuf_free(pconn->pbuf);
    pconn->pbuf = NULL;
    return 0;
}

static int _bNetifConnInit(bNetifConn_t *pconn)
{
    if (pconn->state != B_CONN_DEINIT || pconn->subtype != B_NETIF_CLIENT_FLAG)
    {
        return -1;
    }
    if (0 > _bNetifConnAllocBuf(pconn))
    {
        return -1;
    }
    pconn->err_code  = 0;
    pconn->readable  = 0;
    pconn->writeable = 0;
    if (pconn->type == B_TRANS_TCP)
    {
        pconn->pcb = tcp_new();
        if (pconn->pcb == NULL)
        {
            _bNetifConnFreeBuf(pconn);
            return -1;
        }
        pconn->state = B_CONN_INIT;
        tcp_arg(pconn->pcb, pconn);
        tcp_err(pconn->pcb, _bTcpErrorFn);
    }
    else if (pconn->type == B_TRANS_UDP)
    {
        pconn->pcb = udp_new();
        if (pconn->pcb == NULL)
        {
            _bNetifConnFreeBuf(pconn);
            return -1;
        }
        udp_bind(pconn->pcb, IP_ADDR_ANY, 0);
        udp_recv(pconn->pcb, _bUdpRecvFn, pconn);
        pconn->state = B_CONN_INIT;
    }
    return 0;
}

static int _bNetifConnDeinit(bNetifConn_t *pconn)
{
    if (pconn->state == B_CONN_DEINIT)
    {
        return -1;
    }
    pconn->state = B_CONN_DEINIT;
    if (pconn->type == B_TRANS_TCP)
    {
        tcp_abort(pconn->pcb);
    }
    else if (pconn->type == B_TRANS_UDP)
    {
        udp_remove(pconn->pcb);
    }
    pconn->pcb = NULL;
    _bNetifConnFreeBuf(pconn);
    return 0;
}

static bNetifConn_t *_bNetifServerNewConn(bNetifServer_t *server, void *pcb)
{
    bNetifConn_t *pconn = NULL;
    int           i     = 0;

    for (i = 0; i < SERVER_MAX_CONNECTIONS; i++)
    {
        if (server->conn[i].pcb == NULL)
        {
            pconn = &server->conn[i];
            break;
        }
    }
    if (pconn == NULL)
    {
        return NULL;
    }
    if (0 > _bNetifConnAllocBuf(pconn))
    {
        return NULL;
    }
    pconn->callback    = server->callback;
    pconn->cb_arg      = server->user_data;
    pconn->err_code    = 0;
    pconn->pcb         = pcb;
    pconn->readable    = 0;
    pconn->writeable   = 0;
    pconn->subtype     = B_NETIF_SERVER_FLAG;
    pconn->type        = server->type;
    pconn->remote_ip   = ((struct tcp_pcb *)pcb)->remote_ip.addr;
    pconn->remote_port = ((struct tcp_pcb *)pcb)->remote_port;
    if (pconn->type == B_TRANS_TCP)
    {
        tcp_arg(pconn->pcb, pconn);
        tcp_err(pconn->pcb, _bTcpErrorFn);
    }
    pconn->state = B_CONN_INIT;
    return pconn;
}

static bNetifConn_t *_bNetifUdpServerGetConn(bNetifServer_t *server, uint32_t ip, uint16_t port)
{
    int           i     = 0;
    bNetifConn_t *pconn = NULL;
    for (i = 0; i < SERVER_MAX_CONNECTIONS; i++)
    {
        if (server->conn[i].remote_ip == ip && server->conn[i].remote_port == port &&
            server->conn[i].pcb != NULL)
        {
            pconn = &server->conn[i];
            break;
        }
    }
    if (pconn == NULL)
    {
        for (i = 0; i < SERVER_MAX_CONNECTIONS; i++)
        {
            if (server->conn[i].pcb == NULL)
            {
                pconn = &server->conn[i];
                break;
            }
        }
        if (pconn == NULL)
        {
            return NULL;
        }

        if (0 > _bNetifConnAllocBuf(pconn))
        {
            return NULL;
        }
        pconn->pcb         = server->server_pcb;
        pconn->remote_ip   = ip;
        pconn->remote_port = port;
        pconn->callback    = server->callback;
        pconn->cb_arg      = server->user_data;
        pconn->err_code    = 0;
        pconn->readable    = 0;
        pconn->subtype     = B_NETIF_SERVER_FLAG;
        pconn->type        = server->type;
        _bNetifTrigEvent(pconn, B_EVENT_CONNECTED, NULL);
    }
    return pconn;
}

static void _bNetifTrigEvent(bNetifConn_t *pconn, bNetifConnEvent_t event, void *param)
{
    struct pbuf *pbuf_dat = (struct pbuf *)param;
    if (pconn->state == B_CONN_DEINIT)
    {
        return;
    }
    if (event == B_EVENT_DNS_FAIL || event == B_EVENT_DISCONNECT || event == B_EVENT_ERROR)
    {
        _bNetifConnDeinit(pconn);
    }
    else if (event == B_EVENT_DNS_OK)
    {
        pconn->remote_ip = *((uint32_t *)param);
        pconn->state     = B_CONN_CONNECTING;
    }
    else if (event == B_EVENT_NEW_DATA)
    {
        do
        {
            bFIFO_Write(&pconn->recv_buf, pbuf_dat->payload, pbuf_dat->len);
            pbuf_dat = pbuf_dat->next;
        } while (pbuf_dat != NULL && pbuf_dat->len > 0);
        pconn->readable = 1;
    }
    else if (event == B_EVENT_CONNECTED)
    {
        if (pconn->type == B_TRANS_TCP)
        {
            tcp_recv(pconn->pcb, _bTcpRecvFn);
            tcp_sent(pconn->pcb, _bTcpSendFn);
        }
        pconn->state     = B_CONN_CONNECTED;
        pconn->writeable = 1;
    }
    else if (event == B_EVENT_WAIT_DNS)
    {
        pconn->state = B_CONN_DNS;
    }
    else if (event == B_EVENT_WAIT_CONNECTED)
    {
        pconn->state = B_CONN_WAIT_CONNECTED;
    }
    pconn->callback(event, pconn, pconn->cb_arg);
}

static void _bDnsCallback(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    bNetifConn_t *pconn = (bNetifConn_t *)callback_arg;
    if (ipaddr == NULL)
    {
        _bNetifTrigEvent(pconn, B_EVENT_DNS_FAIL, NULL);
    }
    else
    {
        _bNetifTrigEvent(pconn, B_EVENT_DNS_OK, (void *)&ipaddr->addr);
    }
}

static void _bUdpRecvFn(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                        u16_t port)
{
    bNetifConn_t *pconn = (bNetifConn_t *)arg;
    if (p == NULL)
    {
        _bNetifTrigEvent(pconn, B_EVENT_DISCONNECT, NULL);
    }
    else
    {
        _bNetifTrigEvent(pconn, B_EVENT_NEW_DATA, p);
        pbuf_free(p);
    }
}

static err_t _bTcpRecvFn(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    bNetifConn_t *pconn = (bNetifConn_t *)arg;
    if (p == NULL)
    {
        _bNetifTrigEvent(pconn, B_EVENT_DISCONNECT, NULL);
    }
    else
    {
        _bNetifTrigEvent(pconn, B_EVENT_NEW_DATA, p);
        pbuf_free(p);
    }
    return ERR_OK;
}

static err_t _bTcpSendFn(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    return ERR_OK;
}

static err_t _bTcpConnectFn(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    bNetifConn_t *pconn = (bNetifConn_t *)arg;
    _bNetifTrigEvent(pconn, B_EVENT_CONNECTED, NULL);
    return ERR_OK;
}

static void _bTcpErrorFn(void *arg, err_t err)
{
    bNetifConn_t *pconn = (bNetifConn_t *)arg;
    pconn->err_code     = err;
    if (err == ERR_ISCONN)
    {
        _bNetifTrigEvent(pconn, B_EVENT_CONNECTED, NULL);
    }
    else if (err == ERR_ALREADY)
    {
        ;
    }
    else
    {
        _bNetifTrigEvent(pconn, B_EVENT_ERROR, NULL);
    }
}

static void _bUdpServerRecvFn(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                              u16_t port)
{
    int             ret    = -1;
    bNetifConn_t   *pconn  = NULL;
    bNetifServer_t *server = (bNetifServer_t *)arg;
    pconn                  = _bNetifUdpServerGetConn(server, addr->addr, port);
    if (pconn)
    {
        _bNetifTrigEvent(pconn, B_EVENT_NEW_DATA, p);
    }
    pbuf_free(p);
}

static err_t _bTcpServerAccept(void *arg, struct tcp_pcb *newpcb, err_t err)
{

    int             ret    = -1;
    bNetifConn_t   *pconn  = NULL;
    bNetifServer_t *server = (bNetifServer_t *)arg;
    pconn                  = _bNetifServerNewConn(server, newpcb);
    if (pconn == NULL)
    {
        tcp_abort(newpcb);
    }
    else
    {
        _bNetifTrigEvent(pconn, B_EVENT_CONNECTED, NULL);
    }
    return ERR_OK;
}

/**
 * client handler
 */

static void _bNetifClientAdd(bNetifClient_t *client)
{
    uint8_t exist_f = list_exist_nodes(&bNetifClientListHead, &(client->list));
    if (exist_f == 0)
    {
        list_add(&(client->list), &bNetifClientListHead);
    }
}

static int _bClientInitHandle(bNetifClient_t *client)
{
    ip_addr_t addr;
    err_t     err = dns_gethostbyname(client->remote_addr, &addr, _bDnsCallback, &client->conn);
    if (err == ERR_OK)
    {
        _bNetifTrigEvent(&client->conn, B_EVENT_DNS_OK, &addr.addr);
    }
    else if (err == ERR_INPROGRESS)
    {
        _bNetifTrigEvent(&client->conn, B_EVENT_WAIT_DNS, NULL);
    }
    else
    {
        _bNetifTrigEvent(&client->conn, B_EVENT_DNS_FAIL, NULL);
    }
    return 0;
}

static int _bClientConnectingHandle(bNetifClient_t *client)
{
    int       ret = -1;
    ip_addr_t addr;
    addr.addr = client->conn.remote_ip;
    if (client->conn.type == B_TRANS_TCP)
    {
        if (ERR_OK ==
            tcp_connect(client->conn.pcb, &addr, client->conn.remote_port, _bTcpConnectFn))
        {
            ret = 0;
            _bNetifTrigEvent(&client->conn, B_EVENT_WAIT_CONNECTED, NULL);
        }
    }
    else if (client->conn.type == B_TRANS_UDP)
    {
        if (ERR_OK == udp_connect(client->conn.pcb, &addr, client->conn.remote_port))
        {
            ret = 0;
            _bNetifTrigEvent(&client->conn, B_EVENT_CONNECTED, NULL);
        }
    }
    if (ret != 0)
    {
        _bNetifTrigEvent(&client->conn, B_EVENT_DISCONNECT, NULL);
    }
    return 0;
}

static int _bClientConnectedHandle(bNetifClient_t *client)
{
    return 0;
}

static void _bNetifClientCore()
{
    struct list_head *pos    = NULL;
    bNetifClient_t   *client = NULL;
    list_for_each(pos, &bNetifClientListHead)
    {
        client = list_entry(pos, bNetifClient_t, list);
        if (client->conn.state == B_CONN_INIT)
        {
            _bClientInitHandle(client);
        }
        else if (client->conn.state == B_CONN_CONNECTING)
        {
            _bClientConnectingHandle(client);
        }
        else if (client->conn.state == B_CONN_CONNECTED)
        {
            _bClientConnectedHandle(client);
        }
    }
}

BOS_REG_POLLING_FUNC(_bNetifClientCore);

// PING

static bNetifPing_t bNetifPingInstance = {
    .pcb      = NULL,
    .state    = B_CONN_DEINIT,
    .timer_id = NULL,
};

static void _bNetifPingResult(int result, uint32_t ms)
{
    if (bNetifPingInstance.timer_id != NULL)
    {
        bTimerStop(bNetifPingInstance.timer_id);
    }
    if (bNetifPingInstance.pcb != NULL)
    {
        raw_remove(bNetifPingInstance.pcb);
        bNetifPingInstance.pcb = NULL;
    }

    bNetifPingInstance.state = B_CONN_DEINIT;
    if (bNetifPingInstance.callback != NULL)
    {
        bNetifPingInstance.callback(result, ms, bNetifPingInstance.user_data);
    }
}

static void _bNetifSendPing(uint32_t ip)
{
    struct ip_hdr        *ip_pack = NULL;
    struct icmp_echo_hdr *icmp_pack;
    struct pbuf          *p     = NULL;
    static uint16_t       seqno = 0;
    b_log("ping ip: %d.%d.%d.%d\r\n", ((ip >> 0) & 0xff), ((ip >> 8) & 0xff), ((ip >> 16) & 0xff),
          ((ip >> 24) & 0xff));

    p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr) + 32, PBUF_RAM);
    if (p == NULL)
    {
        _bNetifPingResult(-1, 0);
        return;
    }
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
    if (bNetifPingInstance.pcb != NULL)
    {
        ip_addr_t ipaddr;
        ipaddr.addr = ip;
        raw_sendto(bNetifPingInstance.pcb, p, &ipaddr);
        bNetifPingInstance.s_tick = bHalGetSysTick();
    }
    pbuf_free(p);
    p = NULL;
}

static void _bPingDnsCallback(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    if (ipaddr == NULL)
    {
        b_log_e("dns error...\r\n");
        _bNetifPingResult(-1, 0);
    }
    else
    {
        _bNetifSendPing(ipaddr->addr);
    }
}

B_TIMER_CREATE_ATTR(bNetifPingTimerAttr);

static void _bNetifPingTimerFunc(void *arg)
{
    b_log_e("ping timeout...\r\n");
    _bNetifPingResult(-1, 0);
}

static u8_t _bNetifRawRecvFn(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
    struct ip_hdr        *ip_pack   = NULL;
    struct icmp_echo_hdr *icmp_pack = NULL;
    uint8_t               ip_hlen   = 0;
    uint32_t              r_tick    = 0;
    if (bNetifPingInstance.state != B_CONN_DEINIT && p != NULL)
    {
        r_tick  = bHalGetSysTick();
        ip_pack = (struct ip_hdr *)p->payload;
        ip_hlen = IPH_HL(ip_pack) * 4;
        pbuf_header(p, -ip_hlen);
        icmp_pack = (struct icmp_echo_hdr *)p->payload;
        if (icmp_pack->code == 0 && icmp_pack->type == 0)
        {
            _bNetifPingResult(0, TICKS2MS((r_tick - bNetifPingInstance.s_tick)));
            pbuf_free(p);
            return 1;
        }
    }
    return 0;
}

/**
 * \}
 */

/**
 * \addtogroup NETIF_Exported_Functions
 * \{
 */

uint32_t sys_now()
{
    return bHalGetSysTick();
}

int bNetifAdd(bNetif_t *pInstance, uint32_t ip, uint32_t gw, uint32_t mask)
{
    static uint8_t netif_init_f = 0;
    ip4_addr_t     ip_addr;
    ip4_addr_t     netmask;
    ip4_addr_t     gw_addr;
    if (netif_init_f == 0)
    {
        netif_init_f = 1;
        lwip_init();
    }
    if (pInstance == NULL)
    {
        return -1;
    }
    if (ip == 0 && gw == 0 && mask == 0)
    {
        pInstance->dhcp_en = 1;
    }
    else
    {
        pInstance->dhcp_en = 0;
    }
    IP4_ADDR(&ip_addr, (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, (ip >> 0) & 0xff);
    IP4_ADDR(&gw_addr, (gw >> 24) & 0xff, (gw >> 16) & 0xff, (gw >> 8) & 0xff, (gw >> 0) & 0xff);
    IP4_ADDR(&netmask, (mask >> 24) & 0xff, (mask >> 16) & 0xff, (mask >> 8) & 0xff,
             (mask >> 0) & 0xff);
    netif_add(&pInstance->lwip_netif, &ip_addr, &netmask, &gw_addr, NULL, _bNetifInit,
              ethernet_input);
    netif_set_default(&pInstance->lwip_netif);

    if (netif_is_link_up(&pInstance->lwip_netif))
    {
        /* When the netif is fully configured this function must be called */
        netif_set_up(&pInstance->lwip_netif);
        if (pInstance->dhcp_en)
        {
            _bNetifDhcpStart(&pInstance->lwip_netif);
        }
    }
    else
    {
        /* When the netif link is down this function must be called */
        netif_set_down(&pInstance->lwip_netif);
    }
    netif_set_link_callback(&pInstance->lwip_netif, _bNetifLinkUpdate);
    list_add(&pInstance->list, &bNetifListHead);
    return 0;
}

/**
 * CLIENT
 */

bNetifConn_t *bNetifConnect(bNetifClient_t *client, char *remote, uint16_t port)
{
    if (client == NULL || remote == NULL || strlen(remote) > REMOTE_ADDR_LEN_MAX ||
        client->conn.callback == NULL || (!IS_VALID_TRANS_TYPE(client->conn.type)))
    {
        return NULL;
    }
    if (0 > _bNetifConnInit(&client->conn))
    {
        return NULL;
    }
    client->conn.remote_port = port;
    memset(client->remote_addr, 0, REMOTE_ADDR_LEN_MAX + 1);
    memcpy(client->remote_addr, remote, strlen(remote));
    _bNetifClientAdd(client);
    return &client->conn;
}

/**
 * SERVER
 */
int bNetifSrvListen(bNetifServer_t *server, uint16_t port)
{
    if (server == NULL || server->callback == NULL || (!IS_VALID_TRANS_TYPE(server->type)))
    {
        return -1;
    }
    if (server->state != B_SRV_DEINIT)
    {
        return -2;
    }
    memset(server->conn, 0, sizeof(server->conn));
    if (server->type == B_TRANS_TCP)
    {
        server->server_pcb = tcp_new();
        if (server->server_pcb == NULL)
        {
            return -1;
        }
        server->server_port = port;
        tcp_bind(server->server_pcb, IP_ADDR_ANY, port);
        server->server_pcb = tcp_listen(server->server_pcb);
        tcp_arg(server->server_pcb, server);
        tcp_accept(server->server_pcb, _bTcpServerAccept);
    }
    else if (server->type == B_TRANS_UDP)
    {
        server->server_pcb = udp_new();
        if (server->server_pcb == NULL)
        {
            return -1;
        }
        server->server_port = port;
        ip_addr_t ipaddr;
        IP4_ADDR(&ipaddr, 0, 0, 0, 0);
        udp_bind(server->server_pcb, &ipaddr, port);
        udp_recv(server->server_pcb, _bUdpServerRecvFn, server);
    }
    server->state = B_SRV_LISTEN;
    return 0;
}

int bNetifConnReadData(bNetifConn_t *pconn, uint8_t *pbuf, uint16_t buf_len, uint16_t *rlen)
{
    int      read_len = 0;
    uint16_t fifo_len = 0;
    if (pconn == NULL || pbuf == NULL || buf_len == 0)
    {
        return -1;
    }
    if (pconn->pcb == NULL)
    {
        return -2;
    }
    read_len = bFIFO_Read(&pconn->recv_buf, pbuf, buf_len);
    if (read_len < 0)
    {
        return -3;
    }
    if (rlen)
    {
        *rlen = (uint16_t)(read_len & 0xffff);
    }
    bFIFO_Length(&pconn->recv_buf, &fifo_len);
    if (fifo_len == 0)
    {
        pconn->readable = 0;
    }
    return 0;
}

int bNetifConnWriteData(bNetifConn_t *pconn, uint8_t *pbuf, uint16_t buf_len, uint16_t *wlen)
{
    uint16_t writeable_len = 0;
    if (pconn == NULL || pbuf == NULL || buf_len == 0)
    {
        return -1;
    }
    if (pconn->pcb == NULL)
    {
        return -2;
    }
    if (pconn->type == B_TRANS_TCP)
    {
        writeable_len = tcp_sndbuf((struct tcp_pcb *)pconn->pcb);
        if (writeable_len <= buf_len)
        {
            pconn->writeable = 0;
        }
        writeable_len = (writeable_len > buf_len) ? buf_len : writeable_len;
        tcp_write(pconn->pcb, pbuf, buf_len, TCP_WRITE_FLAG_COPY);
        if (wlen)
        {
            *wlen = writeable_len;
        }
    }
    else if (pconn->type == B_TRANS_UDP)
    {
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, buf_len, PBUF_RAM);
        if (p)
        {
            memcpy(p->payload, pbuf, buf_len);
            udp_send(pconn->pcb, p);
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

uint8_t bNetifConnIsReadable(bNetifConn_t *pconn)
{
    if (pconn == NULL)
    {
        return 0;
    }
    return pconn->readable;
}

uint8_t bNetifConnIsWriteable(bNetifConn_t *pconn)
{
    if (pconn == NULL)
    {
        return 0;
    }
    return pconn->writeable;
}

int bNetifConnDeinit(bNetifConn_t *pconn)
{
    if (pconn == NULL)
    {
        return -1;
    }
    _bNetifTrigEvent(pconn, B_EVENT_DISCONNECT, NULL);
    return 0;
}

// PING
int bNetifPing(const char *remote, uint32_t timeout_ms, pbNetifPingCb_t cb, void *arg)
{
    if (remote == NULL || cb == NULL || timeout_ms == 0)
    {
        return -1;
    }
    if (bNetifPingInstance.state != B_CONN_DEINIT)
    {
        b_log_e("ping busy...\r\n");
        return -2;
    }
    if (bNetifPingInstance.timer_id == NULL)
    {
        bNetifPingInstance.timer_id =
            bTimerCreate(_bNetifPingTimerFunc, B_TIMER_ONCE, NULL, &bNetifPingTimerAttr);
    }
    if (bNetifPingInstance.pcb == NULL)
    {
        bNetifPingInstance.pcb = raw_new(IP_PROTO_ICMP);
        if (bNetifPingInstance.pcb == NULL)
        {
            b_log_e("new pcb error...\r\n");
            return -3;
        }
        raw_bind(bNetifPingInstance.pcb, IP4_ADDR_ANY);
        raw_recv(bNetifPingInstance.pcb, _bNetifRawRecvFn, NULL);
    }
    bNetifPingInstance.callback  = cb;
    bNetifPingInstance.user_data = arg;
    bNetifPingInstance.state     = B_CONN_INIT;
    bTimerStart(bNetifPingInstance.timer_id, timeout_ms);

    ip_addr_t addr;
    err_t     err = dns_gethostbyname(remote, &addr, _bPingDnsCallback, NULL);
    if (err == ERR_OK)
    {
        _bNetifSendPing(addr.addr);
    }
    else if (err == ERR_INPROGRESS)
    {
        ;
    }
    else
    {
        b_log_e("dns error...\r\n");
        _bNetifPingResult(-1, 0);
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

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
