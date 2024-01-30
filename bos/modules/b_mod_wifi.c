/**
 *!
 * \file        b_mod_wifi.c
 * \version     v0.0.1
 * \date        2020/05/16
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "modules/inc/b_mod_wifi.h"

#if (defined(_WIFI_ENABLE) && (_WIFI_ENABLE == 1))
#include "core/inc/b_core.h"
#include "drivers/inc/b_driver_cmd.h"
#include "thirdparty/cjson/cjson.h"
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"

#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))
#include "modules/inc/b_mod_netif/b_mod_trans.h"
#include "utils/inc/b_util_fifo.h"

static LIST_HEAD(bWifiTransList);

typedef struct
{
    void            *cb_arg;
    pbTransCb_t      callback;
    uint8_t          readable;
    uint8_t          writeable;
    bFIFO_Info_t     rx_fifo;
    uint16_t         local_port;
    uint16_t         remote_port;
    char             remote_ip[REMOTE_ADDR_LEN_MAX + 1];
    bTransType_t     type;
    struct list_head list;
} bTrans_t;

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
 * \addtogroup WIFI
 * \{
 */

/**
 * \defgroup WIFI_Private_TypesDefinitions
 * \{
 */

typedef enum
{
    WIFI_CMD_APCONFIGNET = 0,
    WIFI_CMD_CONN_AP,
    WIFI_CMD_CONN_TCP,
    WIFI_CMD_CONN_UDP,
    WIFI_CMD_DISCONN,
    WIFI_CMD_PING,
    WIFI_CMD_SEND,
} bWifiCmd_t;

typedef struct
{
    int        fd;
    uint8_t    busy;
    bWifiCmd_t cmd;
    union
    {
        bApInfo_t ap;
    } param;
    pWifiEvtCb_t cb;
    void        *user_data;
} bWifiModule_t;

/**
 * \}
 */

/**
 * \defgroup WIFI_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup WIFI_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup WIFI_Private_Variables
 * \{
 */

static bWifiModule_t bWifiModule = {
    .fd   = -1,
    .busy = 0,
};

/**
 * \}
 */

/**
 * \defgroup WIFI_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup WIFI_Private_Functions
 * \{
 */

static void _bWifiFree(void *addr)
{
    bFree(addr);
}

static void _bWifiResult(uint8_t cmd, uint8_t isok, void *arg, void (*release)(void *))
{
    bWifiModule.busy = 0;
    if (cmd == WIFI_CMD_APCONFIGNET)
    {
        if (isok == 0)
        {
            bWifiModule.cb(B_WIFI_EVT_APCFGNET, NULL, NULL, bWifiModule.user_data);
        }
        else
        {
            bWifiModule.cb(B_WIFI_EVT_APCFGNET, arg, release, bWifiModule.user_data);
        }
    }
    else if (cmd == WIFI_CMD_CONN_AP)
    {
        bWifiModule.cb(B_WIFI_EVT_CONN_AP, &isok, NULL, bWifiModule.user_data);
    }
    else if (bWifiModule.cmd == WIFI_CMD_CONN_TCP)
    {
        bWifiModule.cb(B_WIFI_EVT_CONN_TCP, &isok, NULL, bWifiModule.user_data);
    }
    else if (bWifiModule.cmd == WIFI_CMD_CONN_UDP)
    {
        bWifiModule.cb(B_WIFI_EVT_CONN_UDP, &isok, NULL, bWifiModule.user_data);
    }
    else if (bWifiModule.cmd == WIFI_CMD_DISCONN)
    {
        bWifiModule.cb(B_WIFI_EVT_DISCONN, &isok, NULL, bWifiModule.user_data);
    }
    else if (bWifiModule.cmd == WIFI_CMD_PING)
    {
        bWifiModule.cb(B_WIFI_EVT_PING, &isok, NULL, bWifiModule.user_data);
    }
    else if (bWifiModule.cmd == WIFI_CMD_SEND)
    {
        bWifiModule.cb(B_WIFI_EVT_SEND, &isok, NULL, bWifiModule.user_data);
    }
}

static void _bWifiApCfgNetHandle(bWifiDrvEvent_t event, void *arg, void (*release)(void *),
                                 void *user_data)
{
    int            retval = -1;
    bTcpUdpInfo_t  info;
    bTcpUdpData_t *pdat = NULL;
    if (event == B_EVT_MODE_STA_AP_OK)
    {
        memset(&info, 0, sizeof(bTcpUdpInfo_t));
        info.port = 666;
        retval    = bCtl(bWifiModule.fd, bCMD_WIFI_LOCAL_TCP_SERVER, &info);
        if (retval < 0)
        {
            _bWifiResult(bWifiModule.cmd, 0, NULL, NULL);
        }
    }
    else if (event == B_EVT_LOCAL_TCP_SERVER_OK)
    {
        ;
    }
}

