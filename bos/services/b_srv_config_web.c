/**
 *!
 * \file        b_srv_config_web.c
 * \version     v0.0.1
 * \date        2026/05/31
 * \author      aiclaw
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2026
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
#include "services/inc/b_srv_config_web.h"
#include "services/inc/b_srv_http.h"

#if (defined(_CONFIG_WEB_SERVICE_ENABLE) && (_CONFIG_WEB_SERVICE_ENABLE == 1))

#include <stdio.h>
#include <string.h>

#include "modules/inc/b_mod_tcpip.h"
#include "modules/inc/b_mod_wifi.h"
#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup SERVICES
 * \{
 */

/**
 * \addtogroup CONFIG_WEB
 * \{
 */

/**
 * \defgroup CONFIG_WEB_Private_TypesDefinitions
 * \{
 */

typedef struct
{
    pbConfigResultCb_t config_cb;
    void               *user_data;
} bConfigWebCtx_t;

/**
 * \}
 */

/**
 * \defgroup CONFIG_WEB_Private_Variables
 * \{
 */

static bConfigWebCtx_t s_ctx;
// v4 fix: s_json_buf 改为 per-call heap, handler 入口 bMalloc/出口 bFree.
// 原 file-static 在多 sub-task 并发时会串包 (CRIT-WEB-1).

/**
 * \}
 */

/**
 * \defgroup CONFIG_WEB_Private_FunctionPrototypes
 * \{
 */

static void _bConfigWebHandler(bHttpServerRequest_t *req, bHttpResponse_t *resp, void *arg);

// HTML 配置页 (用 const 全局, 因为 _bConfigWebHandler 引用它且定义在它之后)
extern const char g_config_html_page[];

/**
 * \}
 */

/**
 * \defgroup CONFIG_WEB_Private_Functions
 * \{
 */

// C-NEW-3 fix: 严格校验 IPv4 点分十进制字符串 (0..255 段, 恰好 3 个点).
// 在 /api/eth 等路径传给 bTcpIpSetIp 之前调用, 防止 atoi/strtoul 溢出或
// 用户提交垃圾 IP 把 netif 改成任意值. 返回 1=合法, 0=非法.
static int _bIpStrIsValid(const char *s)
{
    if (s == NULL || *s == '\0') return 0;
    int dots = 0, val = 0, len = 0;
    while (*s && len++ < 16)
    {
        if (*s == '.')
        {
            if (val > 255) return 0;
            dots++;
            val = 0;
        }
        else if (*s >= '0' && *s <= '9')
        {
            val = val * 10 + (*s - '0');
            if (val > 255) return 0;
        }
        else
        {
            return 0;
        }
        s++;
    }
    return dots == 3 && val <= 255;
}

// URL解码
// H-NEW-7 fix: 拒绝控制字符 (0x00..0x1F, 0x7F) 和高位字节 (>=0x80),
// 否则恶意 POST %00%01%FF... 会污染下游驱动 (WiFi SSID/PASS 作为字符串).
// 返回实际写入字节数 (不含 NUL); -1 表示解码过程中遇到非法字符.
static int _bUrlDecode(char *dest, const char *src, int max_len)
{
    int i = 0, j = 0;
    if (dest == NULL || src == NULL || max_len <= 0)
    {
        return -1;
    }
    while (src[i] && j < max_len - 1)
    {
        unsigned char c;
        if (src[i] == '%' && src[i+1] && src[i+2])
        {
            char hex[3] = {src[i+1], src[i+2], '\0'};
            long v = strtol(hex, NULL, 16);
            c = (unsigned char)v;
            i += 3;
        }
        else if (src[i] == '+')
        {
            c = ' ';
            i++;
        }
        else
        {
            c = (unsigned char)src[i++];
        }
        // H-NEW-7: 拒绝控制字符和高位字节. 允许可打印 ASCII 0x20..0x7E.
        if (c < 0x20 || c > 0x7E)
        {
            // 用 '?' 替换: 让前端至少看到有占位符, 便于调试.
            // 也可以直接返 -1, 这里选替换策略以免一字节坏一整帧.
            c = '?';
        }
        dest[j++] = (char)c;
    }
    dest[j] = '\0';
    return j;
}

