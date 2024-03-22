/**
 *!
 * \file        b_mod_link.c
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
#include "modules/inc/b_mod_netif/b_mod_link.h"

#include "hal/inc/b_hal.h"

#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))

#if (defined(_NETIF_USE_LWIP) && (_NETIF_USE_LWIP == 1))

#include "b_section.h"
#include "core/inc/b_core.h"
#include "core/inc/b_device.h"
#include "drivers/inc/b_driver_cmd.h"
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
#include "utils/inc/b_util_log.h"

#ifndef NETIF_LINK_CHECK_INTERVAL
#define NETIF_LINK_CHECK_INTERVAL (200)
#endif

static struct netif bLinkNetif;
static int          bLinkFd = -1;

static err_t _bNetifLinkoutput(struct netif *netif, struct pbuf *p)
{
    if (netif == NULL || p == NULL)
    {
        return ERR_ARG;
    }
    bWrite(bLinkFd, (uint8_t *)p, p->tot_len);
    return ERR_OK;
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

static err_t _bNetifInit(struct netif *netif)
{
    int fd = -1;
    /* Initialize interface hostname */
    netif->hostname = bDeviceDescription(NETIF_DEV_NO);
    netif->name[0]  = NETIF_DEV_NO % 10 + '0';
    netif->name[1]  = (NETIF_DEV_NO % 100) / 10 + '0';

    netif->output     = etharp_output;
    netif->linkoutput = _bNetifLinkoutput;

    fd = bOpen(NETIF_DEV_NO, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return ERR_RTE;
    }
    bLinkFd = fd;

    bMacAddress_t mac_address;
    bCtl(fd, bCMD_MAC_ADDRESS, &mac_address);
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
    uint8_t link_state = netif_is_link_up(&bLinkNetif);
    bCtl(bLinkFd, bCMD_LINK_STATE_CHANGE, &link_state);
}

static void _bNetifDhcpStart(struct netif *netif)
{
#if (defined(NETIF_USE_DHCP) && (NETIF_USE_DHCP == 1))
    struct dhcp *dhcp = netif_dhcp_data(netif);
    if (dhcp == NULL)
    {
        dhcp_start(netif);
    }
#endif
}

static void _bNetlinkCore()
{
    uint8_t         link_state = 0;
    static uint32_t tick       = 0;
    struct pbuf    *p          = NULL;
    uint32_t        len        = 0;
    err_t           err;

    if (bHalGetSysTick() - tick > MS2TICKS(NETIF_LINK_CHECK_INTERVAL))
    {
        tick = bHalGetSysTick();
        bCtl(bLinkFd, bCMD_GET_LINK_STATE, &link_state);
        if (!netif_is_link_up(&bLinkNetif) && (link_state))
        {
            /* network cable is connected */
            netif_set_link_up(&bLinkNetif);
            netif_set_up(&bLinkNetif);
#if (defined(NETIF_USE_DHCP) && (NETIF_USE_DHCP == 1))
            _bNetifDhcpStart(&bLinkNetif);
#endif
        }
        else if (netif_is_link_up(&bLinkNetif) && (!link_state))
        {
            /* network cable is disconnected */
            netif_set_link_down(&bLinkNetif);
            netif_set_down(&bLinkNetif);
        }
    }
    if (netif_is_link_up(&bLinkNetif))
    {
        p = NULL;
        bRead(bLinkFd, (uint8_t *)&p, 0);
        if (p != NULL)
        {
            err = bLinkNetif.input(p, &bLinkNetif);
            if (err != ERR_OK)
            {
                pbuf_free(p);
                p = NULL;
            }
        }
    }
    sys_check_timeouts();
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section bos_polling
#endif
BOS_REG_POLLING_FUNC(_bNetlinkCore);
#ifdef BSECTION_NEED_PRAGMA
#pragma section 
#endif
int bNetlinkInit()
{
#if (defined(_NETIF_USE_LWIP) && (_NETIF_USE_LWIP == 1))
    ip4_addr_t ip_addr;
    ip4_addr_t netmask;
    ip4_addr_t gw_addr;

    lwip_init();
#if (defined(NETIF_USE_STATIC_IP) && (NETIF_USE_STATIC_IP == 1))
    b_assert_log(ip4addr_aton(NETIF_IP_ADDRESS, &ip_addr));
    b_assert_log(ip4addr_aton(NETIF_GW_ADDRESS, &gw_addr));
    b_assert_log(ip4addr_aton(NETIF_MASK_ADDRESS, &netmask));
#else
    ip_addr.addr = 0;
    gw_addr.addr = 0;
    netmask.addr = 0;
#endif
    netif_add(&bLinkNetif, &ip_addr, &netmask, &gw_addr, NULL, _bNetifInit, ethernet_input);
    netif_set_default(&bLinkNetif);

    if (netif_is_link_up(&bLinkNetif))
    {
        /* When the netif is fully configured this function must be called */
        netif_set_up(&bLinkNetif);
#if (defined(NETIF_USE_DHCP) && (NETIF_USE_DHCP == 1))
        _bNetifDhcpStart(&bLinkNetif);
#else
        ip_addr_t dns_addr;
        dns_addr.addr = 0x08080808;
        dns_setserver(0, &dns_addr);
        dns_addr.addr = 0x72727272;
        dns_setserver(1, &dns_addr);
#endif
    }
    else
    {
        /* When the netif link is down this function must be called */
        netif_set_down(&bLinkNetif);
    }
    netif_set_link_callback(&bLinkNetif, _bNetifLinkUpdate);
#endif
    return 0;
}

#else

int bNetlinkInit()
{
    return 0;
}

#endif

#endif

uint32_t sys_now()
{
    return bHalGetSysTick();
}
