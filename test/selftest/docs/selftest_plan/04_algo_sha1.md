# 模块 04：SHA1 / HMAC-SHA1

**文件**: `bos/algorithm/algo_hmac_sha1.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

HMAC-SHA1 模块实现纯 SHA1 哈希和 HMAC-SHA1 消息认证码。纯软件实现，无任何硬件依赖。

### 1.2 核心 API

```c
// HMAC-SHA1：key + data 计算消息认证码，digest 输出 20 字节
void hmac_sha1(uint8_t *key, int key_length,
               uint8_t *data, int data_length,
               uint8_t *digest);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bSha1Hmac` | HMAC-SHA1("abc", key="") 首字节 | 0x9B |

---

## 三、测试文件

```
test/selftest/test_algo.c
```

**依赖**:
- `bos/algorithm/algo_hmac_sha1.c`
- 无任何硬件依赖

---

## 四、备注

SHA1 算法用于消息完整性验证，HMAC-SHA1 提供带密钥的认证功能。