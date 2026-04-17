/**
 *!
 * \file        b_drv_esp12f.c
 * \version     v0.0.1
 * \date        2020/02/05
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
#include "drivers/inc/b_drv_esp12f.h"

#include <stdio.h>

#include "core/inc/b_task.h"
#include "utils/inc/b_util_at.h"
#include "utils/inc/b_util_list.h"
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"
#include "utils/inc/b_util_tools.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup B_DRIVER
 * \{
 */

/**
 * \addtogroup ESP12F
 * \{
 */

/**
 * \defgroup ESP12F_Private_Defines
 * \{
 */
#define DRIVER_NAME ESP12F

#define DRIVER_PRIVATE_TYPE bEsp12fPrivate_t

#define DRIVER_ESP12F_DEBUG_ENABLE 0

#ifndef ESP12F_CMD_TIMEOUT
#define WIFIMODULE_CMD_TIMEOUT (5000)
#else
#define WIFIMODULE_CMD_TIMEOUT (ESP12F_CMD_TIMEOUT)
#endif

#define WIFIMODULE_TCPUDP_PCB_NUM (5)
#ifdef ESP12F_LISTEN_PCB_NUMBER
#define WIFIMODULE_LISTEN_PCB_NUM (ESP12F_LISTEN_PCB_NUMBER)
#else
#define WIFIMODULE_LISTEN_PCB_NUM (2)
#endif
#define WIFIMODULE_PCB_NUM_MAX (WIFIMODULE_TCPUDP_PCB_NUM + WIFIMODULE_LISTEN_PCB_NUM)
#define WIFIMODULE_TCPUDP_PCB_INDEX_IS_VALID(index) (index < WIFIMODULE_TCPUDP_PCB_NUM)
#define WIFIMODULE_LISTEN_PCB_INDEX_IS_VALID(index) \
    (index >= WIFIMODULE_TCPUDP_PCB_NUM && (index < WIFIMODULE_PCB_NUM_MAX))
#define WIFIMODULE_PCB_INDEX_IS_VALID(index) (index < WIFIMODULE_PCB_NUM_MAX)

#define WIFIMODULE_PRIV_CMD_PCB_CONNECT (bCMD_WIFI_NUMBER_MAX + 0)
#define WIFIMODULE_PRIV_CMD_PCB_SENDDATA (bCMD_WIFI_NUMBER_MAX + 1)
#define WIFIMODULE_PRIV_CMD_PCB_DISCONNECT (bCMD_WIFI_NUMBER_MAX + 2)
#define WIFIMODULE_PRIV_CMD_PCB_LISTEN (bCMD_WIFI_NUMBER_MAX + 3)

#define WIFIMODULE_CMD_RESULT_OK (0x01)
#define WIFIMODULE_CMD_RESULT_FAIL (0x02)

#define WIFIMODULE_CLEAR_CMD_RESULT(p) (((p)->cmd_ctx.cmd_result) = 0)
#define WIFIMODULE_SET_CMD_RESULT_OK(p) (((p)->cmd_ctx.cmd_result) = 1)
#define WIFIMODULE_SET_CMD_RESULT_FAIL(p) (((p)->cmd_ctx.cmd_result) = 2)
#define WIFIMODULE_CMD_RESULT_IS_OK(p) (((p)->cmd_ctx.cmd_result) == 1)
#define WIFIMODULE_CMD_RESULT_IS_FAIL(p) (((p)->cmd_ctx.cmd_result) == 2)

#define WIFIMODULE_CMD_IS_FORCE_WAIT(timeout) ((timeout) & 0x8000)
#define WIFIMODULE_CMD_SET_FORCE_WAIT(timeout) (((timeout) & 0x7fff) | 0x8000)
#define WIFIMODULE_CMD_TIMEOUT_GET(timeout) (((timeout) & 0x7fff))

#define WIFIMODULE_CONN_NUMBER_RESET(p) (((p)->link_number) = 0)
#define WIFIMODULE_CONN_NUMBER_INCREASE(p) (((p)->link_number) += 1)
#define WIFIMODULE_CONN_NUMBER_DECREASE(p) (((p)->link_number > 0) ? (((p)->link_number) -= 1) : 0)
#define WIFIMODULE_CONN_NUMBER_GET(p) (((p)->link_number))

#define WIFIMODULE_IS_BUSY(p) (((p)->busy) != 0)
#define WIFIMODULE_SET_BUSY(p) (((p)->busy) = 1)
#define WIFIMODULE_CLEAR_BUSY(p) (((p)->busy) = 0)

#define WIFIMODULE_CLEAR_CTL_CMD(p) (((p)->ctl_ctx.ctl_cmd) = 0xff)
#define WIFIMODULE_SET_CTL_CMD(p, cmd) (((p)->ctl_ctx.ctl_cmd) = (cmd))
#define WIFIMODULE_GET_CTL_CMD(p) ((p)->ctl_ctx.ctl_cmd)
#define WIFIMODULE_IS_PRIV_CMD(p) (((p)->ctl_ctx.ctl_cmd) >= bCMD_WIFI_NUMBER_MAX)

#ifndef ESP12F_CMD_TX_BUF_LEN
#define WIFIMODULE_SENDBUF_MAX (128)
#else
#define WIFIMODULE_SENDBUF_MAX (ESP12F_CMD_TX_BUF_LEN)
#endif

#ifndef ESP12F_CMD_RX_BUF_LEN
#define WIFIMODULE_RECVBUF_MAX (1024)
#else
#define WIFIMODULE_RECVBUF_MAX (ESP12F_CMD_RX_BUF_LEN)
#endif

#define WIFIMODULE_CMD_TABLE_NUMBER_MAX (3)

#define WIFI_PCB_STATE_IDLE (0)
#define WIFI_PCB_STATE_ASSIGNED (WIFI_PCB_STATE_IDLE + 1)
#define WIFI_PCB_STATE_WAIT_LISTEN (WIFI_PCB_STATE_ASSIGNED + 1)
#define WIFI_PCB_STATE_WAIT_STOP_LISTEN (WIFI_PCB_STATE_WAIT_LISTEN + 1)
#define WIFI_PCB_STATE_LISTENING (WIFI_PCB_STATE_WAIT_STOP_LISTEN + 1)
#define WIFI_PCB_STATE_LISTEN_STOP (WIFI_PCB_STATE_LISTENING + 1)
#define WIFI_PCB_STATE_ACCEPT (WIFI_PCB_STATE_LISTEN_STOP + 1)
#define WIFI_PCB_STATE_WAIT_CONNECT (WIFI_PCB_STATE_ACCEPT + 1)
#define WIFI_PCB_STATE_CONNECTING (WIFI_PCB_STATE_WAIT_CONNECT + 1)
#define WIFI_PCB_STATE_CONNECTED (WIFI_PCB_STATE_CONNECTING + 1)
#define WIFI_PCB_STATE_SENDING (WIFI_PCB_STATE_CONNECTED + 1)
#define WIFI_PCB_STATE_WAIT_DISCONNECT (WIFI_PCB_STATE_SENDING + 1)
#define WIFI_PCB_STATE_DISCONNECTING (WIFI_PCB_STATE_WAIT_DISCONNECT + 1)

const char *bWifiPcbStateStr[] = {"IDLE",         "ASSIGNED",    "WAIT_LISTEN", "WAIT_STOP_LISTEN",
                                  "LISTENING",    "LISTEN_STOP", "ACCEPT",      "WAIT_CONNECT",
                                  "CONNECTING",   "CONNECTED",   "SENDING",     "WAIT_DISCONNECT",
                                  "DISCONNECTING"};

#define WIFI_PCB_STATE_IS_WRITEABLE(state) \
    (((state) == WIFI_PCB_STATE_CONNECTED) || ((state) == WIFI_PCB_STATE_SENDING))

#define WIFI_STA_CONNECTED_STR "WIFI GOT IP"
#define WIFI_STA_DISCONNECTED_STR "WIFI DISCONNECT"
#define WIFI_IP_CONNECT_STR ",CONNECT"
#define WIFI_IP_RECV_DATA_STR "+IPD,"
#define WIFI_IP_DISCONNECT_STR ",CLOSED"
#define WIFI_AP_CONNECTED_STR "+STA_CONNECTED:"
#define WIFI_AP_DISCONNECTED_STR "+STA_DISCONNECTED:"
/**
 * \}
 */

/**
 * \defgroup ESP12F_Private_TypesDefinitions
 * \{
 */

typedef uint16_t (*pbAtCmdFunc_t)(void *arg, char **pcmdbuf);
typedef void (*pbRelease_t)(void *p);
typedef struct
{
    char         *pcmd;
    char         *resp;
    uint16_t      timeout;
    pbAtCmdFunc_t cmd_f;
} bWifiModuleCmdUnit_t;

typedef struct
{
    bWifiDrvEvent_t ok_event;
    bWifiDrvEvent_t fail_event;
} bWifiModuleCtlEvent_t;

