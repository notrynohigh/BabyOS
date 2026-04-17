/**
 * \file        b_mbedtls_config.h
 * \brief       BabyOS mbedTLS configuration - Flash-optimized
 *
 * 配置项分级：
 *   [MUST]  - 必须启用，否则SSL/TLS基本功能不可用
 *   [DEFAULT] - 默认启用，推荐保留
 *   [OPT]   - 可选，根据功能需求启用（禁用可节省Flash）
 *
 * 估算各选项Flash占用（基于mbedtls 3.6.0，GCC ARM Cortex-M4）：
 *   MBEDTLS_DEBUG_C:           ~5-10KB  (OPT)
 *   MBEDTLS_SHA384_C:          ~2KB     (OPT)
 *   MBEDTLS_SHA512_C:          ~3KB     (OPT)
 *   MBEDTLS_PEM_PARSE_C:       ~3KB     (OPT, 证书PEM格式)
 *   MBEDTLS_BASE64_C:          ~1KB     (OPT, PEM依赖)
 *   MBEDTLS_GENPRIME:          ~3KB     (OPT, RSA密钥生成)
 *   MBEDTLS_KEY_EXCHANGE_RSA:  ~2KB     (OPT, RSA密钥交换)
 *   AES-256 ciphersuite:       ~3KB     (OPT, AES-128通常够用)
 *
 * 关键修正：
 *   - MBEDTLS_RSA_C 和 MBEDTLS_PKCS1_V15 必须启用（验证RSA签名证书）
 *   - MBEDTLS_PEM_PARSE_C 默认启用（绝大多数证书是PEM格式）
 *
 * 编译时检查：
 *   默认配置（最小化）:    ~90KB  (RSA验证+ECDHE-ECDSA+AES128-SHA256)
 *   完整配置（所有OPT）:   ~130KB
 */

#ifndef __B_MBEDTLS_CONFIG_H__
#define __B_MBEDTLS_CONFIG_H__

#include "utils/inc/b_util_log.h"
#include "utils/inc/b_util_memp.h"

// [MUST] Platform抽象层
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_STD_CALLOC bCalloc
#define MBEDTLS_PLATFORM_STD_FREE bFree

// 注意：不要在用户配置中定义 MBEDTLS_ALLOW_PRIVATE_ACCESS。
// mbedtls 库自身已在 common.h 中定义该宏，库内部访问自己的私有成员不受影响。
// BabyOS SSL 模块不直接访问任何 MBEDTLS_PRIVATE 成员（自签名证书场景使用
// mbedtls_x509_crt_get_ca_istrue() 公开访问器或 MBEDTLS_SSL_VERIFY_NONE）。

#if (defined(_SSL_ENABLE) && (_SSL_ENABLE == 1))

// [OPT] 调试支持 - 生产环境禁用可节省 ~5-10KB
// 仅在需要诊断TLS握手问题时启用
// 原 _MBEDTLS_DEBUG_ENABLE 开关逻辑保留
#if (defined(_MBEDTLS_DEBUG_ENABLE) && (_MBEDTLS_DEBUG_ENABLE == 1))
#define MBEDTLS_DEBUG_C
#endif

// [MUST] 熵源和随机数
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_NO_DEFAULT_ENTROPY_SOURCE
#define MBEDTLS_AES_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

// [MUST] 加密算法基础
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_MD_C
#define MBEDTLS_OID_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_AES_ROM_TABLES  // 性能优化，几乎不占Flash

// [MUST] TLS协议支持
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

// [MUST] SHA-256（GCM必需）
#define MBEDTLS_SHA256_C

// [OPT] SHA-384 - 仅在需要 AES-256-GCM-SHA384 加密套件时启用
#if (defined(_MBEDTLS_SHA384_ENABLE) && (_MBEDTLS_SHA384_ENABLE == 1))
#define MBEDTLS_SHA384_C
#endif

// [OPT] SHA-512 - 当前配置不需要
// #define MBEDTLS_SHA512_C

// [MUST] GCM认证加密（AES-GCM必需）
#define MBEDTLS_GCM_C

// [MUST] 椭圆曲线（ECDHE密钥交换）
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

// [MUST] PKI基础（X.509证书解析）
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C

// [MUST] RSA签名验证 - 绝大多数证书（包含DigiCert、Let's Encrypt等）
// 使用 PKCS#1 v1.5 + SHA-256 with RSA 签名，必须启用才能验证证书
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15

// [MUST] RSA密钥交换 - 支持RSA签名证书的TLS握手
// tool生成的测试证书是RSA签名，需要ECDHE-RSA套件
// 公网证书如Let's Encrypt等也多为RSA签名
// 这会占用额外 ~2KB Flash，但是兼容性的必要代价
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED

// [OPT] RSA-PSS 签名验证 - 兜底, 启用以兼容 PSS 签名证书
// 当前 sig_algs offer 不写 PSS (见 b_mod_ssl.c), 所以正常握手不会触发
// PSS 路径. 但若未来服务器强制要求 PSS, 或者需要验证 PSS 签名证书链,
// mbedTLS 验签走的是 mbedtls_pk_verify_ext() 的 PSS 分支, 编译期由
// MBEDTLS_X509_RSASSA_PSS_SUPPORT 控制. 打开后 ~0.5KB Flash 增量.
// 不打开的代价: 遇到 PSS 签名证书时 mbedtls_pk_verify 返回 -0x4380.
// 跟 MBEDTLS_PKCS1_V21 配套 (PSS 是 PKCS#1 v2.1 的一种).
#define MBEDTLS_X509_RSASSA_PSS_SUPPORT

