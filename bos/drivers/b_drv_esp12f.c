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

#define _AT_ENABLE 1

#include "utils/inc/b_util_at.h"
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"
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

#ifndef ESP12F_CMD_TIMEOUT
#define WIFIMODULE_CMD_TIMEOUT (5000)
#else
#define WIFIMODULE_CMD_TIMEOUT (ESP12F_CMD_TIMEOUT)
#endif

#define WIFIMODULE_PCB_NUM_MAX (5)
#define WIFIMODULE_PCB_INDEX_IS_VALID(index) (index < WIFIMODULE_PCB_NUM_MAX)

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
#define WIFIMODULE_CONN_NUMBER_INCREASE(p) (((p)->link_number) += 0)
#define WIFIMODULE_CONN_NUMBER_DECREASE(p) (((p)->link_number > 0) ? (((p)->link_number) -= 1) : 0)
#define WIFIMODULE_CONN_NUMBER_GET(p) (((p)->link_number))

#define WIFIMODULE_IS_BUSY(p) (((p)->busy) != 0)
#define WIFIMODULE_SET_BUSY(p) (((p)->busy) = 1)
#define WIFIMODULE_CLEAR_BUSY(p) (((p)->busy) = 0)

#define WIFIMODULE_CLEAR_CTL_CMD(p) (((p)->ctl_ctx.ctl_cmd) = 0xff)
#define WIFIMODULE_SET_CTL_CMD(p, cmd) (((p)->ctl_ctx.ctl_cmd) = (cmd))

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
#define WIFI_PCB_STATE_ASSIGNED (1)
#define WIFI_PCB_STATE_WAIT_CONNECT (2)
#define WIFI_PCB_STATE_CONNECTING (3)
#define WIFI_PCB_STATE_CONNECTED (4)
#define WIFI_PCB_STATE_DISCONNECT (5)

/**
 * \}
 */

/**
 * \defgroup ESP12F_Private_TypesDefinitions
 * \{
 */

typedef char *(*pbAtCmdFunc_t)(void *prv);
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
        bWifiModuleCmdUnit_t *pcmdunit;
        uint8_t               unit_number;
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
static char *_bSetApInfo(void *arg);
static char *_bJoinAp(void *arg);
/**
 * \}
 */

/**
 * \defgroup ESP12F_Private_Variables
 * \{
 */

bDRIVER_HALIF_TABLE(bESP12F_HalIf_t, DRIVER_NAME);

static bEsp12fPrivate_t bEspRunInfo[bDRIVER_HALIF_NUM(bESP12F_HalIf_t, DRIVER_NAME)];

const static bWifiModuleCtlEvent_t bEsp12fCtlEvent[bCMD_WIFI_NUMBER_MAX] = {
    {0, 0},                                       // reserved
    {B_EVT_MODE_STA_OK, B_EVT_MODE_STA_FAIL},     // bCMD_WIFI_MODE_STA
    {B_EVT_MODE_AP_OK, B_EVT_MODE_STA_FAIL},      // bCMD_WIFI_MODE_AP
    {B_EVT_MODE_STA_AP_OK, B_EVT_MODE_STA_FAIL},  // bCMD_WIFI_MODE_STA_AP
    {B_EVT_JOIN_AP_OK, B_EVT_MODE_STA_FAIL},      // bCMD_WIFI_JOIN_AP
};

const static bWifiModuleCmdUnit_t bEspCmdReset[1] = {
    {"AT+RST\r\n", "OK", WIFIMODULE_CMD_SET_FORCE_WAIT(4000), NULL}};

const static bWifiModuleCmdUnit_t bEspCmdInit[2]      = {{"AT\r\n", "OK", 300, NULL},
                                                         {"ATE0\r\n", "OK", 300, NULL},
                                                         {"AT+CIPSTAMAC_CUR?\r\n", ">", 300, NULL}};
const static bWifiModuleCmdUnit_t bEspCmdStaMode[2]   = {{"AT+CWMODE=1\r\n", "OK", 300, NULL},
                                                         {"AT+CIPMUX=1\r\n", "OK", 300, NULL}};
const static bWifiModuleCmdUnit_t bEspCmdApMode[3]    = {{"AT+CWMODE=2\r\n", "OK", 300, NULL},
                                                         {NULL, NULL, 300, _bSetApInfo},
                                                         {"AT+CIPMUX=1\r\n", "OK", 300, NULL}};
const static bWifiModuleCmdUnit_t bEspCmdApStaMode[3] = {{"AT+CWMODE=3\r\n", "OK", 300, NULL},
                                                         {NULL, NULL, 300, _bSetApInfo},
                                                         {"AT+CIPMUX=1\r\n", "OK", 300, NULL}};
const static bWifiModuleCmdUnit_t bEspCmdJoinAp[1]    = {{NULL, "OK", 15000, _bJoinAp}};

