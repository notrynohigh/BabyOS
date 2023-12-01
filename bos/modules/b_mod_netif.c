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

#if LWIP_NETIF_HOSTNAME
    /* Initialize interface hostname */
    netif->hostname = bDeviceDescription(pbnetif->mac_dev);
#endif /* LWIP_NETIF_HOSTNAME */

    netif->name[0] = pbnetif->mac_dev % 10 + '0';
    netif->name[1] = (pbnetif->mac_dev % 100) / 10 + '0';

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
        lwip_init();
    }
    if (pInstance == NULL)
    {
        return -1;
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