typedef struct
{
    uint8_t number;
    struct
    {
        const bWifiModuleCmdUnit_t *pcmdunit;
        uint8_t                     unit_number;
    } cmd_table[WIFIMODULE_CMD_TABLE_NUMBER_MAX];
} bWifiModuleCmd_t;

typedef struct
{
    uint8_t       busy;
    uint8_t       link_number;
    bMacAddress_t mac;
    bTaskAttr_t   task_attr;
    struct
    {
        uint8_t ctl_cmd;
        union
        {
            bApInfo_t ap;
        } ctl_param;
    } ctl_ctx;
    struct
    {
        uint8_t          cmd_index;
        uint8_t          cmd_result;
        uint8_t          cmd_unit_index;
        bWifiModuleCmd_t cmd_data;
    } cmd_ctx;
    struct
    {
        bLinkStateCb_t     link_cb;
        pTcpIpCallback_t   tcpip_cb;
        void              *tcpip_cb_arg;
        bWifiDrvCallback_t cb;
    } cb_ctx;
    struct
    {
        bAtStruct_t        at;
        bHalUartIdleAttr_t uart_attr;
        uint8_t            uart_recv_buf[WIFIMODULE_RECVBUF_MAX + 1];
        uint8_t            uart_send_buf[WIFIMODULE_SENDBUF_MAX + 1];
    } at_ctx;

    struct
    {
        uint8_t  is_tcp;
        uint8_t  state;
        uint32_t ip;
        uint16_t port;
        uint16_t local_port;
        void    *pcb;
        struct
        {
            const uint8_t *pbuf;
            uint16_t       len;
        } send_data;
    } pcb_ctx[WIFIMODULE_PCB_NUM_MAX];
} bEsp12fPrivate_t;

typedef struct
{
    uint8_t pcb_index;
    void *private;
} bWifiModulePcb_t;

/**
 * \}
 */

/**
 * \defgroup ESP12F_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ESP12F_Private_FunctionPrototypes
 * \{
 */
static uint16_t _bSetApInfo(void *arg, char **pcmdbuf);
static uint16_t _bJoinAp(void *arg, char **pcmdbuf);
static uint16_t _bConnectRemote(void *arg, char **pcmdbuf);
static uint16_t _bTcpUdpSendStart(void *arg, char **pcmdbuf);
static uint16_t _bTcpUdpSendData(void *arg, char **pcmdbuf);
static uint16_t _bDisconnectRemote(void *arg, char **pcmdbuf);
static uint16_t _bTcpListen(void *arg, char **pcmdbuf);
/**
 * \}
 */

/**
 * \defgroup ESP12F_Private_Variables
 * \{
 */

bDRIVER_HALIF_TABLE(bESP12F_HalIf_t, DRIVER_NAME);

static DRIVER_PRIVATE_TYPE bEspRunInfo[bDRIVER_HALIF_NUM(bESP12F_HalIf_t, DRIVER_NAME)];

const static bWifiModuleCtlEvent_t bWifiCtlEvent[bCMD_WIFI_NUMBER_MAX] = {
    {0, 0},                                          // reserved
    {B_EVT_MODE_STA_OK, B_EVT_MODE_STA_FAIL},        // bCMD_WIFI_MODE_STA
    {B_EVT_MODE_AP_OK, B_EVT_MODE_AP_FAIL},          // bCMD_WIFI_MODE_AP
    {B_EVT_MODE_STA_AP_OK, B_EVT_MODE_STA_AP_FAIL},  // bCMD_WIFI_MODE_STA_AP
    {B_EVT_JOIN_AP_OK, B_EVT_JOIN_AP_FAIL},          // bCMD_WIFI_JOIN_AP
};

const static bWifiModuleCmdUnit_t bEspCmdReset[] = {
    {"AT\r\n", "OK", 300, NULL}, {"AT+RST\r\n", "OK", WIFIMODULE_CMD_SET_FORCE_WAIT(4000), NULL}};

const static bWifiModuleCmdUnit_t bEspCmdInit[]      = {{"AT\r\n", "OK", 300, NULL},
                                                        {"ATE0\r\n", "OK", 300, NULL},
                                                        {"AT+CIPSTAMAC_CUR?\r\n", "OK", 300, NULL}};
const static bWifiModuleCmdUnit_t bEspCmdStaMode[]   = {{"AT+CWMODE=1\r\n", "OK", 300, NULL},
                                                        {"AT+CIPMUX=1\r\n", "OK", 300, NULL}};
const static bWifiModuleCmdUnit_t bEspCmdApMode[]    = {{"AT+CWMODE=2\r\n", "OK", 300, NULL},
                                                        {NULL, NULL, 300, _bSetApInfo},
                                                        {"AT+CIPMUX=1\r\n", "OK", 300, NULL}};
const static bWifiModuleCmdUnit_t bEspCmdApStaMode[] = {{"AT+CWMODE=3\r\n", "OK", 300, NULL},
                                                        {NULL, NULL, 300, _bSetApInfo},
                                                        {"AT+CIPMUX=1\r\n", "OK", 300, NULL}};
const static bWifiModuleCmdUnit_t bEspCmdJoinAp[]    = {{NULL, "OK,WIFI GOT IP", 15000, _bJoinAp}};

const static bWifiModuleCmdUnit_t bEspCmdConnectRemote[] = {
    {NULL, "OK,ALREADY CONNECTED", 1000, _bConnectRemote}};

const static bWifiModuleCmdUnit_t bEspCmdSendData[] = {{NULL, ">", 300, _bTcpUdpSendStart},
                                                       {NULL, "SEND OK", 1000, _bTcpUdpSendData}};

const static bWifiModuleCmdUnit_t bEspCmdDisconnectRemote[] = {
    {NULL, "OK,UNLINK", 300, _bDisconnectRemote}};

const static bWifiModuleCmdUnit_t bEspCmdTcpListen[] = {{NULL, "OK", 300, _bTcpListen}};

/*/**
 * \brief
 */

/**
 * \defgroup ESP12F_Private_Functions
 * \{
 */

static void _bWifiInvokeLinkCb(DRIVER_PRIVATE_TYPE *_priv)
{
    if (_priv->cb_ctx.link_cb.cb)
    {
        _priv->cb_ctx.link_cb.cb((WIFIMODULE_CONN_NUMBER_GET(_priv) > 0) ? 1 : 0,
                                 _priv->cb_ctx.link_cb.arg);
    }
}

static void _bWifiInvokeTcpIpCb(DRIVER_PRIVATE_TYPE *_priv, bTcpIpEvent_t event, void *param)
{
    b_log_i("wifi invoke tcpip cb %d\r\n", event);
    if (_priv->cb_ctx.tcpip_cb)
    {
        _priv->cb_ctx.tcpip_cb(event, param, _priv->cb_ctx.tcpip_cb_arg);
    }
}

static void _bWifiSetPcbStatus(DRIVER_PRIVATE_TYPE *_priv, uint8_t pcb_index, uint8_t status)
{
    if (pcb_index < WIFIMODULE_PCB_NUM_MAX)
    {
        // b_log("current:%d --> %d\r\n", _priv->pcb_ctx[pcb_index].state, status);
        b_log_i("wifi set pcb[%d] status %s --> %s\r\n", pcb_index,
                bWifiPcbStateStr[_priv->pcb_ctx[pcb_index].state], bWifiPcbStateStr[status]);
        _priv->pcb_ctx[pcb_index].state = status;
    }
}

//----------------------------------------------------------------------------------------------------
//------------------------与模组相关，解析AT指令收到数据------------------------------------------------
//----------------------------------------------------------------------------------------------------

