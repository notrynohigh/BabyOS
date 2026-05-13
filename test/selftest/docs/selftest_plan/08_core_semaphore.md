# 模块 08：Semaphore 信号量

**文件**: `bos/core/b_sem.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Semaphore 模块实现标准计数信号量，提供非阻塞的 acquire 和 release 操作。

### 1.2 核心 API

```c
// 创建信号量
bSemId_t bSemCreate(uint32_t max_count, uint32_t initial_count, bSemAttr_t *attr);

// 非阻塞获取（信号量为 0 返回 -1）
int bSemAcquireNonblock(bSemId_t id);

// 释放信号量
int bSemRelease(bSemId_t id);

// 查询当前计数
uint32_t bSemGetCount(bSemId_t id);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bSemCreate` | 创建 max=1, init=1 | id 非 NULL |
| `test_bSemAcquireRelease` | acquire(0)→-1, release→count=1, 再 acquire→0 | 正确同步 |
| `test_bSemCounting` | 创建 max=3, init=3, acquire 3 次 | count=0, 再 acquire→-1 |

---

## 三、测试文件

```
test/selftest/test_core.c
```

**依赖**:
- `bos/core/b_sem.c`
- `bos/core/inc/b_sem.h`
- 无硬件依赖

---

## 四、备注

信号量为非阻塞设计，acquire 在 value=0 时立即返回 -1。