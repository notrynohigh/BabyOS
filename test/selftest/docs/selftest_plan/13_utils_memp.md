# 模块 13：MemPool 内存池

**文件**: `bos/utils/b_util_memp.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

MemPool 模块实现固定大小块内存池，依赖 BabyOS 内部分配器（bMalloc/bFree）。

### 1.2 核心 API

```c
// 分配/释放
void *bMalloc(uint32_t size);
void *bCalloc(uint32_t num, uint32_t size);
void bFree(void *paddr);

// 查询
uint32_t bGetFreeSize(void);
uint32_t bGetTotalSize(void);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bMemAllocFree` | 分配 64 字节，写入 0xAA，释放 | 分配成功，内存可访问 |
| `test_bMemSizes` | 查询总大小和空闲大小 | total > 0, free <= total |
| `test_bCalloc` | calloc(8, 4)，验证内容为 0 | 分配成功，内容全 0 |

---

## 三、测试文件

```
test/selftest/test_utils.c
```

**依赖**:
- `bos/utils/b_util_memp.c`
- `bos/utils/inc/b_util_memp.h`
- 无硬件依赖

---

## 四、备注

MemPool 使用 BabyOS 内部分配器，不依赖系统堆。