const static bWifiModuleCmdUnit_t bEspCmdConnectRemote[1] = {{NULL, "OK", 1000, _bConnectRemote}};

/*/**
 * \brief
 */

/**
 * \defgroup ESP12F_Private_Functions
 * \{
 */
//----------------------------------------------------------------------------------------------------
//------------------------注册到其他模块的回调等接口----------------------------------------------------
//----------------------------------------------------------------------------------------------------
static void _bAtCmdCb(uint8_t isok, void *user_data)
{
    bDriverInterface_t *pdrv = (bDriverInterface_t *)user_data;
    bDRIVER_GET_PRIVATE(_priv, bEsp12fPrivate_t, pdrv);
    _priv->cmd_ctx.cmd_result = (isok) ? WIFIMODULE_CMD_RESULT_OK : WIFIMODULE_CMD_RESULT_FAIL;
    b_log_w("at result:%d\r\n", isok);
}

static void _bAtNewDataCb(uint8_t *pbuf, uint16_t len, void (*pfree)(void *), void *user_data)
{
}

static void _bAtSendData(const uint8_t *pbuf, uint16_t len, void *user_data)
{
    bDRIVER_GET_HALIF(phal, bESP12F_HalIf_t, (bDriverInterface_t *)user_data);
    bHalUartSend(*phal, pbuf, len);
    b_log(">>%s\r\n", pbuf);
}

static int _bHalUartIdleCb(uint8_t *pbuf, uint16_t len, void *user_data)
{
    bDRIVER_GET_PRIVATE(_priv, bEsp12fPrivate_t, (bDriverInterface_t *)user_data);
    bAtFeedData(&_priv->at_ctx.at, pbuf, len);
    return 0;
}

//--------------------------------------------------------------------------------------------------
// ---------------------通过函数拼装AT指令-----------------------------------------------------------
//--------------------------------------------------------------------------------------------------
static char *_bSetApInfo(void *arg)
{
    bEsp12fPrivate_t *prv = (bEsp12fPrivate_t *)arg;
    memset(prv->at_ctx.uart_send_buf, 0, sizeof(prv->at_ctx.uart_send_buf));
    snprintf(prv->at_ctx.uart_send_buf, ESP12F_CMD_BUF_LEN, "AT+CWSAP=\"%s\",\"%s\",5,%d\r\n",
             prv->ctl_ctx.ctl_param.ap.ssid, prv->ctl_ctx.ctl_param.ap.passwd,
             prv->ctl_ctx.ctl_param.ap.encryption);
    return &prv->at_ctx.uart_send_buf[0];
}

static char *_bJoinAp(void *arg)
{
    bEsp12fPrivate_t *prv = (bEsp12fPrivate_t *)arg;
    memset(prv->at_ctx.uart_send_buf, 0, sizeof(prv->at_ctx.uart_send_buf));
    snprintf(prv->at_ctx.uart_send_buf, ESP12F_CMD_BUF_LEN, "AT+CWJAP=\"%s\",\"%s\"\r\n",
             prv->ctl_ctx.ctl_param.ap.ssid, prv->ctl_ctx.ctl_param.ap.passwd);
    return &prv->at_ctx.uart_send_buf[0];
}

static char *_bConnectRemote(void *arg)
{
    int               i          = 0;
    char             *pcb_type   = "TCP";
    char              ip_str[16] = {0};
    bEsp12fPrivate_t *prv        = (bEsp12fPrivate_t *)arg;
    memset(prv->at_ctx.uart_send_buf, 0, sizeof(prv->at_ctx.uart_send_buf));
    for (i = 0; i < WIFIMODULE_PCB_NUM_MAX; i++)
    {
        if (prv->pcb_ctx[i].state == WIFI_PCB_STATE_WAIT_CONNECT)
        {
            prv->pcb_ctx[i].state = WIFI_PCB_STATE_CONNECTING;
            break;
        }
    }
    if (i >= WIFIMODULE_PCB_NUM_MAX)
    {
        return NULL;
    }
    if (prv->pcb_ctx[i].is_tcp == 0)
    {
        pcb_type = "UDP";
    }
    sprintf(ip_str, "%d.%d.%d.%d", (prv->pcb_ctx[i].ip >> 24) & 0xff,
            (prv->pcb_ctx[i].ip >> 16) & 0xff, (prv->pcb_ctx[i].ip >> 8) & 0xff,
            prv->pcb_ctx[i].ip & 0xff);
    snprintf(prv->at_ctx.uart_send_buf, ESP12F_CMD_BUF_LEN, "AT+CIPSTART=%d,\"%s\",\"%s\",%d\r\n",
             i, pcb_type, ip_str, prv->pcb_ctx[i].port);
    return &prv->at_ctx.uart_send_buf[0];
}