static void _bEsp12fDataParse(bDriverInterface_t *pdrv, char *pdata, uint16_t len)
{
    int32_t     conn_index = 0, recv_len = 0, tmp = 0;
    const char *pstr = NULL;
    int         ret  = 0;
    bDRIVER_GET_PRIVATE(_priv, DRIVER_PRIVATE_TYPE, pdrv);
    if (pdata == NULL || len == 0)
    {
        return;
    }
    if (((len >= strlen(WIFI_STA_CONNECTED_STR)) && (strstr(pdata, WIFI_STA_CONNECTED_STR) != 0)) ||
        ((len >= strlen(WIFI_AP_CONNECTED_STR)) && (strstr(pdata, WIFI_AP_CONNECTED_STR) != 0)))
    {
        WIFIMODULE_CONN_NUMBER_INCREASE(_priv);
        b_log("link up ......\r\n");
        _bWifiInvokeLinkCb(_priv);
    }
    else if (((len >= strlen(WIFI_STA_DISCONNECTED_STR)) &&
              (strstr(pdata, WIFI_STA_DISCONNECTED_STR) != 0)) ||
             ((len >= strlen(WIFI_AP_DISCONNECTED_STR)) &&
              (strstr(pdata, WIFI_AP_DISCONNECTED_STR) != 0)))
    {
        WIFIMODULE_CONN_NUMBER_DECREASE(_priv);
        b_log("link down......\r\n");
        _bWifiInvokeLinkCb(_priv);
    }
    else if ((len >= strlen(WIFI_IP_RECV_DATA_STR)) && (strstr(pdata, WIFI_IP_RECV_DATA_STR) != 0))
    {
        ret = bParseString(pdata, ",", 1, &conn_index, &pstr);
        ret += bParseString(pdata, ",", 2, &recv_len, &pstr);
        const char *data_str = pstr;
        if ((pstr != NULL) && (ret == 0) && (conn_index < WIFIMODULE_PCB_NUM_MAX))
        {
            ret = bParseString(data_str, ":", 0, &recv_len, &pstr);
            ret += bParseString(data_str, ":", 1, &tmp, &pstr);
            if (ret == 0 && recv_len > 0 && pstr != NULL)
            {
                if (_priv->pcb_ctx[conn_index].state >= WIFI_PCB_STATE_CONNECTED &&
                    _priv->pcb_ctx[conn_index].state < WIFI_PCB_STATE_WAIT_DISCONNECT)
                {
#if DRIVER_ESP12F_DEBUG_ENABLE
                    b_log("recv:%d bytes\r\n    %s\r\n", recv_len, pstr);
                    b_log_hex(pstr, recv_len);
                    b_log("\r\n");
#endif
                    bTcpIpNewDataArg_t new_data;
                    new_data.pcb     = _priv->pcb_ctx[conn_index].pcb;
                    new_data.pbuf    = (uint8_t *)pstr;
                    new_data.len     = recv_len;
                    new_data.release = NULL;
                    _bWifiInvokeTcpIpCb(_priv, B_TCPIP_E_NEW_DATA, &new_data);
                }
            }
        }
    }
    else if ((len >= strlen(WIFI_IP_DISCONNECT_STR)) &&
             (strstr(pdata, WIFI_IP_DISCONNECT_STR) != 0))
    {
        ret = bParseString(pdata, ",", 0, &conn_index, &pstr);
        if ((0 == ret) && (conn_index < WIFIMODULE_PCB_NUM_MAX))
        {
            if (_priv->pcb_ctx[conn_index].state >= WIFI_PCB_STATE_CONNECTED &&
                _priv->pcb_ctx[conn_index].state < WIFI_PCB_STATE_WAIT_DISCONNECT)
            {
                _bWifiSetPcbStatus(_priv, conn_index, WIFI_PCB_STATE_ASSIGNED);
                _bWifiInvokeTcpIpCb(_priv, B_TCPIP_E_DISCONNECT, _priv->pcb_ctx[conn_index].pcb);
            }
        }
    }
    else if ((len >= strlen(WIFI_IP_CONNECT_STR)) && (strstr(pdata, WIFI_IP_CONNECT_STR) != 0))
    {
        ret = bParseString(pdata, ",", 0, &conn_index, &pstr);
        if ((0 == ret) && WIFIMODULE_TCPUDP_PCB_INDEX_IS_VALID(conn_index) &&
            _priv->pcb_ctx[conn_index].state == WIFI_PCB_STATE_IDLE)
        {
            bWifiModulePcb_t *module_pcb = NULL;
            module_pcb                   = (bWifiModulePcb_t *)bCalloc(1, sizeof(bWifiModulePcb_t));
            b_assert_log(module_pcb != NULL);
            _priv->pcb_ctx[conn_index].is_tcp         = 1;
            _priv->pcb_ctx[conn_index].pcb            = module_pcb;
            _priv->pcb_ctx[conn_index].send_data.pbuf = NULL;
            _priv->pcb_ctx[conn_index].send_data.len  = 0;
            module_pcb->pcb_index                     = conn_index;
            module_pcb->private                       = _priv;
            _bWifiSetPcbStatus(_priv, conn_index, WIFI_PCB_STATE_CONNECTED);
            bTcpIpAccetpArg_t accept_arg;
            accept_arg.new_pcb       = module_pcb;
            accept_arg.netif.private = _priv;
            _bWifiInvokeTcpIpCb(_priv, B_TCPIP_E_ACCEPT, &accept_arg);
        }
    }
}

//----------------------------------------------------------------------------------------------------
//------------------------注册到其他模块的回调等接口----------------------------------------------------
//----------------------------------------------------------------------------------------------------
static void _bAtCmdCb(bAtCmdResult_t cmd_result, char *pdata, uint16_t len, void (*release)(void *),
                      void *user_data)
{
    bDriverInterface_t *pdrv = (bDriverInterface_t *)user_data;
    bDRIVER_GET_PRIVATE(_priv, DRIVER_PRIVATE_TYPE, pdrv);

    if (cmd_result == AT_CMD_RESULT_OK)
    {
        _priv->cmd_ctx.cmd_result = WIFIMODULE_CMD_RESULT_OK;
    }
    else if (cmd_result == AT_CMD_RESULT_ERROR || cmd_result == AT_CMD_RESULT_TIMEOUT)
    {
        _priv->cmd_ctx.cmd_result = WIFIMODULE_CMD_RESULT_FAIL;
    }
    else
    {
        ;
    }
    _bEsp12fDataParse(pdrv, pdata, len);
    if (release && pdata)
    {
        release(pdata);
    }
}

static void _bAtSendData(const uint8_t *pbuf, uint16_t len, void *user_data)
{
    bDRIVER_GET_HALIF(phal, bESP12F_HalIf_t, (bDriverInterface_t *)user_data);
    bHalUartSend(*phal, pbuf, len);
    b_log(">>%s\r\n", pbuf);
}

static int _bHalUartIdleCb(uint8_t *pbuf, uint16_t len, void *user_data)
{
    bDRIVER_GET_PRIVATE(_priv, DRIVER_PRIVATE_TYPE, (bDriverInterface_t *)user_data);
    bAtFeedData(&_priv->at_ctx.at, pbuf, len);
    return 0;
}

//--------------------------------------------------------------------------------------------------
// ---------------------通过函数拼装AT指令-----------------------------------------------------------
//--------------------------------------------------------------------------------------------------
static uint16_t _bSetApInfo(void *arg, char **pcmdbuf)
{
    DRIVER_PRIVATE_TYPE *prv = (DRIVER_PRIVATE_TYPE *)arg;
    memset(prv->at_ctx.uart_send_buf, 0, sizeof(prv->at_ctx.uart_send_buf));
    snprintf((char *)prv->at_ctx.uart_send_buf, WIFIMODULE_SENDBUF_MAX,
             "AT+CWSAP=\"%s\",\"%s\",5,%d\r\n", prv->ctl_ctx.ctl_param.ap.ssid,
             prv->ctl_ctx.ctl_param.ap.passwd, prv->ctl_ctx.ctl_param.ap.encryption);
    *pcmdbuf = (char *)&prv->at_ctx.uart_send_buf[0];
    return strlen(*pcmdbuf);
}

static uint16_t _bJoinAp(void *arg, char **pcmdbuf)
{
    DRIVER_PRIVATE_TYPE *prv = (DRIVER_PRIVATE_TYPE *)arg;
    memset(prv->at_ctx.uart_send_buf, 0, sizeof(prv->at_ctx.uart_send_buf));
    snprintf((char *)prv->at_ctx.uart_send_buf, WIFIMODULE_SENDBUF_MAX,
             "AT+CWJAP=\"%s\",\"%s\"\r\n", prv->ctl_ctx.ctl_param.ap.ssid,
             prv->ctl_ctx.ctl_param.ap.passwd);
    *pcmdbuf = (char *)&prv->at_ctx.uart_send_buf[0];
    return strlen(*pcmdbuf);
}

static uint16_t _bConnectRemote(void *arg, char **pcmdbuf)
{
    int                  i          = 0;
    char                *pcb_type   = "TCP";
    char                 ip_str[16] = {0};
    DRIVER_PRIVATE_TYPE *prv        = (DRIVER_PRIVATE_TYPE *)arg;
    memset(prv->at_ctx.uart_send_buf, 0, sizeof(prv->at_ctx.uart_send_buf));
    for (i = 0; i < WIFIMODULE_PCB_NUM_MAX; i++)
    {
        if (prv->pcb_ctx[i].state == WIFI_PCB_STATE_WAIT_CONNECT)
        {
            _bWifiSetPcbStatus(prv, i, WIFI_PCB_STATE_CONNECTING);
            break;
        }
    }
    if (i >= WIFIMODULE_PCB_NUM_MAX)
    {
        return 0;
    }
    if (prv->pcb_ctx[i].is_tcp == 0)
    {
        pcb_type = "UDP";
    }
    sprintf(ip_str, "%d.%d.%d.%d", (prv->pcb_ctx[i].ip >> 24) & 0xff,
            (prv->pcb_ctx[i].ip >> 16) & 0xff, (prv->pcb_ctx[i].ip >> 8) & 0xff,
            prv->pcb_ctx[i].ip & 0xff);
    b_log("connecting to %s:%d %d\r\n", ip_str, prv->pcb_ctx[i].port, prv->pcb_ctx[i].local_port);

    if (prv->pcb_ctx[i].is_tcp == 0 && prv->pcb_ctx[i].local_port != 0)
    {
        snprintf((char *)prv->at_ctx.uart_send_buf, WIFIMODULE_SENDBUF_MAX,
                 "AT+CIPSTART=%d,\"%s\",\"%s\",%d,%d\r\n", i, pcb_type, ip_str,
                 prv->pcb_ctx[i].port, prv->pcb_ctx[i].local_port);
    }
    else
    {
        snprintf((char *)prv->at_ctx.uart_send_buf, WIFIMODULE_SENDBUF_MAX,
                 "AT+CIPSTART=%d,\"%s\",\"%s\",%d\r\n", i, pcb_type, ip_str, prv->pcb_ctx[i].port);
    }
    *pcmdbuf = ((char *)&prv->at_ctx.uart_send_buf[0]);
    return strlen(*pcmdbuf);
}

