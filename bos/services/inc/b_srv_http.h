/**
 *!
 * \file        b_srv_http.h
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
#ifndef __B_SRV_HTTP_H__
#define __B_SRV_HTTP_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"
#include "core/inc/b_task.h"
#include "modules/inc/b_mod_tcpip.h"

#if (defined(_HTTP_SERVICE_ENABLE) && (_HTTP_SERVICE_ENABLE == 1))

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
 * \defgroup HTTP_Exported_TypesDefinitions
 * \{
 */

//=========================================== 通用类型 ===========================================
/**
 * @brief HTTP请求类型
 */
typedef enum
{
    B_HTTP_GET = 0,
    B_HTTP_POST,
    B_HTTP_PUT,
    B_HTTP_DELETE,
} bHttpReqType_t;
#define HTTPREQ_TYPE_IS_VALID(t) \
    ((t) == B_HTTP_GET || (t) == B_HTTP_POST || (t) == B_HTTP_PUT || (t) == B_HTTP_DELETE)

/**
 * @brief HTTP响应信息
 *
 * CRIT-WEB-1 fix (v4 review): handler 可能把 body 写到 heap (避免并发 sub-task
 * 共享 file-static 缓冲). HTTP server 在 body send 完后必须释放该内存, 否则
 * 泄漏. 调用方约定:
 *   - body 为 const string literal (如 "{\"code\":0,...}"): body_free 必须 = NULL
 *   - body 为 heap 内存:  把 body_free 设为 bFree 函数指针 (签名: void (*)(void *))
 *
 * HTTP server (PT ctx 和非 PT ctx) 在 send 完 body 后无条件检查 resp->body_free,
 * 非 NULL 则调用. 注意: caller 调用 handler 前应当 memset(&resp, 0, ...) 保证
 * body_free 默认为 NULL, 不会被错误释放.
 */
typedef struct
{
    int         status_code;   // 状态码
    const char *content_type;  // Content-Type
    const char *body;          // 响应体
    int         body_len;      // body长度
    void      (*body_free)(void *);  // body 释放回调, NULL=无需释放 (const string literal)
} bHttpResponse_t;

//=========================================== 客户端类型 ===========================================
#if (defined(_HTTP_CLIENT_ENABLE) && (_HTTP_CLIENT_ENABLE == 1))
/**
 * @brief HTTP接收数据结构
 */
typedef struct
{
    uint8_t *pdat;
    uint16_t len;
    void (*release)(void *);
} bHttpRecvData_t;

/**
 * @brief HTTP事件类型
 */
typedef enum
{
    B_HTTP_EVENT_CONNECTED = 0,
    B_HTTP_EVENT_RECV_DATA,  // callback param : bHttpRecvData_t
    B_HTTP_EVENT_DESTROY,
    B_HTTP_EVENT_ERR_BASE = -100,
    B_HTTP_EVENT_ERROR,
    B_HTTP_EVENT_CONN_FAIL,
    B_HTTP_EVENT_SSL_FAIL,
    B_HTTP_EVENT_RECV_TIMEOUT,
} bHttpEvent_t;

/**
 * @brief HTTP客户端回调
 */
typedef void (*pHttpCb_t)(bHttpEvent_t event, void *param, void *arg);

/**
 * @brief HTTP描述符类型
 * 64位平台上必须足够大以容纳指针
 */
typedef intptr_t bHttpFd_t;
#endif

//=========================================== 服务端类型 ===========================================
#if (defined(_HTTP_SERVER_ENABLE) && (_HTTP_SERVER_ENABLE == 1))
/**
 * @brief HTTP请求信息（服务端接收）
 */
typedef struct
{
    bHttpReqType_t method;     // 请求方法
    const char    *url;        // URL路径
    const char    *body;       // 请求体
    int            body_len;   // body长度
    void          *user_data;  // 用户数据
} bHttpServerRequest_t;

/**
 * @brief HTTP服务端请求处理回调
 */
typedef void (*bHttpServerHandler_t)(bHttpServerRequest_t *req, bHttpResponse_t *resp, void *arg);
#endif

/**
 * \}
 */

/**
 * \defgroup HTTP_Exported_Functions
 * \{
 */

//=========================================== 客户端API ===========================================
#if (defined(_HTTP_CLIENT_ENABLE) && (_HTTP_CLIENT_ENABLE == 1))
/**
 * @brief 初始化HTTP客户端
 * @param cb 事件回调
 * @param user_data 用户数据
 * @return HTTP描述符，失败返回负值
 */
bHttpFd_t bHttpInit(pHttpCb_t cb, void *user_data);

/**
 * @brief 发送HTTP请求
 * @param httpfd HTTP描述符
 * @param type 请求类型 B_HTTP_GET / B_HTTP_POST / B_HTTP_PUT / B_HTTP_DELETE
 * @param url 完整URL
 * @param head 自定义头部，可为NULL
 * @param body 请求体，可为NULL
 * @return 0成功，负值失败
 */
int bHttpRequest(bHttpFd_t httpfd, bHttpReqType_t type, const char *url, const char *head,
                 const char *body);

/**
 * @brief 反初始化HTTP客户端
 * @param httpfd HTTP描述符
 * @return 0成功，负值失败
 */
