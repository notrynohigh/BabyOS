# 模块 38：GUI 图形界面

**文件**: `bos/modules/b_mod_gui.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

GUI 模块封装 LVGL/Arm-2D，提供液晶显示和触摸交互功能。

### 1.2 核心 API

```c
// 注册 GUI 实例
int bGUIRegist(bGUIInstance_t *pInstance);

// 获取 GUI 句柄
int bGUIGetHandle(uint32_t lcd_dev_no, UG_GUI **p_gui_handle);

// 选择显示设备
int bGUISelect(uint32_t lcd_dev_no);

// 位图绘制
int bGUIDrawBmp(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                const uint8_t *bmp);
```

---

## 二、实际测试用例

无独立测试函数（GUI 需要真实显示硬件支持）。

---

## 三、测试文件

无独立文件。

**依赖**:
- `bos/modules/b_mod_gui.c`
- `_GUI_ENABLE=1`

---

## 四、备注

GUI 模块需要真实 LCD/触摸硬件支持，selftest 环境不包含。