static uint16_t _bDisconnectRemote(void *arg, char **pcmdbuf)
{
    int                  i   = 0;
    DRIVER_PRIVATE_TYPE *prv = (DRIVER_PRIVATE_TYPE *)arg;
    memset(prv->at_ctx.uart_send_buf, 0, sizeof(prv->at_ctx.uart_send_buf));
    for (i = 0; i < WIFIMODULE_PCB_NUM_MAX; i++)
    {
        if (prv->pcb_ctx[i].state == WIFI_PCB_STATE_WAIT_DISCONNECT)
        {
            _bWifiSetPcbStatus(prv, i, WIFI_PCB_STATE_DISCONNECTING);
            break;
        }
    }
    if (i >= WIFIMODULE_PCB_NUM_MAX)
    {
        return 0;
    }
    snprintf((char *)prv->at_ctx.uart_send_buf, WIFIMODULE_SENDBUF_MAX, "AT+CIPCLOSE=%d\r\n", i);
    *pcmdbuf = ((char *)&prv->at_ctx.uart_send_buf[0]);
    return strlen(*pcmdbuf);
}

static uint16_t _bTcpUdpSendStart(void *arg, char **pcmdbuf)
{
    int                  i   = 0;
    DRIVER_PRIVATE_TYPE *prv = (DRIVER_PRIVATE_TYPE *)arg;
    memset(prv->at_ctx.uart_send_buf, 0, sizeof(prv->at_ctx.uart_send_buf));
    for (i = 0; i < WIFIMODULE_PCB_NUM_MAX; i++)
    {
        if (prv->pcb_ctx[i].state == WIFI_PCB_STATE_CONNECTED &&
            (prv->pcb_ctx[i].send_data.pbuf != NULL))
        {
            break;
        }
    }
    b_log("send start %d\r\n", i);
    if (i >= WIFIMODULE_PCB_NUM_MAX)
    {
        return 0;
    }
    _bWifiSetPcbStatus(prv, i, WIFI_PCB_STATE_SENDING);
    snprintf((char *)prv->at_ctx.uart_send_buf, WIFIMODULE_SENDBUF_MAX, "AT+CIPSEND=%d,%d\r\n", i,
             prv->pcb_ctx[i].send_data.len);
    *pcmdbuf = ((char *)&prv->at_ctx.uart_send_buf[0]);
    return strlen(*pcmdbuf);
}

static uint16_t _bTcpUdpSendData(void *arg, char **pcmdbuf)
{
    int                  i   = 0;
    DRIVER_PRIVATE_TYPE *prv = (DRIVER_PRIVATE_TYPE *)arg;
    memset(prv->at_ctx.uart_send_buf, 0, sizeof(prv->at_ctx.uart_send_buf));
    for (i = 0; i < WIFIMODULE_TCPUDP_PCB_NUM; i++)
    {
        if (prv->pcb_ctx[i].state == WIFI_PCB_STATE_SENDING)
        {
            break;
        }
    }
    if (i >= WIFIMODULE_TCPUDP_PCB_NUM)
    {
        return 0;
    }
    b_log("[%d][%p]sending %d bytes\r\n", i, (char *)prv->pcb_ctx[i].send_data.pbuf,
          prv->pcb_ctx[i].send_data.len);
    *pcmdbuf = (char *)prv->pcb_ctx[i].send_data.pbuf;
    return prv->pcb_ctx[i].send_data.len;
}

static uint16_t _bTcpListen(void *arg, char **pcmdbuf)
{
    int                  i   = 0;
    DRIVER_PRIVATE_TYPE *prv = (DRIVER_PRIVATE_TYPE *)arg;
    memset(prv->at_ctx.uart_send_buf, 0, sizeof(prv->at_ctx.uart_send_buf));
    for (i = 0; i < WIFIMODULE_PCB_NUM_MAX; i++)
    {
        if (prv->pcb_ctx[i].state == WIFI_PCB_STATE_WAIT_LISTEN)
        {
            break;
        }
        else if (prv->pcb_ctx[i].state == WIFI_PCB_STATE_WAIT_STOP_LISTEN)
        {
            break;
        }
    }
    if (i >= WIFIMODULE_PCB_NUM_MAX)
    {
        return 0;
    }

    if (prv->pcb_ctx[i].state == WIFI_PCB_STATE_WAIT_LISTEN)
    {
        b_log("[%d] listen %d\r\n", i, prv->pcb_ctx[i].local_port);
        _bWifiSetPcbStatus(prv, i, WIFI_PCB_STATE_LISTENING);
        snprintf((char *)prv->at_ctx.uart_send_buf, WIFIMODULE_SENDBUF_MAX, "AT+CIPSERVER=1,%d\r\n",
                 prv->pcb_ctx[i].local_port);
    }
    else if (prv->pcb_ctx[i].state == WIFI_PCB_STATE_WAIT_STOP_LISTEN)
    {
        b_log("[%d] stop listen %d\r\n", i, prv->pcb_ctx[i].local_port);
        _bWifiSetPcbStatus(prv, i, WIFI_PCB_STATE_LISTEN_STOP);
        snprintf((char *)prv->at_ctx.uart_send_buf, WIFIMODULE_SENDBUF_MAX, "AT+CIPSERVER=0,%d\r\n",
                 prv->pcb_ctx[i].local_port);
    }
    *pcmdbuf = ((char *)&prv->at_ctx.uart_send_buf[0]);
    return strlen(*pcmdbuf);
}

//--------------------------------------------------------------------------------------------------

static void _bWifiModuleCtlCmdResult(DRIVER_PRIVATE_TYPE *_priv, uint8_t is_ok)
{
    if (_priv->cb_ctx.cb.cb && (_priv->ctl_ctx.ctl_cmd < (bCMD_WIFI_NUMBER_MAX)))
    {
        if (is_ok)
        {
            _priv->cb_ctx.cb.cb(bWifiCtlEvent[_priv->ctl_ctx.ctl_cmd].ok_event, NULL, NULL,
                                _priv->cb_ctx.cb.user_data);
        }
        else
        {
            _priv->cb_ctx.cb.cb(bWifiCtlEvent[_priv->ctl_ctx.ctl_cmd].fail_event, NULL, NULL,
                                _priv->cb_ctx.cb.user_data);
        }
    }
}

static int _bWifiModuleAddExecCmd(DRIVER_PRIVATE_TYPE *_priv, const bWifiModuleCmdUnit_t *pcmdunit,
                                  uint8_t unit_number)
{
    if (WIFIMODULE_IS_BUSY(_priv))
    {
        return -1;
    }
    if (_priv->cmd_ctx.cmd_data.number >= WIFIMODULE_CMD_TABLE_NUMBER_MAX)
    {
        b_log_e("cmd table is full\r\n");
        return -1;
    }
    _priv->cmd_ctx.cmd_data.cmd_table[_priv->cmd_ctx.cmd_data.number].pcmdunit    = pcmdunit;
    _priv->cmd_ctx.cmd_data.cmd_table[_priv->cmd_ctx.cmd_data.number].unit_number = unit_number;
    _priv->cmd_ctx.cmd_data.number += 1;
    return 0;
}

static void _bWifiModuleExecCmdStart(DRIVER_PRIVATE_TYPE *_priv)
{
    if (WIFIMODULE_IS_BUSY(_priv))
    {
        return;
    }
    if (_priv->cmd_ctx.cmd_data.number == 0)
    {
        return;
    }
    _priv->cmd_ctx.cmd_unit_index = 0;
    _priv->cmd_ctx.cmd_index      = 0;
    WIFIMODULE_SET_BUSY(_priv);
    b_log("exec cmd start %d \r\n", _priv->ctl_ctx.ctl_cmd);
}

static void _bWifiModuleExecCmdStop(DRIVER_PRIVATE_TYPE *_priv)
{
    _priv->cmd_ctx.cmd_data.number = 0;
    _priv->cmd_ctx.cmd_unit_index  = 0;
    _priv->cmd_ctx.cmd_index       = 0;
    WIFIMODULE_CLEAR_BUSY(_priv);
}

static const bWifiModuleCmdUnit_t *_bWifiModuleCurrentCmdUnit(DRIVER_PRIVATE_TYPE *_priv)
{
    if (WIFIMODULE_IS_BUSY(_priv))
    {
        return &_priv->cmd_ctx.cmd_data.cmd_table[_priv->cmd_ctx.cmd_index]
                    .pcmdunit[_priv->cmd_ctx.cmd_unit_index];
    }
    return NULL;
}

