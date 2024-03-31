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

#ifndef ESP12F_CMD_BUF_LEN
#define ESP12F_CMD_BUF_LEN (128)
#endif  // !ESP12F_CMD_BUF_LEN

#ifndef ESP12F_CMD_TIMEOUT
#define ESP12F_CMD_TIMEOUT (5000)
#endif

#define ESP12F_CONN_NUM_MAX (5)

#define ESP12F_CMD_RESET_BIT (0x01)
#define ESP12F_CMD_INIT_BIT (0x02)

#define ESP12F_CMD_RESULT_OK (0x01)
#define ESP12F_CMD_RESULT_FAIL (0x02)

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
} bEsp12fCmd_t;

typedef struct
{
    bWifiDrvEvent_t ok_event;
    bWifiDrvEvent_t fail_event;
} bEsp12fCtlEvent_t;

typedef struct
{
    uint8_t busy;
    struct
    {
        bTcpUdpInfo_t info;
        uint8_t       connected;
    } conn[ESP12F_CONN_NUM_MAX];
    uint8_t             conn_id;
    uint8_t             cmd;
    const bEsp12fCmd_t *pcmd;
    uint8_t             cmd_num;
    uint8_t             cmd_index;
    uint8_t             result;
    uint8_t             step;
    uint8_t             uart_buf[ESP12F_UART_RX_BUF_LEN];
    char                cmd_tmp[ESP12F_CMD_BUF_LEN + 1];
    union
    {
        bApInfo_t     ap;
        bTcpUdpInfo_t tcpudp;
        char          remote[WIFI_REMOTE_ADDR_LEN_MAX + 1];
        bTcpUdpData_t dat;
    } param;
    bAtStruct_t        at;
    bHalUartIdleAttr_t uart_attr;
    bTaskAttr_t        task_attr;
    bWifiDrvCallback_t cb;
} bEsp12fPrivate_t;

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
static char *_bPing(void *arg);
static char *_bLocalTcpServer(void *arg);
static char *_bRemoteTcpServer(void *arg);
static char *_bRemoteUdpServer(void *arg);
static char *_bCloseTcpUdp(void *arg);
static char *_bTcpUdpSend(void *arg);
/**
 * \}
 */

/**
 * \defgroup ESP12F_Private_Variables
 * \{
 */

bDRIVER_HALIF_TABLE(bESP12F_HalIf_t, DRIVER_NAME);

static bEsp12fPrivate_t bEspRunInfo[bDRIVER_HALIF_NUM(bESP12F_HalIf_t, DRIVER_NAME)];

const static bEsp12fCtlEvent_t bEsp12fCtlEvent[] = {
    {0, 0},                                       // reserved
    {B_EVT_MODE_STA_OK, B_EVT_MODE_STA_FAIL},     // bCMD_WIFI_MODE_STA
    {B_EVT_MODE_AP_OK, B_EVT_MODE_STA_FAIL},      // bCMD_WIFI_MODE_AP
    {B_EVT_MODE_STA_AP_OK, B_EVT_MODE_STA_FAIL},  // bCMD_WIFI_MODE_STA_AP
    {B_EVT_JOIN_AP_OK, B_EVT_MODE_STA_FAIL},      // bCMD_WIFI_JOIN_AP
    {B_EVT_PING_OK, B_EVT_PING_FAIL},             // bCMD_WIFI_PING

    {B_EVT_LOCAL_TCP_SERVER_OK, B_EVT_LOCAL_TCP_SERVER_FAIL},  // bCMD_WIFI_LOCAL_TCP_SERVER
    {B_EVT_LOCAL_UDP_SERVER_OK, B_EVT_LOCAL_UDP_SERVER_FAIL},  // bCMD_WIFI_LOCAL_UDP_SERVER
    {B_EVT_CONN_TCP_SERVER_OK, B_EVT_CONN_TCP_SERVER_FAIL},    // bCMD_WIFI_REMOT_TCP_SERVER
    {B_EVT_CONN_UDP_SERVER_OK, B_EVT_CONN_UDP_SERVER_FAIL},    // bCMD_WIFI_REMOT_UDP_SERVER
    {B_EVT_CLOSE_CONN_OK, B_EVT_CLOSE_CONN_FAIL},              // bCMD_WIFI_TCPUDP_CLOSE
    {B_EVT_CONN_SEND_OK, B_EVT_CONN_SEND_FAIL},                // bCMD_WIFI_TCPUDP_SEND
};

