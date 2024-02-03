/**
 *!
 * \file        b_srv_tcpip.c
 * \version     v0.0.1
 * \date        2023/08/27
 * \author      Bean(notrynohigh@outlook.com)
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 Bean
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
#include "services/inc/b_srv_tcpip.h"

#if (defined(_TCPIP_SERVICE_ENABLE) && (_TCPIP_SERVICE_ENABLE == 1))

#include <string.h>

#include "core/inc/b_sem.h"
#include "core/inc/b_task.h"
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_utc.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup SERVICES
 * \{
 */

/**
 * \addtogroup TCPIP
 * \{
 */

/**
 * \defgroup TCPIP_Private_TypesDefinitions
 * \{
 */

typedef struct
{
    uint32_t seconds;
    uint32_t fraction;
} bNtpTimestamp_t;

typedef struct
{
    uint8_t         li_vn_mode;       // Leap indicator, version number, and mode
    uint8_t         stratum;          // Stratum level of the local clock
    uint8_t         poll;             // Maximum interval between successive messages
    uint8_t         precision;        // Precision of the local clock
    uint32_t        root_delay;       // Total round trip delay time
    uint32_t        root_dispersion;  // Max error allowed from primary clock source
    uint32_t        ref_id;           // Reference clock identifier
    bNtpTimestamp_t ref_time;         // Reference timestamp
    bNtpTimestamp_t orig_time;        // Originate timestamp
    bNtpTimestamp_t recv_time;        // Receive timestamp
    bNtpTimestamp_t trans_time;       // Transmit timestamp
} bNtpPacket_t;

typedef struct
{
    bNetifClient_t ntp_client;
    bNetifConn_t  *pconn;
    uint32_t       interval_s;
} bNtpPcb_t;

/**
 * \}
 */

/**
 * \defgroup TCPIP_Private_Defines
 * \{
 */
// NTP时间的起始时间
#define B_NTP_TIMESTAMP_DELTA 2208988800ull
#define B_NTP_SERVER_NUM (3)
#define B_NTP_TIMEOUT_S (20)
/**
 * \}
 */

/**
 * \defgroup TCPIP_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TCPIP_Private_Variables
 * \{
 */
static bNtpPcb_t bNtpPcb;
B_TASK_CREATE_ATTR(bNtpTask);
static const char *bNtpServer[B_NTP_SERVER_NUM] = {_NTP_SERVER_1, _NTP_SERVER_2, _NTP_SERVER_3};

/**
 * \}
 */

/**
 * \defgroup TCPIP_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup TCPIP_Private_Functions
 * \{
 */

static void _bNtpConnCallback(bNetifConnEvent_t event, void *param, void *arg)
{
    bNetifConn_t *pconn = (bNetifConn_t *)param;
    if (event == B_EVENT_DNS_OK)
    {
        b_log("conn ip: %d.%d.%d.%d port:%d\r\n", ((pconn->remote_ip >> 0) & 0xff),
              ((pconn->remote_ip >> 8) & 0xff), ((pconn->remote_ip >> 16) & 0xff),
              ((pconn->remote_ip >> 24) & 0xff), pconn->remote_port);
    }
}

PT_THREAD(_bNtpTaskFunc)(struct pt *pt, void *arg)
{
    static uint8_t ntp_server_index = 0;
    bNtpPacket_t   packet;
    uint16_t       rlen     = 0;
    uint64_t       ntp_time = 0;
    PT_BEGIN(pt);
    while (1)
    {
        bNtpPcb.pconn =
            bNetifConnect(&bNtpPcb.ntp_client, (char *)bNtpServer[ntp_server_index], 123);
        if (bNtpPcb.pconn == NULL)
        {
            bTaskDelayMs(pt, 10000);
            break;
        }
        PT_WAIT_UNTIL(pt, bNetifConnIsWriteable(bNtpPcb.pconn) == 1, B_NTP_TIMEOUT_S * 1000);
        if (pt->retval == PT_RETVAL_TIMEOUT)
        {
            b_log_e("ntp send fail...\r\n");
            bNetifConnDeinit(bNtpPcb.pconn);
            bNtpPcb.pconn    = NULL;
            ntp_server_index = (ntp_server_index + 1) % B_NTP_SERVER_NUM;
            bTaskDelayMs(pt, 10000);
            break;
        }
        memset(&packet, 0, sizeof(bNtpPacket_t));
        packet.li_vn_mode = 0x1b;
        bNetifConnWriteData(bNtpPcb.pconn, (uint8_t *)&packet, sizeof(bNtpPacket_t), NULL);
        PT_WAIT_UNTIL(pt, (bNetifConnIsReadable(bNtpPcb.pconn) == 1), B_NTP_TIMEOUT_S * 1000);
        if (pt->retval == PT_RETVAL_TIMEOUT)
        {
            b_log_e("ntp recv timeout.. \r\n");
            bNetifConnDeinit(bNtpPcb.pconn);
            bNtpPcb.pconn    = NULL;
            ntp_server_index = (ntp_server_index + 1) % B_NTP_SERVER_NUM;
            bTaskDelayMs(pt, 10000);
            break;
        }
        bNetifConnReadData(bNtpPcb.pconn, (uint8_t *)&packet, sizeof(bNtpPacket_t), &rlen);
        if (rlen == sizeof(bNtpPacket_t))
        {
            if (packet.recv_time.seconds <= packet.trans_time.seconds &&
                (packet.trans_time.seconds - packet.recv_time.seconds) <= 1)
            {
                ntp_time = ntohl(packet.trans_time.seconds) - B_NTP_TIMESTAMP_DELTA;
                bUTC_SetTime(ntp_time);
            }
        }
        bNetifConnDeinit(bNtpPcb.pconn);
        bNtpPcb.pconn = NULL;
        bTaskDelayMs(pt, bNtpPcb.interval_s * 1000);
    }
    PT_END(pt);
}