//--------------------------------------------------------------------------------------------------

static void _bWifiModuleCtlCmdResult(bEsp12fPrivate_t *_priv, uint8_t is_ok)
{
    if (_priv->cb_ctx.cb.cb && (_priv->ctl_ctx.ctl_cmd < (bCMD_WIFI_NUMBER_MAX)))
    {
        if (is_ok)
        {
            _priv->cb_ctx.cb.cb(bEsp12fCtlEvent[_priv->ctl_ctx.ctl_cmd].ok_event, NULL, NULL,
                                _priv->cb_ctx.cb.user_data);
        }
        else
        {
            _priv->cb_ctx.cb.cb(bEsp12fCtlEvent[_priv->ctl_ctx.ctl_cmd].fail_event, NULL, NULL,
                                _priv->cb_ctx.cb.user_data);
        }
    }
}

static int _bWifiModuleAddExecCmd(bEsp12fPrivate_t *_priv, const bWifiModuleCmdUnit_t *pcmdunit,
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

static void _bWifiModuleExecCmdStart(bEsp12fPrivate_t *_priv)
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
}

static void _bWifiModuleExecCmdStop(bEsp12fPrivate_t *_priv)
{
    _priv->cmd_ctx.cmd_data.number = 0;
    _priv->cmd_ctx.cmd_unit_index  = 0;
    _priv->cmd_ctx.cmd_index       = 0;
    WIFIMODULE_CLEAR_BUSY(_priv);
}

static bWifiModuleCmdUnit_t *_bWifiModuleCurrentCmdUnit(bEsp12fPrivate_t *_priv)
{
    if (WIFIMODULE_IS_BUSY(_priv))
    {
        return &_priv->cmd_ctx.cmd_data.cmd_table[_priv->cmd_ctx.cmd_index]
                    .pcmdunit[_priv->cmd_ctx.cmd_unit_index];
    }
    return NULL;
}