static void _bWifiConnApHandle(bWifiDrvEvent_t event, void *arg, void (*release)(void *),
                               void *user_data)
{
    int retval = -1;
    if (event == B_EVT_MODE_STA_OK)
    {
        retval = bCtl(bWifiModule.fd, bCMD_WIFI_JOIN_AP, &bWifiModule.param.ap);
        if (retval < 0)
        {
            _bWifiResult(bWifiModule.cmd, 0, NULL, NULL);
        }
    }
    else if (event == B_EVT_JOIN_AP_OK)
    {
        _bWifiResult(bWifiModule.cmd, 1, NULL, NULL);
    }
}

static void _bWifiDrvCb(bWifiDrvEvent_t event, void *arg, void (*release)(void *), void *user_data)
{
#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))
    bTrans_t *ptrans = (bTrans_t *)user_data;
#endif
    bWifiApInfo_t apinfo;
    b_log_w("[%d]evt:%d %p\r\n", bWifiModule.cmd, event, arg);
    if (event < 0)
    {
        _bWifiResult(bWifiModule.cmd, 0, NULL, NULL);
        return;
    }
    if (event == B_EVT_CONN_NEW_DATA && arg != NULL)
    {
        bTcpUdpData_t *pdat = (bTcpUdpData_t *)arg;
        if (pdat->len > 0 && pdat->pbuf != NULL)
        {
#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))
            struct list_head *pos    = NULL;
            bTrans_t         *ptrans = NULL;
            list_for_each(pos, &bWifiTransList)
            {
                ptrans = list_entry(pos, bTrans_t, list);
                if (strcmp(ptrans->remote_ip, pdat->conn.ip) == 0 &&
                    pdat->conn.port == ptrans->remote_port)
                {
                    bFIFO_Write(&ptrans->rx_fifo, pdat->pbuf, pdat->len);
                    ptrans->readable = 1;
                    ptrans->callback(B_TRANS_NEW_DATA, NULL, ptrans->cb_arg);
                    break;
                }
            }
#endif
            bWifiNewData_t new_data;
            new_data.pbuf = pdat->pbuf;
            new_data.len  = pdat->len;
            bWifiModule.cb(B_WIFI_EVT_RECV, &new_data, NULL, bWifiModule.user_data);

            if (bWifiModule.cmd == WIFI_CMD_APCONFIGNET)
            {
                memset(&apinfo, 0, sizeof(bWifiApInfo_t));
                cJSON *root = cJSON_Parse((const char *)pdat->pbuf);
                if (root != NULL)
                {
                    cJSON *ssid   = cJSON_GetObjectItem(root, "ssid");
                    cJSON *passwd = cJSON_GetObjectItem(root, "passwd");
                    if (ssid != NULL && cJSON_IsString(ssid) &&
                        strlen(ssid->valuestring) <= WIFI_SSID_LEN_MAX)
                    {
                        memcpy(&apinfo.ssid[0], ssid->valuestring, strlen(ssid->valuestring));
                    }
                    if (passwd != NULL && cJSON_IsString(passwd) &&
                        strlen(passwd->valuestring) <= WIFI_PASSWD_LEN_MAX)
                    {
                        memcpy(&apinfo.passwd[0], passwd->valuestring, strlen(passwd->valuestring));
                    }
                    cJSON_Delete(root);
                    if (strlen(apinfo.ssid) > 0)
                    {
                        _bWifiResult(bWifiModule.cmd, 1, &apinfo, NULL);
                    }
                }
            }
        }
        if (release)
        {
            if (arg)
            {
                release(arg);
            }
        }
    }

    if (bWifiModule.cmd == WIFI_CMD_APCONFIGNET)
    {
        _bWifiApCfgNetHandle(event, arg, release, user_data);
    }
    else if (bWifiModule.cmd == WIFI_CMD_CONN_AP)
    {
        _bWifiConnApHandle(event, arg, release, user_data);
    }
    else if (bWifiModule.cmd == WIFI_CMD_CONN_TCP)
    {
        if (event == B_EVT_CONN_TCP_SERVER_OK)
        {
#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))
            if (ptrans != NULL)
            {
                ptrans->writeable = 1;
                ptrans->callback(B_TRANS_CONNECTED, NULL, ptrans->cb_arg);
            }
#endif
            _bWifiResult(bWifiModule.cmd, 1, NULL, NULL);
        }
    }
    else if (bWifiModule.cmd == WIFI_CMD_CONN_UDP)
    {
        if (event == B_EVT_CONN_UDP_SERVER_OK)
        {
#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))
            if (ptrans != NULL)
            {
                ptrans->writeable = 1;
                ptrans->callback(B_TRANS_CONNECTED, NULL, ptrans->cb_arg);
            }
#endif
            _bWifiResult(bWifiModule.cmd, 1, NULL, NULL);
        }
    }
    else if (bWifiModule.cmd == WIFI_CMD_DISCONN)
    {
        if (event == B_EVT_CLOSE_CONN_OK)
        {
            _bWifiResult(bWifiModule.cmd, 1, NULL, NULL);
        }
    }
    else if (bWifiModule.cmd == WIFI_CMD_PING)
    {
        if (event == B_EVT_PING_OK)
        {
            _bWifiResult(bWifiModule.cmd, 1, NULL, NULL);
        }
    }
    else if (bWifiModule.cmd == WIFI_CMD_SEND)
    {
        if (event == B_EVT_CONN_SEND_OK)
        {
            _bWifiResult(bWifiModule.cmd, 1, NULL, NULL);
        }
    }
}

