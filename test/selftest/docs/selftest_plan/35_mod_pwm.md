# 模块 35：PWM 脉宽调制

**文件**: `bos/modules/b_mod_pwm.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

PWM 模块提供软件 PWM 功能，通过 Timer 驱动 GPIO 输出方波。

### 1.2 核心 API

```c
// 启动/停止/重置
int bSoftPwmStart(bSoftPwmInstance_t *pPwmInstance, pPwmHandler handler);
int bSoftPwmStop(bSoftPwmInstance_t *pPwmInstance);
int bSoftPwmReset(bSoftPwmInstance_t *pPwmInstance);
```

---

## 二、实际测试用例

无独立测试函数（PWM 模块依赖硬件 Timer 和 GPIO）。

---

## 三、测试文件

无独立文件。

**依赖**:
- `bos/modules/b_mod_pwm.c`
- `_PWM_ENABLE=1`

---

## 四、备注

PWM 模块需要真实硬件 Timer 和 GPIO 支持，selftest 环境不包含。