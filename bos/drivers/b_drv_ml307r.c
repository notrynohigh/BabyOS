/**
 *!
 * \file        b_drv_ml307r.c
 * \version     v0.0.1
 * \date        2024/07/08
 * \author      Haimeng(haimeng.chen@qq.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 haimeng
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
#include "drivers/inc/b_drv_ml307r.h"

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
 * \addtogroup ML307R
 * \{
 */

/**
 * \defgroup ML307R_Private_Defines
 * \{
 */
#define DRIVER_NAME ML307R

#ifndef ML307R_CMD_BUF_LEN
#define ML307R_CMD_BUF_LEN (128)
#endif  // !ML307R_CMD_BUF_LEN

#ifndef ML307R_CMD_TIMEOUT
#define ML307R_CMD_TIMEOUT (5000)
#endif

#define ML307R_CONN_NUM_MAX (5)

#define ML307R_CMD_RESET_BIT (0x01)
#define ML307R_CMD_INIT_BIT (0x02)

#define ML307R_CMD_RESULT_OK (0x01)
#define ML307R_CMD_RESULT_FAIL (0x02)

#ifndef CONNECT_RECVBUF_MAX
#define CONNECT_RECVBUF_MAX (1024)
#endif

/**
 * \}
 */

/**
 * \defgroup ML307R_Private_TypesDefinitions
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
} bMl307rCmd_t;

typedef struct
{
    bWifiDrvEvent_t ok_event;
    bWifiDrvEvent_t fail_event;
} bMl307rCtlEvent_t;

typedef struct
{
    uint8_t busy;
    struct
    {
        bTcpUdpInfo_t info;
        uint8_t       connected;
    } conn[ML307R_CONN_NUM_MAX];
    uint8_t             conn_id;
    uint8_t             cmd;
    const bMl307rCmd_t *pcmd;
    uint8_t             cmd_num;
    uint8_t             cmd_index;
    uint8_t             result;
    uint8_t             step;
    uint8_t             uart_buf[CONNECT_RECVBUF_MAX];
    char                cmd_tmp[ML307R_CMD_BUF_LEN + 1];
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
} bMl307rPrivate_t;

/**
 * \}
 */

/**
 * \defgroup ML307R_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup ML307R_Private_FunctionPrototypes
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

bDRIVER_HALIF_TABLE(bML307R_HalIf_t, DRIVER_NAME);

static bMl307rPrivate_t bMl307rRunInfo[bDRIVER_HALIF_NUM(bML307R_HalIf_t, DRIVER_NAME)];

const static bMl307rCtlEvent_t bMl307rCtlEvent[] = {
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

const static bMl307rCmd_t bMl307rCmdReset             = {"AT+RST\r\n", "OK", 300, NULL};
const static bMl307rCmd_t bMl307rCmdInit[2]           = {{"AT\r\n", "OK", 300, NULL},
                                                      {"ATE0\r\n", "OK", 300, NULL}};
const static bMl307rCmd_t bMl307rCmdStaMode[2]        = {{"AT+CWMODE=1\r\n", "OK", 300, NULL},
                                                      {"AT+CIPMUX=1\r\n", "OK", 300, NULL}};
const static bMl307rCmd_t bMl307rCmdApMode[3]         = {{"AT+CWMODE=2\r\n", "OK", 300, NULL},
                                                      {NULL, NULL, 300, _bSetApInfo},
                                                      {"AT+CIPMUX=1\r\n", "OK", 300, NULL}};
const static bMl307rCmd_t bMl307rCmdApStaMode[3]      = {{"AT+CWMODE=3\r\n", "OK", 300, NULL},
                                                      {NULL, NULL, 300, _bSetApInfo},
                                                      {"AT+CIPMUX=1\r\n", "OK", 300, NULL}};
const static bMl307rCmd_t bMl307rCmdJoinAp[1]         = {{NULL, "OK", 15000, _bJoinAp}};
const static bMl307rCmd_t bMl307rCmdPing[1]           = {{NULL, "OK", 3000, _bPing}};
const static bMl307rCmd_t bMl307rCmdLocalTcpServer[1] = {{NULL, "OK", 300, _bLocalTcpServer}};
const static bMl307rCmd_t bMl307rCmdRemotTcpServer[1] = {{NULL, "OK", 1000, _bRemoteTcpServer}};
const static bMl307rCmd_t bMl307rCmdRemotUdpServer[1] = {{NULL, "OK", 1000, _bRemoteUdpServer}};
const static bMl307rCmd_t bMl307rCmdCloseTcpUdp[1]    = {{NULL, "OK", 300, _bCloseTcpUdp}};
const static bMl307rCmd_t bMl307rCmdSendData[1]       = {{NULL, ">", 300, _bTcpUdpSend}};
/**
 * \}
 */