static int _bWifiSetDrvCbArg(void *arg)
{
    int retval = -1;
    if (bWifiModule.busy)
    {
        b_log_e("busy cmd:%d\r\n", bWifiModule.cmd);
        return -1;
    }
    retval = bCtl(bWifiModule.fd, bCMD_WIFI_SET_CALLBACK_ARG, arg);
    return retval;
}

/**
 * \}
 */

/**
 * \addtogroup WIFI_Exported_Functions
 * \{
 */

int bWifiInit(uint32_t dev_no, pWifiEvtCb_t cb, void *user_data)
{
    bWifiDrvCallback_t drv_cb;
    if (bWifiModule.fd >= 0 || cb == NULL)
    {
        return -1;
    }
    int fd = bOpen(dev_no, BCORE_FLAG_RW);
    if (fd < 0)
    {
        return -1;
    }
    bWifiModule.fd        = fd;
    bWifiModule.cb        = cb;
    bWifiModule.user_data = user_data;
    drv_cb.cb             = _bWifiDrvCb;
    drv_cb.user_data      = NULL;
    bCtl(fd, bCMD_WIFI_REG_CALLBACK, &drv_cb);
    return 0;
}

int bWifiDeinit()
{
    if (bWifiModule.fd < 0)
    {
        return -1;
    }
    bClose(bWifiModule.fd);
    bWifiModule.fd = -1;
    return 0;
}

int bWifiApConfigNet(const char *ssid, const char *passwd)
{
    int retval = -1;
    if (bWifiModule.busy != 0 || bWifiModule.fd < 0 || ssid == NULL)
    {
        return -1;
    }
    if (strlen(ssid) > WIFI_SSID_LEN_MAX)
    {
        return -1;
    }
    if (passwd != NULL)
    {
        if (strlen(passwd) > WIFI_PASSWD_LEN_MAX)
        {
            return -1;
        }
        if (strlen(passwd) == 0)
        {
            passwd = NULL;
        }
    }
    bApInfo_t ap_info;
    memset(&ap_info, 0, sizeof(ap_info));
    memcpy(&ap_info.ssid[0], ssid, strlen(ssid));
    if (passwd == NULL)
    {
        ap_info.encryption = 0;
    }
    else
    {
        ap_info.encryption = 3;
        memcpy(&ap_info.passwd[0], passwd, strlen(passwd));
    }
    retval = bCtl(bWifiModule.fd, bCMD_WIFI_MODE_STA_AP, &ap_info);
    if (retval >= 0)
    {
        bWifiModule.cmd  = WIFI_CMD_APCONFIGNET;
        bWifiModule.busy = 1;
    }
    return retval;
}