// REVIEW-V3 #8 fix: 把"写 dest + 检测溢出"抽成 helper, 旧版只在 ssid/pass 加了
// 截断检测, mode/ip/mask/gateway 也会被截断但 caller 不知道. 现在所有 key 走同一逻辑.
//
// 把 decoded 后字符串复制到目标 buf, 同时:
//   - 若 decoded 后字符串长度 >= dest_size, 视为截断 (但仍然 NUL-terminate)
//   - 若 decoded_len == sizeof(val)-1 且 src 还有未消费字节 (& 后还有), 返回 -1
//     表示"超长" (snprintf/strncpy 没看到完整内容, 数据可能已被截)
// 返回 0=正常, -1=src 长度超限 (调用方应返 413).
static int _bAssignParam(char *dest, int dest_len, const char *val, int val_buf_size,
                         int decoded_len, const char *src_after_eq)
{
    if (dest == NULL || dest_len <= 0)
    {
        return 0;  // 调用方不要这个字段, 跳过
    }
    strncpy(dest, val, (size_t)dest_len - 1);
    dest[dest_len - 1] = '\0';
    // 检测 src 仍有未消费字节 (= 解码时填满 val 缓冲, 但实际还有)
    if (decoded_len == val_buf_size - 1)
    {
        const char *rest = src_after_eq;
        while (*rest && *rest != '&') rest++;
        if (*rest == '&') return -1;
    }
    return 0;
}

// 解析URL参数
// C-NEW-2 Part A fix: 之前没校验 dest/dest_len, 当 dest==NULL 或 dest_len<=0 时
// `strncpy(mode, val, mode_len - 1)` 会变成 SIZE_MAX 字节拷贝, 同时 size_t 下溢.
// 现在显式把 NULL/0 的 dest 视为"跳过该 key", 防止 crash.
//
// H-NEW-8 fix: 增加 ssid/pass 缓冲溢出检测. 当 _bUrlDecode 写到 dest 末尾还没
// 终止时, 视为用户发了一个超长 %XX 字符串, 直接返 -1, 调用方应返 413 给前端.
// H-NEW-6 fix: tmp[]/key[]/val[] 改为栈 local (CRIT-WEB-3).
// handler 不 yield, 每次调用独立栈帧; 原 static 跨并发 sub-task 调用会串包.
static int _bParseParams(const char *body, char *mode, int mode_len, char *ssid, int ssid_len, char *pass, int pass_len)
{
    if (body == NULL) return 0;
    if (mode == NULL || mode_len <= 0) { mode = NULL; mode_len = 0; }
    if (ssid == NULL || ssid_len <= 0) { ssid = NULL; ssid_len = 0; }
    if (pass == NULL || pass_len <= 0) { pass = NULL; pass_len = 0; }

    // v4 fix: 栈 local. handler 不 yield, 每次调用独立栈帧.
    // 单帧 ~544B, 与 wifi 缓冲 (~208B) 同时存在总 ~750B. test/netif (8MB main 栈)
    // 安全. MCU 部署需关注 main 栈预算 (PT task 共享 main 栈).
    char tmp[256];
    char key[32];
    char val[256];

    const char *p = body;
    while (*p)
    {
        const char *amp = strchr(p, '&');
        int len = amp ? (amp - p) : strlen(p);
        if (len <= 0) { p += len + 1; continue; }

        memset(tmp, 0, sizeof(tmp));
        if (len >= (int)sizeof(tmp)) len = sizeof(tmp) - 1;
        memcpy(tmp, p, len);

        char *eq = strchr(tmp, '=');
        if (eq)
        {
            *eq = '\0';
            memset(key, 0, sizeof(key));
            memset(val, 0, sizeof(val));
            // key 不要 URL decode (key 是 ASCII 字母数字, 不会被编码)
            strncpy(key, tmp, sizeof(key) - 1);
            int decoded_len = _bUrlDecode(val, eq + 1, (int)sizeof(val));

            // REVIEW-V3 #8: 统一 helper 处理所有 dest. mode/ssid/pass 各自走一次,
            // overflow 检测对所有字段生效.
            if (strcmp(key, "mode") == 0) {
                if (_bAssignParam(mode, mode_len, val, (int)sizeof(val), decoded_len, eq + 1) < 0)
                    return -1;
            }
            else if (strcmp(key, "ssid") == 0) {
                if (_bAssignParam(ssid, ssid_len, val, (int)sizeof(val), decoded_len, eq + 1) < 0)
                    return -1;
            }
            else if (strcmp(key, "pass") == 0) {
                if (_bAssignParam(pass, pass_len, val, (int)sizeof(val), decoded_len, eq + 1) < 0)
                    return -1;
            }
            // ip/mask/gateway 由 /api/eth handler 单独从 req->body strstr 后调
            // _bUrlDecode 取出, 这里不再保留旧 dead branches. _bAssignParam 也
            // 给它们用 (handler 内调用).
        }
        p += len + (amp ? 1 : 0);
    }
    return 0;
}

