# 模块 44：TCPIP Service

**文件**: `bos/services/b_srv_tcpip.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

TCPIP Service 提供 NTP 时间同步和 HTTP 客户端功能。

### 1.2 核心 API

```c
// 初始化（内部调用 bTcpIpInit）
int bTcpipSrvInit(const bNetCardInfo_t *pnetcard, uint8_t number);

// NTP 时间同步
int bSntpStart(uint32_t interval_s);

// HTTP 客户端
int bHttpInit(pHttpCb_t cb, void *user_data);
int bHttpRequest(int httpfd, bHttpReqType_t type, const char *url,
                 const char *head, const char *body);
```

---

## 二、实际测试用例

无独立测试函数（TCPIP Service 通过 MQTT 测试间接验证）。

---

## 三、测试文件

无独立文件。

**依赖**:
- `bos/services/b_srv_tcpip.c`
- `bos/services/inc/b_srv_tcpip.h`
- `_TCPIP_SERVICE_ENABLE=1`

---

## 四、备注

TCPIP Service 测试通过 test_service.c 中的 MQTT 测试验证。