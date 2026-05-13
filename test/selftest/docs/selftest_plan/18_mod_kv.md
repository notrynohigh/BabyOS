# 模块 18：KV 键值存储

**文件**: `bos/modules/b_mod_kv.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

KV 模块实现嵌入式 Flash 键值存储，支持多区块管理和自动垃圾回收。

### 1.2 核心 API

```c
// 初始化
int bKVInit(bKVInstance_t *pinstance);

// 设置/获取值
int bKVSetValue(bKVInstance_t *pinstance, const char *key, const uint8_t *pbuf, uint32_t len);
int bKVGetValue(bKVInstance_t *pinstance, const char *key,
                uint8_t *pbuf, uint32_t len, uint32_t *prlen);

// 删除/查询
int bKVDeleteValue(bKVInstance_t *pinstance, const char *key);
int bKVGetValueLength(bKVInstance_t *pinstance, const char *key);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bKVSetGet` | SetValue("test_key") → GetValue | 值一致 |
| `test_bKVDelete` | Delete 后 GetValueLength | 返回 -1 |
| `test_bKVMultipleKeys` | 10 个不同 key 各自独立 | 互不干扰 |
| `test_bKVOverwrite` | 同一 key 两次 SetValue | 最新值覆盖旧值 |

---

## 三、测试文件

```
test/selftest/test_modules.c
```

**依赖**:
- `bos/modules/b_mod_kv.c`
- `bos/modules/inc/b_mod_kv.h`
- Device 模块（TESTFLASH）
- CRC32 模块

---

## 四、备注

KV 模块测试使用 TESTFLASH 作为存储后端。