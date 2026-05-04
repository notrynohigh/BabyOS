# 模块 41：Protocol Service 协议服务

**文件**: `bos/services/b_srv_protocol.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Protocol Service 是 BabyOS 协议体系的调度中心，通过 name 查找已注册的协议实例。

### 1.2 核心 API

```c
// 初始化（按 name 查找已注册的协议实例）
bProtSrvId_t bProtSrvInit(bProtSrvAttr_t *attr, bProtSrvGetInfo_t func);

// 解析/打包
int bProtSrvParse(bProtSrvId_t id, uint8_t *in, uint16_t i_len, uint8_t *out, uint16_t o_len);
int bProtSrvPackage(bProtSrvId_t id, bProtoCmd_t cmd, uint8_t *buf, uint16_t buf_len);

// 订阅/取消订阅
int bProtSrvSubscribe(bProtSrvId_t id, bProtSrvSubscribe_t *psub);
int bProtSrvUnsubscribe(bProtSrvId_t id, bProtSrvSubscribe_t *psub);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bProtSrvInit` | 初始化 "xmodem128" 协议 | id 非 NULL |
| `test_bProtSrvParse` | 解析 SOH 帧 | len>=0, out[0]=0x06(ACK) |
| `test_bProtSrvParseEOT` | 解析 EOT 帧 | len>=0, out[0]=0x06 |
| `test_bProtSrvParseBad` | 解析错误帧 | len>=0, out[0]=0x06 |
| `test_bProtSrvPackage` | 打包 START/STOP 命令 | buf[0]=0x15/0x18 |
| `test_bProtSrvSubscribe` | 订阅命令 | 返回 0 |

---

## 三、测试文件

```
test/selftest/test_service.c
```

**依赖**:
- `bos/services/b_srv_protocol.c`
- `bos/services/inc/b_srv_protocol.h`
- `_PROTOCOL_SERVICE_ENABLE=1`

---

## 四、备注

Protocol Service 测试使用 xmodem128 协议（entry 0，可靠访问）。