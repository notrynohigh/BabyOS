# 模块 39：WiFi

**文件**: `bos/modules/b_mod_wifi.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

WiFi 模块提供 ESP8266/ESP32 等 WiFi 模组的 AT 命令封装。

### 1.2 核心 API

```c
// 初始化/反初始化
int bWifiInit(uint32_t dev_no, pWifiEvtCb_t cb, void *user_data);
int bWifiDeinit(void);

// 设置模式
int bWifiSetMode(bWifiMode_t mode);  // STATION/AP/STATION_AP

// 连接 AP
int bWifiJoinAp(const char *ssid, const char *passwd);
```

---

## 二、实际测试用例

无独立测试函数（WiFi 需要真实模组硬件支持）。

---

## 三、测试文件

无独立文件。

**依赖**:
- `bos/modules/b_mod_wifi.c`
- `_WIFI_ENABLE=1`

---

## 四、备注

WiFi 模块需要真实 WiFi 模组硬件支持，selftest 环境不包含。