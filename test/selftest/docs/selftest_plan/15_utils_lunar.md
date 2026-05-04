# 模块 15：Lunar 农历转换

**文件**: `bos/utils/b_util_lunar.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Lunar 模块实现公历到农历的单向转换，支持 1901-2099 年。

### 1.2 核心 API

```c
// 公历 → 农历（返回 0=成功，-1=参数无效）
int bSolar2Lunar(uint16_t syear, uint8_t smonth, uint8_t sday, bLunarInfo_t *plunar);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bLunarCalendar` | 2021-02-12（春节）→ 农历 | month=1, day=1 |

---

## 三、测试文件

```
test/selftest/test_utils.c
```

**依赖**:
- `bos/utils/b_util_lunar.c`
- `bos/utils/inc/b_util_lunar.h`
- 无硬件依赖

---

## 四、备注

农历历法表覆盖 1901-2099 年，超出范围需更新数据。