static const bWifiModuleCmdUnit_t *_bWifiModuleNextCmd(DRIVER_PRIVATE_TYPE *_priv)
{
    const bWifiModuleCmdUnit_t *pcmdunit = NULL;
    if (_priv->cmd_ctx.cmd_data.number == 0)
    {
        return NULL;
    }
    _priv->cmd_ctx.cmd_unit_index += 1;
    if (_priv->cmd_ctx.cmd_unit_index >=
        _priv->cmd_ctx.cmd_data.cmd_table[_priv->cmd_ctx.cmd_index].unit_number)
    {
        _priv->cmd_ctx.cmd_unit_index = 0;
        _priv->cmd_ctx.cmd_index += 1;
        if (_priv->cmd_ctx.cmd_index >= _priv->cmd_ctx.cmd_data.number)
        {
            _priv->cmd_ctx.cmd_index = 0;
            WIFIMODULE_CLEAR_BUSY(_priv);
            return NULL;
        }
    }
    pcmdunit = &_priv->cmd_ctx.cmd_data.cmd_table[_priv->cmd_ctx.cmd_index]
                    .pcmdunit[_priv->cmd_ctx.cmd_unit_index];
    return pcmdunit;
}

static void _bWifiCtlResult(uint8_t cmd, uint8_t isok, DRIVER_PRIVATE_TYPE *_priv)
{
    _priv->cb_ctx.cb.cb(isok ? bWifiCtlEvent[cmd].ok_event : bWifiCtlEvent[cmd].fail_event, NULL,
                        NULL, _priv->cb_ctx.cb.user_data);
}

static void _bWifiPrivCmdHandle(DRIVER_PRIVATE_TYPE *_priv, uint8_t cmd)
{
    int ret = 0;
    b_log_i("private cmd handle %d\r\n", cmd);
    if (cmd == WIFIMODULE_PRIV_CMD_PCB_CONNECT)
    {
        ret = _bWifiModuleAddExecCmd(_priv, &bEspCmdConnectRemote[0],
                                     sizeof(bEspCmdConnectRemote) / sizeof(bWifiModuleCmdUnit_t));
    }
    else if (cmd == WIFIMODULE_PRIV_CMD_PCB_SENDDATA)
    {
        ret = _bWifiModuleAddExecCmd(_priv, &bEspCmdSendData[0],
                                     sizeof(bEspCmdSendData) / sizeof(bWifiModuleCmdUnit_t));
    }
    else if (cmd == WIFIMODULE_PRIV_CMD_PCB_DISCONNECT)
    {
        ret =
            _bWifiModuleAddExecCmd(_priv, &bEspCmdDisconnectRemote[0],
                                   sizeof(bEspCmdDisconnectRemote) / sizeof(bWifiModuleCmdUnit_t));
    }
    else if (cmd == WIFIMODULE_PRIV_CMD_PCB_LISTEN)
    {
        ret = _bWifiModuleAddExecCmd(_priv, &bEspCmdTcpListen[0],
                                     sizeof(bEspCmdTcpListen) / sizeof(bWifiModuleCmdUnit_t));
    }
    b_log_i("priv cmd handle ret %d\r\n", ret);
    WIFIMODULE_SET_CTL_CMD(_priv, cmd);
    _bWifiModuleExecCmdStart(_priv);
}

static void _bWifiPrivCmdResultHandle(uint8_t cmd, uint8_t isok, DRIVER_PRIVATE_TYPE *_priv)
{
    uint8_t i = 0;
    b_log_i("private cmd result handle %d %d\r\n", cmd, isok);
    if (cmd == WIFIMODULE_PRIV_CMD_PCB_CONNECT)
    {
        for (i = 0; i < WIFIMODULE_PCB_NUM_MAX; i++)
        {
            if (_priv->pcb_ctx[i].state == WIFI_PCB_STATE_CONNECTING)
            {
                if (isok)
                {
                    _bWifiSetPcbStatus(_priv, i, WIFI_PCB_STATE_CONNECTED);
                    _bWifiInvokeTcpIpCb(_priv, B_TCPIP_E_CONNECTED, _priv->pcb_ctx[i].pcb);
                }
                else
                {
                    _bWifiSetPcbStatus(_priv, i, WIFI_PCB_STATE_ASSIGNED);
                    _bWifiInvokeTcpIpCb(_priv, B_TCPIP_E_DISCONNECT, _priv->pcb_ctx[i].pcb);
                }
            }
        }
    }
    else if (cmd == WIFIMODULE_PRIV_CMD_PCB_SENDDATA)
    {
        for (i = 0; i < WIFIMODULE_PCB_NUM_MAX; i++)
        {
            if (_priv->pcb_ctx[i].state == WIFI_PCB_STATE_SENDING)
            {
                if (isok)
                {
                    bTcpIpSendDoneArg_t sendone_arg;
                    sendone_arg.pcb = _priv->pcb_ctx[i].pcb;
                    sendone_arg.len = _priv->pcb_ctx[i].send_data.len;
                    _bWifiSetPcbStatus(_priv, i, WIFI_PCB_STATE_CONNECTED);
                    _bWifiInvokeTcpIpCb(_priv, B_TCPIP_E_SEND_DONE, &sendone_arg);
                }
                else
                {
                    _bWifiSetPcbStatus(_priv, i, WIFI_PCB_STATE_ASSIGNED);
                    _bWifiInvokeTcpIpCb(_priv, B_TCPIP_E_DISCONNECT, _priv->pcb_ctx[i].pcb);
                }
                _priv->pcb_ctx[i].send_data.pbuf = NULL;
                _priv->pcb_ctx[i].send_data.len  = 0;
                b_log("--clear send buf[%d]\r\n", i);
            }
        }
    }
    else if (cmd == WIFIMODULE_PRIV_CMD_PCB_DISCONNECT)
    {
        for (i = 0; i < WIFIMODULE_PCB_NUM_MAX; i++)
        {
            if (_priv->pcb_ctx[i].state == WIFI_PCB_STATE_DISCONNECTING)
            {
                if (isok)
                {
                    _priv->pcb_ctx[i].send_data.pbuf = NULL;
                    _priv->pcb_ctx[i].send_data.len  = 0;
                    b_log("disconnect clear send buf....\r\n");
                    if (_priv->pcb_ctx[i].pcb)
                    {
                        bFree(_priv->pcb_ctx[i].pcb);
                        _priv->pcb_ctx[i].pcb = NULL;
                    }
                    _bWifiSetPcbStatus(_priv, i, WIFI_PCB_STATE_IDLE);
                }
                else
                {
                    _bWifiSetPcbStatus(_priv, i, WIFI_PCB_STATE_WAIT_DISCONNECT);
                }
            }
        }
    }
    else if (cmd == WIFIMODULE_PRIV_CMD_PCB_LISTEN)
    {
        for (i = 0; i < WIFIMODULE_PCB_NUM_MAX; i++)
        {
            if (_priv->pcb_ctx[i].state == WIFI_PCB_STATE_LISTENING)
            {
                if (isok)
                {
                    _bWifiSetPcbStatus(_priv, i, WIFI_PCB_STATE_ACCEPT);
                }
                else
                {
                    _bWifiSetPcbStatus(_priv, i, WIFI_PCB_STATE_WAIT_LISTEN);
                }
            }
            else if (_priv->pcb_ctx[i].state == WIFI_PCB_STATE_LISTEN_STOP)
            {
                if (isok)
                {
                    if (_priv->pcb_ctx[i].pcb)
                    {
                        bFree(_priv->pcb_ctx[i].pcb);
                        _priv->pcb_ctx[i].pcb = NULL;
                    }
                    _bWifiSetPcbStatus(_priv, i, WIFI_PCB_STATE_IDLE);
                }
                else
                {
                    _bWifiSetPcbStatus(_priv, i, WIFI_PCB_STATE_WAIT_STOP_LISTEN);
                }
            }
        }
    }
}

