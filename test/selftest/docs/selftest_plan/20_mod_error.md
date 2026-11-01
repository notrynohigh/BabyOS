# 模块 20：Error 错误管理

**文件**: `bos/modules/b_mod_error.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Error 模块实现轮询式错误管理队列，记录错误并按级别触发回调。

### 1.2 核心 API

```c
// 初始化
int bErrorInit(pecb cb);

// 注册错误
int bErrorRegist(uint8_t err, uint32_t interval_ms, uint32_t level);

// 查询/清除
int bErrorIsExist(uint8_t e_no);
int bErrorIsEmpty(void);
int bErrorClear(uint8_t e_no);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bErrorInit` | 正常初始化，幂等 | 返回 0 |
| `test_bErrorRegist` | 注册错误 | 返回 0 |
| `test_bErrorIsExistClear` | 注册→存在→清除→不存在 | 正确切换 |
| `test_bErrorClearInvalid` | 清除未注册错误 | 返回 0 |

---

## 三、测试文件

```
test/selftest/test_modules.c
```

**依赖**:
- `bos/modules/b_mod_error.c`
- `bos/modules/inc/b_mod_error.h`
- `bHalGetSysTick()`

---

## 四、备注

Error 模块是轻量级错误处理框架，不是异常捕获机制。