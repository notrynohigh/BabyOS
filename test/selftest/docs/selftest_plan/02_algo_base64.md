# 模块 02：Base64 编解码

**文件**: `bos/algorithm/algo_base64.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Base64 模块实现标准的 Base64 编码和解码，纯软件实现，无任何硬件依赖。通过查表完成字节到字符的映射，输入输出均为内存缓冲区。

### 1.2 核心 API

```c
// 编码：输入字节数组，输出 Base64 字符串（null 结尾）
// 返回值：输出字符串长度（不含结尾 null）
uint16_t base64_encode(const uint8_t *in, uint16_t inlen, char *out);

// 解码：输入 Base64 字符串，输出字节数组
// 返回值：输出字节长度（0 表示输入格式错误）
uint16_t base64_decode(const char *in, uint16_t inlen, uint8_t *out);
```

### 1.3 实现细节

**编码表** (`base64en`，64 项):
- 索引 0–25: 'A'–'Z'
- 索引 26–51: 'a'–'z'
- 索引 52–61: '0'–'9'
- 索引 62: '+'
- 索引 63: '/'

**解码表** (`base64de`，256 项): ASCII 值为索引，映射回 0–63；无效字符映射为 255

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bBase64Encode` | 编码 `0x00 0xFF 0x42 0xAB 0xCD 0xEE 0x77 0x11` 后解码 | 往返一致 |

**测试向量**: RFC 4648 标准向量

---

## 三、测试文件

```
test/selftest/test_algo.c
```

**依赖**:
- `bos/algorithm/algo_base64.c`
- `bos/algorithm/inc/algo_base64.h`
- 无任何硬件依赖

---

## 四、备注

Base64 编码通过往返测试（encode → decode）验证正确性。