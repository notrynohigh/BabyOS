/**
 * \file kv_main.c
 * \brief
 * \version 0.1
 * \date 2022-10-29
 * \author notrynohigh (notrynohigh@outlook.com)
 *
 * Copyright (c) 2020 by notrynohigh. All Rights Reserved.
 */
#include <time.h>

#include "../port.h"
#include "b_os.h"

#define TEST_TCP_UDP (0)
#define TEST_NTP (0)
#define TEST_HTTP_CLIENT (1)
#define TEST_HTTP_SERVER (1)

#if TEST_TCP_UDP
static bTaskAttr_t sTaskAttrTcp;
#endif

#if (TEST_HTTP_CLIENT)
static bTaskAttr_t sTaskAttrHttpClient;
#endif

#if (TEST_HTTP_SERVER)
#include "services/inc/b_srv_http.h"
#include "thirdparty/cjson/cjson.h"
static bTaskAttr_t sTaskAttrHttpServer;
#endif

const static bNetCardInfo_t bNetCardInfo[] = {
    [0] =
        {
            .dev_no    = bTESTMAC,
            .priority  = 0,
            .ignore_ip = 1,
        },
};

static void _MainMonitor()
{
    uint32_t        free_memory = 0;
    bUTC_DateTime_t tm;
    bUTC2Struct(&tm, bUTC_GetTime(), 8.0);
    free_memory = bGetFreeSize();
    b_log("[%d:%d:%d]i am alive ..%d Bytes \r\n", tm.hour, tm.minute, tm.second, free_memory);
}

#if TEST_TCP_UDP
void bTcpCallback(bTransEvent_t event, void *param, void *arg)
{
    b_log("trans event:%d param:%p arg:%p \r\n", event, param, arg);
}

PT_THREAD(bWifiTestTask666)(struct pt *pt, void *arg)
{
    static int sockfd = -1;
    B_TASK_INIT_BEGIN();
    // ...
    B_TASK_INIT_END();

    PT_BEGIN(pt);
    while (1)
    {
        sockfd = bSocket(B_TRANS_CONN_TCP, bTcpCallback, NULL);
        if (sockfd >= 0)
        {
            b_log("sockfd:%d \r\n", sockfd);
            b_log("connecting... \r\n");
            bConnect(sockfd, "192.168.3.50", 666);
            PT_WAIT_UNTIL(pt, bSocketIsConnected(sockfd), 2000);
            b_log("connected???%d\r\n", bSocketIsConnected(sockfd));
            if (bSocketIsConnected(sockfd))
            {
                bSend(sockfd, (uint8_t *)"[linux]hello world666\r\n",
                      strlen("[linux]hello world666\r\n"), NULL);
                bTaskDelayMs(pt, 5000);
            }
            bShutdown(sockfd);
            sockfd = -1;
        }
        bTaskDelayMs(pt, 5000);
    }
    PT_END(pt);
}
#endif

#if (TEST_HTTP_CLIENT)

void HttpCallback(bHttpEvent_t event, void *param, void *arg)
{
    if (event == B_HTTP_EVENT_RECV_DATA)
    {
        bHttpRecvData_t *pdata = (bHttpRecvData_t *)param;
        b_log("[HTTP] recv[%d]:%s \r\n", pdata->len, pdata->pdat);
        if (pdata->release)
        {
            pdata->release(pdata->pdat);
            pdata->pdat = NULL;
        }
    }
    else if (event < 0 || event == B_HTTP_EVENT_DESTROY)
    {
        b_log("[HTTP] event err/destroy: %d\r\n", event);
    }
    else
    {
        b_log("[HTTP] event: %d\r\n", event);
    }
}

PT_THREAD(bHttpClientTask)(struct pt *pt, void *arg)
{
    static int httpfd = -1;
    B_TASK_INIT_BEGIN();
    httpfd = bHttpInit(HttpCallback, NULL);
    B_TASK_INIT_END();

    PT_BEGIN(pt);
    while (1)
    {
        b_log("[HTTP] >>> GET https\r\n");
        bHttpRequest(httpfd, B_HTTP_GET, "https://127.0.0.1:8443/test", NULL, NULL);
        bTaskDelayMs(pt, 15000);

        b_log("[HTTP] >>> POST https\r\n");
        bHttpRequest(httpfd, B_HTTP_POST, "https://127.0.0.1:8443/test",
                     "Content-Type: application/json\r\n", "{\"name\":\"babyos\"}");
        bTaskDelayMs(pt, 15000);
    }
    PT_END(pt);
}

