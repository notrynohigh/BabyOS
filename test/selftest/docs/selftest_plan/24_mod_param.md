# 模块 24：Param 运行时参数

**文件**: `bos/modules/b_mod_param.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Param 模块通过 Shell 命令提供运行时变量读写功能。变量通过 `bPARAM_REG_INSTANCE` 注册到 Flash 段。

### 1.2 核心 API（Shell 命令）

| Shell 命令 | 功能 |
|-----------|------|
| `param` | 列出所有参数 |
| `param <name>` | 读取参数 |
| `param <name> <value>` | 写入参数 |

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bParamShellSet` | param g_param_test_val -777 | 变量被设置为 -777 |
| `test_bParamShellSet2` | 设置两个不同参数 | 各自正确 |
| `test_bParamShellSetRoundTrip` | param g_param_test_val 0 | 变量被设置为 0 |
| `test_bParamShellGet` | param g_param_test_val（不变） | 验证读取 |
| `test_bParamShellList` | param（列出所有参数） | 正常执行 |

---

## 三、测试文件

```
test/selftest/test_modules.c
```

**依赖**:
- `bos/modules/b_mod_param.c`
- `bos/modules/inc/b_mod_param.h`
- Shell 模块
- `_NR_MICRO_SHELL_ENABLE=1`

---

## 四、备注

Param 模块无独立 C API，全部通过 Shell 命令操作。