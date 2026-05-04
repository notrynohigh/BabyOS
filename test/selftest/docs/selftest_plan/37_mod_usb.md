# 模块 37：USB 设备

**文件**: `bos/modules/b_mod_usb.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

USB 模块封装 TinyUSB 库，提供 USB 设备功能。

### 1.2 核心 API

```c
// 初始化
int bUSBInit(uint8_t *dev_descrip, uint8_t **config_descrip,
             uint8_t config_num, uint8_t interface_num);

// HID 报告描述符
int bUSBHidSetReportDescrip(uint8_t **report_descrip, uint8_t report_num);
```

---

## 二、实际测试用例

无独立测试函数（USB 需要真实硬件支持）。

---

## 三、测试文件

无独立文件。

**依赖**:
- `bos/modules/b_mod_usb.c`
- `_USB_ENABLE=1`

---

## 四、备注

USB 模块需要真实 USB 硬件支持，PC 环境通常无 USB 设备，selftest 环境不包含。