# 模块 33：TCPIP Socket 接口

**文件**: `bos/modules/b_mod_tcpip.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

TCPIP 模块提供 Socket 风格的 TCP/UDP 网络接口。

### 1.2 核心 API

```c
// 初始化
int bTcpIpInit(const bNetCardInfo_t *pnetcard, uint8_t number);

// Socket 操作
int bSocket(bTransType_t type, pbTransCb_t cb, void *user_data);
int bConnect(int sockfd, char *remote, uint16_t port);
int bBind(int sockfd, uint16_t port);
int bRecv(int sockfd, uint8_t *pbuf, uint16_t buf_len, uint16_t *rlen);
int bSend(int sockfd, uint8_t *pbuf, uint16_t buf_len, uint16_t *wlen);
int bShutdown(int sockfd);

// 查询
uint8_t bSocketIsConnected(int sockfd);
```

---

## 二、实际测试用例

无独立测试函数（TCPIP 通过 MQTT Service 测试间接验证）。

---

## 三、测试文件

无独立文件。

**依赖**:
- `b_mod_tcpip.h`
- `b_drv_testmac.h`
- `_TCPIP_ENABLE=1`

---

## 四、备注

TCPIP 模块测试通过 test_service.c 中的 MQTT 测试验证完整网络链路。