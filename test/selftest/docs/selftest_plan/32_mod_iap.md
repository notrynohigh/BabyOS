# 模块 32：IAP 原地升级

**文件**: `bos/modules/b_mod_iap.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

IAP 模块实现 MCU 原地升级，支持固件接收、CRC 校验、分块存储和备份恢复。

### 1.2 核心 API

```c
// 初始化
int bIapInit(uint32_t cache_dev_no, uint32_t backup_dev_no, uint32_t backup_time_s);

// 事件处理
int bIapEventHandler(bIapEvent_t event, void *arg);

// 查询
uint8_t bIapGetStatus(void);
uint8_t bIapBackupIsValid(void);
uint8_t bIapPercentage(void);
```

---

## 二、实际测试用例

无独立测试函数（IAP 通过 OTA Service 测试间接验证）。

---

## 三、测试文件

无独立文件。

**依赖**:
- `bos/modules/b_mod_iap.c`
- `bos/modules/inc/b_mod_iap.h`
- HAL Flash 函数

---

## 四、备注

IAP 测试通过 test_service.c 中的 OTA Service 测试验证。