# 模块 21：State 状态机

**文件**: `bos/modules/b_mod_state.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

State 模块实现轻量级状态机，支持扩展状态机（命名实例）。

### 1.2 核心 API

```c
// 创建/添加状态机
int bStateCreate(const char *name, bStateAttr_t *attr);
int bStateAdd(const char *name, bStateInfo_t *pinfo);

// 状态转移
int bStateTransferExt(const char *name, uint32_t state);

// 触发事件
int bStateInvokeEventExt(const char *name, uint32_t event, void *arg);

// 获取当前状态
int bGetCurrentStateExt(const char *name);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bStateCreate` | 创建状态机 | 返回 0 |
| `test_bStateAdd` | 添加状态节点 | 返回 0 |
| `test_bStateTransfer` | 状态转移 | enter/exit 回调被调用 |
| `test_bStateInvokeEvent` | 触发事件 | 事件处理函数被调用 |

---

## 三、测试文件

```
test/selftest/test_modules.c
```

**依赖**:
- `bos/modules/b_mod_state.c`
- `bos/modules/inc/b_mod_state.h`
- Task 模块

---

## 四、备注

State 模块通过 `bSECTION_FOR_EACH` 管理状态节点。