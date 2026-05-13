# 模块 27：Protocol 协议编解码

**文件**: `bos/modules/b_mod_protocol.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Protocol 模块实现 BabyOS 私有协议（TEA 加密 + 累加和校验）。

### 1.2 核心 API

通过 `bPROTOCOL_REG_INSTANCE` 注册到 Protocol Service：
```
bPROTOCOL_REG_INSTANCE("bos", _bProtocolParse, _bProtocolPackage);
```

---

## 二、实际测试用例

无独立测试函数（BabyOS 私有协议测试依赖于 Protocol Service 通用测试）。

---

## 三、测试文件

```
test/selftest/test_modules.c
```

**依赖**:
- `bos/services/inc/b_srv_protocol.h`
- `bos/modules/b_mod_protocol.c`

---

## 四、备注

BabyOS 私有协议测试见 [41_srv_protocol.md](41_srv_protocol.md)。