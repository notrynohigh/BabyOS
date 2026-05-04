# 模块 31：SSL/TLS

**文件**: `bos/modules/b_mod_ssl.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

SSL 模块封装 mbedtls，提供 TLS/SSL 客户端功能。

### 1.2 核心 API

```c
// 初始化
bSSLHandle_t bSSLInit(const char *hostname, bSSLCert_t *cert);

// 断开连接
int bSSLDeinit(bSSLHandle_t ssl);

// TLS 握手
int bSSLHandshake(bSSLHandle_t ssl, int sockfd);

// 收发数据
int bSSLRecv(bSSLHandle_t ssl, uint8_t *pbuf, uint16_t buf_len, uint16_t *rlen);
int bSSLSend(bSSLHandle_t ssl, uint8_t *pbuf, uint16_t buf_len, uint16_t *wlen);
```

---

## 二、实际测试用例

无独立测试函数（SSL 通过 HTTP Client 测试间接验证）。

---

## 三、测试文件

无独立文件。

**依赖**:
- `b_mod_ssl.c`
- mbedtls 库
- `_SSL_ENABLE=1`

---

## 四、备注

SSL 通过 HTTPS 请求间接测试（见 test_service.c 中的 MQTT 测试）。