int bWifiJoinAp(const char *ssid, const char *passwd)
{
    int retval = -1;
    if (bWifiModule.busy != 0 || bWifiModule.fd < 0 || ssid == NULL)
    {
        return -1;
    }
    if (strlen(ssid) > WIFI_SSID_LEN_MAX)
    {
        return -1;
    }
    if (passwd != NULL)
    {
        if (strlen(passwd) > WIFI_PASSWD_LEN_MAX)
        {
            return -1;
        }
        if (strlen(passwd) == 0)
        {
            passwd = NULL;
        }
    }
    memset(&bWifiModule.param.ap, 0, sizeof(bApInfo_t));
    memcpy(&bWifiModule.param.ap.ssid[0], ssid, strlen(ssid));
    if (passwd == NULL)
    {
        bWifiModule.param.ap.encryption = 0;
    }
    else
    {
        bWifiModule.param.ap.encryption = 3;
        memcpy(&bWifiModule.param.ap.passwd[0], passwd, strlen(passwd));
    }
    retval = bCtl(bWifiModule.fd, bCMD_WIFI_MODE_STA, NULL);
    if (retval >= 0)
    {
        bWifiModule.cmd  = WIFI_CMD_CONN_AP;
        bWifiModule.busy = 1;
    }
    return retval;
}

int bWifiConnTcp(const char *remote, uint16_t port)
{
    int           retval = -1;
    bTcpUdpInfo_t info;
    if (bWifiModule.busy != 0 || bWifiModule.fd < 0 || remote == NULL ||
        strlen(remote) > WIFI_REMOTE_ADDR_LEN_MAX || strlen(remote) == 0)
    {
        return -1;
    }
    memset(&info, 0, sizeof(bTcpUdpInfo_t));
    memcpy(&info.ip[0], remote, strlen(remote));
    info.port = port;
    retval    = bCtl(bWifiModule.fd, bCMD_WIFI_REMOT_TCP_SERVER, &info);
    if (retval >= 0)
    {
        bWifiModule.cmd  = WIFI_CMD_CONN_TCP;
        bWifiModule.busy = 1;
    }
    return retval;
}

int bWifiConnUdp(const char *remote, uint16_t port)
{
    int           retval = -1;
    bTcpUdpInfo_t info;
    if (bWifiModule.busy != 0 || bWifiModule.fd < 0 || remote == NULL ||
        strlen(remote) > WIFI_REMOTE_ADDR_LEN_MAX || strlen(remote) == 0)
    {
        return -1;
    }
    memset(&info, 0, sizeof(bTcpUdpInfo_t));
    memcpy(&info.ip[0], remote, strlen(remote));
    info.port = port;
    retval    = bCtl(bWifiModule.fd, bCMD_WIFI_REMOT_UDP_SERVER, &info);
    if (retval >= 0)
    {
        bWifiModule.cmd  = WIFI_CMD_CONN_UDP;
        bWifiModule.busy = 1;
    }
    return retval;
}

int bWifiDisconn(const char *remote, uint16_t port)
{
    int           retval = -1;
    bTcpUdpInfo_t info;
    if (bWifiModule.busy != 0 || bWifiModule.fd < 0 || remote == NULL ||
        strlen(remote) > WIFI_REMOTE_ADDR_LEN_MAX || strlen(remote) == 0)
    {
        b_log_e("err: %d %d %p %d %s\r\n", bWifiModule.busy, bWifiModule.fd, remote, strlen(remote),
                remote);
        return -1;
    }
    memset(&info, 0, sizeof(bTcpUdpInfo_t));
    memcpy(&info.ip[0], remote, strlen(remote));
    info.port = port;
    retval    = bCtl(bWifiModule.fd, bCMD_WIFI_TCPUDP_CLOSE, &info);
    if (retval >= 0)
    {
        bWifiModule.cmd  = WIFI_CMD_DISCONN;
        bWifiModule.busy = 1;
    }
    return retval;
}

int bWifiPing(const char *remote)
{
    int retval = -1;
    if (bWifiModule.busy != 0 || bWifiModule.fd < 0 || remote == NULL ||
        strlen(remote) > WIFI_REMOTE_ADDR_LEN_MAX || strlen(remote) == 0)
    {
        return -1;
    }
    retval = bCtl(bWifiModule.fd, bCMD_WIFI_PING, (void *)remote);
    if (retval >= 0)
    {
        bWifiModule.cmd  = WIFI_CMD_PING;
        bWifiModule.busy = 1;
    }
    return retval;
}

