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

#include <stdio.h>
#include <string.h>

#include "core/inc/b_sem.h"
#include "core/inc/b_task.h"
#include "thirdparty/http-parser/http_parser.h"
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"
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
    bTaskId_t task_id;
    int       sockfd;
    uint32_t  interval_s;
} bNtpPcb_t;

//----------------------------------------------------------------------------
typedef enum
{
    B_HTTP_STA_INIT,
    B_HTTP_STA_CONNECTING,
    B_HTTP_STA_CONNECTED,
    B_HTTP_STA_RECV_DATA,
    B_HTTP_STA_DISCONNECT,
    B_HTTP_STA_DEINIT,
    B_HTTP_STA_DESTROY,
} bHttpState_t;
typedef struct
{
    uint8_t              is_https;
    bHttpState_t         state;
    char                 host[_HTTP_HOST_LEN_MAX + 1];
    char                 path[_HTTP_PATH_LEN_MAX + 1];
    uint16_t             port;
    pHttpCb_t            callback;
    void                *user_data;
    char                *request;
    int                  sockfd;
    bTaskAttr_t          attr;
    bTaskId_t            task_id;
    http_parser          parse;
    http_parser_settings parse_cb;
    uint8_t             *precv;
    uint16_t             recvbuf_len;
    uint16_t             recvbuf_index;
} bHttpStruct_t;

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
static bNtpPcb_t bNtpPcb = {
    .task_id = 0,
};
B_TASK_CREATE_ATTR(bNtpTask);
static const char *bNtpServer[B_NTP_SERVER_NUM] = {_NTP_SERVER_1, _NTP_SERVER_2, _NTP_SERVER_3};

//------------------------------------------------------------------------------------------------

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

static void _bTcpipSrvFree(void *addr)
{
    bFree(addr);
}

static void _bNtpConnCallback(bTransEvent_t event, void *param, void *arg)
{
    ;
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
        bNtpPcb.sockfd = bSocket(B_TRANS_CONN_UDP, _bNtpConnCallback, NULL);
        if (bNtpPcb.sockfd < 0)
        {
            b_log_e("socket fail...\r\n");
            break;
        }
        b_log("sockfd: %x %d\r\n", bNtpPcb.sockfd, ntp_server_index);
        bConnect(bNtpPcb.sockfd, (char *)bNtpServer[ntp_server_index], 123);
        PT_WAIT_UNTIL(pt, bSockIsWriteable(bNtpPcb.sockfd) == 1, B_NTP_TIMEOUT_S * 1000);
        if (pt->retval == PT_RETVAL_TIMEOUT)
        {
            b_log_e("ntp send fail...\r\n");
            PT_WAIT_UNTIL_FOREVER(pt, bShutdown(bNtpPcb.sockfd) >= 0);
            b_log_e("shutdown..\r\n");
            ntp_server_index = (ntp_server_index + 1) % B_NTP_SERVER_NUM;
            break;
        }
        memset(&packet, 0, sizeof(bNtpPacket_t));
        packet.li_vn_mode = 0x1b;
        bSend(bNtpPcb.sockfd, (uint8_t *)&packet, sizeof(bNtpPacket_t), NULL);
        PT_WAIT_UNTIL(pt, bSockIsReadable(bNtpPcb.sockfd) == 1, B_NTP_TIMEOUT_S * 1000);
        if (pt->retval == PT_RETVAL_TIMEOUT)
        {
            b_log_e("ntp recv timeout.. \r\n");
            PT_WAIT_UNTIL_FOREVER(pt, bShutdown(bNtpPcb.sockfd) >= 0);
            b_log_e("shutdown..\r\n");
            ntp_server_index = (ntp_server_index + 1) % B_NTP_SERVER_NUM;
            bTaskDelayMs(pt, 10000);
            break;
        }
        bRecv(bNtpPcb.sockfd, (uint8_t *)&packet, sizeof(bNtpPacket_t), &rlen);
        if (rlen == sizeof(bNtpPacket_t))
        {
            if (packet.recv_time.seconds <= packet.trans_time.seconds &&
                (packet.trans_time.seconds - packet.recv_time.seconds) <= 1)
            {
                ntp_time = B_SWAP_32(packet.trans_time.seconds) - B_NTP_TIMESTAMP_DELTA;
                bUTC_SetTime(ntp_time);
            }
        }
        PT_WAIT_UNTIL_FOREVER(pt, bShutdown(bNtpPcb.sockfd) >= 0);
        b_log_e("shutdown..\r\n");
        bTaskDelayMs(pt, bNtpPcb.interval_s * 1000);
    }
    PT_END(pt);
}

