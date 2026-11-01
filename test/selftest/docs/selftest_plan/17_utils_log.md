# 模块 17：Log 日志

**文件**: `bos/utils/b_util_log.c`
**审核状态**: ✅ 已完成（2026-05-12）

---

## 一、代码理解

### 1.1 概述

Log 模块提供格式化日志输出功能，仅在 `_DEBUG_ENABLE=1` 时编译。

### 1.2 核心 API

```c
// 输出日志
void bLogOut(uint8_t type, const char *ptr_file, const char *ptr_func,
             uint32_t line, const char *fmt, ...);

// 注册自定义输出
void bLogRegOutputBytes(void (*pfn)(uint8_t *pbuf, uint16_t len));
```

---

## 二、实际测试用例

无独立测试函数（日志模块依赖 `b_log` 宏，测试时通过其他模块间接验证）。

---

## 三、测试文件

无独立文件，依赖其他模块测试间接验证。

---

## 四、备注

Log 模块为编译检查类，验证 `_DEBUG_ENABLE` 配置下编译成功。