# 模块 11：Device 设备抽象

**文件**: `bos/core/b_device.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Device 模块是 BabyOS 硬件抽象核心，所有外设通过统一 API（open/read/write/ctl/close）访问。

### 1.2 核心 API（通过 bOpen/bRead/bWrite/bCtl/bClose）

```c
// 通过 bOpen/bRead/bWrite/bCtl/bClose 使用
// bFlashErase_t 用于擦除操作
typedef struct { uint32_t addr; uint32_t num; } bFlashErase_t;
```

---

## 二、实际测试用例

| 测试函数 | 描述 | 预期 |
|---------|------|------|
| `test_bDeviceNull` | 打开无效设备号 | fd < 0 |
| `test_bDeviceFlash` | 擦除→写入→读取，验证数据一致 | 往返一致 |
| `test_bDeviceDuplicateOpen` | 重复打开同一设备 | fd2 < 0（被拒绝） |

---

## 三、测试文件

```
test/selftest/test_core.c
```

**依赖**:
- `bos/core/b_device.c`
- `bos/core/inc/b_device.h`
- `b_device_list.h`
- TESTFLASH 驱动

---

## 四、备注

Device 模块测试使用 TESTFLASH（RAM 模拟 Flash）作为测试设备。