static bWifiModuleCmdUnit_t *_bWifiModuleNextCmd(bEsp12fPrivate_t *_priv)
{
    bWifiModuleCmdUnit_t *pcmdunit = NULL;
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

static void _bEsp12fCtlResult(uint8_t cmd, uint8_t isok, bEsp12fPrivate_t *_priv)
{
    _priv->cb_ctx.cb.cb(isok ? bEsp12fCtlEvent[cmd].ok_event : bEsp12fCtlEvent[cmd].fail_event,
                        NULL, NULL, _priv->cb_ctx.cb.user_data);
}

PT_THREAD(bEsp12fTask)(struct pt *pt, void *arg)
{
    bWifiModuleCmdUnit_t *pcmdunit = NULL;
    bDriverInterface_t   *pdrv     = (bDriverInterface_t *)arg;
    bDRIVER_GET_PRIVATE(_priv, bEsp12fPrivate_t, pdrv);
    PT_BEGIN(pt);
    while (1)
    {
        if (WIFIMODULE_IS_BUSY(_priv))
        {
            pcmdunit = _bWifiModuleCurrentCmdUnit(_priv);
            if (pcmdunit != NULL)
            {
                WIFIMODULE_CLEAR_CMD_RESULT(_priv);
                if (pcmdunit->pcmd)
                {
                    bAtSendCmd(&_priv->at_ctx.at, pcmdunit->pcmd, pcmdunit->resp,
                               WIFIMODULE_CMD_TIMEOUT_GET(pcmdunit->timeout));
                }
                else
                {
                    bAtSendCmd(&_priv->at_ctx.at, pcmdunit->cmd_f(_priv), pcmdunit->resp,
                               WIFIMODULE_CMD_TIMEOUT_GET(pcmdunit->timeout));
                }
                PT_WAIT_UNTIL(
                    pt, WIFIMODULE_CMD_RESULT_IS_OK(_priv) || WIFIMODULE_CMD_RESULT_IS_FAIL(_priv),
                    ESP12F_CMD_TIMEOUT);

                if (WIFIMODULE_CMD_RESULT_IS_OK(_priv))
                {
                    if (WIFIMODULE_CMD_IS_FORCE_WAIT(pcmdunit->timeout))
                    {
                        bTaskDelayMs(pt, WIFIMODULE_CMD_TIMEOUT_GET(pcmdunit->timeout));
                    }
                    if (_bWifiModuleNextCmd(_priv) == NULL)
                    {
                        _bWifiModuleCtlCmdResult(_priv, 1);
                    }
                }
                else if (WIFIMODULE_CMD_RESULT_IS_FAIL(_priv))
                {
                    _bWifiModuleExecCmdStop(_priv);
                    _bWifiModuleCtlCmdResult(_priv, 0);
                }
            }
            if (!WIFIMODULE_IS_BUSY(_priv))
            {
                if (WIFIMODULE_CMD_RESULT_IS_OK(_priv))
                {
                    _bEsp12fCtlResult(_priv->ctl_ctx.ctl_cmd, 1, _priv);
                }
                else if (WIFIMODULE_CMD_RESULT_IS_FAIL(_priv))
                {
                    _bEsp12fCtlResult(_priv->ctl_ctx.ctl_cmd, 0, _priv);
                }
            }
        }
        bTaskYield(pt);
    }
    PT_END(pt);
}
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------

static void _bEsp12fRegCallback(pTcpIpCallback_t cb, void *arg, bTcpIpNetif_t *pnetif)
{
    if (pnetif == NULL || cb == NULL)
    {
        return;
    }
    bEsp12fPrivate_t *prv = (bEsp12fPrivate_t *)pnetif->private;
    if (prv != NULL)
    {
        prv->cb_ctx.tcpip_cb     = cb;
        prv->cb_ctx.tcpip_cb_arg = arg;
    }
}

static int _bEsp12fGetPcb(bEsp12fPrivate_t *prv)
{
    int i = 0;
    for (i = 0; i < WIFIMODULE_PCB_NUM_MAX; i++)
    {
        if (prv->pcb_ctx[i].state == WIFI_PCB_STATE_IDLE)
        {
            prv->pcb_ctx[i].state = WIFI_PCB_STATE_ASSIGNED;
            return i;
        }
    }
    return -1;
}

static void *_bEsp12fNewTcp(bTcpIpNetif_t *pnetif)
{
    bWifiModulePcb_t *module_pcb = NULL;
    if (pnetif == NULL)
    {
        return NULL;
    }
    bEsp12fPrivate_t *prv = (bEsp12fPrivate_t *)pnetif->private;
    if (prv == NULL)
    {
        return NULL;
    }
    module_pcb = (bWifiModulePcb_t *)bCalloc(1, sizeof(bWifiModulePcb_t));
    if (module_pcb == NULL)
    {
        return NULL;
    }
    int pcb_index = _bEsp12fGetPcb(prv);
    if (pcb_index < 0)
    {
        bFree(module_pcb);
        return NULL;
    }
    prv->pcb_ctx[pcb_index].is_tcp = 1;
    module_pcb->pcb_index          = pcb_index;
    module_pcb->private            = prv;
    return module_pcb;
}

static int _bEsp12fBind(void *pcb, uint16_t port)
{
    return 0;
}

static int _bEsp12fConnect(void *pcb, uint32_t ip, uint16_t port)
{
    bWifiModulePcb_t *_pcb = (bWifiModulePcb_t *)pcb;

    if (_pcb == NULL || !WIFIMODULE_PCB_INDEX_IS_VALID(_pcb->pcb_index))
    {
        return -1;
    }
    bEsp12fPrivate_t *_priv = (bEsp12fPrivate_t *)_pcb->private;
    if (_priv == NULL)
    {
        return NULL;
    }
    if (WIFIMODULE_IS_BUSY(_priv))
    {
        return -2;
    }
    WIFIMODULE_CLEAR_CTL_CMD(_priv);
    _priv->pcb_ctx[_pcb->pcb_index].ip    = ip;
    _priv->pcb_ctx[_pcb->pcb_index].port  = port;
    _priv->pcb_ctx[_pcb->pcb_index].state = WIFI_PCB_STATE_WAIT_CONNECT;
    _bWifiModuleAddExecCmd(_priv, &bEspCmdConnectRemote[0],
                           sizeof(bEspCmdConnectRemote) / sizeof(bWifiModuleCmdUnit_t));
    _bWifiModuleExecCmdStart(_priv);
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
    bDRIVER_GET_PRIVATE(_priv, bEsp12fPrivate_t, pdrv);
    b_log("ctl:%d\r\n", cmd);
    if (!WIFIMODULE_IS_BUSY(_priv))
    {
        _priv->ctl_ctx.ctl_cmd = cmd;
    }
    switch (cmd)
    {
        case bCMD_GET_DRIVER_NETIF:
        {
            if (param == NULL)
            {
                return -1;
            }
            ((bDriverNetif_t *)param)->private = _priv;
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
        }
        break;
        case bCMD_GET_STACK_IF:
        {
            if (param == NULL)
            {
                return -1;
            }
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
    memset(pdrv->_private._p, 0, sizeof(bEsp12fPrivate_t));
    // 创建异步消息处理任务
    bTaskCreate("esp12f", bEsp12fTask, pdrv, &bEspRunInfo[pdrv->drv_no].task_attr);
    // AT初始化，注册回调和发送函数
    bAtInit(&bEspRunInfo[pdrv->drv_no].at_ctx.at, _bAtCmdCb, _bAtNewDataCb, _bAtSendData, pdrv);
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
 */
/************************ Copyright (c) 2019 Bean *****END OF FILE********/