PT_THREAD(bEsp12fTask)(struct pt *pt, void *arg)
{
    uint8_t                     i               = 0;
    uint8_t                     wait_cmd_result = 0;
    char                       *pcmd            = NULL;
    uint16_t                    cmd_len         = 0;
    const bWifiModuleCmdUnit_t *pcmdunit        = NULL;
    bDriverInterface_t         *pdrv            = (bDriverInterface_t *)arg;
    bDRIVER_GET_PRIVATE(_priv, DRIVER_PRIVATE_TYPE, pdrv);
    PT_BEGIN(pt);
    while (1)
    {
        if (WIFIMODULE_IS_BUSY(_priv))
        {
            pcmdunit = _bWifiModuleCurrentCmdUnit(_priv);
            if (pcmdunit != NULL)
            {
                WIFIMODULE_CLEAR_CMD_RESULT(_priv);
                wait_cmd_result = 1;
                b_log("cmd:%s, %s, %p\r\n", ((pcmdunit->pcmd != NULL) ? pcmdunit->pcmd : "null"),
                      pcmdunit->resp, pcmdunit->cmd_f);
                if (pcmdunit->pcmd)
                {
                    bAtSendCmd(&_priv->at_ctx.at, pcmdunit->pcmd, pcmdunit->resp,
                               WIFIMODULE_CMD_TIMEOUT_GET(pcmdunit->timeout));
                }
                else
                {
                    cmd_len = pcmdunit->cmd_f(_priv, &pcmd);
                    if (cmd_len > 0 && pcmd != NULL)
                    {
                        bAtSendCmd2(&_priv->at_ctx.at, pcmd, cmd_len, pcmdunit->resp,
                                    WIFIMODULE_CMD_TIMEOUT_GET(pcmdunit->timeout));
                    }
                    else
                    {
                        b_log_e("cmd_f return error%d %p\r\n", cmd_len, pcmd);
                        wait_cmd_result = 0;
                    }
                }
                if (wait_cmd_result)
                {
                    PT_WAIT_UNTIL(
                        pt,
                        WIFIMODULE_CMD_RESULT_IS_OK(_priv) || WIFIMODULE_CMD_RESULT_IS_FAIL(_priv),
                        ESP12F_CMD_TIMEOUT + WIFIMODULE_CMD_TIMEOUT_GET(pcmdunit->timeout));
                }
                if (WIFIMODULE_CMD_RESULT_IS_OK(_priv))
                {
                    if (WIFIMODULE_CMD_IS_FORCE_WAIT(pcmdunit->timeout))
                    {
                        bTaskDelayMs(pt, WIFIMODULE_CMD_TIMEOUT_GET(pcmdunit->timeout));
                    }
                    if (_bWifiModuleNextCmd(_priv) == NULL)
                    {
                        _bWifiModuleExecCmdStop(_priv);
                    }
                }
                else
                {
                    _bWifiModuleExecCmdStop(_priv);
                }
            }
            if (!WIFIMODULE_IS_BUSY(_priv))
            {
                if (WIFIMODULE_IS_PRIV_CMD(_priv))
                {
                    _bWifiPrivCmdResultHandle(WIFIMODULE_GET_CTL_CMD(_priv),
                                              WIFIMODULE_CMD_RESULT_IS_OK(_priv), _priv);
                }
                else
                {
                    _bWifiCtlResult(WIFIMODULE_GET_CTL_CMD(_priv),
                                    WIFIMODULE_CMD_RESULT_IS_OK(_priv), _priv);
                }
            }
        }
        else
        {
            if (WIFIMODULE_CONN_NUMBER_GET(_priv))
            {
                for (i = 0; i < WIFIMODULE_PCB_NUM_MAX; i++)
                {
                    if (_priv->pcb_ctx[i].state == WIFI_PCB_STATE_WAIT_CONNECT)
                    {
                        _bWifiPrivCmdHandle(_priv, WIFIMODULE_PRIV_CMD_PCB_CONNECT);
                        _bWifiInvokeTcpIpCb(_priv, B_TCPIP_E_CONNECTING, _priv->pcb_ctx[i].pcb);
                        break;
                    }
                    else if (_priv->pcb_ctx[i].state == WIFI_PCB_STATE_CONNECTED)
                    {
                        if (_priv->pcb_ctx[i].send_data.pbuf != NULL)
                        {
                            _bWifiPrivCmdHandle(_priv, WIFIMODULE_PRIV_CMD_PCB_SENDDATA);
                            break;
                        }
                    }
                    else if (_priv->pcb_ctx[i].state == WIFI_PCB_STATE_WAIT_DISCONNECT)
                    {
                        _bWifiPrivCmdHandle(_priv, WIFIMODULE_PRIV_CMD_PCB_DISCONNECT);
                        break;
                    }
                    else if (_priv->pcb_ctx[i].state == WIFI_PCB_STATE_WAIT_LISTEN ||
                             _priv->pcb_ctx[i].state == WIFI_PCB_STATE_WAIT_STOP_LISTEN)
                    {
                        _bWifiPrivCmdHandle(_priv, WIFIMODULE_PRIV_CMD_PCB_LISTEN);
                        break;
                    }
                }
            }
        }
        bTaskYield(pt);
    }
    PT_END(pt);
}
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------

static void _bTcpIpRegCallback(pTcpIpCallback_t cb, void *arg, bTcpIpNetif_t *pnetif)
{
    if (pnetif == NULL || cb == NULL)
    {
        return;
    }
    DRIVER_PRIVATE_TYPE *prv = (DRIVER_PRIVATE_TYPE *)pnetif->private;
    if (prv != NULL)
    {
        prv->cb_ctx.tcpip_cb     = cb;
        prv->cb_ctx.tcpip_cb_arg = arg;
    }
}

static int _bTcpIpGetPcb(DRIVER_PRIVATE_TYPE *prv)
{
    int i = 0;
    for (i = 0; i < WIFIMODULE_TCPUDP_PCB_NUM; i++)
    {
        if (prv->pcb_ctx[i].state == WIFI_PCB_STATE_IDLE)
        {
            prv->pcb_ctx[i].is_tcp         = 0;
            prv->pcb_ctx[i].ip             = 0;
            prv->pcb_ctx[i].port           = 0;
            prv->pcb_ctx[i].local_port     = 0;
            prv->pcb_ctx[i].pcb            = NULL;
            prv->pcb_ctx[i].send_data.len  = 0;
            prv->pcb_ctx[i].send_data.pbuf = NULL;
            _bWifiSetPcbStatus(prv, i, WIFI_PCB_STATE_ASSIGNED);
            return i;
        }
    }
    return -1;
}

static int _bTcpIpGetListenPcb(DRIVER_PRIVATE_TYPE *prv)
{
    int i = 0;
    for (i = 0; i < WIFIMODULE_LISTEN_PCB_NUM; i++)
    {
        if (prv->pcb_ctx[i + WIFIMODULE_TCPUDP_PCB_NUM].state == WIFI_PCB_STATE_IDLE)
        {
            prv->pcb_ctx[i + WIFIMODULE_TCPUDP_PCB_NUM].is_tcp         = 0;
            prv->pcb_ctx[i + WIFIMODULE_TCPUDP_PCB_NUM].ip             = 0;
            prv->pcb_ctx[i + WIFIMODULE_TCPUDP_PCB_NUM].port           = 0;
            prv->pcb_ctx[i + WIFIMODULE_TCPUDP_PCB_NUM].local_port     = 0;
            prv->pcb_ctx[i + WIFIMODULE_TCPUDP_PCB_NUM].pcb            = NULL;
            prv->pcb_ctx[i + WIFIMODULE_TCPUDP_PCB_NUM].send_data.len  = 0;
            prv->pcb_ctx[i + WIFIMODULE_TCPUDP_PCB_NUM].send_data.pbuf = NULL;
            _bWifiSetPcbStatus(prv, i + WIFIMODULE_TCPUDP_PCB_NUM, WIFI_PCB_STATE_ASSIGNED);
            return (i + WIFIMODULE_TCPUDP_PCB_NUM);
        }
    }
    return -1;
}

static void *_bTcpIpNewTcp(bTcpIpNetif_t *pnetif)
{
    bWifiModulePcb_t *module_pcb = NULL;
    if (pnetif == NULL)
    {
        return NULL;
    }
    DRIVER_PRIVATE_TYPE *prv = (DRIVER_PRIVATE_TYPE *)pnetif->private;
    if (prv == NULL)
    {
        return NULL;
    }
    module_pcb = (bWifiModulePcb_t *)bCalloc(1, sizeof(bWifiModulePcb_t));
    if (module_pcb == NULL)
    {
        return NULL;
    }
    int pcb_index = _bTcpIpGetPcb(prv);
    if (pcb_index < 0)
    {
        bFree(module_pcb);
        return NULL;
    }
    prv->pcb_ctx[pcb_index].is_tcp         = 1;
    prv->pcb_ctx[pcb_index].pcb            = module_pcb;
    prv->pcb_ctx[pcb_index].send_data.pbuf = NULL;
    prv->pcb_ctx[pcb_index].send_data.len  = 0;
    module_pcb->pcb_index                  = pcb_index;
    module_pcb->private                    = prv;
    b_log("new tcp %d \r\n", pcb_index);
    return module_pcb;
}

static void *_bTcpIpNewUdp(bTcpIpNetif_t *pnetif)
{
    bWifiModulePcb_t *module_pcb = NULL;
    if (pnetif == NULL)
    {
        return NULL;
    }
    DRIVER_PRIVATE_TYPE *prv = (DRIVER_PRIVATE_TYPE *)pnetif->private;
    if (prv == NULL)
    {
        return NULL;
    }
    module_pcb = (bWifiModulePcb_t *)bCalloc(1, sizeof(bWifiModulePcb_t));
    if (module_pcb == NULL)
    {
        return NULL;
    }
    int pcb_index = _bTcpIpGetPcb(prv);
    if (pcb_index < 0)
    {
        bFree(module_pcb);
        return NULL;
    }
    prv->pcb_ctx[pcb_index].is_tcp         = 0;
    prv->pcb_ctx[pcb_index].pcb            = module_pcb;
    prv->pcb_ctx[pcb_index].send_data.pbuf = NULL;
    prv->pcb_ctx[pcb_index].send_data.len  = 0;
    module_pcb->pcb_index                  = pcb_index;
    module_pcb->private                    = prv;
    b_log("new udp %d \r\n", pcb_index);
    return module_pcb;
}

