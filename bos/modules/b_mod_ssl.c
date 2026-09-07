/**
 *!
 * \file        b_mod_ssl.c
 * \version     v0.0.1
 * \date        2020/05/16
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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "modules/inc/b_mod_ssl.h"

#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))
#include "hal/inc/b_hal.h"
#include "modules/inc/b_mod_tcpip.h"

/**
 * \addtogroup BABYOS
 * \{
 */

/**
 * \addtogroup MODULES
 * \{
 */

/**
 * \addtogroup SSL
 * \{
 */

/**
 * \defgroup SSL_Private_TypesDefinitions
 * \{
 */
typedef struct
{
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context  entropy;
} bSSLRandom_t;

typedef struct
{
    bSSLRandom_t        random;
    mbedtls_ssl_context ssl_ctx;
    mbedtls_ssl_config  ssl_conf;
    mbedtls_x509_crt    ca;
    int                 sockfd;
    // mbedtls_ssl_conf_sig_algs() 仅存指针，不复制数据;
    // 列表必须在 SSL 句柄生命周期内保持有效, 因此放在堆上的 bSSL_t 里.
    // 末尾哨兵 MBEDTLS_TLS1_3_SIG_NONE (0x0) 表示列表结束.
    // 数组大小 6: 2 个 SHA256 (RSA/ECDSA) + 可选 2 个 SHA384 (RSA/ECDSA) + 哨兵.
    uint16_t sig_algs[6];
} bSSL_t;

/**
 * \}
 */

/**
 * \defgroup SSL_Private_Defines
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SSL_Private_Macros
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SSL_Private_Variables
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SSL_Private_FunctionPrototypes
 * \{
 */

/**
 * \}
 */

/**
 * \defgroup SSL_Private_Functions
 * \{
 */

static int _bSSLEntropySource(void *data, uint8_t *output, uint32_t len, uint32_t *olen)
{
    uint32_t seed;
    B_UNUSED(data);
    seed = bHalRNGRead();
    if (len > sizeof(seed))
    {
        len = sizeof(seed);
    }
    memcpy(output, &seed, len);
    *olen = len;
    return 0;
}

#if defined(MBEDTLS_DEBUG_C)
static void _bSSLPrint(void *ctx, int level, const char *file, int line, const char *str)
{
    ((void)level);
    ((void)ctx);
    b_log("%s:%04d: %s", file, line, str);
}
#endif

static int _bSSLDRBGInit(bSSLRandom_t *prandom)
{
    if (prandom == NULL)
    {
        return -1;
    }
    mbedtls_ctr_drbg_init(&prandom->ctr_drbg);
    mbedtls_entropy_init(&prandom->entropy);
    mbedtls_entropy_add_source(&prandom->entropy, _bSSLEntropySource, NULL,
                               MBEDTLS_ENTROPY_MAX_GATHER, MBEDTLS_ENTROPY_SOURCE_STRONG);
    mbedtls_ctr_drbg_seed(&prandom->ctr_drbg, mbedtls_entropy_func, &prandom->entropy,
                          (const uint8_t *)"babyos", strlen("babyos"));
    return 0;
}

static int _bSSLDRBGDeinit(bSSLRandom_t *prandom)
{
    if (prandom == NULL)
    {
        return -1;
    }
    mbedtls_entropy_free(&prandom->entropy);
    mbedtls_ctr_drbg_free(&prandom->ctr_drbg);
    return 0;
}

