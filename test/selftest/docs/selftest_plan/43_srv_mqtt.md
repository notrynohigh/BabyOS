# 模块 43：MQTT Service

**文件**: `bos/services/b_srv_mqtt.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

MQTT Service 实现 MQTT 客户端，基于 mqtt-pack 库和 TCPIP Socket。

### 1.2 核心 API

```c
// 启动 MQTT 客户端
int bMqttSrvStartWithCfg(pbMqttCallback_t cb, void *arg);

// 销毁
void bMqttSrvDestroy(void);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bMqttSrvRealConnection` | 连接 babyos.cn MQTT broker | 连接/发布成功或网络不可达 |

---

## 三、测试文件

```
test/selftest/test_service.c
```

**依赖**:
- `bos/services/b_srv_mqtt.c`
- `bos/services/inc/b_srv_mqtt.h`
- `_MQTT_SERVICE_ENABLE=1`

---

## 四、备注

MQTT 测试需要网络可达 babyos.cn:1883。