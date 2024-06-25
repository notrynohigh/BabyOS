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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */

/*Includes ----------------------------------------------*/
#include "modules/inc/b_mod_ssl.h"

#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))
#include "hal/inc/b_hal.h"
#include "modules/inc/b_mod_netif/b_mod_trans.h"

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

static void _bSSLPrint(void *ctx, int level, const char *file, int line, const char *str)
{
    ((void)level);  // 忽略未使用的参数
    ((void)ctx);    // 忽略未使用的参数
    b_log("%s:%04d: %s", file, line, str);
}

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
        return 0;
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
        return 0;
    }
    if (rlen == 0)
    {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return rlen;
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
        b_log_e("mem error..\r\n");
        return NULL;
    }
    if (_bSSLDRBGInit(&p_ssl->random) < 0)
    {
        return NULL;
    }
    mbedtls_ssl_init(&p_ssl->ssl_ctx);
    mbedtls_ssl_config_init(&p_ssl->ssl_conf);
    mbedtls_x509_crt_init(&p_ssl->ca);
    mbedtls_ssl_conf_dbg(&p_ssl->ssl_conf, _bSSLPrint, NULL);
    mbedtls_debug_set_threshold(MBEDTLS_DEBUG_LEVEL);
    mbedtls_ssl_config_defaults(&p_ssl->ssl_conf, MBEDTLS_SSL_IS_CLIENT,
                                MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_rng(&p_ssl->ssl_conf, mbedtls_ctr_drbg_random, &p_ssl->random.ctr_drbg);
    mbedtls_ssl_set_hostname(&p_ssl->ssl_ctx, hostname);
    const int *ciphersuites = mbedtls_ssl_list_ciphersuites();
    printf("Supported ciphersuites:\n");
    while (*ciphersuites != 0)
    {
        b_log("%s\n", mbedtls_ssl_get_ciphersuite_name(*ciphersuites));
        ciphersuites++;
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
        root_cert.pbuf = MBEDTLS_SSL_DEFAULT_CERT;
        root_cert.len  = sizeof(MBEDTLS_SSL_DEFAULT_CERT);
#endif
    }

    if (root_cert.pbuf != NULL && root_cert.len != 0)
    {
        ret = mbedtls_x509_crt_parse(&p_ssl->ca, root_cert.pbuf, root_cert.len);
        if (ret == 0)
        {
            mbedtls_ssl_conf_ca_chain(&p_ssl->ssl_conf, &p_ssl->ca, NULL);
            mbedtls_ssl_conf_authmode(&p_ssl->ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        }
        else
        {
            b_log_e("cert parse error..-0x%x\r\n", 0 - ret);
            mbedtls_ssl_conf_authmode(&p_ssl->ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
        }
    }
    else
    {
        b_log_w("no valid cert ....\r\n");
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
    b_log_e("fail -0x%x\r\n", -ret);
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
