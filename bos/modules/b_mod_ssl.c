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
    mbedtls_ssl_setup(&p_ssl->ssl_ctx, &p_ssl->ssl_conf);
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

int bSSLRecv(bSSLHandle_t ssl, uint8_t *pbuf, uint16_t buf_len, uint16_t *rlen)
{
    int ret = 0;
    if (SSLHANDLE_IS_INVALID(ssl) || (pbuf == NULL && buf_len != 0))
    {
        return -1;
    }
    bSSL_t *p_ssl = (bSSL_t *)ssl;
    ret           = mbedtls_ssl_read(&p_ssl->ssl_ctx, pbuf, buf_len);
    if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ)
    {
        ret = 0;
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
    ret           = mbedtls_ssl_write(&p_ssl->ssl_ctx, pbuf, buf_len);
    if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ)
    {
        ret = 0;
    }
    if (ret >= 0 && wlen)
    {
        *wlen = ret;
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
