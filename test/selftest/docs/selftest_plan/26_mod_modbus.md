# 模块 26：Modbus RTU

**文件**: `bos/modules/b_mod_modbus.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Modbus 模块实现 Modbus RTU Master，仅支持功能码 03（读寄存器）和 16（写多寄存器）。

### 1.2 核心 API（通过 Protocol Service 调用）

```
bPROTOCOL_REG_INSTANCE("modbus_master", _bModbusRTUMasterParse, _bModbusRTUMasterPackage);
```

**解析**：解析从机响应，CRC16_MODBUS 校验
**打包**：组 Read Regs / Write Regs 请求帧

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bModbusParseReadResp` | 解析 Read Regs 响应 (func=0x03) | len>=0, 回调触发 |
| `test_bModbusParseWriteRegsResp` | 解析 Write Regs 响应 (func=0x10) | len>=0, 回调触发 |
| `test_bModbusParseCrcError` | 错误 CRC 帧 | len<0 |
| `test_bModbusParseShortFrame` | 帧长度不足 | len<0 |
| `test_bModbusPkgReadReq` | 打包 Read Regs 请求 | buf[1]=0x03 |
| `test_bModbusPkgWriteReq` | 打包 Write Regs 请求 | buf[1]=0x10, pkglen=13 |
| `test_bModbusPkgBadCmd` | 不支持 cmd | pkglen=-1 |

---

## 三、测试文件

```
test/selftest/test_modules.c
```

**依赖**:
- `bos/services/inc/b_srv_protocol.h`
- `_MODBUS_ENABLE=1`
- CRC16_MODBUS 算法

---

## 四、备注

Modbus 模块通过 Protocol Service 间接使用。