// [OPT] PEM格式证书解析 - 仅在使用PEM格式证书时启用
// 如果只用DER格式证书，可以禁用以节省 ~3-4KB
#if (defined(_MBEDTLS_PEM_PARSE_ENABLE) && (_MBEDTLS_PEM_PARSE_ENABLE == 1))
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C
#endif

// [OPT] 大素数生成 - 仅在需要生成RSA密钥时启用
// 客户端验证服务器证书时不需要
// #define MBEDTLS_GENPRIME

// 内容长度配置
#define MBEDTLS_SSL_IN_CONTENT_LEN CONNECT_RECVBUF_MAX
#define MBEDTLS_SSL_OUT_CONTENT_LEN MBEDTLS_SSL_IN_CONTENT_LEN

// [OPT] 启用MFL协商（推荐）- 避免服务器使用16KB record导致客户端失败
// 编译开销可忽略（增加一个handshake扩展），无运行时开销
#define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH

// 根据 IN_CONTENT_LEN 自动选择MFL等级
#if (MBEDTLS_SSL_IN_CONTENT_LEN >= 4096)
#define B_SSL_MAX_FRAG_LEN MBEDTLS_SSL_MAX_FRAG_LEN_4096
#elif (MBEDTLS_SSL_IN_CONTENT_LEN >= 2048)
#define B_SSL_MAX_FRAG_LEN MBEDTLS_SSL_MAX_FRAG_LEN_2048
#elif (MBEDTLS_SSL_IN_CONTENT_LEN >= 1024)
#define B_SSL_MAX_FRAG_LEN MBEDTLS_SSL_MAX_FRAG_LEN_1024
#else
#define B_SSL_MAX_FRAG_LEN MBEDTLS_SSL_MAX_FRAG_LEN_512
#endif

// 加密套件配置
// 必须同时支持 ECDHE-ECDSA 和 ECDHE-RSA，因为：
//   - 工具生成的测试服务器证书是 RSA 签名（需要 ECDHE-RSA 套件）
//   - 公网证书多为 ECDSA 签名（需要 ECDHE-ECDSA 套件）
// 默认只保留 SHA256 套件以节省 ~3KB（避免 SHA384 占用）
// 如需 AES-256 强度，启用 _MBEDTLS_SHA384_ENABLE
#define MBEDTLS_SSL_CIPHERSUITES                       \
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,   \
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256, \
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384, \
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384

#if (defined(_SSL_DEFAULT_CERT_ENABLE) && (_SSL_DEFAULT_CERT_ENABLE == 1))

// 默认CA证书：用于验证HTTPS服务器
// 包含两个证书：
//   1. DigiCert Global Root CA（公网CA，用于访问公网HTTPS）
//   2. tool生成的localhost自签名证书（用于本地HTTPS测试）
// 默认CA证书链：用于验证HTTPS服务器
// 同时包含两个证书（mbedtls 会自动选择匹配的CA）：
//   1. DigiCert Global Root CA（公网HTTPS常用）
//   2. tool 生成的 localhost 自签名证书（本地HTTPS测试，v3+CA:TRUE）
// localhost 证书必须和 _SSL_SKIP_HOSTNAME_VERIFY=1 配合使用（IP与CN不匹配）
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
    "MIIC4jCCAcqgAwIBAgIUD5zJdkctZKhrwjNgQVAYxYX4xKcwDQYJKoZIhvcNAQEL\n" \
    "BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDYwNzExMDIwNloXDTI3MDYw\n" \
    "NzExMDIwNlowFDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEF\n" \
    "AAOCAQ8AMIIBCgKCAQEA9OYRPscQrTkHz/zMrUpwFF9614x0TYMZTVdxEDPdgKr3\n" \
    "pCTA0kTrkvUh39nn7rehlVIixtnLldj7tOpA74ChRy9RLdbmvoeDrezf5yuWQhNH\n" \
    "gnv1Eq5/Rmr7V0mHzTe3xjAQ7TOlzwynUK19hQRxNvg3ceGfTADgYXX7EBtUYHmw\n" \
    "fOZtJ9bGBXsPZ5S9zspTsdkyJw9NiWZwguq/Npl9j63Q6AYR/mgAHQYy8V3qVqzp\n" \
    "WbTE1E9tNy5HoJGL18qE9TAHpYEW1B9bF7OFcQflCNv7ufHYKGd6+TI9EkY9RaRs\n" \
    "lNB+j3Y4i5Hg4LiqG4HrudYycJnSWMGGMOTRSS+cOwIDAQABoywwKjAMBgNVHRME\n" \
    "BTADAQH/MBoGA1UdEQQTMBGCCWxvY2FsaG9zdIcEfwAAATANBgkqhkiG9w0BAQsF\n" \
    "AAOCAQEA1txsavK5ItmK9po9eP3d+M45aAY1bY8R6axSrzyujhse/jMXgP4kmQNi\n" \
    "gIDUn/6M0gbaQZq/EqjM/qyeh4JmsgIFDX91BEW7K2P1Nqu3NTspuDDe6EgI6jAp\n" \
    "LIcpY9tRAUD1m26O7XQ0Ht5Sxi2ASxJSvPWj6pkkXT0R3zI0oz1ZUeatZUuH3kkX\n" \
    "CCsX3JDYXl23m4hQUqqiBkxbBkErJDZP4gsGRpuaBrvcdOAIpGV4RRdn0o22X1lx\n" \
    "hRU7uEMPFW5Jq1coOxGXsJJZnMOOODZMeQFa0KSKr3BlpfD2lcVkCY7afzGPuxwx\n" \
    "rYC2sj4DqboRB3StY/NMHu+cAnqomA==\n"                                 \
    "-----END CERTIFICATE-----\n"
#endif

#endif

#endif
