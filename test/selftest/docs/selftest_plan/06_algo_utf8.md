# 模块 06：UTF-8 / Unicode 互转

**文件**: `bos/algorithm/algo_utf8_unicode.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

UTF-8 / Unicode 模块实现 UTF-8 编码和 Unicode 码点之间的相互转换。纯软件实现，无任何硬件依赖。

### 1.2 核心 API

```c
// 查询 UTF-8 首字节返回对应 Unicode 码点的字节长度
int utf8_to_unicode_size(const uint8_t utf8);

// 将 Unicode 码点转换为 UTF-8 编码
int unicode_to_utf8(unsigned long unic, unsigned char *utf8, int utf8_size);

// 将 UTF-8 编码转换为 Unicode 码点
int utf8_to_unicode(const uint8_t *utf8, uint32_t *unicode);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bUtf8Encode` | U+0041→0x41, U+00E8→0xC3 0xA8, U+20AC→0xE2 0x82 0xAC | 返回正确字节数 |
| `test_bUtf8Decode` | 0x41→U+0041, 0xC3 0xA8→U+00E8, 0xC3 0xB1→U+00F1 | 返回正确码点 |
| `test_bUtf8Size` | ASCII 0x41→size=0, 0xC3→size=2 | 符合 BabyOS 约定 |

---

## 三、测试文件

```
test/selftest/test_algo.c
```

**依赖**:
- `bos/algorithm/algo_utf8_unicode.c`
- 无任何硬件依赖

---

## 四、备注

`utf8_to_unicode_size` 返回 0 表示 ASCII（1 字节字符），2-6 表示 UTF-8 字节数。