#endif

#if (TEST_HTTP_SERVER)

// HTTP server 端点: 全部在端口 8080 监听, 仅走明文 HTTP (当前 b_srv_http 的 server
// 实现只支持非加密, HTTPS server 还没接通 mbedTLS 路径). 端口故意跟 client 端的
// 8443 HTTPS 错开, 两个测试能并行跑互不干扰.
//
// 端点清单:
//   GET  /                  - HTML 首页
//   GET  /api/status        - 200 JSON, 服务端基本状态
//   GET  /api/info          - 200 JSON, 设备/编译信息
//   GET  /api/counter       - 200 JSON, 累计请求计数 (单实例共享)
//   POST /api/echo          - 200 JSON, 回显 method/url/body_len/body
//   GET  /api/notfound      - 演示如何回 404
//   GET  /api/crash         - 演示如何回 500
//   其他路径                - 走默认 404 (handler 不识别时)
//
// 注意:
//   - handler 是 bSocket 回调上下文里同步调用的, 不能做阻塞操作 (sleep/malloc 太久
//     都不行), 但 cJSON / bMalloc / 字符串拼接都是安全的
//   - req->body / req->url 是 server 内部 bMalloc 出来的, handler 不应释放
//   - resp->body 必须是 const 字符串, 内部用 strlen 取长度

static uint32_t s_http_server_req_count = 0;

// 把 method 枚举翻译成字符串
static const char *_HttpMethodStr(bHttpReqType_t m)
{
    switch (m)
    {
        case B_HTTP_GET:
            return "GET";
        case B_HTTP_POST:
            return "POST";
        case B_HTTP_PUT:
            return "PUT";
        case B_HTTP_DELETE:
            return "DELETE";
        default:
            return "UNKNOWN";
    }
}

// 构造一个 cJSON 响应并写入 resp
// 把 cJSON 序列化结果放进 static 缓冲, 避免 cJSON 自己的 malloc 出来的字符串
// 还得有人去 free 的问题. 单线程 PT 模型, 一次 handler 结束才进下一次, 缓冲可复用.
#define B_HTTPSRV_JSON_BUF_SIZE 8192
static char s_http_json_buf[B_HTTPSRV_JSON_BUF_SIZE];

static void _HttpJsonResp(bHttpResponse_t *resp, int status, cJSON *json)
{
    if (json == NULL)
    {
        resp->status_code  = 500;
        resp->content_type = "application/json";
        resp->body         = "{\"error\":\"json build failed\"}";
        resp->body_len     = strlen(resp->body);
        return;
    }
    if (cJSON_PrintPreallocated(json, s_http_json_buf, sizeof(s_http_json_buf), 0) == 0)
    {
        cJSON_Delete(json);
        resp->status_code  = 500;
        resp->content_type = "application/json";
        resp->body         = "{\"error\":\"json print failed\"}";
        resp->body_len     = strlen(resp->body);
        return;
    }
    cJSON_Delete(json);
    resp->status_code  = status;
    resp->content_type = "application/json";
    resp->body         = s_http_json_buf;
    resp->body_len     = strlen(s_http_json_buf);
}