static int _bTcpIpBind(void *pcb, uint16_t port)
{
    bWifiModulePcb_t *_pcb = (bWifiModulePcb_t *)pcb;

    if (_pcb == NULL || !WIFIMODULE_PCB_INDEX_IS_VALID(_pcb->pcb_index))
    {
        return -1;
    }
    DRIVER_PRIVATE_TYPE *_priv = (DRIVER_PRIVATE_TYPE *)_pcb->private;
    if (_priv == NULL)
    {
        return -2;
    }
    _priv->pcb_ctx[_pcb->pcb_index].local_port = port;
    return 0;
}

static void *_bTcpIpListen(void *pcb, uint16_t num)
{
    bWifiModulePcb_t *_pcb = (bWifiModulePcb_t *)pcb;

    if (_pcb == NULL || !WIFIMODULE_TCPUDP_PCB_INDEX_IS_VALID(_pcb->pcb_index))
    {
        return NULL;
    }

    DRIVER_PRIVATE_TYPE *_priv = (DRIVER_PRIVATE_TYPE *)_pcb->private;
    if (_priv == NULL)
    {
        return NULL;
    }

    if (_priv->pcb_ctx[_pcb->pcb_index].local_port == 0)
    {
        return NULL;
    }

    int listen_pcb_index = _bTcpIpGetListenPcb(_priv);
    if (listen_pcb_index < 0)
    {
        return NULL;
    }
    _priv->pcb_ctx[listen_pcb_index].is_tcp     = _priv->pcb_ctx[_pcb->pcb_index].is_tcp;
    _priv->pcb_ctx[listen_pcb_index].local_port = _priv->pcb_ctx[_pcb->pcb_index].local_port;
    _priv->pcb_ctx[listen_pcb_index].pcb        = _priv->pcb_ctx[_pcb->pcb_index].pcb;
    _priv->pcb_ctx[_pcb->pcb_index].pcb         = NULL;
    _bWifiSetPcbStatus(_priv, _pcb->pcb_index, WIFI_PCB_STATE_IDLE);
    _pcb->pcb_index = listen_pcb_index;
    _bWifiSetPcbStatus(_priv, _pcb->pcb_index, WIFI_PCB_STATE_WAIT_LISTEN);
    b_log("listen localhost:%d\r\n", _priv->pcb_ctx[_pcb->pcb_index].local_port);
    return _pcb;
}

static int _bTcpIpConnect(void *pcb, uint32_t ip, uint16_t port)
{
    bWifiModulePcb_t *_pcb = (bWifiModulePcb_t *)pcb;

    if (_pcb == NULL || !WIFIMODULE_PCB_INDEX_IS_VALID(_pcb->pcb_index))
    {
        return -1;
    }
    DRIVER_PRIVATE_TYPE *_priv = (DRIVER_PRIVATE_TYPE *)_pcb->private;
    if (_priv == NULL)
    {
        return -2;
    }
    b_log("[%d]connect %x:%d\r\n", _pcb->pcb_index, ip, port);
    _priv->pcb_ctx[_pcb->pcb_index].ip   = ip;
    _priv->pcb_ctx[_pcb->pcb_index].port = port;
    _bWifiSetPcbStatus(_priv, _pcb->pcb_index, WIFI_PCB_STATE_WAIT_CONNECT);
    return 0;
}

static int _bTcpIpSend(void *pcb, const uint8_t *pbuf, uint16_t len)
{
    bWifiModulePcb_t *_pcb = (bWifiModulePcb_t *)pcb;

    if (_pcb == NULL || !WIFIMODULE_PCB_INDEX_IS_VALID(_pcb->pcb_index) || pbuf == NULL || len == 0)
    {
        return -1;
    }
    DRIVER_PRIVATE_TYPE *_priv = (DRIVER_PRIVATE_TYPE *)_pcb->private;
    if (_priv == NULL)
    {
        return -2;
    }
    if (_priv->pcb_ctx[_pcb->pcb_index].state != WIFI_PCB_STATE_CONNECTED)
    {
        return -2;
    }

    if (_priv->pcb_ctx[_pcb->pcb_index].send_data.pbuf != NULL)
    {
        return -3;
    }
    _priv->pcb_ctx[_pcb->pcb_index].send_data.pbuf = pbuf;
    _priv->pcb_ctx[_pcb->pcb_index].send_data.len  = len;
    b_log("[%d][%p]send %d bytes\r\n", _pcb->pcb_index, pbuf, len);
    return len;
}

static int _bTcpIpNetifSend(void *netif, void *pcb, const uint8_t *pbuf, uint16_t len)
{
    B_UNUSED(netif);
    return _bTcpIpSend(pcb, pbuf, len);
}

static int _bTcpIpDelete(void *pcb)
{
    bWifiModulePcb_t *_pcb = (bWifiModulePcb_t *)pcb;
    if (_pcb == NULL || !WIFIMODULE_PCB_INDEX_IS_VALID(_pcb->pcb_index))
    {
        return -1;
    }
    DRIVER_PRIVATE_TYPE *_priv = (DRIVER_PRIVATE_TYPE *)_pcb->private;
    if (_priv == NULL)
    {
        return -2;
    }

    if (WIFIMODULE_LISTEN_PCB_INDEX_IS_VALID(_pcb->pcb_index))
    {
        _bWifiSetPcbStatus(_priv, _pcb->pcb_index, WIFI_PCB_STATE_WAIT_STOP_LISTEN);
    }
    else
    {
        _bWifiSetPcbStatus(_priv, _pcb->pcb_index, WIFI_PCB_STATE_WAIT_DISCONNECT);
    }
    return 0;
}

/*
typedef struct
{
    int (*init)(void *netif);
    void (*loop)(void *netif);
    void (*callback)(pTcpIpCallback_t cb, void *arg);

    // 网卡相关接口，如果有协议栈管理多张网卡的情况，会传入网卡信息；
    int (*set_mac)(uint8_t mac[6], void *netif);
    int (*set_ip)(uint32_t ip, uint32_t mask, uint32_t gateway, void *netif);
    int (*set_link_state)(uint8_t state, void *netif);
    int (*set_default_netif)(void *netif);

    // tcp/udp相关接口
    struct
    {
        void *(*new)(void);
        int (*bind)(void *, uint16_t);
        int (*listen)(void *, uint16_t);
        int (*connect)(void *, uint32_t, uint16_t);
        int (*send)(void *, const uint8_t *, uint16_t);
        int (*recv)(void *, uint8_t *, uint16_t);
        int (*delete)(void *);
    } tcp;

    struct
    {
        void *(*new)(void);
        int (*bind)(void *, uint16_t);
        int (*listen)(void *, uint16_t);
        int (*connect)(void *, uint32_t, uint16_t);
        int (*send)(void *, const uint8_t *, uint16_t);
        int (*recv)(void *, uint8_t *, uint16_t);
        int (*delete)(void *);
    } udp;

    uint8_t (*is_readable)(void *);
    uint8_t (*is_writeable)(void *);
} bTcpIpStackIf_t;
*/

