# 模块 29：Ymodem

**文件**: `bos/modules/b_mod_ymodem.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Ymodem 是 Xmodem 的改进协议，支持 128/1024 字节块，CRC16 校验。

### 1.2 核心 API

通过 `bPROTOCOL_REG_INSTANCE` 注册：
```
bPROTOCOL_REG_INSTANCE("ymodem", _bYmodemParse, _bYmodemPackage);
```

---

## 二、实际测试用例

无独立测试函数（Ymodem 测试依赖于 Protocol Service 通用测试）。

---

## 三、测试文件

```
test/selftest/test_modules.c
```

**依赖**:
- `bos/services/inc/b_srv_protocol.h`
- `_YMODEM_ENABLE=1`

---

## 四、备注

Ymodem 测试见 [41_srv_protocol.md](41_srv_protocol.md)。