static int _bSSLSend(void *ctx, const unsigned char *buf, size_t len)
{
    bSSL_t  *p_ssl = (bSSL_t *)ctx;
    uint16_t wlen  = 0;
    int      ret   = bSend(p_ssl->sockfd, (uint8_t *)buf, len, &wlen);
    if (ret < 0)
    {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    if (wlen == 0)
    {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return wlen;
}

static int _bSSLRecv(void *ctx, unsigned char *buf, size_t len)
{
    bSSL_t  *p_ssl = (bSSL_t *)ctx;
    uint16_t rlen  = 0;
    int      ret   = bRecv(p_ssl->sockfd, buf, len, &rlen);
    if (ret < 0)
    {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    if (rlen == 0)
    {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return rlen;
}

// SSL 证书验证回调: 始终注册, 实际是否调用由 mbedtls_ssl_conf_authmode() 决定.
// _SSL_SKIP_HOSTNAME_VERIFY = 1 时仅对服务器证书 (depth=0) 跳过 CN/SAN 检查,
//   便于 IP 直连测试; 设为 0 则严格校验 CN/SAN.
static int _bSSLVerifyCb(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    (void)ctx;
    (void)crt;
    (void)depth;
#if (defined(_SSL_SKIP_HOSTNAME_VERIFY) && (_SSL_SKIP_HOSTNAME_VERIFY == 1))
    if (depth == 0 && (*flags & MBEDTLS_X509_BADCERT_CN_MISMATCH))
    {
        *flags &= ~MBEDTLS_X509_BADCERT_CN_MISMATCH;
    }
#else
    (void)flags;
#endif
    return 0;
}

/**
 * \}
 */

/**
 * \addtogroup SSL_Exported_Functions
 * \{
 */

bSSLHandle_t bSSLInit(const char *hostname, bSSLCert_t *cert)
{
    int        ret       = 0;
    bSSLCert_t root_cert = {
        .pbuf = NULL,
        .len  = 0,
    };
    if (hostname == NULL)
    {
        return NULL;
    }
    bSSL_t *p_ssl = bCalloc(1, sizeof(bSSL_t));
    if (p_ssl == NULL)
    {
        return NULL;
    }
    if (_bSSLDRBGInit(&p_ssl->random) < 0)
    {
        bFree(p_ssl);
        return NULL;
    }
    mbedtls_ssl_init(&p_ssl->ssl_ctx);
    mbedtls_ssl_config_init(&p_ssl->ssl_conf);
    mbedtls_x509_crt_init(&p_ssl->ca);
#if defined(MBEDTLS_DEBUG_C)
    mbedtls_ssl_conf_dbg(&p_ssl->ssl_conf, _bSSLPrint, NULL);
    mbedtls_debug_set_threshold(MBEDTLS_DEBUG_LEVEL);
#endif
    mbedtls_ssl_config_defaults(&p_ssl->ssl_conf, MBEDTLS_SSL_IS_CLIENT,
                                MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_rng(&p_ssl->ssl_conf, mbedtls_ctr_drbg_random, &p_ssl->random.ctr_drbg);
    mbedtls_ssl_set_hostname(&p_ssl->ssl_ctx, hostname);

    // 注册验证回调（始终注册）
    // 回调内部根据 _SSL_SKIP_HOSTNAME_VERIFY 宏决定是否跳过 CN/SAN 检查
    mbedtls_ssl_conf_verify(&p_ssl->ssl_conf, _bSSLVerifyCb, p_ssl);

#if defined(MBEDTLS_SSL_MAX_FRAGMENT_LENGTH)
    // 通知服务器最大分片大小，避免服务器发送超过客户端缓冲区的record
    // MFL等级根据 MBEDTLS_SSL_IN_CONTENT_LEN 自动选择
    mbedtls_ssl_conf_max_frag_len(&p_ssl->ssl_conf, B_SSL_MAX_FRAG_LEN);
#endif

    // 限制 sig_algs: 只提供 PKCS#1 v1.5 + ECDSA, 不提供 RSA-PSS.
    //   Windows 上 OpenSSL 3.x 一些版本会优先选 PSS (rsa_pss_rsae_sha256),
    //   触发 mbedTLS 3.6 验签的兼容性问题. 不把 PSS 写进 offer 后, 按
    //   RFC 5246 §7.4.1.4.1 server SKE sig_alg 必须在 client offer 列表里,
    //   因此强制选 0x0401 (rsa_pkcs1_sha256), 完全避开 PSS 路径.
    // 列表必须存放在 SSL 句柄生命周期内有效内存, 因为 mbedtls_ssl_conf_sig_algs() 只存指针.
    {
        int idx                = 0;
        p_ssl->sig_algs[idx++] = MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA256;
        p_ssl->sig_algs[idx++] = MBEDTLS_TLS1_3_SIG_ECDSA_SECP256R1_SHA256;
#if (defined(_MBEDTLS_SHA384_ENABLE) && (_MBEDTLS_SHA384_ENABLE == 1))
        p_ssl->sig_algs[idx++] = MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA384;
        p_ssl->sig_algs[idx++] = MBEDTLS_TLS1_3_SIG_ECDSA_SECP384R1_SHA384;
#endif
        p_ssl->sig_algs[idx++] = MBEDTLS_TLS1_3_SIG_NONE;  // 哨兵 = 0
        mbedtls_ssl_conf_sig_algs(&p_ssl->ssl_conf, p_ssl->sig_algs);
    }

    p_ssl->sockfd = -1;
    mbedtls_ssl_set_bio(&p_ssl->ssl_ctx, p_ssl, _bSSLSend, _bSSLRecv, NULL);

    if (cert)
    {
        root_cert.pbuf = cert->pbuf;
        root_cert.len  = cert->len;
    }
    else
    {
#if defined(MBEDTLS_SSL_DEFAULT_CERT)
        root_cert.pbuf = (const uint8_t *)MBEDTLS_SSL_DEFAULT_CERT;
        root_cert.len  = sizeof(MBEDTLS_SSL_DEFAULT_CERT);
#endif
    }

    if (root_cert.pbuf != NULL && root_cert.len != 0)
    {
        ret = mbedtls_x509_crt_parse(&p_ssl->ca, root_cert.pbuf, root_cert.len);
        if (ret == 0)
        {
            mbedtls_ssl_conf_ca_chain(&p_ssl->ssl_conf, &p_ssl->ca, NULL);
#if (defined(_SSL_VERIFY_ENABLE) && (_SSL_VERIFY_ENABLE == 1))
            mbedtls_ssl_conf_authmode(&p_ssl->ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
#else
            mbedtls_ssl_conf_authmode(&p_ssl->ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
#endif
        }
        else
        {
            b_log_e("[SSL] cert parse failed: -0x%x\r\n", -ret);
            mbedtls_ssl_conf_authmode(&p_ssl->ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
        }
    }
    else
    {
        b_log_w("[SSL] no cert configured, verify disabled\r\n");
        mbedtls_ssl_conf_authmode(&p_ssl->ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
    }
    // CRIT-SSL-INIT-1 fix: 检查 mbedtls_ssl_setup 返回值. setup 会分配内部
    // handshake state / in/out buffer 等, 失败时这些子分配可能部分完成. 旧代码
    // 忽略 ret 直接 return p_ssl, 导致:
    //   (a) 调用方用 SSLHANDLE_IS_INVALID (== NULL) 检测不到失败, 继续 bSSLSend
    //       触发 mbedtls_ssl_write 进入异常状态 — 表现为发"明文 GET"或卡死
    //   (b) 失败时 bSSLDeinit 调 mbedtls_ssl_free 释放未完整初始化的 ctx, mbedTLS
    //       内部会检测到 setup 未完成并 abort, 跳过部分子分配释放 — 表现为 ~10KB
    //       泄漏 (bSSL_t 大小 = ssl_context in/out 各 2KB + 其他 ~6KB)
    //
    // 修复: setup 失败时, 完整清理 ctx/conf/ca/random 然后 bFree(p_ssl) 返回 NULL.
    // 这样调用方能正确检测失败, 走重试或跳过本次请求路径, 不会留下半残 ctx.
    ret = mbedtls_ssl_setup(&p_ssl->ssl_ctx, &p_ssl->ssl_conf);
    if (ret != 0)
    {
        b_log_e("[SSL] mbedtls_ssl_setup failed: -0x%x\r\n", -ret);
        mbedtls_x509_crt_free(&p_ssl->ca);
        mbedtls_ssl_config_free(&p_ssl->ssl_conf);
        mbedtls_ssl_free(&p_ssl->ssl_ctx);
        _bSSLDRBGDeinit(&p_ssl->random);
        bFree(p_ssl);
        return NULL;
    }
    return p_ssl;
}

int bSSLDeinit(bSSLHandle_t ssl)
{
    if (SSLHANDLE_IS_INVALID(ssl))
    {
        return -1;
    }
    bSSL_t *p_ssl = (bSSL_t *)ssl;
    p_ssl->sockfd = -1;
    mbedtls_x509_crt_free(&p_ssl->ca);
    mbedtls_ssl_config_free(&p_ssl->ssl_conf);
    mbedtls_ssl_free(&p_ssl->ssl_ctx);
    _bSSLDRBGDeinit(&p_ssl->random);
    bFree(p_ssl);
    return 0;
}

/**
 * \brief
 * \param ssl
 * \param sockfd
 * \return int (< 0: error) (0: success) (1: in progress)
 */
int bSSLHandshake(bSSLHandle_t ssl, int sockfd)
{
    int ret = 0;
    if (SSLHANDLE_IS_INVALID(ssl) || SOCKFD_IS_INVALID(sockfd))
    {
        return -1;
    }
    bSSL_t *p_ssl = (bSSL_t *)ssl;
    if (p_ssl->sockfd != sockfd)
    {
        p_ssl->sockfd = sockfd;
    }
    ret = mbedtls_ssl_handshake(&p_ssl->ssl_ctx);
    if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ)
    {
        return 1;
    }
    if (ret < 0)
    {
        b_log_e("[SSL] handshake fail -0x%x\r\n", -ret);
    }
    return ret;
}

/**
 * \brief
 *        Handshake 收尾: 把 mbedTLS 内部已缓冲的 (但还没交付应用层) record
 *        通过一次 mbedtls_ssl_read 提上来.
 *
 *        注意: 这只**提走 mbedTLS 内部 in_buf 已有的 record**. 它**不会**
 *        主动从 socket 读 — socket 数据通过 _bSSLRecv (被 mbedtls_ssl_read
 *        内部回调) 在 mbedTLS 想要更多数据时自动拉.
 *
 *        调用方协议 (PT-friendly, bSSLHandshake 返回 0 之后调用):
 *          int dr;
 *          while ((dr = bSSLHandshakeFinalize(ssl)) > 0) {
 *              bTaskDelayMs(pt, 10);  // 等下一次 bExec(), 让 wifi polling
 *                                     // 把更多 +IPD 数据喂进 socket, mbedTLS
 *                                     // 内部会消化 (in_buf 增加, 下次进
 *                                     // finalize 时再次提走).
 *          }
 *          // dr == 0: drain 完毕, 可以安全 bSSLSend (server Finished + NST
 *          //          已被消化, mbedTLS out_buf 会 flush).
 *          // dr < 0  : 真错误, 重启或放弃.
 *
 *        退出条件: 一次 bSSLRecv 返回 0 (mbedTLS 内部 in_buf 已空, 且 socket
 *        暂无可读). 此时调用方拿到 0, 视为 drain 完成.
 *
 *        之前 b_srv_http.c 自己用 256B 临时 buffer + 调一次 bSSLRecv 来做
 *        drain, 把 SSL 内部 buffer size 的判断 (mbedtls_ssl_get_bytes_avail)
 *        泄漏到了 service 层, 违反模块边界. 这个 API 把 drain 收回 SSL 模
 *        块内部, 调用方只看到 "drain 还需要吗" 的二元状态.
 *
 * \param ssl   SSL handle (bSSLHandshake 返回 0 之后调用).
 * \return int  1     : 本次 drain 提走一些 record, 但 mbedTLS 内部 in_buf
 *                      还有更多 (mbedtls_ssl_get_bytes_avail > 0), 调用方
 *                      让出 CPU 后再调一次 (等 server 后续 record 到达).
 *              0     : 本次 bSSLRecv 返回 0 字节, mbedTLS in_buf 已空,
 *                      drain 完成, 可以走 bSSLSend.
 *              < 0   : 错误.
 */
int bSSLHandshakeFinalize(bSSLHandle_t ssl)
{
    if (SSLHANDLE_IS_INVALID(ssl))
    {
        return -1;
    }
    bSSL_t *p_ssl = (bSSL_t *)ssl;
    // 一次 drain: 把 mbedTLS in_buf 里能提的明文都提出来 (含 ApplicationData,
    // 我们丢弃). 256B 足够一次提走单条 TLS record (server Finished + NST
    // + 部分 AppData 通常 < 1KB).
    uint8_t  drain_buf[256];
    uint16_t drain_rlen = 0;
    int      rret       = bSSLRecv(ssl, drain_buf, sizeof(drain_buf), &drain_rlen);
    if (rret < 0)
    {
        return -1;
    }
    // 提完看 in_buf 是否还有 record (没消化完). 是 → 返回 1 让调用方让出
    // 等 server 后续 record. 否 → 返回 0 表示 drain 完成.
    if (mbedtls_ssl_get_bytes_avail(&p_ssl->ssl_ctx) > 0)
    {
        return 1;
    }
    return 0;
}

int bSSLRecv(bSSLHandle_t ssl, uint8_t *pbuf, uint16_t buf_len, uint16_t *rlen)
{
    int ret = 0;
    if (SSLHANDLE_IS_INVALID(ssl) || (pbuf == NULL && buf_len != 0))
    {
        return -1;
    }
    bSSL_t *p_ssl = (bSSL_t *)ssl;
    ret = mbedtls_ssl_read(&p_ssl->ssl_ctx, pbuf, buf_len);
    // CRIT-SSL-RECV-WANT fix: WANT_READ/WANT_WRITE 表示 mbedTLS 需要更多
    // socket 数据 (典型: handshake 收尾阶段消化 server NST/Finished, 或加密
    // record 解密需要更多 ciphertext). 这是 **正常** "本次没数据" 信号,
    // 不是错误. 旧实现把它们当 rlen=0 的 "本次没读到" — rlen 是 read length
    // 写 0 是对的, 但 ret 保留 0 让调用方当作 "成功但 0 字节" 也合理.
    // 这里**不**改 bSSLRecv 语义 (太多调用方依赖 ret>=0), 但要保证 rlen=0.
    if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ)
    {
        if (rlen)
        {
            *rlen = 0;
        }
        return 0;
    }
    // CRIT-SSL-RECV-CLOSE-NOTIFY fix: MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY
    // (-0x7880) 是 mbedTLS 收到对端 SSL close_notify alert 后的"干净关闭"
    // 信号, **不是**协议错误. mbedtls_ssl_read 流程 (mbedtls-3.6.0):
    //   1. 消化 AppData record: 返回 plaintext 给调用方 (ret = positive)
    //   2. 下一条 record 是 close_notify: mbedtls_ssl_handle_message_type
    //      返回 PEER_CLOSE_NOTIFY, 透传到 mbedtls_ssl_read
    //
    // 旧实现的 bug: 直接 return ret (负数) → HTTP service 忽略返回值
    //   → 循环继续 → recv_head 已被 mbedTLS 清空 → bSockIsReadable 永远
    //   false → 5s RECV_TIMEOUT. 即使 plaintext 已成功返回上一轮, 这一次
    //   负返回也会让 HTTP service 5s 后才退出, 用户感知是"卡死".
    //
    // 修复策略: 把 PEER_CLOSE_NOTIFY 映射为一个特殊的正数返回值
    //   `B_SSL_RECV_PEER_CLOSED` (= 1), 让调用方一眼看出"对端干净关闭"
    //   — 与 WANT_READ/WANT_WRITE (返回 0) 区分开. *rlen = 0 表示本次没
    //   新 plaintext. 调用方应根据 *rlen 和 bSSLRecv 之前的 plaintext 是否
    //   已交付, 决定继续等新数据还是结束会话.
    //
    // 选 1 而非 0: 调用方经常用 `ret >= 0` 判断成功, 0 容易跟"无数据"混淆.
    //   1 是最小不冲突的正数, 表达"特殊信号".
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
    {
        if (rlen)
        {
            *rlen = 0;
        }
        return 1;  // 特殊: 对端干净关闭, *rlen=0
    }
    if (ret >= 0 && rlen)
    {
        *rlen = ret;
    }
    return ret;
}

int bSSLSend(bSSLHandle_t ssl, uint8_t *pbuf, uint16_t buf_len, uint16_t *wlen)
{
    int ret = 0;
    if (SSLHANDLE_IS_INVALID(ssl) || (pbuf == NULL && buf_len != 0))
    {
        return -1;
    }
    bSSL_t *p_ssl = (bSSL_t *)ssl;
    // CRIT-SSL-SEND-WANT-READ fix: 区分 "写完" 和 "没写完 (WANT_READ/WANT_WRITE)".
    //
    // 旧实现: WANT_READ / WANT_WRITE 都吞成 0 + ret=0. 调用方看到 0 字节以为
    // 已经发完, 实际上 mbedTLS 还在消化 server Finished/NewSessionTicket 等
    // post-handshake record. 这会让 HTTPS 请求在 server 端永远收不到 (因为
    // mbedTLS 在消化完之前不会 flush out_buf), 表现为 5s RECV_TIMEOUT.
    //
    // PT 裸机语义 (bExec 不在 PT 任务内调度):
    //   - WANT_READ: mbedTLS 要先消化 server record. PT 任务内 spin 没有意义
    //     (其他 polling func 不跑). 必须 yield 给上层 PT 任务, 让出 CPU,
    //     等下次 bExec() 时 wifi/recv 路径把 server 数据喂进来再 retry.
    //   - WANT_WRITE: mbedTLS out_buf 满或 ESP-12F send queue 满. 同样需要
    //     yield 让 wifi polling func (_bTcpIpSendPoll) 把 buffer flush 出
    //     空间. PT 任务内 spin 只会空转.
    //
    // 修复: WANT_READ / WANT_WRITE 都立即退出 (不 spin), 通过 *wlen 告诉调用
    // 方 "本次写出了多少字节 (< buf_len)". 上层 PT 任务 bTaskDelayMs 让出,
    // 下一轮 bExec() 后 retry. 写得满的情况, 函数内**只**调一次
    // mbedtls_ssl_write, 避免 PT 任务内 CPU 100% 空转.
    ret = mbedtls_ssl_write(&p_ssl->ssl_ctx, pbuf, buf_len);
    if (ret > 0)
    {
        if (wlen)
        {
            *wlen = (uint16_t)ret;
        }
        return ret;  // 全写完 (或写出一部分, mbedTLS 一次性返回)
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ)
    {
        // 没写出任何字节, 但属于 "还想再尝试". wlen=0 让调用方知道没写完.
        if (wlen)
        {
            *wlen = 0;
        }
        return 0;
    }
    // 真错误 (mbedTLS 协议错误, 证书错误等) — ret < 0
    if (wlen)
    {
        *wlen = 0;
    }
    return ret;
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

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