int bWifiSend(const char *remote, uint16_t port, uint8_t *pbuf, uint16_t len)
{
    int           retval = -1;
    bTcpUdpData_t dat;
    if (bWifiModule.busy != 0 || bWifiModule.fd < 0 || remote == NULL ||
        strlen(remote) > WIFI_REMOTE_ADDR_LEN_MAX || strlen(remote) == 0 || pbuf == NULL ||
        len == 0)
    {
        return -1;
    }
    memset(&dat, 0, sizeof(bTcpUdpData_t));
    memcpy(&dat.conn.ip[0], remote, strlen(remote));
    dat.conn.port = port;
    dat.pbuf      = bMalloc(len);
    if (dat.pbuf == NULL)
    {
        return -2;
    }
    memcpy(dat.pbuf, pbuf, len);
    dat.len     = len;
    dat.release = _bWifiFree;
    retval      = bCtl(bWifiModule.fd, bCMD_WIFI_TCPUDP_SEND, &dat);
    if (retval >= 0)
    {
        bWifiModule.cmd  = WIFI_CMD_SEND;
        bWifiModule.busy = 1;
    }
    else
    {
        bFree(dat.pbuf);
        dat.pbuf = NULL;
    }
    return retval;
}

#if (defined(_NETIF_ENABLE) && (_NETIF_ENABLE == 1))

int bSocket(bTransType_t type, pbTransCb_t cb, void *user_data)
{
    int result;
    if (cb == NULL || (type != B_TRANS_CONN_TCP && type != B_TRANS_CONN_UDP))
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)bMalloc(sizeof(bTrans_t));
    if (ptrans == NULL)
    {
        return -2;
    }
    memset(ptrans, 0, sizeof(bTrans_t));
    uint8_t *pbuf = (uint8_t *)bMalloc(CONNECT_RECVBUF_MAX);
    if (pbuf == NULL)
    {
        bFree(ptrans);
        ptrans = NULL;
        return -2;
    }
    bFIFO_Init(&ptrans->rx_fifo, pbuf, CONNECT_RECVBUF_MAX);
    ptrans->type      = type;
    ptrans->callback  = cb;
    ptrans->cb_arg    = user_data;
    ptrans->readable  = 0;
    ptrans->writeable = 0;
    list_add(&ptrans->list, &bWifiTransList);
    return (int)ptrans;
}

int bConnect(int sockfd, char *remote, uint16_t port)
{
    int retval = -1;
    if (sockfd <= 0 || remote == NULL || strlen(remote) > REMOTE_ADDR_LEN_MAX ||
        strlen(remote) == 0)
    {
        b_log_e("%d %p %d\r\n", sockfd, remote, port);
        return -1;
    }
    bTrans_t *ptrans    = (bTrans_t *)sockfd;
    ptrans->remote_port = port;
    memset(ptrans->remote_ip, 0, REMOTE_ADDR_LEN_MAX + 1);
    memcpy(ptrans->remote_ip, remote, strlen(remote));
    if (_bWifiSetDrvCbArg(ptrans) < 0)
    {
        return -2;
    }
    if (ptrans->type == B_TRANS_CONN_TCP)
    {
        retval = bWifiConnTcp(ptrans->remote_ip, ptrans->remote_port);
    }
    else
    {
        retval = bWifiConnUdp(ptrans->remote_ip, ptrans->remote_port);
    }
    return retval;
}

int bBind(int sockfd, uint16_t port)
{
    return -1;
}

int bListen(int sockfd, int backlog)
{
    return -1;
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
    int      retval        = -1;
    if (sockfd < 0 || pbuf == NULL || buf_len == 0)
    {
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    if (_bWifiSetDrvCbArg(ptrans) < 0)
    {
        return -2;
    }
    retval = bWifiSend(ptrans->remote_ip, ptrans->remote_port, pbuf, buf_len);
    if (retval < 0)
    {
        return -2;
    }
    b_log("send %d \r\n", retval);
    if (retval >= 0 && wlen != NULL)
    {
        *wlen = buf_len;
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
    int retval = -1;
    if (sockfd < 0)
    {
        b_log_e("sockfd == -1\r\n");
        return -1;
    }
    bTrans_t *ptrans = (bTrans_t *)sockfd;
    __list_del(ptrans->list.prev, ptrans->list.next);
    if ((retval = _bWifiSetDrvCbArg(NULL)) < 0)
    {
        b_log_e("set arg error..%d\r\n", retval);
        return -2;
    }
    bWifiDisconn(ptrans->remote_ip, ptrans->remote_port);
    bFree(ptrans->rx_fifo.pbuf);
    bFIFO_Deinit(&ptrans->rx_fifo);
    bFree(ptrans);
    ptrans = NULL;
    return 0;
}

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

/**
 * \}
 */
#endif

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