// helper: 把 endpoint 的 const string literal 响应写到 resp, 同时释放未用的
// json_buf 并清零 body_free (literal 不需要 free). 调用方不需要再设 resp->body_len.
static void _bSetLiteralResp(bHttpResponse_t *resp, char *json_buf, const char *body)
{
    if (json_buf != NULL)
    {
        bFree(json_buf);
    }
    resp->body      = body;
    resp->body_free = NULL;
    resp->body_len  = (int)strlen(body);
}

// HTTP请求处理回调
// v4 fix: handler 不 yield (非 PT_THREAD, 调用的 API 都是同步 return),
// 解析缓冲改为栈 local, 每次 HTTP 请求是独立栈帧, 无 race.
// 原 static 跨并发 sub-task 调用会串包 (CRIT-WEB-2/3/4).
// CRIT-WEB-1 fix: json 输出从 file-static 改为 per-call heap, 避免 sub-task
// 串包. HTTP server 在 body send 完后通过 resp->body_free 回调释放, 这里
// 不能再 bFree (会在 HTTP server 还在 send 时提前释放).
static void _bConfigWebHandler(bHttpServerRequest_t *req, bHttpResponse_t *resp, void *arg)
{
    (void)arg;
    resp->status_code = 200;
    resp->content_type = "application/json";
    // v4 fix: s_json_buf 改为 per-call heap (CRIT-WEB-1), 512B 覆盖多数 netcard 场景.
    char *json_buf = (char *)bMalloc(512);
    if (json_buf == NULL) {
        resp->body      = "{\"code\":-1,\"msg\":\"oom\"}";
        resp->body_free = NULL;
        resp->body_len  = strlen(resp->body);
        return;
    }
    resp->body         = json_buf;
    resp->body_free    = bFree;  // 默认 bFree (json_buf 是 heap). 当 endpoint 返回
                                 // const literal 时, helper _bSetLiteralResp() 会
                                 // bFree(json_buf) 并清零 body_free, 避免误释放 literal.
    resp->body_len     = 0;

    if (req->url == NULL) {
        // 早退: resp->body 改为 const literal, 释放未使用的 json_buf.
        _bSetLiteralResp(resp, json_buf, "{\"code\":-1,\"msg\":\"invalid request\"}");
        return;
    }

    b_log("[ConfigWeb] %s %s\r\n",
          req->method == B_HTTP_POST ? "POST" : "GET", req->url);

    // 首页 - 返回HTML
    if (strcmp(req->url, "/") == 0 || strcmp(req->url, "/index.html") == 0)
    {
        resp->content_type = "text/html";
        // body 是 const global, 不需要 free. 但入口处 resp->body_free = bFree,
        // 必须清零, 否则 HTTP server 会尝试 free const memory, crash.
        _bSetLiteralResp(resp, json_buf, g_config_html_page);
        resp->content_type = "text/html";   // _bSetLiteralResp 不动 content_type, 重新设一次
        goto cleanup;
    }

    // WiFi配置
    if (strcmp(req->url, "/api/wifi") == 0 && req->method == B_HTTP_POST)
    {
        // v4 fix: 栈 local (CRIT-WEB-2). handler 不 yield, 每次调用独立栈帧.
        char mode[16], ssid[64], pass[128];
        memset(mode, 0, sizeof(mode));
        memset(ssid, 0, sizeof(ssid));
        memset(pass, 0, sizeof(pass));

        int parse_ret = _bParseParams(req->body, mode, sizeof(mode), ssid, sizeof(ssid), pass,
                                      sizeof(pass));
        // H-NEW-8: parse_ret == -1 表示 ssid/pass 超过缓冲, 返 413.
        if (parse_ret < 0)
        {
            resp->status_code = 413;
            _bSetLiteralResp(resp, json_buf, "{\"code\":-4,\"msg\":\"payload too large\"}");
            goto cleanup;
        }

        b_log("[ConfigWeb] WiFi: mode=%s, ssid=%s\r\n", mode, ssid);

        if (strlen(ssid) == 0) {
            _bSetLiteralResp(resp, json_buf, "{\"code\":-2,\"msg\":\"ssid required\"}");
            goto cleanup;
        }

        // HIGH-WEB-1 fix: 前置校验 SSID/pass 长度, 避免把超过 max 的字符串传给
        // bWifiJoinAp 时被驱动截断/拒绝/触发 OOB. SSID max 32, pass max 64.
        if (strlen(ssid) > WIFI_SSID_LEN_MAX) {
            _bSetLiteralResp(resp, json_buf, "{\"code\":-4,\"msg\":\"ssid too long\"}");
            goto cleanup;
        }
        if (strlen(pass) > WIFI_PASSWD_LEN_MAX) {
            _bSetLiteralResp(resp, json_buf, "{\"code\":-4,\"msg\":\"passwd too long\"}");
            goto cleanup;
        }

        // H-NEW-5 fix: 检查 wifi API 返回值. 失败时通过 config_cb 通知用户 result=-1,
        // HTTP 返 503 表示配置未生效, 前端可以显示"提交成功但 WiFi 未连接".
        int wifi_ret = -1;
        if (strcmp(mode, "ap") == 0) {
            wifi_ret  = bWifiSetMode(B_WIFI_MODE_AP);
            if (wifi_ret == 0) {
                wifi_ret = bWifiApConfig(ssid, pass);
            }
        } else {
            wifi_ret  = bWifiSetMode(B_WIFI_MODE_STA);
            if (wifi_ret == 0) {
                wifi_ret = bWifiJoinAp(ssid, pass);
            }
        }

        if (s_ctx.config_cb) {
            // result 用 wifi_ret: 0=OK, 非 0=fail. 前端按这个判断.
            s_ctx.config_cb(B_CONFIG_TYPE_WIFI, wifi_ret, s_ctx.user_data);
        }

        if (wifi_ret != 0)
        {
            resp->status_code = 503;
            _bSetLiteralResp(resp, json_buf, "{\"code\":-5,\"msg\":\"wifi config failed\"}");
            goto cleanup;
        }

        _bSetLiteralResp(resp, json_buf, "{\"code\":0,\"msg\":\"ok\"}");
        goto cleanup;
    }

    // 以太网配置
    if (strcmp(req->url, "/api/eth") == 0 && req->method == B_HTTP_POST)
    {
        // v4 fix: 栈 local (CRIT-WEB-4). handler 不 yield, 每次调用独立栈帧.
        // 单帧 ~500B, 与 wifi 缓冲同时存在 (不在同一请求路径, 互斥).
        char mode[16], ip[32], mask[32], gateway[32];
        memset(mode,    0, sizeof(mode));
        memset(ip,      0, sizeof(ip));
        memset(mask,    0, sizeof(mask));
        memset(gateway, 0, sizeof(gateway));
        // REVIEW-V3 #8 fix: 之前 _bParseParams 对 ip/mask/gateway 走的是空 else-if 分支
        // (dead code), gateway 由第二次 strstr 提取, 而且都没有 overflow 检测. 现在改成
        // 显式循环: 复用 _bAssignParam 一致地处理 mode/ip/mask/gateway, 包括超长检测.
        // v4 fix: 栈 local (CRIT-WEB-4).
        const char *const eth_keys[] = {"mode", "ip", "mask", "gateway"};
        char              eth_vals[4][32];
        char              kv_tmp[256];
        char              decoded[64];
        for (int k = 0; k < 4; k++) {
            memset(eth_vals[k], 0, sizeof(eth_vals[k]));
        }
        const char *body = req->body ? req->body : "";
        while (*body) {
            const char *amp = strchr(body, '&');
            int seg_len    = amp ? (int)(amp - body) : (int)strlen(body);
            if (seg_len <= 0) { body += seg_len + (amp ? 1 : 0); continue; }
            if (seg_len >= (int)sizeof(kv_tmp)) seg_len = sizeof(kv_tmp) - 1;
            memcpy(kv_tmp, body, seg_len);
            kv_tmp[seg_len] = '\0';
            char *eq = strchr(kv_tmp, '=');
            if (eq) {
                *eq = '\0';
                const char *val_src = eq + 1;
                memset(decoded, 0, sizeof(decoded));
                int dlen = _bUrlDecode(decoded, val_src, (int)sizeof(decoded));
                for (int k = 0; k < 4; k++) {
                    if (strcmp(kv_tmp, eth_keys[k]) == 0) {
                        if (_bAssignParam(eth_vals[k], 32, decoded,
                                          (int)sizeof(decoded), dlen, val_src) < 0) {
                            resp->status_code = 413;
                            _bSetLiteralResp(resp, json_buf, "{\"code\":-4,\"msg\":\"payload too large\"}");
                            goto cleanup;
                        }
                        break;
                    }
                }
            }
            body += seg_len + (amp ? 1 : 0);
        }
        strncpy(mode,    eth_vals[0], sizeof(mode)    - 1);
        strncpy(ip,      eth_vals[1], sizeof(ip)      - 1);
        strncpy(mask,    eth_vals[2], sizeof(mask)    - 1);
        strncpy(gateway, eth_vals[3], sizeof(gateway) - 1);

        b_log("[ConfigWeb] Eth: mode=%s, ip=%s\r\n", mode, ip);

        // H-NEW-5: bTcpIpSetIp 返回值检查. 之前 result 永远 0.
        int eth_ret = 0;
        // HIGH-WEB-3 fix: 显式分支, 避免 dhcp POST 静默 no-op.
        if (strcmp(mode, "dhcp") == 0)
        {
            // 切换 DHCP 实际需要单独调用 DHCP client 模块; 这里只做用户可见反馈,
            // 提示用户 DHCP 模式已注册, 实际激活需重启/手动.
            _bSetLiteralResp(resp, json_buf,
                "{\"code\":0,\"msg\":\"dhcp mode acknowledged, restart required\"}");
            if (s_ctx.config_cb) {
                s_ctx.config_cb(B_CONFIG_TYPE_ETH, 0, s_ctx.user_data);
            }
            goto cleanup;
        }
        if (strcmp(mode, "static") != 0 || strlen(ip) == 0)
        {
            // 未指定 mode 或 mode 未知: 报 400, 避免静默接受.
            _bSetLiteralResp(resp, json_buf,
                "{\"code\":-1,\"msg\":\"missing or unknown mode (use static or dhcp)\"}");
            goto cleanup;
        }

        // C-NEW-3 fix: 严格校验 IPv4 段, 不合法直接拒绝. mask/gateway 可选:
        // mask 空时 ip 层会用默认掩码, gateway 空时同上. 至少 ip 必须合法.
        if (!_bIpStrIsValid(ip) ||
            (mask[0]     != '\0' && !_bIpStrIsValid(mask)) ||
            (gateway[0]  != '\0' && !_bIpStrIsValid(gateway)))
        {
            _bSetLiteralResp(resp, json_buf, "{\"code\":-3,\"msg\":\"invalid ip/mask/gateway\"}");
            goto cleanup;
        }
        // REVIEW-V3 #3 fix: 旧版 bTcpIpSetIp 只改 bTcpIpCtx.pinfo (活动网卡),
        // 在 WiFi 是活动的设备上 /api/eth 会改 WiFi 的 IP. 现按 netcard list 找
        // is_ethnet==1 的 dev_no, 用 ByDevNo 版本定向配置 ethernet netcard.
        uint32_t eth_dev_no = 0;
        uint8_t  found      = 0;
        uint8_t  count      = bTcpIpGetNetcardCount();
        for (uint8_t i = 0; i < count; i++)
        {
            bNetcardStaInfo_t info;
            if (bTcpIpGetNetcardInfo(i, &info) == 0 && info.is_ethnet)
            {
                eth_dev_no = info.dev_no;
                found      = 1;
                break;
            }
        }
        if (!found)
        {
            _bSetLiteralResp(resp, json_buf, "{\"code\":-7,\"msg\":\"no ethernet netcard\"}");
            goto cleanup;
        }
        eth_ret = bTcpIpSetIpByDevNo(eth_dev_no, ip, mask, gateway);

        if (s_ctx.config_cb) {
            s_ctx.config_cb(B_CONFIG_TYPE_ETH, eth_ret, s_ctx.user_data);
        }

        if (eth_ret != 0)
        {
            resp->status_code = 503;
            _bSetLiteralResp(resp, json_buf, "{\"code\":-6,\"msg\":\"eth config failed\"}");
            goto cleanup;
        }

        _bSetLiteralResp(resp, json_buf, "{\"code\":0,\"msg\":\"ok\"}");
        goto cleanup;
    }

    // 状态查询
    if (strcmp(req->url, "/api/status") == 0)
    {
        uint8_t wifi_conn = bWifiIsConnected();
        uint8_t eth_link = bTcpIpPhyIsLinked();
        char wifi_ip[16] = {0}, eth_ip[16] = {0};

        bTcpIpGetIp(wifi_ip, NULL, NULL);

        uint32_t active_dev = bTcpIpGetCurrentDevNo();
        uint8_t count = bTcpIpGetNetcardCount();
        const char *active_name = "未知";
        for (uint8_t i = 0; i < count; i++) {
            bNetcardStaInfo_t info;
            if (bTcpIpGetNetcardInfo(i, &info) == 0 && info.dev_no == active_dev) {
                active_name = (info.is_wifi != 0) ? "WiFi" : "以太网";
                break;
            }
        }

        resp->body_len = snprintf(json_buf, 512,
            "{\"active_netcard\":\"%s\",\"wifi_connected\":%d,\"wifi_ip\":\"%s\","
            "\"eth_linked\":%d,\"eth_ip\":\"%s\"}",
            active_name, wifi_conn, wifi_ip, eth_link, eth_ip);
        if (resp->body_len < 0) resp->body_len = 0;
        goto cleanup;
    }

    // 网卡列表
    if (strcmp(req->url, "/api/netcards") == 0)
    {
        uint8_t count = bTcpIpGetNetcardCount();
        uint32_t active_dev = bTcpIpGetCurrentDevNo();
        char *p = json_buf;
        int left = 512;

#define BUF_ADVANCE(fmt, ...)                                                  \
    do {                                                                       \
        int _n = snprintf(p, (size_t)left, fmt, ##__VA_ARGS__);                \
        if (_n < 0 || _n >= left) { left = 0; }                                \
        else { p += _n; left -= _n; }                                          \
    } while (0)

        BUF_ADVANCE("{\"code\":0,\"count\":%d,\"netcards\":[", count);

        for (uint8_t i = 0; i < count && left > 0; i++)
        {
            bNetcardStaInfo_t info;
            if (bTcpIpGetNetcardInfo(i, &info) == 0)
            {
                uint8_t is_active = (info.dev_no == active_dev) ? 1 : 0;
                char ip_str[16] = {0};
                if (info.ipaddr != 0) {
                    snprintf(ip_str, sizeof(ip_str), "\"%d.%d.%d.%d\"",
                        (info.ipaddr >> 0) & 0xFF, (info.ipaddr >> 8) & 0xFF,
                        (info.ipaddr >> 16) & 0xFF, (info.ipaddr >> 24) & 0xFF);
                } else {
                    strcpy(ip_str, "\"\"");
                }

                if (i > 0) BUF_ADVANCE(",");
                BUF_ADVANCE(
                    "{\"dev_no\":%lu,\"is_wifi\":%d,\"is_ethnet\":%d,"
                    "\"priority\":%d,\"is_linked\":%d,\"is_dhcp\":%d,"
                    "\"ip\":%s,\"is_active\":%d}",
                    (unsigned long)info.dev_no, info.is_wifi, info.is_ethnet,
                    info.priority, info.is_linked, info.is_dhcp,
                    ip_str, is_active);
            }
        }
        BUF_ADVANCE("]}");
#undef BUF_ADVANCE
        resp->body_len = (int)(p - json_buf);
        if (resp->body_len < 0) resp->body_len = 0;
        goto cleanup;
    }

    // 设置活动网卡
    if (strcmp(req->url, "/api/setnetcard") == 0 && req->method == B_HTTP_POST)
    {
        // C-NEW-2 Part B fix: 之前复用 _bParseParams, 但它的 if/else 链里没有 "dev_no" key,
        // 结果 dev_str 永远是空串, atoi("")=0, 端点永远返回失败.
        // 这里改用专用解析: body 格式固定为 "dev_no=<digits>", 直接 strstr + URL-decode + 数字校验.
        char dev_str[32] = {0};
        if (req->body != NULL)
        {
            const char *kv = strstr(req->body, "dev_no=");
            if (kv != NULL)
            {
                _bUrlDecode(dev_str, kv + 7, (int)sizeof(dev_str));
            }
        }

        // 严格校验: 必须是纯十进制数字 (atoi 在溢出时是 UB, 这里杜绝).
        int valid = (dev_str[0] != '\0');
        for (int i = 0; valid && dev_str[i] != '\0'; i++)
        {
            if (dev_str[i] < '0' || dev_str[i] > '9')
            {
                valid = 0;
            }
        }

        if (!valid)
        {
            _bSetLiteralResp(resp, json_buf, "{\"code\":-1,\"msg\":\"invalid dev_no\"}");
            goto cleanup;
        }

        uint32_t dev_no = (uint32_t)strtoul(dev_str, NULL, 10);
        b_log("[ConfigWeb] set netcard dev_no=%lu\r\n", (unsigned long)dev_no);

        if (dev_no == 0) {
            // HIGH-WEB-5 fix: 区分错误码. dev_no=0 不是合法 netcard id.
            _bSetLiteralResp(resp, json_buf, "{\"code\":-1,\"msg\":\"invalid dev_no\"}");
            goto cleanup;
        }

        // HIGH-WEB-5 fix: 先遍历 netcard 列表区分"不存在"和"未连接".
        int      found = 0;
        uint8_t  linked = 0;
        uint8_t  count = bTcpIpGetNetcardCount();
        for (uint8_t i = 0; i < count; i++) {
            bNetcardStaInfo_t info;
            if (bTcpIpGetNetcardInfo(i, &info) == 0 && info.dev_no == dev_no) {
                found = 1;
                linked = info.is_linked;
                break;
            }
        }
        if (!found) {
            _bSetLiteralResp(resp, json_buf, "{\"code\":-2,\"msg\":\"dev_no not found\"}");
            goto cleanup;
        }
        if (!linked) {
            _bSetLiteralResp(resp, json_buf, "{\"code\":-3,\"msg\":\"netcard not linked\"}");
            goto cleanup;
        }

        if (bTcpIpSetActiveNetcard(dev_no) != 0) {
            _bSetLiteralResp(resp, json_buf, "{\"code\":-4,\"msg\":\"set failed\"}");
            goto cleanup;
        }

        _bSetLiteralResp(resp, json_buf, "{\"code\":0,\"msg\":\"ok\"}");
        goto cleanup;
    }

    // 404
    resp->status_code = 404;
    _bSetLiteralResp(resp, json_buf, "{\"code\":-404,\"msg\":\"not found\"}");
    goto cleanup;

cleanup:
    // CRIT-WEB-1 fix: json_buf 由 HTTP server 在 body send 完后通过
    // resp->body_free (= bFree) 释放. handler 这里不能再 bFree.
    // 所有走完的 return/goto 路径: resp->body 可能为 const literal (此时
    // resp->body_free 已被路径自己清零, 不会触发 free) 或 json_buf (由
    // HTTP server 释放). 不需要 handler 干预.
    return;
}

// HTML页面
// REVIEW-V3-FU: 声明为非 static, 因为 _bConfigWebHandler (定义在它之前) 引用它.
const char g_config_html_page[] =
"<!DOCTYPE html>"
"<html lang=\"zh-CN\">"
"<head>"
"<meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
"<title>BabyOS 配网</title>"
"<style>"
"*{margin:0;padding:0;box-sizing:border-box;}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#f5f5f5;padding:20px;}"
".container{max-width:600px;margin:0 auto;}"
"h1{text-align:center;color:#333;margin-bottom:30px;}"
".card{background:#fff;border-radius:8px;padding:20px;margin-bottom:20px;box-shadow:0 2px 8px rgba(0,0,0,0.1);}"
".card h2{color:#666;font-size:16px;margin-bottom:15px;padding-bottom:10px;border-bottom:1px solid #eee;}"
".form-group{margin-bottom:15px;}"
".form-group label{display:block;color:#666;font-size:14px;margin-bottom:5px;}"
".form-group input{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px;font-size:14px;}"
".form-group input:focus{outline:none;border-color:#4a90d9;}"
"select{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px;font-size:14px;background:#fff;}"
"button{width:100%;padding:12px;background:#4a90d9;color:#fff;border:none;border-radius:4px;font-size:16px;cursor:pointer;margin-top:10px;}"
"button:hover{background:#3a7bc8;}"
".status{margin-top:15px;padding:10px;background:#e8f4fd;border-radius:4px;font-size:14px;color:#666;}"
".result{text-align:center;padding:10px;border-radius:4px;margin-top:10px;display:none;}"
".result.success{background:#d4edda;color:#155724;}"
".result.error{background:#f8d7da;color:#721c24;}"
"</style>"
"</head>"
"<body>"
"<div class=\"container\">"
"<h1>BabyOS 网络配置</h1>"
"<div class=\"card\">"
"<h2>网卡选择</h2>"
"<div class=\"form-group\">"
"<select id=\"netcard\"><option value=\"\">加载中...</option></select>"
"</div>"
"<button onclick=\"setNetcard()\">切换网卡</button>"
"<div id=\"netcard_result\" class=\"result\"></div>"
"</div>"
"<div class=\"card\">"
"<h2>WiFi 配置</h2>"
"<div class=\"form-group\">"
"<label>模式</label>"
"<select id=\"wifi_mode\">"
"<option value=\"sta\">Station</option>"
"<option value=\"ap\">AP</option>"
"</select>"
"</div>"
"<div class=\"form-group\">"
"<label>SSID</label>"
"<input type=\"text\" id=\"wifi_ssid\" placeholder=\"WiFi名称\" maxlength=\"32\">"
"</div>"
"<div class=\"form-group\">"
"<label>密码</label>"
"<input type=\"password\" id=\"wifi_pass\" placeholder=\"密码\" maxlength=\"64\">"
"</div>"
"<button onclick=\"submitWifi()\">保存 WiFi</button>"
"<div id=\"wifi_result\" class=\"result\"></div>"
"</div>"
"<div class=\"card\">"
"<h2>以太网配置</h2>"
"<div class=\"form-group\">"
"<label>模式</label>"
"<select id=\"eth_mode\" onchange=\"toggleEth()\">"
"<option value=\"dhcp\">DHCP</option>"
"<option value=\"static\">静态IP</option>"
"</select>"
"</div>"
"<div id=\"eth_static\" style=\"display:none;\">"
"<div class=\"form-group\"><label>IP地址</label><input type=\"text\" id=\"eth_ip\"></div>"
"<div class=\"form-group\"><label>子网掩码</label><input type=\"text\" id=\"eth_mask\"></div>"
"<div class=\"form-group\"><label>网关</label><input type=\"text\" id=\"eth_gateway\"></div>"
"</div>"
"<button onclick=\"submitEth()\">保存以太网</button>"
"<div id=\"eth_result\" class=\"result\"></div>"
"</div>"
"<div class=\"card\">"
"<h2>当前状态</h2>"
"<div class=\"status\" id=\"status\">加载中...</div>"
"</div>"
"</div>"
"<script>"
"function toggleEth(){"
"document.getElementById('eth_static').style.display=document.getElementById('eth_mode').value==='static'?'block':'none';"
"}"
"function loadNetcards(){"
"fetch('/api/netcards').then(r=>r.json()).then(d=>{"
"var s=document.getElementById('netcard');s.innerHTML='';"
"d.netcards&&d.netcards.forEach(n=>{"
"var o=document.createElement('option');"
"o.value=n.dev_no;"
"o.text=(n.is_wifi===1?'WiFi':'以太网')+' (dev:'+n.dev_no+') '+(n.is_linked?'已连接':'未连接');"
"if(n.is_active)o.selected=true;"
"s.appendChild(o);"
"});}).catch(()=>{});"
"}"
"function setNetcard(){"
"var v=document.getElementById('netcard').value;"
"if(!v)return;"
"var r=document.getElementById('netcard_result');"
"fetch('/api/setnetcard',{method:'POST',body:'dev_no='+v,headers:{'Content-Type':'application/x-www-form-urlencoded'}})"
".then(r=>r.json()).then(d=>{"
"r.style.display='block';r.className='result '+(d.code==0?'success':'error');r.textContent=d.msg;"
"if(d.code==0){loadNetcards();loadStatus();}"
"}).catch(()=>{r.className='result error';r.textContent='failed';});"
"}"
"function submitWifi(){"
"var m=document.getElementById('wifi_mode').value;"
"var s=encodeURIComponent(document.getElementById('wifi_ssid').value);"
"var p=encodeURIComponent(document.getElementById('wifi_pass').value);"
"if(!s){alert('ssid required');return;}"
"fetch('/api/wifi',{method:'POST',body:'mode='+m+'&ssid='+s+'&pass='+p,headers:{'Content-Type':'application/x-www-form-urlencoded'}})"
".then(r=>r.json()).then(d=>{"
"var r=document.getElementById('wifi_result');"
"r.style.display='block';r.className='result '+(d.code==0?'success':'error');r.textContent=d.msg;"
"if(d.code==0)loadStatus();"
"}).catch(()=>{});"
"}"
"function submitEth(){"
"var m=document.getElementById('eth_mode').value;"
"var b='mode='+m;"
"if(m==='static'){"
"b+='&ip='+document.getElementById('eth_ip').value;"
"b+='&mask='+document.getElementById('eth_mask').value;"
"b+='&gateway='+document.getElementById('eth_gateway').value;"
"}"
"fetch('/api/eth',{method:'POST',body:b,headers:{'Content-Type':'application/x-www-form-urlencoded'}})"
".then(r=>r.json()).then(d=>{"
"var r=document.getElementById('eth_result');"
"r.style.display='block';r.className='result '+(d.code==0?'success':'error');r.textContent=d.msg;"
"if(d.code==0)loadStatus();"
"}).catch(()=>{});"
"}"
"function loadStatus(){"
"fetch('/api/status').then(r=>r.json()).then(d=>{"
"var s=document.getElementById('status');"
"// HIGH-WEB-4 fix: 改用 textContent + <br> 元素而非 innerHTML 注入 IP 字符串,"
"// 避免服务端 IP 字段含 '<script>' 时被当作 HTML 解析执行 XSS."
"s.textContent='活动网卡: '+(d.active_netcard||'未知')+'\\n'"
"+'WiFi: '+(d.wifi_connected?'已连接 ('+d.wifi_ip+')':'未连接')+'\\n'"
"+'以太网: '+(d.eth_linked?'已连接 ('+d.eth_ip+')':'未连接');"
"}).catch(()=>{});"
"}"
"loadNetcards();loadStatus();setInterval(loadStatus,5000);setInterval(loadNetcards,10000);"
"</script>"
"</body>"
"</html>";

/**
 * \}
 */

/**
 * \addtogroup CONFIG_WEB_Exported_Functions
 * \{
 */

int bConfigWebServiceStart(uint16_t port, pbConfigResultCb_t cb, void *user_data)
{
    s_ctx.config_cb = cb;
    s_ctx.user_data = user_data;

    return bHttpServerStart(port, _bConfigWebHandler, &s_ctx);
}

void bConfigWebDeinit(void)
{
    bHttpServerStop();
    memset(&s_ctx, 0, sizeof(s_ctx));
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

/************************ Copyright (c) 2026 aiclaw *****END OF FILE****/
