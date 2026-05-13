# 模块 16：AT Parser

**文件**: `bos/utils/b_util_at.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

AT Parser 模块实现 AT 命令解析器，通过 protothread 协程处理 AT 命令发送和响应等待。

### 1.2 核心 API

```c
// 初始化
int bAtInit(bAtStruct_t *pat, pAtCmdCb_t cmd_cb,
            pAtSendData_t send, void *user_data);

// 发送命令
int bAtSendCmd(bAtStruct_t *pat, const char *pcmd,
               const char *resp, uint16_t timeout);

// 喂入响应数据
int bAtFeedData(bAtStruct_t *pat, uint8_t *pbuf, uint16_t len);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bParseString` | 解析 "key=123"，提取数值 | num=123 |

---

## 三、测试文件

```
test/selftest/test_utils.c
```

**依赖**:
- `bos/utils/b_util_at.c`
- `bos/utils/inc/b_util_at.h`
- Task 模块

---

## 四、备注

AT Parser 依赖 Task 模块创建内部任务。