static void _HttpServerHandler(bHttpServerRequest_t *req, bHttpResponse_t *resp, void *arg)
{
    s_http_server_req_count++;
    b_log("[HTTPSRV] %s %s (body_len=%d) [count=%u]\r\n", _HttpMethodStr(req->method), req->url,
          req->body_len, s_http_server_req_count);
    (void)arg;

    if (req->method == B_HTTP_GET && strcmp(req->url, "/") == 0)
    {
        // HTML 首页: 用 bHttpSendHtml 的等价路径, 但我们要自己填 status_code=200
        const char *html =
            "<!doctype html><html><head><title>BabyOS HTTP Server</title></head>"
            "<body><h1>BabyOS HTTP Server</h1>"
            "<p>It works! 你可以试试:</p>"
            "<ul>"
            "<li>GET /api/status</li>"
            "<li>GET /api/info</li>"
            "<li>GET /api/counter</li>"
            "<li>POST /api/echo  (body: hello)</li>"
            "<li>GET /api/notfound (演示 404)</li>"
            "<li>GET /api/crash (演示 500)</li>"
            "</ul></body></html>";
        resp->status_code  = 200;
        resp->content_type = "text/html; charset=utf-8";
        resp->body         = html;
        resp->body_len     = strlen(html);
        return;
    }

    if (req->method == B_HTTP_GET && strcmp(req->url, "/api/status") == 0)
    {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "status", "ok");
        cJSON_AddStringToObject(j, "service", "b_srv_http server");
        cJSON_AddNumberToObject(j, "req_count", (double)s_http_server_req_count);
        cJSON_AddNumberToObject(j, "free_mem", (double)bGetFreeSize());
        _HttpJsonResp(resp, 200, j);
        return;
    }

    if (req->method == B_HTTP_GET && strcmp(req->url, "/api/info") == 0)
    {
        bUTC_DateTime_t tm;
        bUTC2Struct(&tm, bUTC_GetTime(), 8.0);
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "fw_name", FW_NAME);
        cJSON_AddNumberToObject(j, "fw_version", FW_VERSION);
        cJSON_AddStringToObject(j, "utc",
                                "n/a");  // 时区 8h 转换出来的字符串太长, 简化为 n/a
        cJSON_AddNumberToObject(j, "free_mem", (double)bGetFreeSize());
        _HttpJsonResp(resp, 200, j);
        return;
    }

    if (req->method == B_HTTP_GET && strcmp(req->url, "/api/counter") == 0)
    {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddNumberToObject(j, "count", (double)s_http_server_req_count);
        _HttpJsonResp(resp, 200, j);
        return;
    }

    if (req->method == B_HTTP_POST && strcmp(req->url, "/api/echo") == 0)
    {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "method", _HttpMethodStr(req->method));
        cJSON_AddStringToObject(j, "url", req->url);
        cJSON_AddNumberToObject(j, "body_len", (double)req->body_len);
        if (req->body && req->body_len > 0)
        {
            // body 可能不是 null 结尾, 用临时缓冲复制一次
            char *copy = (char *)bMalloc(req->body_len + 1);
            if (copy)
            {
                memcpy(copy, req->body, req->body_len);
                copy[req->body_len] = '\0';
                cJSON_AddStringToObject(j, "body", copy);
                bFree(copy);
            }
            else
            {
                cJSON_AddNullToObject(j, "body");
            }
        }
        else
        {
            cJSON_AddNullToObject(j, "body");
        }
        _HttpJsonResp(resp, 200, j);
        return;
    }

    if (req->method == B_HTTP_GET && strcmp(req->url, "/api/notfound") == 0)
    {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "error", "not_found");
        cJSON_AddStringToObject(j, "url", req->url);
        _HttpJsonResp(resp, 404, j);
        return;
    }

    if (req->method == B_HTTP_GET && strcmp(req->url, "/api/crash") == 0)
    {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "error", "internal_error");
        _HttpJsonResp(resp, 500, j);
        return;
    }

    // 默认: 404
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "error", "path_not_handled");
    cJSON_AddStringToObject(j, "method", _HttpMethodStr(req->method));
    cJSON_AddStringToObject(j, "url", req->url);
    _HttpJsonResp(resp, 404, j);
}

PT_THREAD(bHttpServerTask)(struct pt *pt, void *arg)
{
    (void)arg;
    PT_BEGIN(pt);

    // 启动监听: 端口 8080, 跟 client HTTPS 8443 错开
    if (bHttpServerStart(8080, _HttpServerHandler, NULL) < 0)
    {
        b_log_e("[HTTPSRV] start failed\r\n");
        PT_EXIT(pt);
    }
    b_log("[HTTPSRV] listening on port 8080\r\n");

    // server 在自己 task 里跑, 这里只需要保持 task 不退出
    while (1)
    {
        bTaskDelayMs(pt, 60000);
    }

    PT_END(pt);
}

#endif

int main()
{
    port_init();
    bInit();

    bTcpipSrvInit(&bNetCardInfo[0], 1);

#if TEST_NTP
    bSntpStart(300);
#endif

#if TEST_TCP_UDP
    bTaskCreate("tcp", bWifiTestTask666, NULL, &sTaskAttrTcp);
#endif

#if (TEST_HTTP_CLIENT)
    bTaskCreate("httpclient", bHttpClientTask, NULL, &sTaskAttrHttpClient);
#endif

#if (TEST_HTTP_SERVER)
    bTaskCreate("httpsrv", bHttpServerTask, NULL, &sTaskAttrHttpServer);
#endif

    while (1)
    {
        bExec();
        BOS_PERIODIC_TASK(_MainMonitor, 5000);
    }
    return 0;
}
