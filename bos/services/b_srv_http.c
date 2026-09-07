/**
 *!
 * \file        b_srv_http.c
 * \version     v0.0.1
 * \date        2026/05/31
 * \author      BabyOS Team
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 BabyOS
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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "services/inc/b_srv_http.h"

#if (defined(_HTTP_SERVICE_ENABLE) && (_HTTP_SERVICE_ENABLE == 1))

#include <stdio.h>
#include <string.h>

#include "core/inc/b_task.h"
#include "modules/inc/b_mod_tcpip.h"
#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))
#include "modules/inc/b_mod_ssl.h"
#endif
#include "thirdparty/http-parser/http_parser.h"
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"

// MCU 资源保护 (流式风格, 全程零堆分配, 除了响应头动态拼):
//   - 接收: ctx 内置固定 rbuf[_HTTP_MAX_REQ_SIZE] (BSS, 不堆碎片, 不 OOM)
//   - body: rbuf 内的子区段, 0-copy (handler req.body 指向 rbuf+body_off)
//   - 响应头: handler 返回后 bMalloc 拼, send 完 bFree (一次性, 大小 < 256B)
//   - 超过 _HTTP_MAX_REQ_SIZE 的请求返 413
// 整体模型: sub-task 栈占用 < 200B, ctx 含 _HTTP_MAX_REQ_SIZE 字节内置数组
// 8 并发也只占 ctx BSS _HTTP_MAX_REQ_SIZE * 8 = 1KB
#ifndef _HTTP_RECV_CHUNK_SIZE
#define _HTTP_RECV_CHUNK_SIZE 256  // 单次 bRecv 容量
#endif
// MCU 友好上限: 总请求大小 (header+body) 不能超过这个值, 超过返 413
// 嵌入式 8~16 并发, 512B 即 4~8KB BSS, 完全可以接受
#ifndef _HTTP_MAX_REQ_SIZE
#define _HTTP_MAX_REQ_SIZE 512  // header + body 总和
#endif
#ifndef _HTTP_SEND_CHUNK_TIMEOUT_MS
#define _HTTP_SEND_CHUNK_TIMEOUT_MS 5000
#endif
// 客户端自动重试间隔 (ms): PT task 内置自动循环发起请求的间隔, 避免业务层
// 与 PT task 状态机撞 "client busy" 死锁. 调小可加快重试, 调大可降低负载.
#ifndef _HTTP_AUTOLOOP_INTERVAL_MS
#define _HTTP_AUTOLOOP_INTERVAL_MS 15000
#endif

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup SERVICES
 * \{
 */

/**
 * \addtogroup HTTP
 * \{
 */

/**
 * \defgroup HTTP_Private_TypesDefinitions
 * \{
 */

//=========================================== 客户端（兼容原有接口）
//===========================================
#if (defined(_HTTP_CLIENT_ENABLE) && (_HTTP_CLIENT_ENABLE == 1))
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
    pHttpCb_t            callback;  // 兼容原有接口
    void                *user_data;
    /* 流式动态分配: 请求串按需 bMalloc, 处理完 bFree */
    char                *request;
    int                  request_len;
    bSocketFd_t          sockfd;
    bTaskAttr_t          attr;
    bTaskId_t            task_id;
    http_parser          parser;
    http_parser_settings parse_cb;
    /* 流式接收: rbuf 指针, http_parser 边解析边扩张 (bRealloc) */
    char                *precv;
    int                  recvbuf_len;   // 已收到总字节数
    int                  recvbuf_cap;   // 已分配容量
    /* HTTPS truncated 等待: server 发了 headers 但 body 没继续推时, 允许
       等 server 200ms × N 次. 跨 PT yield 持久 (PT local var yield 后值丢).
       每个新请求从 bHttpRequest 走 _bHttpBuildRequest 时重置. */
    uint8_t              truncated_wait_iters;
#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))
    bSSLHandle_t ssl;
#endif
} bHttpClientCtx_t;
#endif

//=========================================== 服务端 ===========================================
#if (defined(_HTTP_SERVER_ENABLE) && (_HTTP_SERVER_ENABLE == 1))
// client 连接上下文 (每个 client 一个, 跑在独立 sub-task)
struct _bHttpServerConn_s;
typedef struct _bHttpServerConn_s bHttpConnCtx_t;

// 客户端槽: 主 task 把新 accept 进来的 trans 句柄塞进空槽, sub-task 自己取走
typedef struct
{
    bSocketFd_t     client_fd;  // -1/0 = 槽空; 非负 = b_mod_tcpip 分配的 trans 句柄
    bTaskId_t       worker;     // 占这个槽的 sub-task id
    bHttpConnCtx_t *ctx;        // sub-task 关联的 ctx 指针, 停服务时 bFree
} bHttpConnSlot_t;

typedef struct
{
    bHttpServerHandler_t handler;
    void                *user_data;
    bSocketFd_t          listen_fd;
    bTaskAttr_t          attr;
    bTaskId_t            task_id;
    uint16_t             port;
    volatile uint8_t     stop_requested;   // bHttpServerStop 置位, sub-task 自检退出
    volatile uint8_t     startup_error;    // CRITICAL-C2 fix: socket/bind/listen 失败置位
    bHttpConnSlot_t      slots[SERVER_MAX_CONNECTIONS];
} bHttpServer_t;

// 流式接收 + 处理完成后再释放:
//   - url_buf[]  = URL 缓存 (一次性填入, _HTTP_PATH_LEN_MAX 字节, 处理完即释放)
//   - rbuf*      = 收报缓冲 (动态 bRealloc, http_parser 边收边解析)
//   - body*      = body 指针 (POST/PUT, bRealloc 累积)
//   - resp_hdr*  = 响应头拼接 (handler 返回后 bMalloc 拼, send 完 bFree)
// 处理完一帧 (handler 跑完 + response send 完) 释放 url_buf/rbuf/body/resp_hdr
struct _bHttpServerConn_s
{
    bHttpServer_t  *server;
    bHttpConnSlot_t *slot;
    bSocketFd_t      client_fd;     // PT 跨 yield 必须持久, 放 ctx 不放栈
    bTaskAttr_t      attr;
    bTaskId_t        task_id;
    http_parser          parser;
    http_parser_settings parse_cb;
    /* URL 短, 用 ctx 内置 _HTTP_PATH_LEN_MAX+1 字节 */
    char                 url_buf[_HTTP_PATH_LEN_MAX + 1];
    /* rbuf 是流式接收区, MCU 友好: ctx 内置固定 _HTTP_MAX_REQ_SIZE 字节 (BSS, 不堆碎片) */
    char                 rbuf[_HTTP_MAX_REQ_SIZE];
    int                  rbuf_len;
    int                  body_off;   // body 在 rbuf 内的起点 offset
    int                  body_len;   // body 累积长度
    uint8_t              headers_done;  // 0=未完, 1=header 完, 2=message 完
    bHttpReqType_t       method;
    /* 响应头: handler 跑完后动态 bMalloc 拼, send 完 bFree */
    char                *resp_hdr;
    int                  resp_hdr_len;
    /* REVIEW-V3 #1 fix: per-connection PT send state, 替换原 file-static.
       每个 sub-task 各持一份, 跨 yield 持久, 不互相串包. */
    int                  send_off;          // header 发送进度, 跨 PT_WAIT_UNTIL yield 保留
    int                  body_send_off;     // body 发送进度, 跨 PT_WAIT_UNTIL yield 保留
    bHttpSendCtx_t       send_ctx;
};
#endif

/**
 * \}
 */

/**
 * \defgroup HTTP_Private_Variables
 * \{
 */

#if (defined(_HTTP_CLIENT_ENABLE) && (_HTTP_CLIENT_ENABLE == 1))
static bHttpClientCtx_t *s_http_client = NULL;
B_TASK_CREATE_ATTR(bHttpClientTask);
#endif

#if (defined(_HTTP_SERVER_ENABLE) && (_HTTP_SERVER_ENABLE == 1))
static bHttpServer_t *s_http_server = NULL;
B_TASK_CREATE_ATTR(bHttpServerTask);
#endif

/**
 * \}
 */

/**
 * \defgroup HTTP_Private_FunctionPrototypes
 * \{
 */

#if (defined(_HTTP_CLIENT_ENABLE) && (_HTTP_CLIENT_ENABLE == 1))
static int   _bHttpParseUrl(const char *url, char *host, char *path, uint16_t *port,
                            uint8_t *ishttps);
static int   _bHttpBuildRequest(bHttpClientCtx_t *http, bHttpReqType_t type, const char *head,
                                const char *body);
static void  _bHttpResult(bHttpClientCtx_t *http, bHttpEvent_t evt, void *param);
static void  _bHttpTransCb(bTransEvent_t event, void *param, void *arg);
static int   _bHttpParseComplete(http_parser *parser);
#endif

#if (defined(_HTTP_SERVER_ENABLE) && (_HTTP_SERVER_ENABLE == 1))
static int  _bHttpBuildResponse(bHttpResponse_t *resp, char *buf, int buf_size);
static void _bHttpServerAcceptCb(bTransEvent_t event, void *param, void *arg);
static int  _bHttpOnUrl(http_parser *p, const char *at, size_t length);
static int  _bHttpOnHeadersComplete(http_parser *p);
static int  _bHttpOnBody(http_parser *p, const char *at, size_t length);
static int  _bHttpOnMessageComplete(http_parser *p);
PT_THREAD(_bHttpClientConnTask)(struct pt *pt, void *arg);
#endif

/**
 * \}
 */

/**
 * \defgroup HTTP_Private_Functions
 * \{
 */

