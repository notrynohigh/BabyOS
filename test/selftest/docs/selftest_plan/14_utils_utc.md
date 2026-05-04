# 模块 14：UTC 时间

**文件**: `bos/utils/b_util_utc.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

UTC 模块实现 UTC 时间管理和日历转换，支持时区转换。

### 1.2 核心 API

```c
// 设置/获取 UTC 时间
void bUTC_SetTime(bUTC_t utc);
bUTC_t bUTC_GetTime(void);

// UTC ↔ 日历结构体转换
void bUTC2Struct(bUTC_DateTime_t *tm, bUTC_t utc, double zone);
bUTC_t bStruct2UTC(bUTC_DateTime_t tm, double zone);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bUTCStructConversion` | UTC 1577836800 (2020-01-01) → struct | year=2020, month=1, day=1 |
| `test_bUTCSetGet` | SetTime(1609459200) 再 GetTime | 返回相同值 |
| `test_bUTCTimestampRoundTrip` | 2020-07-04 → UTC → struct | 往返一致 |

---

## 三、测试文件

```
test/selftest/test_utils.c
```

**依赖**:
- `bos/utils/b_util_utc.c`
- `bos/utils/inc/b_util_utc.h`
- `bHalGetSysTickPlus()`（HAL mock）

---

## 四、备注

UTC 时间基准从 1970 年 1 月 1 日起算。