/**
 * \defgroup ML307R_Private_Functions
 * \{
 */
//----------------------------------------------------------------------------------------------------
//------------------------注册到其他模块的回调等接口----------------------------------------------------
//----------------------------------------------------------------------------------------------------
static void _bAtCmdCb(uint8_t isok, void *user_data)
{
    bDriverInterface_t *pdrv = (bDriverInterface_t *)user_data;
    bDRIVER_GET_PRIVATE(_priv, bMl307rPrivate_t, pdrv);
    _priv->result = (isok) ? ML307R_CMD_RESULT_OK : ML307R_CMD_RESULT_FAIL;
    b_log_w("at result:%d\r\n", isok);
}

static void _bMl307rFree(void *addr)
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
    pdat->release = _bMl307rFree;
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
    bDRIVER_GET_PRIVATE(_priv, bMl307rPrivate_t, pdrv);
    bTcpUdpData_t *pdat    = NULL;
    char          *p       = NULL;
    char          *tmp_p   = NULL;
    int            conn_id = 0;
    int            rlen    = 0;
    int            retval  = -1;
    //+IPD,0,16:12312312312313
    tmp_p = (char *)pbuf;
    while (1)
    {
        p = strstr((const char *)tmp_p, "+IPD,");
        if (p != NULL)
        {
            retval = sscanf(p, "+IPD,%d,%d:%*s", &conn_id, &rlen);
            if (retval == 2 && rlen > 0)
            {
                if (conn_id >= 0 && conn_id < ML307R_CONN_NUM_MAX)
                {
                    p = strstr(p, ":");
                    p = p + 1;
                    b_log("read:%p, %d\r\n", p, rlen);

#if 1
                    bTcpUdpData_t new_data;
                    new_data.pbuf    = (uint8_t *)p;
                    new_data.len     = rlen;
                    new_data.release = NULL;
                    memcpy(&new_data.conn, &_priv->conn[conn_id].info, sizeof(bTcpUdpInfo_t));
                    _priv->cb.cb(B_EVT_CONN_NEW_DATA, &new_data, NULL, _priv->cb.user_data);
#else
                    pdat = (bTcpUdpData_t *)_bAtNewDataMalloc((uint8_t *)p, rlen);
                    if (pdat != NULL)
                    {
                        memcpy(&pdat->conn, &_priv->conn[conn_id].info, sizeof(bTcpUdpInfo_t));
                        _priv->cb.cb(B_EVT_CONN_NEW_DATA, pdat, _bAtNewDataFree,
                                     _priv->cb.user_data);
                    }
#endif
                }
                tmp_p = p + rlen;
                if (((uint32_t)tmp_p) >= ((uint32_t)(pbuf + len)))
                {
                    break;
                }
            }
            else
            {
                b_log_e("sscanf error %d %d \r\n", retval, rlen);
                break;
            }
        }
        else
        {
            break;
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
    bDRIVER_GET_HALIF(phal, bML307R_HalIf_t, (bDriverInterface_t *)user_data);
    bHalUartSend(*phal, pbuf, len);
    b_log(">>%s\r\n", pbuf);
}

static int _bHalUartIdleCb(uint8_t *pbuf, uint16_t len, void *user_data)
{
    bDRIVER_GET_PRIVATE(_priv, bMl307rPrivate_t, (bDriverInterface_t *)user_data);
    bAtFeedData(&_priv->at, pbuf, len);
    return 0;
}

//--------------------------------------------------------------------------------------------------
// ---------------------通过函数拼装AT指令-----------------------------------------------------------
//--------------------------------------------------------------------------------------------------
static char *_bSetApInfo(void *arg)
{
    bMl307rPrivate_t *prv = (bMl307rPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ML307R_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ML307R_CMD_BUF_LEN, "AT+CWSAP=\"%s\",\"%s\",5,%d\r\n",
             prv->param.ap.ssid, prv->param.ap.passwd, prv->param.ap.encryption);
    return &prv->cmd_tmp[0];
}

static char *_bJoinAp(void *arg)
{
    bMl307rPrivate_t *prv = (bMl307rPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ML307R_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ML307R_CMD_BUF_LEN, "AT+CWJAP=\"%s\",\"%s\"\r\n", prv->param.ap.ssid,
             prv->param.ap.passwd);
    return &prv->cmd_tmp[0];
}

static char *_bPing(void *arg)
{
    bMl307rPrivate_t *prv = (bMl307rPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ML307R_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ML307R_CMD_BUF_LEN, "AT+PING=\"%s\"\r\n", prv->param.remote);
    return &prv->cmd_tmp[0];
}

static char *_bLocalTcpServer(void *arg)
{
    bMl307rPrivate_t *prv = (bMl307rPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ML307R_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ML307R_CMD_BUF_LEN, "AT+CIPSERVER=1,%d\r\n", prv->param.tcpudp.port);
    return &prv->cmd_tmp[0];
}

static char *_bRemoteTcpServer(void *arg)
{
    bMl307rPrivate_t *prv = (bMl307rPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ML307R_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ML307R_CMD_BUF_LEN, "AT+CIPSTART=%d,\"TCP\",\"%s\",%d\r\n", prv->conn_id,
             prv->param.tcpudp.ip, prv->param.tcpudp.port);
    return &prv->cmd_tmp[0];
}

static char *_bRemoteUdpServer(void *arg)
{
    bMl307rPrivate_t *prv = (bMl307rPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ML307R_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ML307R_CMD_BUF_LEN, "AT+CIPSTART=%d,\"UDP\",\"%s\",%d\r\n", prv->conn_id,
             prv->param.tcpudp.ip, prv->param.tcpudp.port);
    return &prv->cmd_tmp[0];
}

static char *_bCloseTcpUdp(void *arg)
{
    bMl307rPrivate_t *prv = (bMl307rPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ML307R_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ML307R_CMD_BUF_LEN, "AT+CIPCLOSE=%d\r\n", prv->conn_id);
    return &prv->cmd_tmp[0];
}

static char *_bTcpUdpSend(void *arg)
{
    bMl307rPrivate_t *prv = (bMl307rPrivate_t *)arg;
    memset(prv->cmd_tmp, 0, ML307R_CMD_BUF_LEN);
    snprintf(prv->cmd_tmp, ML307R_CMD_BUF_LEN, "AT+CIPSEND=%d,%d\r\n", prv->conn_id,
             prv->param.dat.len);
    return &prv->cmd_tmp[0];
}

//--------------------------------------------------------------------------------------------------

static void _bMl307rCtlResult(uint8_t cmd, uint8_t isok, bDriverInterface_t *pdrv)
{
    bDRIVER_GET_PRIVATE(_priv, bMl307rPrivate_t, pdrv);
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
    _priv->cb.cb(isok ? bMl307rCtlEvent[cmd].ok_event : bMl307rCtlEvent[cmd].fail_event, NULL, NULL,
                 _priv->cb.user_data);
}

PT_THREAD(bMl307rTask)(struct pt *pt, void *arg)
{
    bDriverInterface_t *pdrv = (bDriverInterface_t *)arg;
    bDRIVER_GET_PRIVATE(_priv, bMl307rPrivate_t, pdrv);
    if (_priv->busy == 0)
    {
        return 0;
    }
    PT_BEGIN(pt);
    while (1)
    {
        if (_priv->step & ML307R_CMD_RESET_BIT)
        {
            _priv->result = 0;
            bAtSendCmd(&_priv->at, bMl307rCmdReset.pcmd, bMl307rCmdReset.resp, bMl307rCmdReset.timeout);
            PT_WAIT_UNTIL(pt, _priv->result != 0, ML307R_CMD_TIMEOUT);
            if (_priv->result == ML307R_CMD_RESULT_OK)
            {
                bTaskDelayMs(pt, 4000);
                _priv->step &= ~(ML307R_CMD_RESET_BIT);
            }
            else
            {
                _bMl307rCtlResult(_priv->cmd, 0, pdrv);
            }
        }
        else if (_priv->step & ML307R_CMD_INIT_BIT)
        {
            _priv->result = 0;
            if (bMl307rCmdInit[_priv->cmd_index].pcmd)
            {
                bAtSendCmd(&_priv->at, bMl307rCmdInit[_priv->cmd_index].pcmd,
                           bMl307rCmdInit[_priv->cmd_index].resp,
                           bMl307rCmdInit[_priv->cmd_index].timeout);
            }
            else
            {
                bAtSendCmd(&_priv->at, bMl307rCmdInit[_priv->cmd_index].cmd_f(_priv),
                           bMl307rCmdInit[_priv->cmd_index].resp,
                           bMl307rCmdInit[_priv->cmd_index].timeout);
            }
            PT_WAIT_UNTIL(pt, _priv->result != 0, ML307R_CMD_TIMEOUT);
            if (_priv->result == ML307R_CMD_RESULT_OK)
            {
                _priv->cmd_index += 1;
                if (_priv->cmd_index >= sizeof(bMl307rCmdInit) / sizeof(bMl307rCmd_t))
                {
                    _priv->cmd_index = 0;
                    _priv->step &= ~(ML307R_CMD_INIT_BIT);
                }
            }
            else
            {
                _bMl307rCtlResult(_priv->cmd, 0, pdrv);
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
            PT_WAIT_UNTIL(pt, _priv->result != 0, ML307R_CMD_TIMEOUT);
            if (_priv->result == ML307R_CMD_RESULT_OK)
            {
                _priv->cmd_index += 1;
                if (_priv->cmd_index >= _priv->cmd_num)
                {
                    _bMl307rCtlResult(_priv->cmd, 1, pdrv);
                }
            }
            else
            {
                _bMl307rCtlResult(_priv->cmd, 0, pdrv);
            }
        }
        bTaskYield(pt);
    }
    PT_END(pt);
}
//--------------------------------------------------------------------------------------------------
//  私有功能函数
//--------------------------------------------------------------------------------------------------
static void _bMl307rProcessCmd(bMl307rPrivate_t *_priv, uint8_t cmd, const bMl307rCmd_t *pcmdtable,
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
        _priv->step |= ML307R_CMD_RESET_BIT;
    }
    if (init)
    {
        _priv->step |= ML307R_CMD_INIT_BIT;
    }
}

static int _bMl307rGetConn(bMl307rPrivate_t *_priv, bTcpUdpInfo_t *pinfo, uint8_t *unused_connid)
{
    int i      = 0;
    int retval = -1;
    for (i = 0; i < ML307R_CONN_NUM_MAX; i++)
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
static int _bML307RCtl(bDriverInterface_t *pdrv, uint8_t cmd, void *param)
{
    int     retval  = -1;
    uint8_t conn_id = 0;
    bDRIVER_GET_PRIVATE(_priv, bMl307rPrivate_t, pdrv);
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
            _bMl307rProcessCmd(_priv, cmd, &bMl307rCmdStaMode[0],
                               sizeof(bMl307rCmdStaMode) / sizeof(bMl307rCmd_t), 1, 1);
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
                _bMl307rProcessCmd(_priv, cmd, &bMl307rCmdApMode[0],
                                   sizeof(bMl307rCmdApMode) / sizeof(bMl307rCmd_t), 1, 1);
            }
            else if (cmd == bCMD_WIFI_MODE_STA_AP)
            {
                _bMl307rProcessCmd(_priv, cmd, &bMl307rCmdApStaMode[0],
                                   sizeof(bMl307rCmdApStaMode) / sizeof(bMl307rCmd_t), 1, 1);
            }
            else if (cmd == bCMD_WIFI_JOIN_AP)
            {
                _bMl307rProcessCmd(_priv, cmd, &bMl307rCmdJoinAp[0],
                                   sizeof(bMl307rCmdJoinAp) / sizeof(bMl307rCmd_t), 0, 0);
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
            _bMl307rProcessCmd(_priv, cmd, &bMl307rCmdPing[0],
                               sizeof(bMl307rCmdPing) / sizeof(bMl307rCmd_t), 0, 0);
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
            retval  = _bMl307rGetConn(_priv, &_priv->param.tcpudp, &conn_id);
            b_log_w("retval %d cnn_id %02x \r\n", retval, conn_id);
            if (cmd == bCMD_WIFI_LOCAL_TCP_SERVER)
            {
                _bMl307rProcessCmd(_priv, cmd, &bMl307rCmdLocalTcpServer[0],
                                   sizeof(bMl307rCmdLocalTcpServer) / sizeof(bMl307rCmd_t), 0, 0);
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
                _bMl307rProcessCmd(_priv, cmd, &bMl307rCmdRemotTcpServer[0],
                                   sizeof(bMl307rCmdRemotTcpServer) / sizeof(bMl307rCmd_t), 0, 0);
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
                _bMl307rProcessCmd(_priv, cmd, &bMl307rCmdRemotUdpServer[0],
                                   sizeof(bMl307rCmdRemotUdpServer) / sizeof(bMl307rCmd_t), 0, 0);
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
                _bMl307rProcessCmd(_priv, cmd, &bMl307rCmdCloseTcpUdp[0],
                                   sizeof(bMl307rCmdCloseTcpUdp) / sizeof(bMl307rCmd_t), 0, 0);
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
            retval = _bMl307rGetConn(_priv, &_priv->param.tcpudp, NULL);
            if (retval < 0)
            {
                b_log_e("cannt find..\r\n");
                return -2;
            }
            memcpy(&_priv->param.dat, param, sizeof(bTcpUdpData_t));
            _priv->conn_id = retval;
            _bMl307rProcessCmd(_priv, cmd, &bMl307rCmdSendData[0],
                               sizeof(bMl307rCmdSendData) / sizeof(bMl307rCmd_t), 0, 0);
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
int bML307R_Init(bDriverInterface_t *pdrv)
{
    bDRIVER_STRUCT_INIT(pdrv, DRIVER_NAME, bML307R_Init);
    pdrv->ctl         = _bML307RCtl;
    pdrv->_private._p = &bMl307rRunInfo[pdrv->drv_no];
    memset(pdrv->_private._p, 0, sizeof(bMl307rPrivate_t));
    // 创建异步消息处理任务
    bTaskCreate("ml307r", bMl307rTask, pdrv, &bMl307rRunInfo[pdrv->drv_no].task_attr);
    // AT初始化，注册回调和发送函数
    bAtInit(&bMl307rRunInfo[pdrv->drv_no].at, _bAtCmdCb, _bAtNewDataCb, _bAtSendData, pdrv);
    // 串口接收初始化，注册接收空闲回调
    bHAL_UART_INIT_ATTR(&bMl307rRunInfo[pdrv->drv_no].uart_attr,
                        &(bMl307rRunInfo[pdrv->drv_no].uart_buf[0]), CONNECT_RECVBUF_MAX, 50,
                        _bHalUartIdleCb, pdrv);
    bHalUartReceiveIdle(*((bML307R_HalIf_t *)pdrv->hal_if), &bMl307rRunInfo[pdrv->drv_no].uart_attr);
    return 0;
}

#ifdef BSECTION_NEED_PRAGMA
#pragma section driver_init
#endif
bDRIVER_REG_INIT(B_DRIVER_ML307R, bML307R_Init);
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
