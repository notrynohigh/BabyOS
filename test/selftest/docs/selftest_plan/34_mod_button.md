# 模块 34：Button 按钮

**文件**: `bos/modules/b_mod_button.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Button 模块封装 flexiblebutton 第三方库，提供 GPIO 按键事件检测。

### 1.2 核心 API

```c
// 添加按键
int bButtonAddKey(bButtonInstance_t *pbutton, flex_button_t *pflex);
```

---

## 二、实际测试用例

无独立测试函数（Button 测试通过 Driver 测试间接验证）。

---

## 三、测试文件

无独立文件。

**依赖**:
- `bos/modules/b_mod_button.c`
- `bos/modules/inc/b_mod_button.h`
- `_FLEXIBLEBUTTON_ENABLE=1`

---

## 四、备注

Button 测试见 test_drivers.c 中的 bKEY 驱动测试。