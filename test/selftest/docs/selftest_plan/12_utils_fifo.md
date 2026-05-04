# 模块 12：FIFO 队列

**文件**: `bos/utils/b_util_fifo.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

FIFO 模块实现环形缓冲区，支持固定大小内存区域的读写操作。

### 1.2 核心 API

```c
// 初始化
int bFIFO_Init(bFIFO_Instance_t *pinstance, void *pbuf, uint16_t size);

// 查询长度
int bFIFO_Length(bFIFO_Instance_t *pinstance, uint16_t *plen);

// 清空
int bFIFO_Flush(bFIFO_Instance_t *pinstance);

// 写入/读取
int bFIFO_Write(bFIFO_Instance_t *pinstance, uint8_t *pbuf, uint16_t size);
int bFIFO_Read(bFIFO_Instance_t *pinstance, uint8_t *pbuf, uint16_t size);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bFIFOInit` | 初始化 FIFO | 返回 0 |
| `test_bFIFOWriteRead` | 写入 4 字节再读出 | 数据一致，返回 4 |
| `test_bFIFOLength` | 写入 1 字节后查询 | Length=1 |
| `test_bFIFOFlush` | Flush 后 Length=0 | 返回 0 |

---

## 三、测试文件

```
test/selftest/test_utils.c
```

**依赖**:
- `bos/utils/b_util_fifo.c`
- `bos/utils/inc/b_util_fifo.h`
- 无硬件依赖

---

## 四、备注

FIFO Write/Read 返回实际传输字节数，非请求数。