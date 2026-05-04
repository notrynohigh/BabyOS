# 模块 10：Task 任务调度

**文件**: `bos/core/b_task.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Task 模块基于 Protothreads 实现轻量级协作式任务调度。

### 1.2 核心 API

```c
// 创建任务
bTaskId_t bTaskCreate(const char *name, bTaskFunc_t func, void *argument, bTaskAttr_t *attr);

// 挂起/恢复/删除任务
void bTaskSuspend(bTaskId_t id);
void bTaskResume(bTaskId_t id);
void bTaskRemove(bTaskId_t id);

// 获取任务名/id
const char *bTaskGetName(bTaskId_t id);
bTaskId_t bTaskGetId(void);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bTaskCreate` | 创建任务，验证名称，返回幂等 id | id 非 NULL |
| `test_bTaskSuspendResume` | Suspend 后任务不运行，Resume 后恢复 | g_task_counter 正确 |
| `test_bTaskYield` | 每次 bExec 调用一次任务 | g_task_yield_count 递增 |
| `test_bTaskGetId` | 获取当前任务 ID（仅任务内有效） | 正确获取 |

---

## 三、测试文件

```
test/selftest/test_core.c
```

**依赖**:
- `bos/core/b_task.c`
- `bos/core/inc/b_task.h`
- `bos/thirdparty/pt/pt.h`

---

## 四、备注

Task 使用 Protothreads 协作式调度，每个任务必须主动让出 CPU。