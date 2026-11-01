# 模块 25：Shell 命令行

**文件**: `bos/modules/b_mod_shell.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Shell 模块封装 MicroShell，提供命令行解析入口。

### 1.2 核心 API

```c
// 初始化
void bShellInit(void);

// 解析输入
int bShellParse(uint8_t *pbuf, uint16_t len);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bShellInitIdempotent` | 多次初始化 | 不崩溃 |
| `test_bShellParseNull` | pbuf=NULL | 返回 -1 |
| `test_bShellParseZeroLen` | len=0 | 返回 -1 |
| `test_bShellParseEmptyStr` | 空字符串 | 返回 -1 |
| `test_bShellVersionCmd` | 喂入 "bos -v\r" | 返回 0 |
| `test_bShellUnknownCmd` | 未注册命令 | 不崩溃 |

---

## 三、测试文件

```
test/selftest/test_modules.c
```

**依赖**:
- `bos/modules/b_mod_shell.c`
- `bos/modules/inc/b_mod_shell.h`
- `_NR_MICRO_SHELL_ENABLE=1`

---

## 四、备注

Shell 模块测试通过 `bShellParse` 直接喂入命令字符串。