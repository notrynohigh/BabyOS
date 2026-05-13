# 模块 23：Select I/O 多路复用

**文件**: `bos/modules/b_mod_select.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Select 模块实现 I/O 多路复用接口，监视多个文件描述符的可读/可写状态。

### 1.2 核心 API

```c
// I/O 多路复用
int bSelect(int maxfdp, bFdSet_t *readfds, bFdSet_t *writefds, bFdSet_t *errorfds);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bSelectNullFd` | 三组 fd 全为 NULL | 返回 -1 |
| `test_bSelectReadable` | TESTFLASH fd 设为读集 | 返回 >=0，fd 保留 |
| `test_bSelectWritable` | TESTFLASH fd 设为写集 | 返回 >=0，fd 保留 |

---

## 三、测试文件

```
test/selftest/test_modules.c
```

**依赖**:
- `bos/modules/b_mod_select.c`
- `bos/modules/inc/b_mod_select.h`
- Device 模块

---

## 四、备注

Select 是轮询模型（poll），非阻塞，每次调用遍历所有 fd。