//--------------------------------------------------------http--

static void _bHttpTrigEvent(bHttpStruct_t *http, bHttpEvent_t event, void *param,
                            uint32_t param_len)
{
}

static void _bHttpClientCallback(bNetifConnEvent_t event, void *param, void *arg)
{
    if (event == B_EVENT_CONNECTED)
    {
        _bHttpTrigEvent(arg, B_HTTP_EVENT_CONNECTED, NULL, 0);
    }
    else if (event < 0)
    {
        _bHttpTrigEvent(arg, B_HTTP_EVENT_DISCONNECT, NULL, 0);
    }
}

static int _bHttpParseUrl(const char *url, char *host, char *path, uint16_t *port, uint8_t *ishttps)
{
    // 检查是否是HTTPS
    if (strncmp(url, "https://", 8) == 0)
    {
        *ishttps = 1;
    }
    else if (strncmp(url, "http://", 7) == 0)
    {
        *ishttps = 0;
    }
    else
    {
        return -1;
    }

    // 跳过"http://"或"https://"
    const char *start = url + (*ishttps ? 8 : 7);

    // 查找host的结束位置
    const char *end = strchr(start, '/');
    if (end == NULL)
    {
        end = url + strlen(url);
    }
    if ((end - start) > _HTTP_HOST_LEN_MAX)
    {
        return -1;
    }
    // 复制host到目标字符串
    strncpy(host, start, end - start);
    host[end - start] = '\0';

    // 解析port
    const char *portStart = strchr(host, ':');
    if (portStart != NULL)
    {
        *port = atoi(portStart + 1);
    }
    else
    {
        *port = (*ishttps ? 443 : 80);
    }

    // 解析path
    if (*end != '\0')
    {
        if (strlen(end) > _HTTP_PATH_LEN_MAX)
        {
            return -1;
        }
        strcpy(path, end);
    }
    else
    {
        strcpy(path, "/");
    }
    return 0;
}

static int _bHttpGetRequest(bHttpStruct_t *http, char *head, char *body)
{
    int      index = 0, ret = 0;
    uint32_t len = strlen("GET \r\nHost: \r\n\r\n\r\n\r\n\r\n");
    len += strlen(http->path);
    len += strlen(http->host);
    if (head)
    {
        len += strlen(head);
    }
    if (body)
    {
        len += strlen(body);
    }
    len += 4;
    http->buf = bNetifMalloc(len);
    if (http->buf == NULL)
    {
        return -1;
    }
    http->buf_len = len;
    memset(http->buf, 0, len);
    index = sprintf("GET %s\r\nHost: %s\r\n", http->path, http->host);
    if (head)
    {
        ret = sprintf(http->buf + index, "%s\r\n\r\n", head);
        index += ret;
    }
    if (body)
    {
        ret = sprintf(http->buf + index, "%s\r\n\r\n", body);
        index += ret;
    }
    return 0;
}

/**
 * \}
 */

/**
 * \addtogroup TCPIP_Exported_Functions
 * \{
 */

int bSntpStart(uint32_t interval_s)
{
    static uint8_t b_ntp_init_f = 0;
    if (b_ntp_init_f == 0)
    {
        b_ntp_init_f = 1;
        memset(&bNtpPcb, 0, sizeof(bNtpPcb));
        B_NETIF_CLIENT_STRUCT_INIT(&bNtpPcb.ntp_client, B_TRANS_UDP, _bNtpConnCallback, 0, 128);
        bNtpPcb.pconn      = NULL;
        bNtpPcb.interval_s = 60 * 60;
        bTaskCreate("ntp", _bNtpTaskFunc, NULL, &bNtpTask);
    }
    if (interval_s == 0)
    {
        return -1;
    }
    bNtpPcb.interval_s = interval_s;
    return 0;
}

int bHttpRequest(bHttpStruct_t *http, bHttpReqType_t type, char *url, char *head, char *body)
{
    int ret = -1;
    if (http == NULL || !HTTPREQ_TYPE_IS_VALID(type) || url == NULL)
    {
        return -1;
    }

    if (http->state != B_HTTP_STA_DEINIT)
    {
        return -2;
    }

    memset(http->host, 0, sizeof(http->host));
    memset(http->path, 0, sizeof(http->path));
    ret = _bHttpParseUrl(url, http->host, http->path, &http->port, &http->is_https);
    if (ret < 0)
    {
        return -3;
    }
    if (type == B_HTTP_GET)
    {
        ret = _bHttpGetRequest(http, head, body);
    }
    if (ret < 0)
    {
        return -4;
    }
    B_NETIF_CLIENT_STRUCT_INIT(&http->client, B_TRANS_TCP, _bHttpClientCallback, http, 0);
    http->pconn = bNetifConnect(&http->client, http->host, http->port);
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

/************************ Copyright (c) 2023 Bean *****END OF FILE****/
