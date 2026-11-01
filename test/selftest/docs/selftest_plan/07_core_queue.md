# 模块 07：Queue 消息队列

**文件**: `bos/core/b_queue.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Queue 模块实现环形消息队列，支持固定大小消息的无阻塞存取。无内存分配，缓冲由调用方提供。

### 1.2 核心 API

```c
// 创建队列
bQueueId_t bQueueCreate(uint32_t msg_count, uint32_t msg_size, bQueueAttr_t *attr);

// 非阻塞写入（队列满返回 -2）
int bQueuePutNonblock(bQueueId_t id, const void *msg_ptr);

// 非阻塞读取（队列空返回 -3）
int bQueueGetNonblock(bQueueId_t id, void *msg_ptr);

// 重置 / 删除
int bQueueReset(bQueueId_t id);
int bQueueDelete(bQueueId_t id);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bQueueCreate` | 创建 5 消息队列 | id 非 NULL |
| `test_bQueuePutGet` | 写满 5 条再读出 | 返回 -2（满），返回 -3（空），数据一致 |
| `test_bQueueReset` | 重置后队列空 | 读取返回 -3 |

---

## 三、测试文件

```
test/selftest/test_core.c
```

**依赖**:
- `bos/core/b_queue.c`
- `bos/core/inc/b_queue.h`
- 无硬件依赖

---

## 四、备注

Queue 是无锁实现，裸机环境直接使用安全。