const static bEsp12fCmd_t bEspCmdReset             = {"AT+RST\r\n", "OK", 300, NULL};
const static bEsp12fCmd_t bEspCmdInit[2]           = {{"AT\r\n", "OK", 300, NULL},
                                                      {"ATE0\r\n", "OK", 300, NULL}};
const static bEsp12fCmd_t bEspCmdStaMode[2]        = {{"AT+CWMODE=1\r\n", "OK", 300, NULL},
                                                      {"AT+CIPMUX=1\r\n", "OK", 300, NULL}};
const static bEsp12fCmd_t bEspCmdApMode[3]         = {{"AT+CWMODE=2\r\n", "OK", 300, NULL},
                                                      {NULL, NULL, 300, _bSetApInfo},
                                                      {"AT+CIPMUX=1\r\n", "OK", 300, NULL}};
const static bEsp12fCmd_t bEspCmdApStaMode[3]      = {{"AT+CWMODE=3\r\n", "OK", 300, NULL},
                                                      {NULL, NULL, 300, _bSetApInfo},
                                                      {"AT+CIPMUX=1\r\n", "OK", 300, NULL}};
const static bEsp12fCmd_t bEspCmdJoinAp[1]         = {{NULL, "OK", 15000, _bJoinAp}};
const static bEsp12fCmd_t bEspCmdPing[1]           = {{NULL, "OK", 3000, _bPing}};
const static bEsp12fCmd_t bEspCmdLocalTcpServer[1] = {{NULL, "OK", 300, _bLocalTcpServer}};
const static bEsp12fCmd_t bEspCmdRemotTcpServer[1] = {{NULL, "OK", 1000, _bRemoteTcpServer}};
const static bEsp12fCmd_t bEspCmdRemotUdpServer[1] = {{NULL, "OK", 1000, _bRemoteUdpServer}};
const static bEsp12fCmd_t bEspCmdCloseTcpUdp[1]    = {{NULL, "OK", 300, _bCloseTcpUdp}};
const static bEsp12fCmd_t bEspCmdSendData[1]       = {{NULL, ">", 300, _bTcpUdpSend}};
/**
 * \}
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
    _priv->result = (isok) ? ESP12F_CMD_RESULT_OK : ESP12F_CMD_RESULT_FAIL;
    b_log_w("at result:%d\r\n", isok);
}

static void _bEsp12fFree(void *addr)
{
    bFree(addr);
}
static void *_bAtNewDataMalloc(uint8_t *pbuf, uint16_t len)
{
    if (len == 0 || pbuf == NULL)
    {
        return NULL;
    }
    bTcpUdpData_t *pdat = bMalloc(sizeof(bTcpUdpData_t));
    if (pdat == NULL)
    {
        return NULL;
    }
    uint8_t *ptmp = bMalloc(len + 1);
    if (ptmp == NULL)
    {
        bFree(pdat);
        return NULL;
    }
    memset(ptmp, 0, len + 1);
    memcpy(ptmp, pbuf, len);
    pdat->len     = len;
    pdat->pbuf    = ptmp;
    pdat->release = _bEsp12fFree;
    return pdat;
}

static void _bAtNewDataFree(void *p)
{
    bTcpUdpData_t *dat = (bTcpUdpData_t *)p;
    if (dat == NULL)
    {
        return;
    }
    if (dat->release)
    {
        if (dat->pbuf)
        {
            dat->release(dat->pbuf);
            dat->pbuf = NULL;
        }
    }
    bFree(dat);
}

static void _bAtNewDataCb(uint8_t *pbuf, uint16_t len, void (*pfree)(void *), void *user_data)
{
    bDriverInterface_t *pdrv = (bDriverInterface_t *)user_data;
    bDRIVER_GET_PRIVATE(_priv, bEsp12fPrivate_t, pdrv);
    bTcpUdpData_t *pdat    = NULL;
    char          *p       = NULL;
    char          *tmp_p   = NULL;
    int            conn_id = 0;
    int            rlen    = 0;
    int            retval  = -1;
    //+IPD,0,16:12312312312313
    p = strstr((const char *)pbuf, "+IPD,");
    if (p != NULL)
    {
        retval = sscanf(p, "+IPD,%d,%d:%*s", &conn_id, &rlen);
        if (retval == 2 && rlen > 0)
        {
            if (conn_id >= 0 && conn_id < ESP12F_CONN_NUM_MAX)
            {
                p = strstr(p, ":");
                p = p + 1;
                b_log("read:%p, %d\r\n", p, rlen);
                pdat = (bTcpUdpData_t *)_bAtNewDataMalloc((uint8_t *)p, rlen);
                if (pdat != NULL)
                {
                    memcpy(&pdat->conn, &_priv->conn[conn_id].info, sizeof(bTcpUdpInfo_t));
                    _priv->cb.cb(B_EVT_CONN_NEW_DATA, pdat, _bAtNewDataFree, _priv->cb.user_data);
                }
            }
        }
        else
        {
            b_log_e("sscanf error %d %d \r\n", retval, rlen);
        }
    }
    if (pfree)
    {
        if (pbuf)
        {
            pfree(pbuf);
            pbuf = NULL;
        }
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
    bDRIVER_GET_PRIVATE(_priv, bEsp12fPrivate_t, (bDriverInterface_t *)user_data);
    bAtFeedData(&_priv->at, pbuf, len);
    return 0;
}

//--------------------------------------------------------------------------------------------------
// ---------------------通过函数拼装AT指令-----------------------------------------------------------
//--------------------------------------------------------------------------------------------------
static char *_bSetApInfo(void *arg)
{
    bEsp12fPrivate_t *prv = (bEsp12fPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ESP12F_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ESP12F_CMD_BUF_LEN, "AT+CWSAP=\"%s\",\"%s\",5,%d\r\n",
             prv->param.ap.ssid, prv->param.ap.passwd, prv->param.ap.encryption);
    return &prv->cmd_tmp[0];
}

static char *_bJoinAp(void *arg)
{
    bEsp12fPrivate_t *prv = (bEsp12fPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ESP12F_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ESP12F_CMD_BUF_LEN, "AT+CWJAP=\"%s\",\"%s\"\r\n", prv->param.ap.ssid,
             prv->param.ap.passwd);
    return &prv->cmd_tmp[0];
}

static char *_bPing(void *arg)
{
    bEsp12fPrivate_t *prv = (bEsp12fPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ESP12F_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ESP12F_CMD_BUF_LEN, "AT+PING=\"%s\"\r\n", prv->param.remote);
    return &prv->cmd_tmp[0];
}

static char *_bLocalTcpServer(void *arg)
{
    bEsp12fPrivate_t *prv = (bEsp12fPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ESP12F_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ESP12F_CMD_BUF_LEN, "AT+CIPSERVER=1,%d\r\n", prv->param.tcpudp.port);
    return &prv->cmd_tmp[0];
}

static char *_bRemoteTcpServer(void *arg)
{
    bEsp12fPrivate_t *prv = (bEsp12fPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ESP12F_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ESP12F_CMD_BUF_LEN, "AT+CIPSTART=%d,\"TCP\",\"%s\",%d\r\n", prv->conn_id,
             prv->param.tcpudp.ip, prv->param.tcpudp.port);
    return &prv->cmd_tmp[0];
}

static char *_bRemoteUdpServer(void *arg)
{
    bEsp12fPrivate_t *prv = (bEsp12fPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ESP12F_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ESP12F_CMD_BUF_LEN, "AT+CIPSTART=%d,\"UDP\",\"%s\",%d\r\n", prv->conn_id,
             prv->param.tcpudp.ip, prv->param.tcpudp.port);
    return &prv->cmd_tmp[0];
}

static char *_bCloseTcpUdp(void *arg)
{
    bEsp12fPrivate_t *prv = (bEsp12fPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ESP12F_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ESP12F_CMD_BUF_LEN, "AT+CIPCLOSE=%d\r\n", prv->conn_id);
    return &prv->cmd_tmp[0];
}

static char *_bTcpUdpSend(void *arg)
{
    bEsp12fPrivate_t *prv = (bEsp12fPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ESP12F_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ESP12F_CMD_BUF_LEN, "AT+CIPSEND=%d,%d\r\n", prv->conn_id,
             prv->param.dat.len);
    return &prv->cmd_tmp[0];
}

//--------------------------------------------------------------------------------------------------

static void _bEsp12fCtlResult(uint8_t cmd, uint8_t isok, bDriverInterface_t *pdrv)
{
    bDRIVER_GET_PRIVATE(_priv, bEsp12fPrivate_t, pdrv);
    if (cmd == bCMD_WIFI_TCPUDP_SEND)
    {
        b_log("send data...\r\n");
        if (isok)
        {
            _bAtSendData(_priv->param.dat.pbuf, _priv->param.dat.len, pdrv);
        }
        if (_priv->param.dat.release)
        {
            _priv->param.dat.release(_priv->param.dat.pbuf);
            _priv->param.dat.pbuf = NULL;
        }
    }
    _priv->busy      = 0;
    _priv->cmd_index = 0;
    _priv->cmd_num   = 0;
    _priv->pcmd      = NULL;
    _priv->cb.cb(isok ? bEsp12fCtlEvent[cmd].ok_event : bEsp12fCtlEvent[cmd].fail_event, NULL, NULL,
                 _priv->cb.user_data);
}

PT_THREAD(bEsp12fTask)(struct pt *pt, void *arg)
{
    bDriverInterface_t *pdrv = (bDriverInterface_t *)arg;
    bDRIVER_GET_PRIVATE(_priv, bEsp12fPrivate_t, pdrv);
    if (_priv->busy == 0)
    {
        return 0;
    }
    PT_BEGIN(pt);
    while (1)
    {
        if (_priv->step & ESP12F_CMD_RESET_BIT)
        {
            _priv->result = 0;
            bAtSendCmd(&_priv->at, bEspCmdReset.pcmd, bEspCmdReset.resp, bEspCmdReset.timeout);
            PT_WAIT_UNTIL(pt, _priv->result != 0, ESP12F_CMD_TIMEOUT);
            if (_priv->result == ESP12F_CMD_RESULT_OK)
            {
                bTaskDelayMs(pt, 4000);
                _priv->step &= ~(ESP12F_CMD_RESET_BIT);
            }
            else
            {
                _bEsp12fCtlResult(_priv->cmd, 0, pdrv);
            }
        }
        else if (_priv->step & ESP12F_CMD_INIT_BIT)
        {
            _priv->result = 0;
            if (bEspCmdInit[_priv->cmd_index].pcmd)
            {
                bAtSendCmd(&_priv->at, bEspCmdInit[_priv->cmd_index].pcmd,
                           bEspCmdInit[_priv->cmd_index].resp,
                           bEspCmdInit[_priv->cmd_index].timeout);
            }
            else
            {
                bAtSendCmd(&_priv->at, bEspCmdInit[_priv->cmd_index].cmd_f(_priv),
                           bEspCmdInit[_priv->cmd_index].resp,
                           bEspCmdInit[_priv->cmd_index].timeout);
            }
            PT_WAIT_UNTIL(pt, _priv->result != 0, ESP12F_CMD_TIMEOUT);
            if (_priv->result == ESP12F_CMD_RESULT_OK)
            {
                _priv->cmd_index += 1;
                if (_priv->cmd_index >= sizeof(bEspCmdInit) / sizeof(bEsp12fCmd_t))
                {
                    _priv->cmd_index = 0;
                    _priv->step &= ~(ESP12F_CMD_INIT_BIT);
                }
            }
            else
            {
                _bEsp12fCtlResult(_priv->cmd, 0, pdrv);
            }
        }
        else if (_priv->pcmd != NULL && _priv->cmd_num > 0 && _priv->cmd_index < _priv->cmd_num)
        {
            _priv->result = 0;

            if (_priv->pcmd[_priv->cmd_index].pcmd)
            {
                bAtSendCmd(&_priv->at, _priv->pcmd[_priv->cmd_index].pcmd,
                           _priv->pcmd[_priv->cmd_index].resp,
                           _priv->pcmd[_priv->cmd_index].timeout);
            }
            else
            {
                bAtSendCmd(&_priv->at, _priv->pcmd[_priv->cmd_index].cmd_f(_priv),
                           _priv->pcmd[_priv->cmd_index].resp,
                           _priv->pcmd[_priv->cmd_index].timeout);
            }
            PT_WAIT_UNTIL(pt, _priv->result != 0, ESP12F_CMD_TIMEOUT);
            if (_priv->result == ESP12F_CMD_RESULT_OK)
            {
                _priv->cmd_index += 1;
                if (_priv->cmd_index >= _priv->cmd_num)
                {
                    _bEsp12fCtlResult(_priv->cmd, 1, pdrv);
                }
            }
            else
            {
                _bEsp12fCtlResult(_priv->cmd, 0, pdrv);
            }
        }
        bTaskDelayMs(pt, 100);
    }
    PT_END(pt);
}
//--------------------------------------------------------------------------------------------------
//  私有功能函数
//--------------------------------------------------------------------------------------------------
static void _bEsp12fProcessCmd(bEsp12fPrivate_t *_priv, uint8_t cmd, const bEsp12fCmd_t *pcmdtable,
                               uint8_t cmdtable_size, uint8_t reset, uint8_t init)
{
    _priv->cmd_index = 0;
    _priv->step      = 0;
    _priv->busy      = 1;
    _priv->cmd       = cmd;
    _priv->pcmd      = pcmdtable;
    _priv->cmd_num   = cmdtable_size;
    if (reset)
    {
        _priv->step |= ESP12F_CMD_RESET_BIT;
    }
    if (init)
    {
        _priv->step |= ESP12F_CMD_INIT_BIT;
    }
}

static int _bEsp12fGetConn(bEsp12fPrivate_t *_priv, bTcpUdpInfo_t *pinfo, uint8_t *unused_connid)
{
    int i      = 0;
    int retval = -1;
    for (i = 0; i < ESP12F_CONN_NUM_MAX; i++)
    {
        if (_priv->conn[i].connected == 1)
        {
            if (_priv->conn[i].info.port == pinfo->port &&
                strcmp(_priv->conn[i].info.ip, pinfo->ip) == 0)
            {
                return i;
            }
        }
        else if (unused_connid)
        {
            *unused_connid = i;
        }
    }
    return retval;
}

//--------------------------------------------------------------------------------------------------
static int _bESP12FCtl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    int     retval  = -1;
    uint8_t conn_id = 0;
    bDRIVER_GET_PRIVATE(_priv, bEsp12fPrivate_t, pdrv);
    if (_priv->busy)
    {
        return -1;
    }
    b_log("ctl:%d\r\n", cmd);
    _priv->cmd = cmd;
    switch (cmd)
    {
        case bCMD_WIFI_REG_CALLBACK:
        {
            if (param == NULL)
            {
                return -1;
            }
            bWifiDrvCallback_t *pcb = (bWifiDrvCallback_t *)param;
            _priv->cb.cb            = pcb->cb;
            _priv->cb.user_data     = pcb->user_data;
        }
        break;
        case bCMD_WIFI_SET_CALLBACK_ARG:
        {
            _priv->cb.user_data = param;
        }
        break;
        case bCMD_WIFI_MODE_STA:
        {
            _bEsp12fProcessCmd(_priv, cmd, &bEspCmdStaMode[0],
                               sizeof(bEspCmdStaMode) / sizeof(bEsp12fCmd_t), 1, 1);
        }
        break;
        case bCMD_WIFI_MODE_AP:
        case bCMD_WIFI_MODE_STA_AP:
        case bCMD_WIFI_JOIN_AP:
        {
            if (param == NULL)
            {
                return -1;
            }
            memcpy(&_priv->param.ap, param, sizeof(bApInfo_t));
            if (cmd == bCMD_WIFI_MODE_AP)
            {
                _bEsp12fProcessCmd(_priv, cmd, &bEspCmdApMode[0],
                                   sizeof(bEspCmdApMode) / sizeof(bEsp12fCmd_t), 1, 1);
            }
            else if (cmd == bCMD_WIFI_MODE_STA_AP)
            {
                _bEsp12fProcessCmd(_priv, cmd, &bEspCmdApStaMode[0],
                                   sizeof(bEspCmdApStaMode) / sizeof(bEsp12fCmd_t), 1, 1);
            }
            else if (cmd == bCMD_WIFI_JOIN_AP)
            {
                _bEsp12fProcessCmd(_priv, cmd, &bEspCmdJoinAp[0],
                                   sizeof(bEspCmdJoinAp) / sizeof(bEsp12fCmd_t), 0, 0);
            }
        }
        break;
        case bCMD_WIFI_PING:
        {
            if (param == NULL || strlen(param) > WIFI_REMOTE_ADDR_LEN_MAX)
            {
                return -1;
            }
            memset(&_priv->param.remote[0], 0, sizeof(_priv->param.remote));
            memcpy(&_priv->param.remote[0], param, strlen(param));
            _bEsp12fProcessCmd(_priv, cmd, &bEspCmdPing[0],
                               sizeof(bEspCmdPing) / sizeof(bEsp12fCmd_t), 0, 0);
        }
        break;
        case bCMD_WIFI_LOCAL_TCP_SERVER:
        case bCMD_WIFI_LOCAL_UDP_SERVER:
        case bCMD_WIFI_REMOT_TCP_SERVER:
        case bCMD_WIFI_REMOT_UDP_SERVER:
        case bCMD_WIFI_TCPUDP_CLOSE:
        {
            if (param == NULL)
            {
                return -1;
            }
            memcpy(&_priv->param.tcpudp, param, sizeof(bTcpUdpInfo_t));
            conn_id = 0xff;
            retval  = _bEsp12fGetConn(_priv, &_priv->param.tcpudp, &conn_id);
            b_log_w("retval %d cnn_id %02x \r\n", retval, conn_id);
            if (cmd == bCMD_WIFI_LOCAL_TCP_SERVER)
            {
                _bEsp12fProcessCmd(_priv, cmd, &bEspCmdLocalTcpServer[0],
                                   sizeof(bEspCmdLocalTcpServer) / sizeof(bEsp12fCmd_t), 0, 0);
            }
            else if (cmd == bCMD_WIFI_REMOT_TCP_SERVER)
            {
                if (conn_id == 0xff)
                {
                    b_log_e("conn full, use conn_id 0\r\n");
                    return -2;
                }
                _priv->conn_id = conn_id;
                memcpy(&_priv->conn[conn_id], &_priv->param.tcpudp, sizeof(bTcpUdpInfo_t));
                _priv->conn[conn_id].connected = 1;
                _bEsp12fProcessCmd(_priv, cmd, &bEspCmdRemotTcpServer[0],
                                   sizeof(bEspCmdRemotTcpServer) / sizeof(bEsp12fCmd_t), 0, 0);
            }
            else if (cmd == bCMD_WIFI_REMOT_UDP_SERVER)
            {
                if (conn_id == 0xff)
                {
                    b_log_e("conn full\r\n");
                    return -2;
                }
                _priv->conn_id = conn_id;
                memcpy(&_priv->conn[conn_id], &_priv->param.tcpudp, sizeof(bTcpUdpInfo_t));
                _priv->conn[conn_id].connected = 1;
                _bEsp12fProcessCmd(_priv, cmd, &bEspCmdRemotUdpServer[0],
                                   sizeof(bEspCmdRemotUdpServer) / sizeof(bEsp12fCmd_t), 0, 0);
            }
            else if (cmd == bCMD_WIFI_TCPUDP_CLOSE)
            {
                if (retval < 0)
                {
                    b_log_e("cannt find..\r\n");
                    return -2;
                }
                _priv->conn_id                = retval;
                _priv->conn[retval].connected = 0;
                _bEsp12fProcessCmd(_priv, cmd, &bEspCmdCloseTcpUdp[0],
                                   sizeof(bEspCmdCloseTcpUdp) / sizeof(bEsp12fCmd_t), 0, 0);
            }
        }
        break;
        case bCMD_WIFI_TCPUDP_SEND:
        {
            if (param == NULL)
            {
                return -1;
            }
            bTcpUdpData_t *pdat = (bTcpUdpData_t *)param;
            if (pdat->pbuf == NULL)
            {
                return -1;
            }
            retval = _bEsp12fGetConn(_priv, &_priv->param.tcpudp, NULL);
            if (retval < 0)
            {
                b_log_e("cannt find..\r\n");
                return -2;
            }
            memcpy(&_priv->param.dat, param, sizeof(bTcpUdpData_t));
            _priv->conn_id = retval;
            _bEsp12fProcessCmd(_priv, cmd, &bEspCmdSendData[0],
                               sizeof(bEspCmdSendData) / sizeof(bEsp12fCmd_t), 0, 0);
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
    bAtInit(&bEspRunInfo[pdrv->drv_no].at, _bAtCmdCb, _bAtNewDataCb, _bAtSendData, pdrv);
    // 串口接收初始化，注册接收空闲回调
    bHAL_UART_INIT_ATTR(&bEspRunInfo[pdrv->drv_no].uart_attr,
                        &(bEspRunInfo[pdrv->drv_no].uart_buf[0]), ESP12F_UART_RX_BUF_LEN, 50,
                        _bHalUartIdleCb, pdrv);
    bHalUartReceiveIdle(*((bESP12F_HalIf_t *)pdrv->hal_if), &bEspRunInfo[pdrv->drv_no].uart_attr);
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
