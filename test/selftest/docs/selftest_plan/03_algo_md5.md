# 模块 03：MD5 摘要

**文件**: `bos/algorithm/algo_md5.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

MD5 模块实现 MD5 哈希算法，提供 4 种输出格式（原始 16 字节、32 字符 hex、16 字符 hex 截断、低 8 字节）。纯软件实现，无任何硬件依赖。

### 1.2 核心 API

```c
// 原始 16 字节输出
void md5_hex_16(uint8_t *input, uint32_t ilen, uint8_t output[16]);

// 32 字符 hex 字符串（output[32]）
void md5_32(uint8_t *input, uint32_t ilen, uint8_t output[32]);

// 16 字符 hex 字符串（output[16]）
// 注意：此函数输出的是 out[4..15] 的 hex，而非原始 out[0..15]
void md5_16(uint8_t *input, uint32_t ilen, uint8_t output[16]);

// 低 8 字节 hex（output[8]）
void md5_hex_8(uint8_t *input, uint32_t ilen, uint8_t output[8]);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bMd5String` | MD5("abc") 前4字节 | 0x90, 0x01, 0x50, 0x98 |
| `test_bMd5String` | MD5("") 空字符串前2字节 | 0xD4, 0x1D |

**标准向量**:
- MD5("abc") = 900150983cd24fb0d6963f7d28e17f72
- MD5("") = d41d8cd98f00b204e9800998ecf8427e

---

## 三、测试文件

```
test/selftest/test_algo.c
```

**依赖**:
- `bos/algorithm/algo_md5.c`
- `bos/algorithm/inc/algo_md5.h`
- 无任何硬件依赖

---

## 四、备注

MD5 模块没有增量计算（update/final 分离）接口，只能一次性计算全部数据。对于大文件或流式数据，需要自行分块并缓存最后一个不完整块。