//--------------------------------------------------------http--
//--------------------------------------------------------http--
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

static char *_bHttpGetRequest(bHttpStruct_t *http)
{
    char *request      = NULL;
    int   request_size = 0;
    request_size       = strlen("GET /") + strlen(http->path) + strlen(" HTTP/1.1\r\n") +
                   strlen("Host: ") + strlen(http->host) + strlen("\r\n") +
                   strlen("Connection: close\r\n") + strlen("\r\n") + 1;

    request = (char *)bMalloc(request_size);
    if (request != NULL)
    {
        memset(request, 0, request_size);
        snprintf(request, request_size, "GET /%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                 http->path, http->host);
    }
    return request;
}

static char *_bHttpPostRequest(bHttpStruct_t *http, const char *head, const char *body)
{
    char *request      = NULL;
    int   request_size = 0;

    request_size = strlen("POST /") + strlen(http->path) + strlen(" HTTP/1.1\r\n") +
                   strlen("Host: ") + strlen(http->host) + strlen("\r\n") +
                   strlen("Content-Length: xxxxxx\r\n") + strlen("\r\n") + strlen(body) +
                   strlen("\r\n") + 1;
    if (head != NULL)
    {
        request_size += strlen(head);
    }
    request = (char *)bMalloc(request_size);
    if (request == NULL)
    {
        return NULL;
    }
    memset(request, 0, request_size);
    // 组装请求字符串
    if (head != NULL)
    {
        snprintf(request, request_size,
                 "POST /%s HTTP/1.1\r\nHost: %s\r\n%sContent-Length: %d\r\n\r\n"
                 "%s\r\n",
                 http->path, http->host, head, strlen(body), body);
    }
    else
    {
        snprintf(request, request_size,
                 "POST /%s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n"
                 "%s\r\n",
                 http->path, http->host, strlen(body), body);
    }
    return request;
}

static void _bHttpResult(bHttpStruct_t *http, bHttpEvent_t evt, void *param)
{
    if (evt < 0 || evt == B_HTTP_EVENT_RECV_DATA || evt == B_HTTP_STA_DESTROY)
    {
        if (http->request)
        {
            bFree(http->request);
            http->request = NULL;
        }
        http->sockfd = -1;
        http->state  = B_HTTP_STA_DEINIT;
    }
    http->callback(evt, param, http->user_data);
    if (http->precv)
    {
        bFree(http->precv);
        http->precv = NULL;
    }
    http->recvbuf_index = 0;
    http->recvbuf_len   = 0;
}

static void _bHttpTransCb(bTransEvent_t event, void *param, void *arg)
{
}

int on_message_complete(http_parser *parser)
{
    bHttpStruct_t *http = (bHttpStruct_t *)parser->data;
    http->state         = B_HTTP_STA_RECV_DATA;
    return 0;
}

PT_THREAD(_bHttpTaskFunc)(struct pt *pt, void *arg)
{
    bHttpStruct_t *http = (bHttpStruct_t *)arg;
    PT_BEGIN(pt);
    while (1)
    {
        if (http->state == B_HTTP_STA_DEINIT)
        {
            break;
        }
        if (http->state == B_HTTP_STA_DESTROY)
        {
            if (http->sockfd > 0)
            {
                b_log_e("http destroy...\r\n");
                PT_WAIT_UNTIL_FOREVER(pt, bShutdown(http->sockfd) >= 0);
                b_log_e("shutdown..\r\n");
                _bHttpResult(http, B_HTTP_EVENT_DESTROY, NULL);
            }
            bTaskRemove(http->task_id);
            bFree(http);
            return 0;
        }
        http->sockfd = bSocket(B_TRANS_CONN_TCP, _bHttpTransCb, http);
        if (http->sockfd < 0)
        {
            b_log_e("socket fail...\r\n");
            _bHttpResult(http, B_HTTP_EVENT_ERROR, NULL);
            break;
        }
        b_log("sockfd: %x %s %d %s\r\n", http->sockfd, http->host, http->port, http->path);
        bConnect(http->sockfd, http->host, http->port);
        PT_WAIT_UNTIL(pt, bSockIsWriteable(http->sockfd) == 1, 5000);
        if (pt->retval == PT_RETVAL_TIMEOUT)
        {
            b_log_e("http conn fail...\r\n");
            PT_WAIT_UNTIL_FOREVER(pt, bShutdown(http->sockfd) >= 0);
            b_log_e("shutdown..\r\n");
            _bHttpResult(http, B_HTTP_EVENT_ERROR, NULL);
            break;
        }
        _bHttpResult(http, B_HTTP_EVENT_CONNECTED, NULL);
        b_log("send:%s\r\n", http->request);
        bSend(http->sockfd, (uint8_t *)http->request, strlen(http->request), NULL);
        http->parse.data = http;
        http_parser_init(&http->parse, HTTP_RESPONSE);
        http_parser_settings_init(&http->parse_cb);
        http->parse_cb.on_message_complete = on_message_complete;
        int      parse_len                 = 0;
        uint16_t readlen                   = 0;
        for (;;)
        {
            PT_WAIT_UNTIL(pt, bSockIsReadable(http->sockfd) == 1, 5000);
            if (pt->retval == PT_RETVAL_TIMEOUT)
            {
                b_log_e("http recv timeout.. \r\n");
                PT_WAIT_UNTIL_FOREVER(pt, bShutdown(http->sockfd) >= 0);
                b_log_e("shutdown..\r\n");
                _bHttpResult(http, B_HTTP_EVENT_ERROR, NULL);
                break;
            }
            if ((http->recvbuf_len - http->recvbuf_index) <= 128)
            {
                b_log("realloc-----\r\n");
                http->recvbuf_len += 1024;
                http->precv = bRealloc(http->precv, http->recvbuf_len);
                if (http->precv == NULL)
                {
                    b_log_e("malloc error.. \r\n");
                    PT_WAIT_UNTIL_FOREVER(pt, bShutdown(http->sockfd) >= 0);
                    b_log_e("shutdown..\r\n");
                    _bHttpResult(http, B_HTTP_EVENT_ERROR, NULL);
                    break;
                }
            }
            bRecv(http->sockfd, http->precv + http->recvbuf_index,
                  http->recvbuf_len - http->recvbuf_index, &readlen);
            if (readlen > 0)
            {
                parse_len =
                    http_parser_execute(&http->parse, &http->parse_cb,
                                        (const char *)(http->precv + http->recvbuf_index), readlen);
                b_log("parse_len %d readlen %d index:%d\r\n", parse_len, readlen,
                      http->recvbuf_index);
                http->recvbuf_index += readlen;
                if (http->state == B_HTTP_STA_RECV_DATA)
                {
                    bHttpRecvData_t dat;
                    dat.pdat    = http->precv;
                    dat.len     = http->recvbuf_index;
                    dat.release = _bTcpipSrvFree;
                    PT_WAIT_UNTIL_FOREVER(pt, bShutdown(http->sockfd) >= 0);
                    _bHttpResult(http, B_HTTP_EVENT_RECV_DATA, &dat);
                    http->precv = NULL;
                    break;
                }
                else if (parse_len < 0)
                {
                    b_log_e("parse error.. \r\n");
                    PT_WAIT_UNTIL_FOREVER(pt, bShutdown(http->sockfd) >= 0);
                    b_log_e("shutdown..\r\n");
                    _bHttpResult(http, B_HTTP_EVENT_ERROR, NULL);
                    break;
                }
            }
        }
    }
    PT_END(pt);
}

/**
 * \}
 */

/**
 * \addtogroup TCPIP_Exported_Functions
 * \{
 */

int bTcpipSrvInit()
{
    return bNetlinkInit();
}

int bSntpStart(uint32_t interval_s)
{
    if (bNtpPcb.task_id == NULL)
    {
        memset(&bNtpPcb, 0, sizeof(bNtpPcb));
        bNtpPcb.sockfd     = -1;
        bNtpPcb.interval_s = 60 * 60;
        bNtpPcb.task_id    = bTaskCreate("ntp", _bNtpTaskFunc, NULL, &bNtpTask);
    }
    if (interval_s == 0)
    {
        return -1;
    }
    bNtpPcb.interval_s = interval_s;
    return 0;
}

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------

int bHttpInit(pHttpCb_t cb, void *user_data)
{
    bHttpStruct_t *http = NULL;
    if (cb == NULL)
    {
        return -1;
    }
    http = (bHttpStruct_t *)bMalloc(sizeof(bHttpStruct_t));
    if (http == NULL)
    {
        return -2;
    }
    memset(http, 0, sizeof(bHttpStruct_t));
    http->callback      = cb;
    http->request       = NULL;
    http->sockfd        = -1;
    http->state         = B_HTTP_STA_DEINIT;
    http->user_data     = user_data;
    http->precv         = NULL;
    http->recvbuf_index = 0;
    http->recvbuf_len   = 0;
    if ((http->task_id = bTaskCreate(NULL, _bHttpTaskFunc, http, &http->attr)) == NULL)
    {
        b_log_e("task create fail..\r\n");
        bFree(http);
        return -3;
    }
    return ((int)http);
}

int bHttpRequest(int httpfd, bHttpReqType_t type, const char *url, const char *head,
                 const char *body)
{
    int            ret     = -1;
    char          *request = NULL;
    bHttpStruct_t *http    = (bHttpStruct_t *)httpfd;
    if (httpfd <= 0 || !HTTPREQ_TYPE_IS_VALID(type) || url == NULL || http->callback == NULL)
    {
        b_log_e("http param errror..%p %d %p\r\n", http, type, url);
        return -1;
    }

    if (http->state != B_HTTP_STA_DEINIT)
    {
        b_log_e("http busy...\r\n");
        return -2;
    }
    memset(http->host, 0, sizeof(http->host));
    memset(http->path, 0, sizeof(http->path));
    ret = _bHttpParseUrl(url, http->host, http->path, &http->port, &http->is_https);
    if (ret < 0)
    {
        b_log_e("parse url fail..%d \r\n", ret);
        return -3;
    }
    b_log("host:%s port %d\n", http->host, http->port);
    b_log("path:%s \n", http->path);
    if (type == B_HTTP_GET)
    {
        request = _bHttpGetRequest(http);
    }
    else if (type == B_HTTP_POST)
    {
        request = _bHttpPostRequest(http, head, body);
    }
    if (request == NULL)
    {
        b_log_e("http create request fail...\r\n");
        return -4;
    }
    http->request = request;
    http->state   = B_HTTP_STA_INIT;
    return 0;
}

int bHttpDeInit(int httpfd)
{
    if (httpfd <= 0)
    {
        return -1;
    }
    bHttpStruct_t *http = (bHttpStruct_t *)httpfd;
    http->state         = B_HTTP_STA_DESTROY;
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
