/**
 * \file        b_mbedtls_config.h
 * \brief       BabyOS mbedTLS configuration
 *
 * 满足 BabyOS 自测和一般网站 HTTPS 验证。
 * 调试/SHA384/PEM 通过 Kconfig 条件编译控制。
 */

#ifndef __B_MBEDTLS_CONFIG_H__
#define __B_MBEDTLS_CONFIG_H__

#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"

/* Platform */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_STD_CALLOC bCalloc
#define MBEDTLS_PLATFORM_STD_FREE bFree

#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))

/* 调试（Kconfig 控制） */
#if (defined(_MBEDTLS_DEBUG_ENABLE) && (_MBEDTLS_DEBUG_ENABLE == 1))
#define MBEDTLS_DEBUG_C
#endif

/* 熵源和随机数 */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_NO_DEFAULT_ENTROPY_SOURCE
#define MBEDTLS_AES_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

/* 基础加密 */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_MD_C
#define MBEDTLS_OID_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_AES_ROM_TABLES

/* TLS 1.2 客户端 */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

/* SHA-256（GCM 必需） */
#define MBEDTLS_SHA256_C

/* SHA-384（可选，Kconfig 控制） */
#if (defined(_MBEDTLS_SHA384_ENABLE) && (_MBEDTLS_SHA384_ENABLE == 1))
#define MBEDTLS_SHA384_C
#endif

/* AES-GCM 认证加密 */
#define MBEDTLS_GCM_C

/* 椭圆曲线（ECDHE 密钥交换） */
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

/* X.509 证书解析 */
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C

/* RSA 签名验证（PKCS#1 v1.5 + v2.1） */
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_X509_RSASSA_PSS_SUPPORT

/* PEM 证书解析（Kconfig 控制，~3KB） */
#if (defined(_MBEDTLS_PEM_PARSE_ENABLE) && (_MBEDTLS_PEM_PARSE_ENABLE == 1))
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C
#endif

/* 内容长度和分片 */
#define MBEDTLS_SSL_IN_CONTENT_LEN CONNECT_RECVBUF_MAX
#define MBEDTLS_SSL_OUT_CONTENT_LEN MBEDTLS_SSL_IN_CONTENT_LEN
#define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH

#if (MBEDTLS_SSL_IN_CONTENT_LEN >= 4096)
#define B_SSL_MAX_FRAG_LEN MBEDTLS_SSL_MAX_FRAG_LEN_4096
#elif (MBEDTLS_SSL_IN_CONTENT_LEN >= 2048)
#define B_SSL_MAX_FRAG_LEN MBEDTLS_SSL_MAX_FRAG_LEN_2048
#elif (MBEDTLS_SSL_IN_CONTENT_LEN >= 1024)
#define B_SSL_MAX_FRAG_LEN MBEDTLS_SSL_MAX_FRAG_LEN_1024
#else
#define B_SSL_MAX_FRAG_LEN MBEDTLS_SSL_MAX_FRAG_LEN_512
#endif

/* 加密套件 */
#if (defined(_MBEDTLS_SHA384_ENABLE) && (_MBEDTLS_SHA384_ENABLE == 1))
#define MBEDTLS_SSL_CIPHERSUITES                         \
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,     \
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,       \
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,       \
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384
#else
#define MBEDTLS_SSL_CIPHERSUITES                         \
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,     \
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
#endif

/* 默认 CA 证书（DigiCert Global Root G2 + tool 生成的 localhost 自签名）
 * tool/mock_https_cert.pem 是 MockHTTPS 服务器使用的自签名证书 (RSA-2048/SHA256/CN=localhost)
 * 设备端把它放进 CA 列表, 验证服务器身份. 两端必须完全匹配. */
#if (defined(_SSL_DEFAULT_CERT_ENABLE) && (_SSL_DEFAULT_CERT_ENABLE == 1))
#define MBEDTLS_SSL_DEFAULT_CERT                                         \
    "-----BEGIN CERTIFICATE-----\n"                                      \
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n" \
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n" \
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n" \
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n" \
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n" \
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n" \
    "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n" \
    "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n" \
    "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n" \
    "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n" \
    "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n" \
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n" \
    "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n" \
    "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n" \
    "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n" \
    "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n" \
    "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n" \
    "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n" \
    "MrY=\n"                                                             \
    "-----END CERTIFICATE-----\n"                                        \
    "-----BEGIN CERTIFICATE-----\n"                                      \
    "MIIDJTCCAg2gAwIBAgIUaAYt4yrD+5Mfk2DO/9gpxNmVDeMwDQYJKoZIhvcNAQEL\n" \
    "BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDgxNjE2NTQzNloXDTM2MDgx\n" \
    "MzE2NTQzNlowFDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEF\n" \
    "AAOCAQ8AMIIBCgKCAQEAsCVzAlBLdnQGYilJpq5bFD6XSSSSetAAb9eZlOI9NAki\n" \
    "cYRZ66Baxo5hdPkY9CBQRos/Fy1awuTuInB0KjS12u+vG5rW2zPBm4YyIsPV7nFo\n" \
    "ohHAfAyFC4t6nrven3ljinqJ5XNqX22Tr92ssXBs3oOCa/z9LCZcQdcODDRurf//\n" \
    "OrnvQz/ewUIguB7a4CHXKdIrJTXaxGHodi2aCwAV+nvLvAsfwP8M/8mTOBpO+D/f\n" \
    "zZhHlsRfFMoVNSQXgzGtPPuyQFLDadZkPTklMDm2CYx2kKO57UFsm7cZb35REGMz\n" \
    "+u59EKU1vh0QGrX+WNa/XrwYLFTag6MVr2H0fdMNQwIDAQABo28wbTAdBgNVHQ4E\n" \
    "FgQUioPJt4fJayimAcEOKajPXkatrZ0wHwYDVR0jBBgwFoAUioPJt4fJayimAcEO\n" \
    "KajPXkatrZ0wDwYDVR0TAQH/BAUwAwEB/zAaBgNVHREEEzARgglsb2NhbGhvc3SH\n" \
    "BH8AAAEwDQYJKoZIhvcNAQELBQADggEBAGqZo5DSzwZ7EDDNT5B514L3PZ0eVcC4\n" \
    "9SpaFBjt0DI3XVJIgZ5CNrCFj7gljO+qBKX+NprfhW/RWy6PsGSLxOcsduQ0evvb\n" \
    "nXo2cYeeZrLdRdHgDqGA3rKYw8igLIdasV3rKtqmeraFxweC3HbOtEgkORp7wB7s\n" \
    "EssDffb9MpQGZ/FZLsG9lxxun0I448flICJ+RBpfd/NFvUc2VBAaFjtVhW9mkkLH\n" \
    "7jWGF9dkyYu1lGfCqZ57skMNlNWbHSJRYhU6dlfsz/tFM3SG2orgUS9kOmpl6tBx\n" \
    "bSsqmdr+zCjmw78u1zZPlBqxhXqhIUsIIrXkqZafseyQcd0/Yp6ePtg=\n"                                 \
    "-----END CERTIFICATE-----\n"
#endif

#endif

#endif
