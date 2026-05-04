# 模块 36：ADC Hub 多路 ADC 管理

**文件**: `bos/modules/b_mod_adchub.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

ADC Hub 模块管理多路 ADC 通道的采集和滤波。

### 1.2 核心 API

```c
// 注册通道
int bAdchubRegist(bAdcInstance_t *pinstance);

// 喂入采样值
int bAdchubFeedValue(uint8_t ad_seq, uint32_t ad_val);
```

---

## 二、实际测试用例

无独立测试函数（ADC Hub 依赖硬件 ADC）。

---

## 三、测试文件

无独立文件。

**依赖**:
- `bos/modules/b_mod_adchub.c`
- `_ADCHUB_ENABLE=1`

---

## 四、备注

ADC Hub 需要真实 ADC 硬件支持，selftest 环境不包含。