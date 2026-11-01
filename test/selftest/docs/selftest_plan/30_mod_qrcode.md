# 模块 30：QRCode 二维码

**文件**: `bos/modules/b_mod_qrcode.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

QRCode 模块封装第三方 qrcode 库，提供二维码生成功能。

### 1.2 核心 API

```c
// 创建二维码
int bQRCodeCreate(bQRCode_t *pInstance, uint8_t *pdata, uint16_t data_len);

// 查询某点
int bQRCodeGetValue(bQRCode_t *pInstance, uint8_t x, uint8_t y);
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bQRCodeCreate` | 创建 "ABC123" 二维码 | 返回 0 |
| `test_bQRCodeGetValue` | 查询二维码内各点 | 返回 0 或 1 |

---

## 三、测试文件

```
test/selftest/test_modules.c
```

**依赖**:
- `bos/modules/b_mod_qrcode.c`
- `bos/modules/inc/b_mod_qrcode.h`
- `bos/thirdparty/qrcode/qrcode.h`
- 无硬件依赖

---

## 四、备注

QRCode 是纯算法模块，测试直接调用 API 即可。