int bHttpDeInit(bHttpFd_t httpfd);
#endif

//=========================================== 服务端API ===========================================
#if (defined(_HTTP_SERVER_ENABLE) && (_HTTP_SERVER_ENABLE == 1))
/**
 * @brief 启动HTTP服务器
 * @param port 监听端口
 * @param handler 请求处理回调
 * @param user_data 用户数据
 * @return 0成功，负值失败
 */
int bHttpServerStart(uint16_t port, bHttpServerHandler_t handler, void *user_data);

/**
 * @brief 停止HTTP服务器
 *
 * 本函数是非阻塞的: 它设置停止标志并关闭 listen/client socket 后立即返回.
 * 真正的 sub-task / ctx 清理由 _bHttpServerTask 在后续 bExec() 周期完成.
 * 调用方必须继续运行 bExec(), 并至少等待 [HTTP] server stopped 日志后再退出 main loop.
 */
void bHttpServerStop(void);

/**
 * @brief PT-friendly HTTP response send state. Holds the in-progress header buffer
 *        and chunk offsets across PT yields. Declare on the PT-task stack (NOT as
 *        a function-local automatic inside a non-PT helper) so it persists across
 *        PT_WAIT_UNTIL without being destroyed — see CLAUDE.md pitfall #3.
 *
 *        多个并发 PT task 各自持有一个 ctx, 不能共享 — 否则 response bytes 互相串包.
 *        Frame buffer 等内部状态共 ~80 字节 (按 _HTTP_SEND_CHUNK_SIZE 调整).
 *
 *        推荐用法:
 *          PT_THREAD(handler)(struct pt *pt, void *arg) {
 *              static bHttpSendCtx_t s_send_ctx;   // PT 栈空间持久
 *              ...
 *              PT_WAIT_THREAD(pt, bHttpSendResponsePTEx(pt, fd, &resp, &s_send_ctx));
 *          }
 */
typedef struct
{
    char  *header_buf;   // 当前响应头 malloc 缓冲 (跨 yield 持久)
    int    header_len;   // header_buf 有效字节数
    int    off;          // header 已发送 offset
    int    body_off;     // body 已发送 offset
    uint8_t timed_out;   // chunk timeout 标志, 防 PT 死循环
} bHttpSendCtx_t;

/**
 * @brief 发送HTTP响应（PT 协作版 — MCU 友好）
 *
 * 内部用 PT_WAIT_UNTIL 等到 socket 可写, 不会 busy-wait 饿死调度器.
 * 必须在 PT_BEGIN/PT_END 之间通过 PT_WAIT_THREAD 调用:
 *     PT_WAIT_THREAD(pt, bHttpSendResponsePT(pt, fd, &resp));
 *
 * 函数返回 0 表示完成 (已 PT_END), 返回非 0 表示让出等待.
 *
 * @warning 本函数内部使用 file-static 状态, **不能在多个并发 PT 任务里同时调用**
 *          (例如 HTTP server 的多个 sub-task 同时收不同 client). 并发场景请用
 *          bHttpSendResponsePTEx 给每个 task 分配独立的 bHttpSendCtx_t.
 *
 * @param pt      任务 protothread 上下文
 * @param client_fd 客户端socket
 * @param resp    响应信息
 * @return 0 完成, 非 0 让出.
 */
char bHttpSendResponsePT(struct pt *pt, int client_fd, const bHttpResponse_t *resp);

/**
 * @brief 发送HTTP响应（PT 协作版 + 独立 ctx — 多连接并发安全）
 *
 * 与 bHttpSendResponsePT 行为一致, 但响应发送状态由调用方提供的 ctx 持有.
 * 每个并发 PT task 各自持一个 bHttpSendCtx_t (推荐放在 PT-thread 的 static 或
 * task 结构体里, 跨 yield 持久), 互不干扰, 可用于 HTTP server 多 sub-task 并发.
 *
 * 用法:
 *     PT_THREAD(_bHttpHandler)(struct pt *pt, void *arg) {
 *         bHttpServerConnCtx_t *conn = (bHttpServerConnCtx_t *)arg;
 *         static bHttpSendCtx_t s_send;   // PT 栈空间, 跨 yield 持久
 *         ...
 *         PT_WAIT_THREAD(pt, bHttpSendResponsePTEx(pt, fd, &resp, &s_send));
 *     }
 *
 * @param pt      任务 protothread 上下文
 * @param client_fd 客户端socket
 * @param resp    响应信息
 * @param ctx     调用方持有的发送状态 (不可与其它并发 task 共享)
 * @return 0 完成, 非 0 让出.
 */
char bHttpSendResponsePTEx(struct pt *pt, int client_fd, const bHttpResponse_t *resp,
                           bHttpSendCtx_t *ctx);

/**
 * @brief 发送HTTP响应（阻塞版 — 仅用于非 PT 上下文，例如外部 host 测试工具）
 *
 * 内部循环里用 bTaskYield() 让出 CPU, 不再 busy-wait.
 * PT 上下文中请改用 bHttpSendResponsePT.
 *
 * @param client_fd 客户端socket
 * @param resp 响应信息
 * @return 0成功，负值失败
 */
int bHttpSendResponse(int client_fd, bHttpResponse_t *resp);

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

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2026 BabyOS Team *****END OF FILE****/
