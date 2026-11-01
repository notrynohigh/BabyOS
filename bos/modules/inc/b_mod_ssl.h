/**
 *!
 * \file        b_mod_ssl.h
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
#ifndef __B_MOD_SSL_H__
#define __B_MOD_SSL_H__

#ifdef __cplusplus
extern "C" {
#endif

/*Includes ----------------------------------------------*/
#include <stdint.h>

#include "b_config.h"

#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/bignum.h"
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/ctr_drbg.h"
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/debug.h"
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/entropy.h"
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/platform.h"
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/rsa.h"
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/ssl.h"
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/x509.h"
#include "thirdparty/mbedtls/bos_mbedtls/mbedtls/net_sockets.h"
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
 * \defgroup SSL_Exported_TypesDefinitions
 * \{
 */
typedef struct
{
    const uint8_t *pbuf;
    uint32_t       len;
} bSSLCert_t;

typedef void *bSSLHandle_t;

/**
 * \}
 */

/**
 * \defgroup SSL_Exported_Defines
 * \{
 */
#define SSLHANDLE_IS_INVALID(h) ((h) == NULL)
/**
 * \}
 */

/**
 * \defgroup SSL_Exported_Functions
 * \{
 */
bSSLHandle_t bSSLInit(const char *hostname, bSSLCert_t *cert);
int          bSSLDeinit(bSSLHandle_t ssl);

/**
 * \brief
 * \param ssl
 * \param sockfd
 * \return int (< 0: error) (0: success) (1: in progress)
 */
int bSSLHandshake(bSSLHandle_t ssl, int sockfd);

/**
 * \brief
 *        接收 SSL 明文数据. PT-friendly 非阻塞语义:
 *
 *        \param ssl      SSL handle.
 *        \param pbuf     调用方提供的明文缓冲区.
 *        \param buf_len  pbuf 容量 (字节).
 *        \param rlen     [out] 实际写入 pbuf 的明文字节数. **总是被设置**:
 *                        正数 = 写入字节数, 0 = 本次没新明文 (含正常重试 + 关闭信号).
 *
 *        \return int
 *          > 0  : 成功, *rlen = 写入 pbuf 的明文字节数 (1~buf_len).
 *          == 1 : **对端干净关闭** (peer 已发 SSL close_notify). *rlen = 0.
 *                 调用方应停止等待新数据, 处理已收到的 plaintext, 然后 bSSLDeinit.
 *          == 0 : 本次没数据 (WANT_READ/WANT_WRITE — 还需要更多 socket 字节).
 *                 *rlen = 0. 调用方应让出 CPU, 下一轮 bExec() 后 retry.
 *          < 0  : 真错误 (协议错 / OOM / invalid handle). *rlen = 0.
 */
int bSSLRecv(bSSLHandle_t ssl, uint8_t *pbuf, uint16_t buf_len, uint16_t *rlen);

/**
 * \brief
 *        Handshake 收尾: 把 mbedTLS 内部已缓冲的 post-handshake record 提上来.
 *        见 b_mod_ssl.c 中的实现注释.
 * \param ssl   SSL handle (bSSLHandshake 返回 0 之后调用).
 * \return int  1     : 还有 record 没消化, 调用方 yield 后再调.
 *              0     : drain 完成, 可走 bSSLSend.
 *              < 0   : 错误.
 */
int bSSLHandshakeFinalize(bSSLHandle_t ssl);

/**
 * \brief
 *        Send `buf_len` bytes of application data over TLS.
 *
 *        PT-friendly 语义 (调用方必须按 *wlen 与 buf_len 比较判断是否重试):
 *          - ret > 0                       : 实际写出的字节数 (mbedTLS 一次
 *          写入完成; 可能等于 buf_len 全写完, 也可能 < buf_len 部分写入).
 *          - ret == 0 且 *wlen == 0        : 没写出任何字节 (WANT_READ /
 *          WANT_WRITE). 调用方必须 yield CPU, 下一轮 bExec() 后 retry.
 *          - ret < 0                        : 真错误 (协议错 / OOM / invalid handle).
 *
 *        **重要**: PT 任务内调本函数不会调度其他 polling func, 所以内部
 *        不自旋 WANT_WRITE / WANT_READ — 直接退出让上层 PT yield.
 *
 * \param ssl   SSL handle (bSSLInit 返回值).
 * \param pbuf  要发送的应用数据.
 * \param buf_len pbuf 中字节数.
 * \param wlen  [out] 实际写出的字节数. 调用方**必须**通过 *wlen 与 buf_len 比较
 *              判断是否需要重试, 不能只看 ret.
 * \return int   见上方语义表.
 */
int bSSLSend(bSSLHandle_t ssl, uint8_t *pbuf, uint16_t buf_len, uint16_t *wlen);

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

/************************ Copyright (c) 2020 Bean *****END OF FILE****/