//=========================================== 客户端实现（兼容原有实现）
//===========================================
#if (defined(_HTTP_CLIENT_ENABLE) && (_HTTP_CLIENT_ENABLE == 1))
static int _bHttpParseUrl(const char *url, char *host, char *path, uint16_t *port, uint8_t *ishttps)
{
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

    const char *start = url + (*ishttps ? 8 : 7);
    const char *end   = strchr(start, '/');
    if (end == NULL)
    {
        end = url + strlen(url);
    }
    if ((end - start) > _HTTP_HOST_LEN_MAX)
    {
        return -1;
    }
    strncpy(host, start, end - start);
    host[end - start] = '\0';

    // 解析端口号，并从host中去除端口部分
    const char *portStart = strchr(host, ':');
    if (portStart != NULL)
    {
        *port = atoi(portStart + 1);
        // 将host截断到':'之前，bConnect只接收纯host
        host[portStart - host] = '\0';
    }
    else
    {
        *port = (*ishttps ? 443 : 80);
    }

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

// 流式动态分配: 把请求串拼到动态 bMalloc 的 ctx->request, 大小按需.
// 返回值: 0=成功, -1=失败
static int _bHttpBuildRequest(bHttpClientCtx_t *http, bHttpReqType_t type, const char *head,
                              const char *body)
{
    const char *method_str = "GET";
    int         has_body   = 0;
    int         body_len   = 0;

    switch (type)
    {
        case B_HTTP_POST:
            method_str = "POST";
            has_body   = 1;
            break;
        case B_HTTP_PUT:
            method_str = "PUT";
            has_body   = 1;
            break;
        case B_HTTP_DELETE:
            method_str = "DELETE";
            break;
        case B_HTTP_GET:
        default:
            method_str = "GET";
            break;
    }

    if (has_body && body != NULL)
    {
        body_len = (int)strlen(body);
    }

    int request_size = (int)strlen(method_str) + 1 + (int)strlen(http->path) +
                       (int)strlen(" HTTP/1.1\r\n") + (int)strlen("Host: ") +
                       (int)strlen(http->host) + (int)strlen("\r\n") +
                       (int)strlen("Connection: close\r\n") + (int)strlen("\r\n") + 1;
    if (head != NULL)
    {
        request_size += (int)strlen(head);
    }
    if (has_body)
    {
        request_size += (int)strlen("Content-Length: xxxxxx\r\n") + body_len;
    }

    // 释放旧的 (理论上请求完成后已 free, 防御性)
    if (http->request != NULL)
    {
        bFree(http->request);
        http->request = NULL;
    }
    http->request = (char *)bMalloc(request_size);
    if (http->request == NULL)
    {
        return -1;
    }
    http->request_len = request_size;
    memset(http->request, 0, request_size);

    if (has_body)
    {
        if (head != NULL)
        {
            snprintf(http->request, request_size,
                     "%s %s HTTP/1.1\r\nHost: %s\r\n%sContent-Length: %d\r\n\r\n", method_str,
                     http->path, http->host, head, body_len);
        }
        else
        {
            snprintf(http->request, request_size,
                     "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n", method_str,
                     http->path, http->host, body_len);
        }
        if (body != NULL && body_len > 0)
        {
            memcpy(http->request + strlen(http->request), body, body_len);
        }
    }
    else
    {
        snprintf(http->request, request_size, "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                 method_str, http->path, http->host);
    }

    return 0;
}

static void _bHttpResult(bHttpClientCtx_t *http, bHttpEvent_t evt, void *param)
{
    if (evt < 0 || evt == B_HTTP_EVENT_RECV_DATA || evt == B_HTTP_EVENT_DESTROY)
    {
        // 流式模型: 一帧请求/响应完成后释放动态 buf
        if (http->request != NULL)
        {
            bFree(http->request);
            http->request = NULL;
        }
        if (http->precv != NULL)
        {
            bFree(http->precv);
            http->precv = NULL;
        }
        http->request_len   = 0;
        http->recvbuf_len   = 0;
        http->recvbuf_cap   = 0;
        http->sockfd        = -1;
#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))
        if (http->ssl != NULL)
        {
            bSSLDeinit(http->ssl);
            http->ssl = NULL;
        }
#endif
        http->state = B_HTTP_STA_DEINIT;
    }
    http->callback(evt, param, http->user_data);
}

static void _bHttpTransCb(bTransEvent_t event, void *param, void *arg)
{
    (void)event;
    (void)param;
    (void)arg;
}

static int _bHttpParseComplete(http_parser *parser)
{
    bHttpClientCtx_t *http = (bHttpClientCtx_t *)parser->data;
    http->state            = B_HTTP_STA_RECV_DATA;
    return 0;
}

PT_THREAD(_bHttpClientTask)(struct pt *pt, void *arg)
{
    int               ret   = 0;
    void             *param = NULL;
    bHttpEvent_t      event = B_HTTP_EVENT_ERROR;
    bHttpClientCtx_t *http  = (bHttpClientCtx_t *)arg;
    bHttpRecvData_t   dat;
    PT_BEGIN(pt);

    while (1)
    {
        if (http->state == B_HTTP_STA_DEINIT)
        {
            // CRIT-HTTP-2 fix: DEINIT 是"空闲"状态，不是任务结束。
            // 之前用 break/remove/free 把 http 直接释放，导致调用方手里的
            // bHttpFd_t 变成悬空指针；后续再 bHttpRequest 时读到脏 state，
            // 就持续报 "client busy"。
            //
            // CRIT-HTTP-AUTOLOOP-2 fix: 不能用 PT_WAIT_UNTIL_FOREVER 永久挂起.
            // 原因是 autoloop 路径 (http_restart 标签下的 if) 是设置 state=INIT
            // 后 bTaskRestart(pt), state 才会变 INIT; 如果 autoloop 内 bSSLInit
            // 失败并保持 state=DEINIT, PT_WAIT_UNTIL_FOREVER 会让 task 永远卡
            // 死在这里, 整个 HTTP client 停摆, 表现为 "i am alive 还在打印但
            // [HTTP] 日志消失".
            //
            // 修复: 用带超时的 PT_WAIT_UNTIL, 超时后让循环自然走到底, 自动
            // 触发 autoloop 重试流程 (bSocket/bConnect/SSL/...), 仍然有 host
            // 就重试 (autoloop 路径), 没 host 就再次挂起等下一次 bHttpRequest.
            // 超时设 16 秒 (略大于 _HTTP_AUTOLOOP_INTERVAL_MS 15s), 保证 autoloop
            // delay 完成后下一轮有足够时间让 bHttpRequest 把 state 推回 INIT.
            PT_WAIT_UNTIL(pt, http->state != B_HTTP_STA_DEINIT, _HTTP_AUTOLOOP_INTERVAL_MS + 1000);
            // 超时但 state 仍 DEINIT: 循环走到底, 自动落进 bSocket/bConnect.
            // 如果 http->host 已清零, bConnect 会失败 -> http_restart -> autoloop
            // 看到 host=='' 不再 retry, 进入下一轮 16s 等待. 这是设计内的 idle.
        }
        if (http->state == B_HTTP_STA_DESTROY)
        {
            if (http->sockfd > 0)
            {
                SOCKET_SHUTDOWN(pt, http->sockfd);
                _bHttpResult(http, B_HTTP_EVENT_DESTROY, NULL);
            }
            bTaskRemove(http->task_id);
            bFree(http);
            break;
        }

        http->sockfd = bSocket(B_TRANS_CONN_TCP, _bHttpTransCb, http);
        if (SOCKFD_IS_INVALID(http->sockfd))
        {
            event = B_HTTP_EVENT_ERROR;
            param = NULL;
            goto http_restart;
        }

        b_log("[HTTP] %s://%s:%d%s\r\n", http->is_https ? "https" : "http", http->host, http->port,
              http->path);
        // HIGH-HTTP-2 fix: 检查 bConnect 返回值. 同步失败 (DNS 错误等) 返回 <0,
        // 之前不检查, 仍 PT_WAIT_UNTIL 等 5s timeout 才跳 CONN_FAIL.
        int conn_ret = bConnect(http->sockfd, http->host, http->port);
        if (conn_ret < 0)
        {
            b_log_w("[HTTP] bConnect fail: %d\r\n", conn_ret);
            event = B_HTTP_EVENT_CONN_FAIL;
            param = NULL;
            goto http_restart;
        }
        PT_WAIT_UNTIL(pt, bSocketIsConnected(http->sockfd) == 1, 5000);

        if (PT_WAIT_IS_TIMEOUT(pt))
        {
            event = B_HTTP_EVENT_CONN_FAIL;
            param = NULL;
            goto http_restart;
        }

        // HTTPS: SSL握手
        // PT 状态机：使用 bTaskDelayMs 让出 CPU，避免死循环
        // 关键：每次 PT 调度进来只调用一次 bSSLHandshake
        //
        // CRIT-HTTPS-HANDSHAKE fix: mbedtls_ssl_handshake() 仅驱动 client 侧的
        // 握手状态机, 但**不能**保证 server 发的 ServerChangeCipherSpec/Finished
        // + 立即跟随的 ApplicationData 全部被消费完. 当 server 在自己的 Finished
        // 后立即发 HTTP 响应 (single TLS record), mbedTLS 在 handshake 返回 0 时
        // 内部缓冲可能还有未消化的握手尾包. 后续 bSSLSend 调 mbedtls_ssl_write()
        // 会先尝试消化剩余握手包再写应用数据, 但 mbedTLS 会返回
        // MBEDTLS_ERR_SSL_WANT_READ (需要再读) — 这被 bSSLSend 内部吞掉返回 0,
        // 表现为 HTTP 请求根本没发出去 (SSL 层阻塞).
        //
        // 修复: 在 handshake 关闭后, **主动**用 bSSLRecv + 临时 1KB buffer 喂
        // mbedTLS 一次, 强迫它处理掉所有缓冲的 server Finished + 紧跟的
        // ApplicationData. 之后 bSSLSend 不会再卡在 WANT_READ, 进入正常
        // PT_WAIT_UNTIL 等响应时, recv_head 里已经有 server 的 HTTP 数据, 能
        // 立刻被 bSockIsReadable 检测到.
#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))
        if (http->is_https && http->ssl != NULL)
        {
            while (1)
            {
                ret = bSSLHandshake(http->ssl, http->sockfd);
                if (ret < 0)
                {
                    b_log_e("[HTTP] ssl handshake failed -0x%x\r\n", -ret);
                    event = B_HTTP_EVENT_ERROR;
                    param = NULL;
                    goto http_restart;
                }
                else if (ret == 0)
                {
                    break;
                }

                // 握手进行中，固定让出 10ms
                // 重要：bTaskDelayMs 内部用 PT_WAIT_UNTIL 让出 CPU
                bTaskDelayMs(pt, 10);
            }
            // 握手收尾: 强迫 mbedTLS 把 server 已发的 post-handshake record
            // (ServerFinished + NewSessionTicket + 部分 ApplicationData) 消化
            // 完. 这步**关键** — 不消化, 后续 bSSLSend 会因 mbedTLS 内部 in_buf
            // 还有未处理 record 而返回 WANT_READ, 导致 server 永远收不到 client
            // GET, 表现为 5s RECV_TIMEOUT.
            //
            // PT 裸机语义: 不能 spin (server 数据可能还没全到 socket). 用
            // bSSLHandshakeFinalize (封装了 mbedTLS in_buf 检查) + 短 PT_DELAY
            // 循环: 每次 finalize 提走当前已就绪 record, 若 mbedTLS 内部还
            // 有更多 record, 让出 CPU, 等下一次 bExec() 让 wifi polling 把
            // 后续 +IPD 数据喂进 socket. 设上限防 server 永久不发剩余 record.
            {
                int finalize_iters = 0;
                const int MAX_FINALIZE_ITER = 50;  // 50 * 10ms = 500ms 上限
                int dr;
                while (finalize_iters < MAX_FINALIZE_ITER
                       && (dr = bSSLHandshakeFinalize(http->ssl)) > 0)
                {
                    finalize_iters++;
                    // 让出 CPU, 等下一次 bExec() 进来时 wifi polling 把更多
                    // +IPD 数据喂进 socket, mbedTLS 自动消化 (in_buf 增加,
                    // 下次 finalize 再提走). 10ms 间隔比 ESP-12F +IPD 推送
                    // 周期略小, 确保 server 每发一帧我们都能进 PT 一次.
                    bTaskDelayMs(pt, 10);
                }
                // dr < 0: 真错误, 走下面的 bSSLSend 时会再检测到, 走
                //        http_restart 路径关闭 socket + 重连.
                // dr == 0: drain 完成, 可以走 bSSLSend.
            }
        }
#endif

        _bHttpResult(http, B_HTTP_EVENT_CONNECTED, NULL);

        // HTTPS: 使用SSL发送
        // CRIT-HTTPS-SEND-DRAIN fix: bSSLSend 可能因 mbedTLS 内部 in_buf 还没
        // 消化完 server post-handshake record 而返回 ret==0 (新语义: wlen=0,
        // mbedTLS 在等 server 数据). 此时需要再走一次 drain, 把 server record
        // 消化完后 mbedTLS 才能 flush out_buf. 用 PT 循环实现:
        //   bSSLSend → 若写完退出 → 若 0 字节走 drain → 下一轮 retry.
        // 设上限避免 server 永远不发剩余 record 时永久 hang.
#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))
        if (http->is_https && http->ssl != NULL)
        {
            uint16_t req_len    = (uint16_t)strlen(http->request);
            uint16_t written    = 0;
            int      send_iters = 0;
            const int MAX_SEND_ITER = 100;  // 100 * 10ms = 1s 上限
            while (written < req_len && send_iters < MAX_SEND_ITER)
            {
                uint16_t wlen = 0;
                int      sret  = bSSLSend(http->ssl, (uint8_t *)http->request + written,
                                          req_len - written, &wlen);
                if (sret < 0)
                {
                    b_log_e("[HTTP] ssl send failed: %d\r\n", sret);
                    event = B_HTTP_EVENT_ERROR;
                    param = NULL;
                    goto http_restart;
                }
                written += wlen;
                if (written >= req_len)
                {
                    break;
                }
                // 还没写完: 可能 WANT_READ (server 还在发 record, mbedTLS 等
                // in_buf 有数据才能 flush) 或 WANT_WRITE (ESP-12F send queue 满).
                // 两者都需要让出 CPU, 等下一次 bExec() 让 wifi/recv 路径推进.
                // 但对 WANT_READ 还应当主动 drain, 避免 bSSLSend 永远返回 0.
                send_iters++;
                bTaskDelayMs(pt, 10);
                // drain: 如果 mbedTLS in_buf 有 server record 但调用方没消
                // 化, bSSLSend 永远卡在 WANT_READ. drain 把已就绪的 record
                // 提走, 让 mbedTLS 知道"server 那边已发完"或"socket 暂时没
                // 数据", 下次 bSSLSend 才能 flush.
                if (bSSLHandshakeFinalize(http->ssl) < 0)
                {
                    b_log_e("[HTTP] ssl drain failed during send\r\n");
                    event = B_HTTP_EVENT_ERROR;
                    param = NULL;
                    goto http_restart;
                }
            }
            if (written < req_len)
            {
                b_log_w("[HTTP] ssl send timeout (%u/%u bytes)\r\n", written, req_len);
                event = B_HTTP_EVENT_ERROR;
                param = NULL;
                goto http_restart;
            }
        }
        else
#endif
        {
            bSend(http->sockfd, (uint8_t *)http->request, strlen(http->request), NULL);
        }

        http->parser.data = http;
        http_parser_init(&http->parser, HTTP_RESPONSE);
        http_parser_settings_init(&http->parse_cb);
        http->parse_cb.on_message_complete = _bHttpParseComplete;
        http->truncated_wait_iters = 0;

        // 流式接收: rbuf 按需 bRealloc, http_parser 同步增量解析
        int      parse_len   = 0;
        uint16_t readlen     = 0;
        int      peer_closed = 0;

        for (;;)
        {
            // HTTPS truncated 检测 (PT_WAIT_UNTIL 之前): 如果上一轮 inner
            // loop 退出时已经有 plaintext + parser 还没 RECV_DATA + server
            // 已不传数据, 直接走 truncated 等待路径 (bTaskDelayMs + 重试),
            // 不要被外层 5000ms PT_WAIT_UNTIL 卡死.
            //
            // 触发条件: HTTPS + 已有 plaintext + state != RECV_DATA + 不是
            // peer_closed (peer_closed 在前面已经处理). 这种情况下 bSSLRecv
            // 返回 WANT_READ, 我们不要傻等 5s, 而是 truncated 短等.
            // 仅 HTTPS 路径需要; _SSL_ENABLE 关闭时此分支整段跳过 (走外层
            // PT_WAIT_UNTIL 即可).
#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))
            if (http->is_https && http->ssl != NULL && http->recvbuf_len > 0 &&
                http->state != B_HTTP_STA_RECV_DATA)
            {
                if (http->truncated_wait_iters < 3)
                {
                    // truncated 等待窗口内: 让出 200ms, 然后**不**回到 outer
                    // for 顶 PT_WAIT_UNTIL, 而是重新尝试 bSSLRecv 一次.
                    // PT 友好: bTaskDelayMs 即 PT_DELAY_MS, yield 后回到这里.
                    http->truncated_wait_iters++;
                    bTaskDelayMs(pt, 200);
                    // 重新尝试 bSSLRecv: mbedTLS 可能消化了 server 后续 record
                    int  sret2  = 0;
                    uint16_t rl2 = 0;
                    sret2 = bSSLRecv(http->ssl,
                                     (uint8_t *)http->precv + http->recvbuf_len,
                                     http->recvbuf_cap - http->recvbuf_len, &rl2);
                    if (sret2 == 1 && rl2 == 0)
                    {
                        // server 发 close_notify 了, 但仍有 body 没收到.
                        // 直接当 truncated body 处理.
                        b_log_w("[HTTP] truncated body (cl=%llu recv=%u), flush partial\r\n",
                                (unsigned long long)http->parser.content_length,
                                (unsigned)http->recvbuf_len);
                        dat.pdat    = (uint8_t *)http->precv;
                        dat.len     = http->recvbuf_len;
                        dat.release = NULL;
                        event       = B_HTTP_EVENT_RECV_DATA;
                        param       = &dat;
                        goto http_restart;
                    }
                    if (sret2 < 0)
                    {
                        b_log_e("[HTTP] ssl recv failed in truncated wait: %d\r\n", sret2);
                        event = B_HTTP_EVENT_ERROR;
                        param = NULL;
                        goto http_restart;
                    }
                    if (rl2 > 0)
                    {
                        // 拿到新 plaintext, 重新 parse
                        size_t pl2 = http_parser_execute(
                            &http->parser, &http->parse_cb,
                            (const char *)(http->precv + http->recvbuf_len), rl2);
                        http->recvbuf_len += rl2;
                        if (pl2 < (size_t)rl2)
                        {
                            // 协议错
                            event = B_HTTP_EVENT_ERROR;
                            param = NULL;
                            goto http_restart;
                        }
                        if (http->state == B_HTTP_STA_RECV_DATA)
                        {
                            // 完整响应
                            dat.pdat    = (uint8_t *)http->precv;
                            dat.len     = http->recvbuf_len;
                            dat.release = NULL;
                            event       = B_HTTP_EVENT_RECV_DATA;
                            param       = &dat;
                            goto http_restart;
                        }
                        // 还有 body 没收到, continue 到 truncated 等待 (iters++)
                    }
                    // 否则 WANT_READ 仍然没数据, continue 到 truncated 等待
                    continue;
                }
                // truncated 等待窗口用尽 (600ms), server 真不再发了. 当 truncated
                // 上抛 partial plaintext, 优于 5s RECV_TIMEOUT.
                b_log_w("[HTTP] truncated body (cl=%llu recv=%u), flush partial\r\n",
                        (unsigned long long)http->parser.content_length,
                        (unsigned)http->recvbuf_len);
                dat.pdat    = (uint8_t *)http->precv;
                dat.len     = http->recvbuf_len;
                dat.release = NULL;
                event       = B_HTTP_EVENT_RECV_DATA;
                param       = &dat;
                goto http_restart;
            }
#endif // _SSL_ENABLE
            // 等待 socket 可读. HTTPS 响应可能跨越多个 IP 包, 单包间隔有时 >10ms
            // (esp. AT 通道 busy 时), 短超时会被误触为 RECV_TIMEOUT 然后 goto
            // http_restart, 残留数据 +IPD,0,N 晚到时没人接收.
            // 改为 5000ms, 期间如果真的有数据会立刻醒来; 5s 内无任何字节
            // 才判定为对端无响应 (RECV_TIMEOUT).
            PT_WAIT_UNTIL(pt, bSockIsReadable(http->sockfd) == 1, 5000);
            if (PT_WAIT_IS_TIMEOUT(pt))
            {
                event = B_HTTP_EVENT_RECV_TIMEOUT;
                param = NULL;
                goto http_restart;
            }

            // 容量不够: 翻倍扩张
            if (http->recvbuf_cap - http->recvbuf_len < _HTTP_RECV_CHUNK_SIZE)
            {
                int new_cap = (http->recvbuf_cap == 0) ? _HTTP_RECV_CHUNK_SIZE
                                                       : http->recvbuf_cap * 2;
                char *new_buf = (char *)bRealloc(http->precv, new_cap);
                if (new_buf == NULL)
                {
                    b_log_e("[HTTP] precv OOM\r\n");
                    event = B_HTTP_EVENT_ERROR;
                    param = NULL;
                    goto http_restart;
                }
                http->precv       = new_buf;
                http->recvbuf_cap = new_cap;
            }

            // CRIT-HTTPS-DRAIN fix: 在 socket readable 之后, 内层循环反复调 bSSLRecv
            // 拉 plaintext 直到 mbedTLS 没新数据 (WANT_READ/WANT_WRITE) 或
            // 出现错误. 这是必要的, 因为 server 可能在同一 TCP 段里堆多条
            // TLS record (AppData + close_notify, 或多个 AppData record),
            // mbedTLS 一次只返回第一条 plaintext, 剩余 record 留在 mbedTLS
            // 内部 in_offt/in_msg 里, **不**会让 recv_head 增加新字节, 因此
            // bSockIsReadable 永远 false → 5s RECV_TIMEOUT. 必须在 socket
            // readable 阶段把 mbedTLS 能消化的一次性都消化完.
            //
            // 退出原因 (peer_closed 标志):
            //   - peer_closed = 1: 对端发 SSL close_notify (sret == 1).
            //     已方必须结束 (走 RECV_DATA 或 ERROR).
            //   - peer_closed = 0: WANT_READ/WANT_WRITE (sret == 0), 还有
            //     plaintext 可能要来. 必须回到外层 PT_WAIT_UNTIL 等可读.
            //
            // 注意: peer_closed 区分很重要 — partial plaintext + WANT_READ
            // 不应该直接发 RECV_DATA (可能 body 还没传完); 但 partial
            // plaintext + close_notify 应该发 RECV_DATA (下游按 truncated
            // body 处理总比 5s 超时好).
            for (;;)
            {
                readlen    = 0;
                peer_closed = 0;  // 重置 (万一上次 outer iter 跳出时残留=1)
#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))
                if (http->is_https && http->ssl != NULL)
                {
                    int sret = bSSLRecv(http->ssl,
                                        (uint8_t *)http->precv + http->recvbuf_len,
                                        http->recvbuf_cap - http->recvbuf_len, &readlen);
                    if (sret == 1)
                    {
                        // 对端干净关闭. 若 readlen > 0 也算最后一次 plaintext,
                        // 继续内层处理 (parse). 否则跳出内层循环.
                        if (readlen == 0)
                        {
                            peer_closed = 1;
                            break;  // 跳出到外层 RECV_DATA / ERROR 判定
                        }
                    }
                    else if (sret < 0)
                    {
                        // 真错误 (协议错 / OOM / invalid handle).
                        b_log_e("[HTTP] ssl recv failed: %d\r\n", sret);
                        event = B_HTTP_EVENT_ERROR;
                        param = NULL;
                        goto http_restart;
                    }
                    // sret == 0 或 sret > 0: 走下面 readlen>0 的 parse 分支
                }
                else
#endif
                {
                    uint16_t chunk = (uint16_t)(http->recvbuf_cap - http->recvbuf_len);
                    ret = bRecv(http->sockfd,
                                (uint8_t *)http->precv + http->recvbuf_len,
                                chunk, &readlen);
                    // HIGH-HTTP-2 fix: 检查 bRecv 返回. ret<0 表示 socket 错误.
                    if (ret < 0)
                    {
                        event = B_HTTP_EVENT_ERROR;
                        param = NULL;
                        goto http_restart;
                    }
                    // 非 HTTPS 路径 bRecv 一次就够, 退出内层.
                    break;
                }

                if (readlen > 0)
                {
                    parse_len = http_parser_execute(
                        &http->parser, &http->parse_cb,
                        (const char *)(http->precv + http->recvbuf_len), readlen);
                    http->recvbuf_len += readlen;

                    if (http->state == B_HTTP_STA_RECV_DATA)
                    {
                        // 整个 HTTP 响应已收完. 跳出内层, 走 RECV_DATA.
                        break;
                    }
                    if (parse_len < 0)
                    {
                        event = B_HTTP_EVENT_ERROR;
                        param = NULL;
                        goto http_restart;
                    }
                    // 还有 plaintext 没消化 (AppData 比 buf_len 大, 或多个
                    // record 堆在一起), 继续内层循环.
                }
                else
                {
                    // sret >= 0 但 readlen == 0 (WANT_READ/WANT_WRITE):
                    // 等更多 socket 数据, 跳出内层让外层 PT_WAIT_UNTIL 等可读.
                    break;
                }
            }  // end of inner drain loop

            // 内层 drain 结束, 决定事件:
            //   - state == RECV_DATA: 完整响应 (HTTP body 收完). RECV_DATA.
            //   - peer_closed && len > 0: close_notify 来了但 body 没收完
            //     (truncated), 走 RECV_DATA 让下游处理.
            //   - peer_closed && len == 0: 没收到任何数据, 走 ERROR.
            //   - !peer_closed && len > 0: 部分 plaintext (e.g. 只有 headers)
            //     还没收到 body — 这是正常的多包响应情况, 不能发 RECV_DATA
            //     (否则下游以为完整响应). 应该回到外层 PT_WAIT_UNTIL 等可读.
            if (http->state == B_HTTP_STA_RECV_DATA)
            {
                dat.pdat    = (uint8_t *)http->precv;
                dat.len     = http->recvbuf_len;
                dat.release = NULL;
                event       = B_HTTP_EVENT_RECV_DATA;
                param       = &dat;
                goto http_restart;
            }
            if (peer_closed)
            {
                if (http->recvbuf_len > 0)
                {
                    dat.pdat    = (uint8_t *)http->precv;
                    dat.len     = http->recvbuf_len;
                    dat.release = NULL;
                    event       = B_HTTP_EVENT_RECV_DATA;
                    param       = &dat;
                    goto http_restart;
                }
                // close_notify 但 0 字节: 没拿到任何响应.
                event = B_HTTP_EVENT_ERROR;
                param = NULL;
                goto http_restart;
            }
            // HTTPS 截断检测已上移到 outer for 顶部 PT_WAIT_UNTIL 之前 (line ~680),
            // 避免被 5s PT_WAIT_UNTIL 卡死. 这里不再重复.
            if (http->recvbuf_len > 0)
            {
                // 有部分 plaintext 但既不是完整响应也不是 close_notify.
                // 继续外层 PT_WAIT_UNTIL 等更多 socket 数据. 5s 内没新
                // 数据就 RECV_TIMEOUT (符合预期, server 真挂了).
                continue;
            }
            // 0 字节 且 不是 close_notify: socket readable 但 bSSLRecv/bRecv
            // 都返回 0. 通常发生在 handshake 后 server 还没传 AppData 时的
            // WANT_READ. 不应该发 ERROR, 继续外层 PT_WAIT_UNTIL 等.
            continue;
        }

    http_restart:
        // CRIT-HTTP-3 fix: 每次循环结束必须先把 state 切到 DEINIT，再通知上层。
        // 旧代码靠 _bHttpResult 内部置 state，但如果走 event==B_HTTP_EVENT_CONNECTED
        // 分支，_bHttpResult 不会清 state，导致 bTaskRestart 后下一轮顶部读到非
        // DEINIT，直接重连，而 bHttpRequest 处却卡在 busy。
        // 这里先关闭 socket、重置状态，再 _bHttpResult 回调。回调里如果判定要
        // 释放资源（非 CONNECTED 路径），会把 request/precv 等清空。
        if (http->sockfd > 0)
        {
            SOCKET_SHUTDOWN(pt, http->sockfd);
            http->sockfd = -1;
        }
        http->state = B_HTTP_STA_DEINIT;
        _bHttpResult(http, event, param);
        // CRIT-HTTP-AUTOLOOP fix: 旧实现 bTaskRestart 跳回顶部后 state=DEINIT,
        // 进入 PT_WAIT_UNTIL_FOREVER 等下一次 bHttpRequest 调用. 但若 main
        // task 在 PT task 还没处理完回调时盲调 bHttpRequest, 会撞上 state !=
        // DEINIT 报 "client busy", 形成死锁 (request 不再被发起).
        //
        // 修复: PT task 内置自动重试循环. 在主循环等 _HTTP_AUTOLOOP_INTERVAL_MS
        // 再用上一次 host/path/port/is_https 重新发起请求. 业务代码依然可以调
        // bHttpRequest 发起新请求 (会覆盖 host/path 等, 然后 PT_WAIT_UNTIL
        // 在新 state=INIT 到达时立即跳出). 这样 bHttpClientTask 不必自己再
        // 反复调 bHttpRequest, 避免与 PT task 内部状态机撞 busy.
        if (event != B_HTTP_EVENT_DESTROY && http->host[0] != '\0')
        {
            bTaskDelayMs(pt, _HTTP_AUTOLOOP_INTERVAL_MS);
            // 间隔内被外部 bHttpRequest 覆盖了 (state 已是 INIT) 就直接跳过自动
            // 重试, 让 PT 顶部继续走.
            if (http->state == B_HTTP_STA_DEINIT)
            {
                // CRIT-HTTPS-AUTOLOOP-SSL fix: 上一次循环结束时 _bHttpResult 调
                // bSSLDeinit(http->ssl) 并把 http->ssl = NULL. autoloop 只调
                // _bHttpBuildRequest 不走 bHttpRequest, ssl 不会被重建. 下次
                // CIPSTART/CIPSEND 成功后, _bHttpClientTask 检测 http->ssl == NULL
                // 走明文路径, HTTPS 请求变明文 HTTP 发出去, server 立即 CLOSED.
                // 修复: autoloop 重新走完整初始化路径 — 调 _bHttpBuildRequest
                // (URL/host 不变, 重建请求串) + bSSLInit (重建 SSL ctx). 这样
                // 与外部 bHttpRequest 路径行为对齐, 不会丢 SSL 上下文.
                if (_bHttpBuildRequest(http, B_HTTP_GET, NULL, NULL) == 0)
                {
#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))
                    if (http->is_https)
                    {
                        // 防御性: 上一次失败路径可能没把 ssl 清干净 (理论 _bHttpResult
                        // 已经处理过, 但不依赖), 这里再保险清一次, 防 leak.
                        if (http->ssl != NULL)
                        {
                            bSSLDeinit(http->ssl);
                            http->ssl = NULL;
                        }
                        http->ssl = bSSLInit(http->host, NULL);
                        if (SSLHANDLE_IS_INVALID(http->ssl))
                        {
                            // bSSLInit 失败 (mbedtls_ssl_setup 或前置步骤 OOM). 不
                            // bTaskRestart — 那样会让 state 永远停在 DEINIT, 下一次
                            // 顶层 PT_WAIT_UNTIL_FOREVER 永远挂起, 整个 HTTP client
                            // 死锁. 正确做法: 释放 request, 让顶层循环跑下一轮, state
                            // 保持 DEINIT → 等下一轮 _HTTP_AUTOLOOP_INTERVAL_MS 再试.
                            b_log_e("[HTTP] ssl init failed (autoloop)\r\n");
                            if (http->request != NULL)
                            {
                                bFree(http->request);
                                http->request = NULL;
                            }
                            // 立即做一次 15s delay (退避), 避免顶层 PT_WAIT_UNTIL
                            // 16s 超时后立刻又触发本路径形成 CPU 100% 忙等. 让 task
                            // 让出 CPU 给其他 polling func 跑, 然后顶层挂起等下一次
                            // autoloop 周期.
                            bTaskDelayMs(pt, _HTTP_AUTOLOOP_INTERVAL_MS);
                        }
                        else
                        {
                            http->state = B_HTTP_STA_INIT;
                        }
                    }
                    else
#endif
                    {
                        http->state = B_HTTP_STA_INIT;
                    }
                }
            }
        }
        bTaskRestart(pt);
    }

    PT_END(pt);
}
#endif

//=========================================== 服务端实现 ===========================================
#if (defined(_HTTP_SERVER_ENABLE) && (_HTTP_SERVER_ENABLE == 1))
// REVIEW-V3 #6 fix: 长度探针以前用手算 formula (容易与下面的 snprintf 格式漂移),
// 而且对 body_len=0 做了特殊 case (脆弱). 现在用 snprintf 本身做 probe:
//   - buf==NULL || buf_size<=0: snprintf(NULL, 0, ...) 在 C99 里是 UB (C23 才合法),
//                              改用 char stub[1] 走 snprintf(stub, 1, ...) —
//                              C99 §7.19.6.5 明确定义, 返回值=应写长度 (不含 NUL),
//                              写入 ≤1 字节 (只 NUL). 这条路径 glibc / newlib / uclibc 都稳.
//   - buf!=NULL: 正常 snprintf 写入.
//
// 模板只需维护一份, 不再有"算错字节数"的隐患. snprintf stub + bMalloc(N+1)
// 多花 1 次 N+1 写入, 但 size 总是 ~80 字节, MCU 可接受.
static int _bHttpBuildResponse(bHttpResponse_t *resp, char *buf, int buf_size)
{
    if (resp == NULL)
    {
        return -1;
    }
    // H-NEW-1 fix: 校验 body/body_len/content_type 不为 NULL/负数.
    // body NULL 但 body_len 非 0 没有意义 (snprintf %s 会 deref NULL).
    if (resp->body == NULL && resp->body_len != 0)
    {
        return -1;
    }
    if (resp->body_len < 0)
    {
        return -1;
    }

    const char *status_text;
    // H-NEW-9 fix: 补全 RFC 9110 全部 100-599 status code 的 reason phrase.
    // 之前未命中落到 "Unknown", HTTP/2 严格客户端会拒绝.
    switch (resp->status_code)
    {
        // 1xx Informational
        case 100: status_text = "Continue"; break;
        case 101: status_text = "Switching Protocols"; break;
        case 102: status_text = "Processing"; break;
        case 103: status_text = "Early Hints"; break;
        // 2xx Success
        case 200: status_text = "OK"; break;
        case 201: status_text = "Created"; break;
        case 202: status_text = "Accepted"; break;
        case 203: status_text = "Non-Authoritative Information"; break;
        case 204: status_text = "No Content"; break;
        case 205: status_text = "Reset Content"; break;
        case 206: status_text = "Partial Content"; break;
        case 207: status_text = "Multi-Status"; break;
        case 208: status_text = "Already Reported"; break;
        case 226: status_text = "IM Used"; break;
        // 3xx Redirection
        case 300: status_text = "Multiple Choices"; break;
        case 301: status_text = "Moved Permanently"; break;
        case 302: status_text = "Found"; break;
        case 303: status_text = "See Other"; break;
        case 304: status_text = "Not Modified"; break;
        case 305: status_text = "Use Proxy"; break;
        case 307: status_text = "Temporary Redirect"; break;
        case 308: status_text = "Permanent Redirect"; break;
        // 4xx Client Error
        case 400: status_text = "Bad Request"; break;
        case 401: status_text = "Unauthorized"; break;
        case 402: status_text = "Payment Required"; break;
        case 403: status_text = "Forbidden"; break;
        case 404: status_text = "Not Found"; break;
        case 405: status_text = "Method Not Allowed"; break;
        case 406: status_text = "Not Acceptable"; break;
        case 407: status_text = "Proxy Authentication Required"; break;
        case 408: status_text = "Request Timeout"; break;
        case 409: status_text = "Conflict"; break;
        case 410: status_text = "Gone"; break;
        case 411: status_text = "Length Required"; break;
        case 412: status_text = "Precondition Failed"; break;
        case 413: status_text = "Payload Too Large"; break;
        case 414: status_text = "URI Too Long"; break;
        case 415: status_text = "Unsupported Media Type"; break;
        case 416: status_text = "Range Not Satisfiable"; break;
        case 417: status_text = "Expectation Failed"; break;
        case 418: status_text = "I'm a teapot"; break;
        case 421: status_text = "Misdirected Request"; break;
        case 422: status_text = "Unprocessable Entity"; break;
        case 423: status_text = "Locked"; break;
        case 424: status_text = "Failed Dependency"; break;
        case 425: status_text = "Too Early"; break;
        case 426: status_text = "Upgrade Required"; break;
        case 428: status_text = "Precondition Required"; break;
        case 429: status_text = "Too Many Requests"; break;
        case 431: status_text = "Request Header Fields Too Large"; break;
        case 451: status_text = "Unavailable For Legal Reasons"; break;
        // 5xx Server Error
        case 500: status_text = "Internal Server Error"; break;
        case 501: status_text = "Not Implemented"; break;
        case 502: status_text = "Bad Gateway"; break;
        case 503: status_text = "Service Unavailable"; break;
        case 504: status_text = "Gateway Timeout"; break;
        case 505: status_text = "HTTP Version Not Supported"; break;
        case 506: status_text = "Variant Also Negotiates"; break;
        case 507: status_text = "Insufficient Storage"; break;
        case 508: status_text = "Loop Detected"; break;
        case 510: status_text = "Not Extended"; break;
        case 511: status_text = "Network Authentication Required"; break;
        // 兜底: 不在表里的返回纯数字 reason phrase, 避免 "Unknown" 被严格客户端拒绝.
        default:
            status_text = "Status-Code";
            break;
    }

    if (buf == NULL || buf_size <= 0)
    {
        // 第一阶段 (probe): 计算响应头长度, 不写任何 buffer.
        // 传 NULL + size=0 给 snprintf 在 C99/C11 中属于未定义行为 (UB); C23 才合法化.
        // 为兼容 C99 同时避免 -D_FORTIFY_SOURCE=2 / -Wformat-truncation 警告,
        // 用一段足够容纳最坏情况 header 的栈 buffer(约 128 字节) 来probe长度,
        // 然后返回实际应写长度, 调用方据此 malloc. 该 buffer 不返回给上层.
        char probe_buf[144];
        int  n = snprintf(probe_buf, sizeof(probe_buf),
                          "HTTP/1.1 %d %s\r\n"
                          "Content-Type: %s\r\n"
                          "Content-Length: %d\r\n"
                          "Connection: close\r\n"
                          "\r\n",
                          resp->status_code, status_text,
                          resp->content_type ? resp->content_type : "text/plain",
                          resp->body_len);
        // snprintf 返回值 >= sizeof 说明 probe buffer 不足, 视为异常(当前 status_text
        // 最长约 30 字节 + content_type 可控, 144 足够). 返回应写长度 (不含 NUL).
        return (n < 0) ? -1 : n;
    }

    int header_len =
        snprintf(buf, (size_t)buf_size,
                 "HTTP/1.1 %d %s\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %d\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 resp->status_code, status_text,
                 resp->content_type ? resp->content_type : "text/plain", resp->body_len);
    return header_len;
}

//---------------------------------------------------- http_parser 回调 ---------------------------------------------------
// 一次性回调, 在 bHttpClientConnTask init 阶段注册
static int _bHttpOnUrl(http_parser *p, const char *at, size_t length)
{
    bHttpConnCtx_t *ctx = (bHttpConnCtx_t *)p->data;
    // http_parser 可能在 url 没解析完前就多次调 on_url (例如 chunked 收包).
    // 累积追加到 url_buf. 总长超过 _HTTP_PATH_LEN_MAX 返 414.
    if (ctx->url_buf[0] == '\0')
    {
        // 第一次: 整段 url (在 rbuf 内的指针)
        if (length >= sizeof(ctx->url_buf))
        {
            return 1;
        }
        memcpy(ctx->url_buf, at, length);
        ctx->url_buf[length] = '\0';
    }
    else
    {
        // 后续: 拼接到末尾
        size_t cur = strlen(ctx->url_buf);
        if (cur + length >= sizeof(ctx->url_buf))
        {
            return 1;
        }
        memcpy(ctx->url_buf + cur, at, length);
        ctx->url_buf[cur + length] = '\0';
    }
    return 0;
}

static int _bHttpOnHeadersComplete(http_parser *p)
{
    bHttpConnCtx_t *ctx = (bHttpConnCtx_t *)p->data;
    // http-parser: 0=DELETE, 1=GET, 2=HEAD, 3=POST, 4=PUT
    switch (p->method)
    {
        case 1: ctx->method = B_HTTP_GET; break;
        case 3: ctx->method = B_HTTP_POST; break;
        case 4: ctx->method = B_HTTP_PUT; break;
        case 0: ctx->method = B_HTTP_DELETE; break;
        default: ctx->method = B_HTTP_GET; break;
    }
    ctx->headers_done = 1;
    return 0;  // 0=继续 body, 1=无 body
}

static int _bHttpOnBody(http_parser *p, const char *at, size_t length)
{
    bHttpConnCtx_t *ctx = (bHttpConnCtx_t *)p->data;
    // body 数据全部在 rbuf 内 (at 是 rbuf+offset), 0-copy: 记下首次 offset, 累加长度
    if (ctx->body_len == 0)
    {
        ctx->body_off = (int)(at - ctx->rbuf);
    }
    ctx->body_len += (int)length;
    return 0;
}

static int _bHttpOnMessageComplete(http_parser *p)
{
    bHttpConnCtx_t *ctx = (bHttpConnCtx_t *)p->data;
    ctx->headers_done = 2;
    return 0;
}

//---------------------------------------------------- accept 回调 ---------------------------------------------------
// 在 bTransEvent (B_TRANS_ACCEPTED) 上下文里被 b_mod_tcpip 派发. 不能阻塞, 不能解析.
// 行为: round-robin 装到第一个空 slot. sub-task i 监听 slot i, 多 client 自动并发.
static uint8_t s_http_rr = 0;  // round-robin 起点, 减少多个 sub-task 都抢 slot 0
static void _bHttpServerAcceptCb(bTransEvent_t event, void *param, void *arg)
{
    bHttpServer_t *server = (bHttpServer_t *)arg;
    bSocketFd_t    client_fd = (bSocketFd_t)(intptr_t)param;

    if (event != B_TRANS_ACCEPTED || SOCKFD_IS_INVALID(client_fd))
    {
        return;
    }

    for (int i = 0; i < SERVER_MAX_CONNECTIONS; i++)
    {
        uint8_t idx = (s_http_rr + i) % SERVER_MAX_CONNECTIONS;
        if (SOCKFD_IS_INVALID(server->slots[idx].client_fd))
        {
            server->slots[idx].client_fd = client_fd;
            s_http_rr                    = (idx + 1) % SERVER_MAX_CONNECTIONS;
            b_log("[HTTPSRV] accepted, slot=%d client_fd=%lld\r\n", idx, (long long)client_fd);
            return;
        }
    }
    b_log_w("[HTTPSRV] reject: no free slot (client_fd=%lld)\r\n", (long long)client_fd);
    bShutdown(client_fd);
}

//   5. shutdown + 清 slot
//   6. 回 1, 处理下一个 client
PT_THREAD(_bHttpClientConnTask)(struct pt *pt, void *arg)
{
    bHttpConnCtx_t     *ctx  = (bHttpConnCtx_t *)arg;
    /* 流式动态分配: sub-task 栈上只剩 resp + req 两个结构 + 几个临时变量.
       ctx->rbuf/body/resp_hdr 都是 char*, 按需 bMalloc/bRealloc, 处理完 bFree.
       整个 task 栈占用 < 64 字节, 即使 20 并发也只占 1.2KB 总栈.

       PT 规则: 跨 yield 必须保留的变量才能 static. 这里 resp/req 每次循环
       开头 memset, 不需要跨 yield 保留; ret/w/o/bo/hl/rlen 只在单次 while(1)
       循环体内使用 (send/recv 循环被 PT_WAIT_UNTIL 打断, 但 yield 回来后这些
       局部变量会被重新初始化, 因此根本不应依赖它们的值). 实际进度保存在
       ctx->resp_hdr/resp_hdr_len/rbuf_len/body_off 等 ctx 字段中, 所以局部
       变量每次重新从 ctx 计算即可. */
    bHttpResponse_t      resp;
    bHttpServerRequest_t req;

    B_TASK_INIT_BEGIN();
    ctx->parser.data = ctx;
    B_TASK_INIT_END();

    PT_BEGIN(pt);
    while (1)
    {
        // 0. server 停止信号: 见 bHttpServerStop 设 stop_requested
        if (ctx->server->stop_requested)
        {
            // 释放当前 sub-task 拥有的 resp_hdr, 不再 accept 新连接
            if (ctx->resp_hdr)
            {
                bFree(ctx->resp_hdr);
                ctx->resp_hdr     = NULL;
                ctx->resp_hdr_len = 0;
            }
            // 自报停: 让 bHttpServerStop 检测到 worker 已退出, 不强制 bTaskRemove
            // HIGH-TCP-4 fix (note: 实际是 HIGH-HTTP-4): 同时清 ctx 指针, 防止
            // 外部 B_TRANS_DISCONNECT 回调晚到时 ctx 已无效. slot->worker 已 NULL.
            if (ctx->slot) {
                ctx->slot->worker = NULL;
                ctx->slot->ctx    = NULL;
            }
            break;  // 跳出 while(1), 后续 PT_END 正常结束 task
        }
        // 1. 扫描所有 slot 抢活 (sub-task 协作: 谁先醒谁拿)
        //    用 ctx->client_fd 持久化 (local var 会被 main loop 反复 init)
        ctx->client_fd = -1;
        do
        {
            int _i;
            for (_i = 0; _i < SERVER_MAX_CONNECTIONS; _i++)
            {
                if (!SOCKFD_IS_INVALID(ctx->server->slots[_i].client_fd))
                {
                    ctx->client_fd = ctx->server->slots[_i].client_fd;
                    // 立刻清掉, 防其他 sub-task 重复抢同一 slot
                    ctx->server->slots[_i].client_fd = -1;
                    break;
                }
            }
            if (SOCKFD_IS_INVALID(ctx->client_fd))
            {
                PT_WAIT_UNTIL(pt, 0, 5);  // 短睡让出 CPU
            }
        } while (SOCKFD_IS_INVALID(ctx->client_fd));

        // 2. rbuf 是 ctx 内置固定数组 (BSS), 不需要 bMalloc, 永不 OOM
        //    大小一次性到位 _HTTP_MAX_REQ_SIZE, 不 mid-conn realloc
        ctx->body_len     = 0;
        ctx->body_off     = 0;
        ctx->rbuf_len     = 0;
        ctx->resp_hdr     = NULL;
        ctx->resp_hdr_len = 0;
        ctx->headers_done = 0;
        ctx->method       = B_HTTP_GET;
        ctx->url_buf[0]   = '\0';
        http_parser_init(&ctx->parser, HTTP_REQUEST);
        ctx->parser.data  = ctx;
        http_parser_settings_init(&ctx->parse_cb);
        ctx->parse_cb.on_url              = _bHttpOnUrl;
        ctx->parse_cb.on_headers_complete = _bHttpOnHeadersComplete;
        ctx->parse_cb.on_body             = _bHttpOnBody;
        ctx->parse_cb.on_message_complete = _bHttpOnMessageComplete;

        // 3. 流式接收: rbuf 按需扩张, http_parser 同步增量解析
        while (ctx->headers_done < 2)
        {
            PT_WAIT_UNTIL(pt, bSockIsReadable(ctx->client_fd) == 1, 5000);
            if (PT_WAIT_IS_TIMEOUT(pt))
            {
                b_log_w("[HTTPSRV] recv timeout, slot=%p\r\n", ctx->slot);
                goto conn_done;
            }

            // 容量检查: rbuf 是 ctx 内置固定数组 sizeof(ctx->rbuf) 字节,
            // 永不 realloc, 永不 OOM. 超过这个上限 (header+body 总和) 返 413
            if (ctx->rbuf_len >= (int)sizeof(ctx->rbuf))
            {
                b_log_w("[HTTPSRV] req too large > %d, slot=%p\r\n",
                        (int)sizeof(ctx->rbuf), ctx->slot);
                bHttpResponse_t err = {
                    .status_code  = 413,
                    .content_type = "text/plain",
                    .body         = "Payload Too Large",
                    .body_len     = 19,
                };
                // PT 上下文调 PT 版: 内部 PT_WAIT_UNTIL 让出, 不再 busy-wait.
                // REVIEW-V3 #1 fix: 用 per-conn ctx, 不与其它 sub-task 共享 send state.
                PT_WAIT_THREAD(pt, bHttpSendResponsePTEx(pt, ctx->client_fd, &err, &ctx->send_ctx));
                goto conn_done;
            }

            // 单次 recv 临时变量, 仅在本轮 PT 调度内使用, yield 后不依赖其值.
            uint16_t rlen = 0;
            uint16_t chunk  = (uint16_t)(sizeof(ctx->rbuf) - ctx->rbuf_len);
            int      ret    = bRecv(ctx->client_fd, (uint8_t *)ctx->rbuf + ctx->rbuf_len, chunk, &rlen);
            if (ret <= 0 || rlen == 0)
            {
                goto conn_done;  // 客户端关闭/出错
            }
            // http_parser 增量解析: parser 内部维护 offset, 续解析只需喂新字节.
            // (跟 client 路径 _bHttpClientTask 行为一致: 传 (precv+recvbuf_len, readlen))
            // rbuf 不在 conn 生命周期内 realloc, body_off 等指针恒定有效.
            // 新字节起点 = rbuf + 旧 rbuf_len, 长度 = rlen. 旧 rbuf_len 还没自增.
            size_t parsed = http_parser_execute(&ctx->parser, &ctx->parse_cb,
                                                 ctx->rbuf + ctx->rbuf_len, rlen);
            ctx->rbuf_len += rlen;
            // http_parser_execute 返回值 = 本次喂入的 rlen 字节里被消费的字节数.
            // 数据不完整 (parser 等更多数据) 时 parsed == rlen (全部消费完但没成 message).
            // 真错误时 parsed < rlen 且 http_errno != HPE_OK.
            if ((unsigned)parsed != (unsigned)rlen &&
                ctx->parser.http_errno != HPE_OK)
            {
                // 解析出错: 发 400
                b_log_w("[HTTPSRV] parse err, slot=%p errno=%d parsed=%u/%u\r\n",
                        ctx->slot, ctx->parser.http_errno,
                        (unsigned)parsed, (unsigned)ctx->rbuf_len);
                bHttpResponse_t err = {
                    .status_code  = 400,
                    .content_type = "text/plain",
                    .body         = "Bad Request",
                    .body_len     = 11,
                };
                // PT 上下文调 PT 版: 内部 PT_WAIT_UNTIL 让出, 不再 busy-wait.
                // REVIEW-V3 #1 fix: per-conn send ctx (见上行注释).
                PT_WAIT_THREAD(pt, bHttpSendResponsePTEx(pt, ctx->client_fd, &err, &ctx->send_ctx));
                goto conn_done;
            }
        }

        // 4. 调 handler (parse 完成, rbuf + body + url_buf 都齐了)
        memset(&resp, 0, sizeof(resp));
        memset(&req,  0, sizeof(req));
        req.method    = ctx->method;
        req.url       = ctx->url_buf;
        // body 指向 rbuf 内部 (0-copy), handler 用完即弃
        req.body      = (ctx->body_len > 0) ? (ctx->rbuf + ctx->body_off) : NULL;
        req.body_len  = ctx->body_len;
        req.user_data = ctx->server->user_data;
        if (ctx->server->handler)
        {
            ctx->server->handler(&req, &resp, ctx->server->user_data);
        }
        if (resp.content_type == NULL)
        {
            resp.content_type = "text/plain";
        }
        if (resp.body_len == 0 && resp.body)
        {
            resp.body_len = strlen(resp.body);
        }

        // 5. 拼响应头到动态堆 buf
        int hl = _bHttpBuildResponse(&resp, NULL, 0);  // 先问长度
        if (hl > 0)
        {
            if (ctx->resp_hdr)
            {
                bFree(ctx->resp_hdr);
            }
            // 多分配 1 字节, 给 snprintf 写 '\0' 终止符留位置, 避免越界写
            ctx->resp_hdr     = (char *)bMalloc(hl + 1);
            ctx->resp_hdr_len = _bHttpBuildResponse(&resp, ctx->resp_hdr, hl + 1);
        }

        // 6. inline send, PT_WAIT_UNTIL 让出 CPU
        //    发送进度必须跨 yield 保留, 放到 ctx 字段中, 而不是局部变量.
        ctx->send_off = 0;
        if (ctx->resp_hdr && ctx->resp_hdr_len > 0)
        {
            uint8_t retry = 0;
            while (ctx->send_off < ctx->resp_hdr_len)
            {
                PT_WAIT_UNTIL(pt, bSockIsWriteable(ctx->client_fd) == 1,
                              _HTTP_SEND_CHUNK_TIMEOUT_MS);
                if (PT_WAIT_IS_TIMEOUT(pt))
                {
                    b_log_w("[HTTPSRV] send header timeout, slot=%p\r\n", ctx->slot);
                    break;
                }
                uint16_t w = 0;
                int ret = bSend(ctx->client_fd, (uint8_t *)ctx->resp_hdr + ctx->send_off,
                                ctx->resp_hdr_len - ctx->send_off, &w);
                if (ret < 0)
                {
                    b_log_w("[HTTPSRV] send header err %d, slot=%p\r\n", ret, ctx->slot);
                    break;
                }
                if (w > 0)
                {
                    ctx->send_off += w;
                    retry = 0;
                }
                else if (++retry >= 5)
                {
                    b_log_w("[HTTPSRV] send header stalled, slot=%p\r\n", ctx->slot);
                    break;
                }
            }
        }
        if (resp.body && resp.body_len > 0 && ctx->send_off >= ctx->resp_hdr_len)
        {
            ctx->body_send_off = 0;
            uint8_t retry = 0;
            while (ctx->body_send_off < (int)resp.body_len)
            {
                PT_WAIT_UNTIL(pt, bSockIsWriteable(ctx->client_fd) == 1,
                              _HTTP_SEND_CHUNK_TIMEOUT_MS);
                if (PT_WAIT_IS_TIMEOUT(pt))
                {
                    b_log_w("[HTTPSRV] send body timeout, slot=%p\r\n", ctx->slot);
                    break;
                }
                uint16_t w = 0;
                int ret = bSend(ctx->client_fd, (uint8_t *)resp.body + ctx->body_send_off,
                                (int)resp.body_len - ctx->body_send_off, &w);
                if (ret < 0)
                {
                    b_log_w("[HTTPSRV] send body err %d, slot=%p\r\n", ret, ctx->slot);
                    break;
                }
                if (w > 0)
                {
                    ctx->body_send_off += w;
                    retry = 0;
                }
                else if (++retry >= 5)
                {
                    b_log_w("[HTTPSRV] send body stalled, slot=%p\r\n", ctx->slot);
                    break;
                }
            }
        }
        // CRIT-WEB-1 fix: handler 可能用 heap 缓冲 (per-call json_buf), HTTP server
        // 必须在 body send 完后释放, 否则泄漏. body_free = NULL 表示 const string
        // literal, 不需要释放.
        if (resp.body_free)
        {
            resp.body_free((void *)resp.body);
        }

    conn_done:
        // 7. 一帧处理完, 重置 per-frame 状态. rbuf 是 ctx 内置数组, 不 bFree
        bShutdown(ctx->client_fd);
        ctx->rbuf_len = 0;
        ctx->body_len = 0;
        ctx->body_off = 0;
        ctx->send_off = 0;
        ctx->body_send_off = 0;
        if (ctx->resp_hdr)
        {
            bFree(ctx->resp_hdr);
            ctx->resp_hdr     = NULL;
            ctx->resp_hdr_len = 0;
        }
        ctx->client_fd = -1;
    }
    PT_END(pt);
}

//---------------------------------------------------- listen 主 task ---------------------------------------------------
// 启动 listen socket + SERVER_MAX_CONNECTIONS 个 sub-task 等待 client. 之后只做心跳.
PT_THREAD(_bHttpServerTask)(struct pt *pt, void *arg)
{
    bHttpServer_t *server = (bHttpServer_t *)arg;

    PT_BEGIN(pt);

    memset(server->slots, 0, sizeof(server->slots));
    server->startup_error = 0;

    // CRITICAL-C2 fix: 把 socket/bind/listen 失败统一收敛到 startup_error 标志,
    // 失败后不 PT_EXIT (否则 _bTaskCore 忽略返回值, 下次调度会重入并 hot loop),
    // 而是进入统一的主循环 idle 等待 bHttpServerStop 来安全释放资源.
    server->listen_fd = bSocket(B_TRANS_CONN_TCP, _bHttpServerAcceptCb, server);
    if (SOCKFD_IS_INVALID(server->listen_fd))
    {
        b_log_e("[HTTP] create socket failed\r\n");
        server->startup_error = 1;
    }
    else if (bBind(server->listen_fd, server->port) < 0)
    {
        b_log_e("[HTTP] bind port %d failed\r\n", server->port);
        bShutdown(server->listen_fd);
        server->listen_fd     = -1;
        server->startup_error = 1;
    }
    else if (bListen(server->listen_fd, 5) < 0)
    {
        b_log_e("[HTTP] listen failed\r\n");
        bShutdown(server->listen_fd);
        server->listen_fd     = -1;
        server->startup_error = 1;
    }

    if (!server->startup_error)
    {
        b_log("[HTTP] server started on port %d\r\n", server->port);

        // 启动 SERVER_MAX_CONNECTIONS 个 sub-task, 每个占一个 slot
        for (int i = 0; i < SERVER_MAX_CONNECTIONS; i++)
        {
            bHttpConnCtx_t *ctx = (bHttpConnCtx_t *)bMalloc(sizeof(bHttpConnCtx_t));
            if (ctx == NULL)
            {
                b_log_e("[HTTP] no mem for conn ctx %d\r\n", i);
                break;
            }
            memset(ctx, 0, sizeof(*ctx));
            ctx->server    = server;
            ctx->slot      = &server->slots[i];
            ctx->client_fd = -1;
            server->slots[i].client_fd = -1;

            ctx->task_id = bTaskCreate("httpconn", _bHttpClientConnTask, ctx, &ctx->attr);
            if (ctx->task_id == NULL)
            {
                b_log_e("[HTTP] sub-task %d create failed\r\n", i);
                bFree(ctx);
                break;
            }
            server->slots[i].worker = ctx->task_id;
            server->slots[i].ctx    = ctx;  // 停服务时释放
        }
    }
    else
    {
        b_log_e("[HTTP] server startup failed, entering idle until bHttpServerStop\r\n");
    }

    // 主循环: 心跳 + 让出 CPU. 也负责清理停服后的 sub-task / ctx.
    // 注: bHttpServerStop 只置 stop_requested 后立刻返回, 由本 task 在后续 bExec()
    //     调度轮次里检测到停止信号, 然后等待 sub-task 自报停并释放资源.
    while (1)
    {
        if (server->stop_requested)
        {
            break;
        }
        bTaskDelayMs(pt, 1000);
    }

    if (!server->startup_error)
    {
        // 关 listen socket (幂等: bHttpServerStop 可能已经关过)
        if (!SOCKFD_IS_INVALID(server->listen_fd))
        {
            bShutdown(server->listen_fd);
            server->listen_fd = -1;
        }

        // 关掉所有 client socket, 触发 sub-task 的 bRecv 返回 <=0 推进到自检退出.
        for (int i = 0; i < SERVER_MAX_CONNECTIONS; i++)
        {
            if (!SOCKFD_IS_INVALID(server->slots[i].client_fd))
            {
                bShutdown(server->slots[i].client_fd);
                server->slots[i].client_fd = -1;
            }
        }

        // 等 sub-task 自报停 (worker = NULL). 单核 PT 调度下需要多次 bExec()
        // 让每个 sub-task 跑完当前 wait. 兜底超时 1s 后强制回收.
        // M-NEW-7 fix: 200ms 太短, sub-task 在 mid-bRecv 状态下要等 lwIP callback
        // 推完才会自检, PC 模拟可达 200ms+, 但裸机 lwIP 周期可能 >500ms.
        // 提到 1s 是更稳的兜底; 单核 MCU 上 bTaskDelayMs 5ms 让出不会卡 main.
        // 注意: 这里用 bTaskDelayMs 让出 CPU, 由用户 main 里的 bExec() 驱动调度.
        uint32_t stop_tick = bHalGetSysTick();
        while (1)
        {
            int alive = 0;
            for (int i = 0; i < SERVER_MAX_CONNECTIONS; i++)
            {
                if (server->slots[i].worker != NULL) { alive++; }
            }
            if (alive == 0) break;
            if (TICK_DIFF_BIT32(stop_tick, bHalGetSysTick()) > MS2TICKS(1000)) break;
            bTaskDelayMs(pt, 5);  // 让出 CPU, 等其他 task 跑完
        }

        // 强制回收仍残留的 sub-task (超时未退). 这里不能再等, 否则 stop 卡死.
        for (int i = 0; i < SERVER_MAX_CONNECTIONS; i++)
        {
            if (server->slots[i].worker)
            {
                bTaskRemove(server->slots[i].worker);
                server->slots[i].worker = NULL;
            }
            if (server->slots[i].ctx)
            {
                bFree(server->slots[i].ctx);
                server->slots[i].ctx = NULL;
            }
        }
    }

    // 清空 server 指针, 通知外部 server 已关闭.
    if (s_http_server == server) { s_http_server = NULL; }
    b_log("[HTTP] server stopped%s\r\n",
          server->startup_error ? " (startup failed)" : "");
    bFree(server);

    PT_END(pt);
}
#endif

/**
 * \}
 */

/**
 * \defgroup HTTP_Exported_Functions
 * \{
 */

//===========================================
// 客户端API（兼容原有接口）===========================================
#if (defined(_HTTP_CLIENT_ENABLE) && (_HTTP_CLIENT_ENABLE == 1))

bHttpFd_t bHttpInit(pHttpCb_t cb, void *user_data)
{
    bHttpClientCtx_t *http = NULL;
    if (cb == NULL)
    {
        return -1;
    }
    http = (bHttpClientCtx_t *)bMalloc(sizeof(bHttpClientCtx_t));
    if (http == NULL)
    {
        return -2;
    }
    memset(http, 0, sizeof(bHttpClientCtx_t));
    http->callback      = cb;
    // 流式动态分配: request/precv 初始 NULL, 按需 bMalloc/bRealloc
    http->sockfd        = -1;
    http->state         = B_HTTP_STA_DEINIT;
    http->user_data     = user_data;

    if ((http->task_id = bTaskCreate(NULL, _bHttpClientTask, http, &http->attr)) == NULL)
    {
        b_log_e("[HTTP] task create failed\r\n");
        bFree(http);
        return -3;
    }

    s_http_client = http;
    return (bHttpFd_t)(intptr_t)http;
}

int bHttpRequest(bHttpFd_t httpfd, bHttpReqType_t type, const char *url, const char *head,
                 const char *body)
{
    bHttpClientCtx_t *http = (bHttpClientCtx_t *)(intptr_t)httpfd;

    if (httpfd <= 0 || !HTTPREQ_TYPE_IS_VALID(type) || url == NULL || http->callback == NULL)
    {
        b_log_e("[HTTP] param error\r\n");
        return -1;
    }

    if (http->state != B_HTTP_STA_DEINIT)
    {
        b_log_e("[HTTP] client busy\r\n");
        return -2;
    }

    memset(http->host, 0, sizeof(http->host));
    memset(http->path, 0, sizeof(http->path));

    if (_bHttpParseUrl(url, http->host, http->path, &http->port, &http->is_https) < 0)
    {
        b_log_e("[HTTP] parse url failed\r\n");
        return -3;
    }

    if (_bHttpBuildRequest(http, type, head, body) < 0)
    {
        b_log_e("[HTTP] build request failed (OOM)\r\n");
        return -4;
    }

    // HTTPS: 初始化SSL
#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))
    if (http->is_https)
    {
        http->ssl = bSSLInit(http->host, NULL);
        if (SSLHANDLE_IS_INVALID(http->ssl))
        {
            b_log_e("[HTTP] ssl init failed\r\n");
            if (http->request)
            {
                bFree(http->request);
                http->request = NULL;
            }
            return -5;
        }
    }
#endif

    http->state = B_HTTP_STA_INIT;
    return 0;
}

int bHttpDeInit(bHttpFd_t httpfd)
{
    if (httpfd <= 0)
    {
        return -1;
    }
    bHttpClientCtx_t *http = (bHttpClientCtx_t *)(intptr_t)httpfd;
    http->state            = B_HTTP_STA_DESTROY;
    s_http_client          = NULL;
    return 0;
}
#endif

//=========================================== 服务端API ===========================================
#if (defined(_HTTP_SERVER_ENABLE) && (_HTTP_SERVER_ENABLE == 1))
int bHttpServerStart(uint16_t port, bHttpServerHandler_t handler, void *user_data)
{
    if (s_http_server != NULL)
    {
        b_log_w("[HTTP] server already started\r\n");
        return -1;
    }
    if (handler == NULL)
    {
        return -2;
    }

    bHttpServer_t *server = (bHttpServer_t *)bMalloc(sizeof(bHttpServer_t));
    if (server == NULL)
    {
        return -3;
    }

    server->port      = port;
    server->handler   = handler;
    server->user_data = user_data;
    server->listen_fd = -1;
    memset(server->slots, 0, sizeof(server->slots));

    server->task_id = bTaskCreate("httpsrv", _bHttpServerTask, server, &server->attr);
    if (server->task_id == NULL)
    {
        bFree(server);
        return -4;
    }

    s_http_server = server;
    return 0;
}

void bHttpServerStop(void)
{
    // 非 PT 上下文 / 非 main() 中, 不能调 bExec() 或 bTaskDelayMs 等 PT API.
    // 这里只做信号标记 + 关 listen socket, 实际清理交给 _bHttpServerTask:
    //   - 用户 main() 继续跑 bExec(), _bHttpServerTask 会在后续调度轮次里
    //     检测到 stop_requested 并清理 sub-task / ctx / server 自身.
    // 调用方无需等待, 内部资源会在若干个 bExec() 周期内释放完毕.
    if (s_http_server == NULL)
    {
        return;
    }

    // 1. 置 stop flag, 让所有 sub-task 在下次 while(1) 顶部自检退出.
    //    (C5 fix: 必须在 shutdown socket 之前设, 否则 sub-task 还可能持有 ctx 引用)
    s_http_server->stop_requested = 1;

    // 2. 关 listen socket, 阻止新连接进来 (bShutdown 是非 PT 调用, 安全)
    if (!SOCKFD_IS_INVALID(s_http_server->listen_fd))
    {
        bShutdown(s_http_server->listen_fd);
    }

    // 3. 关掉所有 client socket, 触发 sub-task 的 bRecv 返回 <=0,
    //    让它能推进到 while(1) 顶部检查 stop_requested.
    //    (bShutdown 是非 PT 调用, 安全)
    for (int i = 0; i < SERVER_MAX_CONNECTIONS; i++)
    {
        if (!SOCKFD_IS_INVALID(s_http_server->slots[i].client_fd))
        {
            bShutdown(s_http_server->slots[i].client_fd);
        }
    }

    b_log("[HTTP] stop requested, cleanup happens in _bHttpServerTask (PT ctx)\r\n");
    // 注: server, ctx, task_id 的最终释放由 _bHttpServerTask 在 PT 上下文里做.
}

// MCU 资源保护: 注: PT_WAIT_UNTIL 必须在 PT_BEGIN/PT_END switch 上下文内,
// 不可跨普通函数调用 (case label not within a switch).
//
// 因此本文件提供两个 API, 业务逻辑 (build + send) 共享同一思路:
//   - bHttpSendResponsePT(pt, ...)   PT 上下文用. 内部 PT_BEGIN/PT_END 包裹,
//                                     用 PT_WAIT_UNTIL 等 socket 可写, 让出 CPU.
//   - bHttpSendResponse(...)         非 PT 上下文用 (host 测试工具). 不可写时
//                                     直接返 -5 (调用方按需重试), 不 yield 不自旋.
// 两条路径都不再 busy-wait. PT 上下文中务必使用 PT 版.

// PT 版: 必须在调用方 PT 上下文里用 PT_WAIT_THREAD 包裹.
// 返回 PT_THREAD_END (0) 完成, PT_WAITING (1) 让出等待.
// 内部状态 (header_buf / off / timed_out) 由调用方传入的 ctx 保存跨 yield.
//
// REVIEW-V3 #1 fix: 旧实现用 file-static 状态, 在 HTTP server 多 sub-task 并发
// 时互相覆盖 — 一个连接的 s_off 是另一个连接的进度, bFree 会释放对方指针.
// 现在 ctx 由调用方持有, 每个并发 task 一个, 互不干扰.
//
// H-NEW-2 fix: 原代码参数无效时不清 s_header_buf, 后续若再以合法参数调用,
// 调用方拿到 stale 指针可能 double-free. 现在入参无效时主动 bFree.
char bHttpSendResponsePTEx(struct pt *pt, int client_fd, const bHttpResponse_t *resp,
                           bHttpSendCtx_t *ctx)
{
    if (client_fd <= 0 || resp == NULL || ctx == NULL)
    {
        // H-NEW-2 增强: 参数无效也要清掉残留 header_buf, 防调用方又传合法参数
        // 时拿到旧 buf 指针. (实际只会指向已 bFree 过的指针, 但 malloc 重用可能
        // 触发双重释放). 这里保险清一下.
        if (ctx && ctx->header_buf)
        {
            bFree(ctx->header_buf);
            ctx->header_buf = NULL;
        }
        return 3;   // PT_ENDED — 参数无效视为已完成
    }

    PT_BEGIN(pt);

    // 1. 计算 header 长度 + 分配 (只跑一次, 后续 yield 跳过)
    ctx->header_len = _bHttpBuildResponse((bHttpResponse_t *)resp, NULL, 0);
    if (ctx->header_len <= 0)
    {
        PT_EXIT(pt);
    }
    ctx->header_buf = (char *)bMalloc(ctx->header_len + 1);
    if (ctx->header_buf == NULL)
    {
        PT_EXIT(pt);
    }
    if (_bHttpBuildResponse((bHttpResponse_t *)resp, ctx->header_buf, ctx->header_len + 1) < 0)
    {
        bFree(ctx->header_buf);
        ctx->header_buf = NULL;
        PT_EXIT(pt);
    }
    ctx->off       = 0;
    ctx->body_off  = 0;
    ctx->timed_out = 0;

    // 2. 发 header (PT_WAIT_UNTIL 让出 CPU, 不 busy-wait)
    while (ctx->off < ctx->header_len)
    {
        PT_WAIT_UNTIL(pt, bSockIsWriteable(client_fd) == 1, _HTTP_SEND_CHUNK_TIMEOUT_MS);
        if (PT_WAIT_IS_TIMEOUT(pt))
        {
            ctx->timed_out = 1;
            break;
        }
        uint16_t wlen = 0;
        uint16_t chunk = (uint16_t)(ctx->header_len - ctx->off);
        int ret = bSend(client_fd, (uint8_t *)ctx->header_buf + ctx->off, chunk, &wlen);
        if (ret > 0 && wlen > 0)
        {
            ctx->off += wlen;
        }
    }

    // 3. 发 body (resp->body 由调用方保证生命周期: handler 路径下要保留到返回后)
    if (resp->body && resp->body_len > 0 && !ctx->timed_out)
    {
        while (ctx->body_off < (int)resp->body_len)
        {
            PT_WAIT_UNTIL(pt, bSockIsWriteable(client_fd) == 1, _HTTP_SEND_CHUNK_TIMEOUT_MS);
            if (PT_WAIT_IS_TIMEOUT(pt))
            {
                ctx->timed_out = 1;
                break;
            }
            uint16_t wlen = 0;
            uint16_t chunk = (uint16_t)(resp->body_len - ctx->body_off);
            int ret = bSend(client_fd, (uint8_t *)(resp->body + ctx->body_off), chunk, &wlen);
            if (ret > 0 && wlen > 0)
            {
                ctx->body_off += wlen;
            }
        }
    }

    // CRIT-WEB-1 fix: handler 可能用 heap 缓冲 (per-call json_buf), 在 body
    // send 完后释放. body_free = NULL 表示 const string literal, 不释放.
    if (resp->body_free)
    {
        resp->body_free((void *)resp->body);
    }

    // 4. 清理
    if (ctx->header_buf)
    {
        bFree(ctx->header_buf);
        ctx->header_buf = NULL;
    }
    PT_END(pt);
}

// 旧版 (单连接) wrapper: 保留 file-static ctx 给只跑一个 PT 任务的场景 (兼容性).
// 注意: 在 HTTP server 多 sub-task 并发时, **不能**调用本函数, 会互相覆盖.
// 多连接场景改用 bHttpSendResponsePTEx.
char bHttpSendResponsePT(struct pt *pt, int client_fd, const bHttpResponse_t *resp)
{
    static bHttpSendCtx_t s_ctx;   // 单 task 复用, 跨 yield 持久
    return bHttpSendResponsePTEx(pt, client_fd, resp, &s_ctx);
}

// 非 PT 版: 用于没有 PT 上下文的场合 (host 工具 / 一次性测试).
// 在裸机 MCU 上, PT 任务上下文必须用 bHttpSendResponsePT; 这里仅 best-effort:
// 不可写时直接返回 -5 (调用方按需重试), 不在非 PT 上下文自旋或 yield (PT 上下文限制).
// PC POSIX 套接字路径下, 一次 send 通常就成功, 不会进入循环.
int bHttpSendResponse(int client_fd, bHttpResponse_t *resp)
{
    if (client_fd <= 0 || resp == NULL)
    {
        return -1;
    }

    int header_len = _bHttpBuildResponse(resp, NULL, 0);
    if (header_len < 0)
    {
        // SRV-HTTP-6 fix: header build probe 失败也要 free body.
        if (resp->body_free) resp->body_free((void *)resp->body);
        return -2;
    }
    char *header_buf = (char *)bMalloc(header_len + 1);
    if (header_buf == NULL)
    {
        // SRV-HTTP-6 fix: header 分配失败也要 free body (caller 传 heap body 时)
        if (resp->body_free) resp->body_free((void *)resp->body);
        return -3;
    }
    int actual_len = _bHttpBuildResponse(resp, header_buf, header_len + 1);
    if (actual_len < 0)
    {
        bFree(header_buf);
        // SRV-HTTP-6 fix: 同上.
        if (resp->body_free) resp->body_free((void *)resp->body);
        return -4;
    }

    uint16_t wlen = 0;
    int      off  = 0;

    while (off < header_len)
    {
        if (bSockIsWriteable(client_fd) == 0)
        {
            // 不可写: 非 PT 上下文不能 yield; 返回 -5 让调用方重试.
            // SRV-HTTP-6 fix (v5 review): header send 失败时如果 caller 传了
            // heap body (body_free != NULL), 必须释放避免泄漏.
            bFree(header_buf);
            if (resp->body_free) resp->body_free((void *)resp->body);
            return -5;
        }
        wlen  = 0;
        uint16_t chunk = (uint16_t)(header_len - off);
        int ret = bSend(client_fd, (uint8_t *)header_buf + off, chunk, &wlen);
        // CRIT-HTTP-1 fix: peer RST 后 bSend 返回 -1, 之前只判断 ret>0 跳过,
        // 导致 off 永远不变, 进入死循环. 现在 ret<0 时跳出并返 -7.
        // SRV-HTTP-6 fix: 同样释放 body.
        if (ret < 0)
        {
            bFree(header_buf);
            if (resp->body_free) resp->body_free((void *)resp->body);
            return -7;
        }
        if (ret > 0 && wlen > 0)
        {
            off += wlen;
        }
    }
    bFree(header_buf);

    if (resp->body && resp->body_len > 0)
    {
        int offset = 0;
        while (offset < resp->body_len)
        {
            if (bSockIsWriteable(client_fd) == 0)
            {
                // HIGH-HTTP-1 fix: 早退前也要释放 body (handler 可能传 heap 缓冲)
                if (resp->body_free) resp->body_free((void *)resp->body);
                return -6;
            }
            wlen  = 0;
            uint16_t chunk = (uint16_t)(resp->body_len - offset);
            int ret = bSend(client_fd, (uint8_t *)(resp->body + offset), chunk, &wlen);
            // CRIT-HTTP-1 fix: peer RST 后 bSend 返回 -1, 必须跳出避免死循环.
            if (ret < 0)
            {
                // HIGH-HTTP-1 fix: 同上, 早退也要释放 body.
                if (resp->body_free) resp->body_free((void *)resp->body);
                return -8;
            }
            if (ret > 0 && wlen > 0)
            {
                offset += wlen;
            }
        }
    }
    // CRIT-WEB-1 fix: handler 可能用 heap 缓冲 (per-call json_buf), 在 body
    // send 完后释放. body_free = NULL 表示 const string literal, 不释放.
    if (resp->body_free)
    {
        resp->body_free((void *)resp->body);
    }

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

/************************ Copyright (c) 2026 BabyOS Team *****END OF FILE****/
