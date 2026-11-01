# 模块 09：Timer 定时器

**文件**: `bos/core/b_timer.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Timer 模块实现软件定时器，支持单次（ONE_SHOT）和周期（PERIOD）模式。

### 1.2 核心 API

```c
// 创建定时器
bTimerId_t bTimerCreate(bTimerFunc_t func, bTimerType_t type, void *argument, bTimerAttr_t *attr);

// 启动定时器（ms=周期）
int bTimerStart(bTimerId_t id, uint32_t ms);

// 停止定时器
int bTimerStop(bTimerId_t id);

// 查询是否运行
uint32_t bTimerIsRunning(bTimerId_t id);

// 删除定时器
int bTimerDelete(bTimerId_t id);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bTimerCreate` | 创建周期定时器 | id 非 NULL |
| `test_bTimerStart` | 启动后 IsRunning=1，Stop 后 IsRunning=0 | 状态正确切换 |

---

## 三、测试文件

```
test/selftest/test_core.c
```

**依赖**:
- `bos/core/b_timer.c`
- `bos/core/inc/b_timer.h`
- Task 模块

---

## 四、备注

定时器精度依赖 `bHalGetSysTick()`，测试环境使用 mock 实现。