static int _bESP12FCtl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    bMacAddress_t *pmac = NULL;
    bDRIVER_GET_PRIVATE(_priv, DRIVER_PRIVATE_TYPE, pdrv);
    switch (cmd)
    {
        case bCMD_GET_DRIVER_NETIF:
        {
            if (param == NULL)
            {
                return -1;
            }
            ((bDriverNetif_t *)param)->private = _priv;
            ((bDriverNetif_t *)param)->type    = B_NETCARD_TYPE_WIFI;
        }
        break;
        case bCMD_WIFI_REG_CALLBACK:
        {
            if (param == NULL)
            {
                return -1;
            }
            bWifiDrvCallback_t *pcb    = (bWifiDrvCallback_t *)param;
            _priv->cb_ctx.cb.cb        = pcb->cb;
            _priv->cb_ctx.cb.user_data = pcb->user_data;
            WIFIMODULE_SET_CTL_CMD(_priv, cmd);
        }
        break;
        case bCMD_WIFI_MODE_STA:
        {
            if (WIFIMODULE_IS_BUSY(_priv))
            {
                return -1;
            }
            _bWifiModuleAddExecCmd(_priv, &bEspCmdReset[0],
                                   sizeof(bEspCmdReset) / sizeof(bWifiModuleCmdUnit_t));
            _bWifiModuleAddExecCmd(_priv, &bEspCmdInit[0],
                                   sizeof(bEspCmdInit) / sizeof(bWifiModuleCmdUnit_t));
            _bWifiModuleAddExecCmd(_priv, &bEspCmdStaMode[0],
                                   sizeof(bEspCmdStaMode) / sizeof(bWifiModuleCmdUnit_t));
            _bWifiModuleExecCmdStart(_priv);
            WIFIMODULE_CONN_NUMBER_RESET(_priv);
            WIFIMODULE_SET_CTL_CMD(_priv, cmd);
        }
        break;
        case bCMD_WIFI_MODE_AP:
        case bCMD_WIFI_MODE_STA_AP:
        case bCMD_WIFI_JOIN_AP:
        {
            if (param == NULL || WIFIMODULE_IS_BUSY(_priv))
            {
                return -1;
            }
            memcpy(&_priv->ctl_ctx.ctl_param.ap, param, sizeof(bApInfo_t));

            if (cmd == bCMD_WIFI_MODE_AP || cmd == bCMD_WIFI_MODE_STA_AP)
            {
                _bWifiModuleAddExecCmd(_priv, &bEspCmdReset[0],
                                       sizeof(bEspCmdReset) / sizeof(bWifiModuleCmdUnit_t));
                _bWifiModuleAddExecCmd(_priv, &bEspCmdInit[0],
                                       sizeof(bEspCmdInit) / sizeof(bWifiModuleCmdUnit_t));
            }
            if (cmd == bCMD_WIFI_MODE_AP)
            {
                _bWifiModuleAddExecCmd(_priv, &bEspCmdApMode[0],
                                       sizeof(bEspCmdApMode) / sizeof(bWifiModuleCmdUnit_t));
            }
            else if (cmd == bCMD_WIFI_MODE_STA_AP)
            {
                _bWifiModuleAddExecCmd(_priv, &bEspCmdApStaMode[0],
                                       sizeof(bEspCmdApStaMode) / sizeof(bWifiModuleCmdUnit_t));
            }
            else if (cmd == bCMD_WIFI_JOIN_AP)
            {
                _bWifiModuleAddExecCmd(_priv, &bEspCmdJoinAp[0],
                                       sizeof(bEspCmdJoinAp) / sizeof(bWifiModuleCmdUnit_t));
            }
            _bWifiModuleExecCmdStart(_priv);
            WIFIMODULE_CONN_NUMBER_RESET(_priv);
            WIFIMODULE_SET_CTL_CMD(_priv, cmd);
        }
        break;
        case bCMD_GET_MAC_ADDRESS:
        {
            if (param == NULL)
            {
                return -1;
            }
            pmac = (bMacAddress_t *)param;
            memcpy(pmac->address, &_priv->mac.address[0], sizeof(_priv->mac.address));
        }
        break;
        case bCMD_SET_MAC_ADDRESS:
        {
            ;
        }
        break;
        case bCMD_GET_LINK_STATE:
        {
            if (param == NULL)
            {
                return -1;
            }
            *(uint8_t *)param = (WIFIMODULE_CONN_NUMBER_GET(_priv) > 0);
        }
        break;
        case bCMD_REG_BUF_LIST:
        {
            ;
        }
        break;
        case bCMD_REG_LINK_CALLBACK:
        {
            if (param == NULL)
            {
                return -1;
            }
            memcpy(&_priv->cb_ctx.link_cb, param, sizeof(bLinkStateCb_t));
            if (WIFIMODULE_CONN_NUMBER_GET(_priv))
            {
                _bWifiInvokeLinkCb(_priv);
            }
        }
        break;
        case bCMD_GET_STACK_IF:
        {
            if (param == NULL)
            {
                return -1;
            }
            ((bTcpIpStackIf_t *)param)->init              = NULL;
            ((bTcpIpStackIf_t *)param)->loop              = NULL;
            ((bTcpIpStackIf_t *)param)->reg_callback      = _bTcpIpRegCallback;
            ((bTcpIpStackIf_t *)param)->is_writeable      = NULL;
            ((bTcpIpStackIf_t *)param)->is_readable       = NULL;
            ((bTcpIpStackIf_t *)param)->set_mac           = NULL;
            ((bTcpIpStackIf_t *)param)->set_ip            = NULL;
            ((bTcpIpStackIf_t *)param)->set_link_state    = NULL;
            ((bTcpIpStackIf_t *)param)->set_default_netif = NULL;
            ((bTcpIpStackIf_t *)param)->tcp.new           = _bTcpIpNewTcp;
            ((bTcpIpStackIf_t *)param)->tcp.bind          = _bTcpIpBind;
            ((bTcpIpStackIf_t *)param)->tcp.listen        = _bTcpIpListen;
            ((bTcpIpStackIf_t *)param)->tcp.connect       = _bTcpIpConnect;
            ((bTcpIpStackIf_t *)param)->tcp.send          = _bTcpIpSend;
            ((bTcpIpStackIf_t *)param)->tcp.recv          = NULL;
            ((bTcpIpStackIf_t *)param)->tcp.delete        = _bTcpIpDelete;
            ((bTcpIpStackIf_t *)param)->udp.new           = _bTcpIpNewUdp;
            ((bTcpIpStackIf_t *)param)->udp.bind          = _bTcpIpBind;
            ((bTcpIpStackIf_t *)param)->udp.listen        = NULL;
            ((bTcpIpStackIf_t *)param)->udp.connect       = _bTcpIpConnect;
            ((bTcpIpStackIf_t *)param)->udp.send          = _bTcpIpNetifSend;
            ((bTcpIpStackIf_t *)param)->udp.recv          = NULL;
            ((bTcpIpStackIf_t *)param)->udp.delete        = _bTcpIpDelete;
        }
        break;
        default:
            break;
    }
    return 0;
}
/**
 * \}
 */

/**
 * \addtogroup ESP12F_Exported_Functions
 * \{
 */
int bESP12F_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bESP12F_Init);
    pdrv->ctl         = _bESP12FCtl;
    pdrv->_private._p = &bEspRunInfo[pdrv->drv_no];
    memset(pdrv->_private._p, 0, sizeof(DRIVER_PRIVATE_TYPE));
    // 创建异步消息处理任务
    bTaskCreate("esp12f", bEsp12fTask, pdrv, &bEspRunInfo[pdrv->drv_no].task_attr);
    // AT初始化，注册回调和发送函数
    bAtInit(&bEspRunInfo[pdrv->drv_no].at_ctx.at, _bAtCmdCb, _bAtSendData, pdrv);
    // 串口接收初始化，注册接收空闲回调
    bHAL_UART_INIT_ATTR(&bEspRunInfo[pdrv->drv_no].at_ctx.uart_attr,
                        &(bEspRunInfo[pdrv->drv_no].at_ctx.uart_recv_buf[0]),
                        WIFIMODULE_RECVBUF_MAX, 50, _bHalUartIdleCb, pdrv);
    bHalUartReceiveIdle(*((bESP12F_HalIf_t *)pdrv->hal_if),
                        &bEspRunInfo[pdrv->drv_no].at_ctx.uart_attr);
    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_ESP12F, bESP12F_Init);
#ifdef BSECTION_NEED_PRAGMA
#pragma section
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
// https://docs.ai-thinker.com/固件汇总  MQTT透传AT固件 （固件号：1112）
/**
################################################
[00:15:04.169]发→◇AT
□
[00:15:04.181]收←◆AT

OK

[00:15:12.066]发→◇ATE0
□
[00:15:12.076]收←◆ATE0

OK

[00:15:17.475]发→◇AT+CWMODE=1
□
[00:15:17.484]收←◆
OK

[00:15:21.314]发→◇AT+CIPMUX=1
□
[00:15:21.327]收←◆
OK

[00:15:56.405]发→◇AT+CWJAP="12345xxx","55667788"
□
[00:15:58.342]收←◆WIFI CONNECTED

[00:16:01.707]收←◆WIFI GOT IP

OK

[00:18:01.185]发→◇AT+CIPSTART=0,"TCP","192.168.169.161",666
□
[00:18:01.387]收←◆0,CONNECT

OK

[00:18:20.018]收←◆
+IPD,0,16:12312312312313


[00:18:30.771]收←◆
+IPD,0,16:12312312312313

[18:51:02.706]发→◇AT+CIPSEND=1,3
□
[18:51:02.708]收←◆AT+CIPSEND=1,3

OK
>
[18:51:13.665]发→◇123□
[18:51:13.669]收←◆
Recv 3 bytes

[18:51:13.717]收←◆
SEND OK


收←◆1,CLOSED

[18:58:42.230]收←◆+STA_CONNECTED:"xxxxxxxxxxx"

[18:59:04.006]收←◆+STA_DISCONNECTED:"xxxxxxxxxx"

[22:40:55.306]发→◇AT+CIPSERVER=1,123
□
[22:40:55.308]收←◆AT+CIPSERVER=1,123

OK

[22:42:31.709]收←◆0,CONNECT

[22:42:38.532]收←◆
+IPD,0,8:123445

[22:42:44.579]收←◆
+IPD,0,8:123445

[22:42:58.104]发→◇AT+CIPSERVER=1,123
□
[22:42:58.107]收←◆AT+CIPSERVER=1,123
no change

OK

[22:43:03.319]收←◆0,CLOSED

 */
/************************ Copyright (c) 2019 Bean *****END OF FILE********/
