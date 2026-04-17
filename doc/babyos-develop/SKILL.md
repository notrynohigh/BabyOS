---
name: babyos-develop
description: |
  BabyOS 嵌入式框架核心开发助手。Use when developing BabyOS main code in bos/ directory, including:
  - 新增软件模块 (bos/modules/b_mod_xxx.c)
  - 新增硬件驱动 (bos/drivers/b_drv_xxx.c)
  - 新增服务层 (bos/services/)
  - 修改现有模块功能
  - MCU 适配 (bos/mcu/ 移植到新芯片)
  - HAL 接口设计和实现
  - Section 机制 (bDRIVER_REG_INIT, BOS_REG_POLLING_FUNC)
  - 理解模块架构 (Device、Task、Timer、FS、KV、MQTT、TCP/IP)

  Also triggers when user mentions: BabyOS开发, b_mod_xxx, b_drv_xxx, MCU移植, HAL, 嵌入式模块开发
---

# BabyOS 核心开发助手

## 项目结构

```
BabyOS/
├── bos/              # 核心代码
│   ├── algorithm/   # 算法: CRC, MD5, Base64, SHA1, Sort, UTF-8
│   ├── core/        # 核心: b_device.c, b_task.c, b_timer.c, b_queue.c, b_sem.c
│   ├── hal/         # 硬件抽象层接口 (HAL function declarations)
│   ├── mcu/         # MCU 移植代码 (ST, ESP32-C3, Renesas 等)
│   ├── modules/      # 软件模块 (b_mod_xxx.c)
│   ├── drivers/      # 硬件驱动 (b_drv_xxx.c)
│   ├── services/     # 高层服务 (OTA, HTTP, NTP, MQTT, Protocol)
│   ├── thirdparty/  # 第三方库 (FatFS, LittleFS, lwIP, LVGL, Unity)
│   ├── utils/       # 工具: FIFO, Memory Pool, UTC, Log
│   ├── b_section.h  # 链接器段扫描
│   └── b_os.h       # 主头文件
├── _config/         # 共享配置工具 (Kconfig)
└── test/            # 测试代码
```

---

## MCU 移植指南

### 1. 移植步骤

移植 BabyOS 到新 MCU 需要实现以下内容：

```
bos/mcu/
├── b_mcu_stm32f4xx/    # STM32F4 系列
├── b_mcu_esp32c3/       # ESP32-C3
├── b_mcu_r5f100/        # Renesas RL78
└── ...
```

### 2. 必须实现的 HAL 函数

在 `bos/hal/b_hal_xxx.c` 中实现：

```c
// 系统基础
void bHalInit(void);                      // 系统初始化
void bHalDelayMs(uint32_t ms);           // 延时 (阻塞)
uint32_t bHalGetSysTick(void);           // 系统时钟 (毫秒)

// GPIO
int bHalGpioInit(bHalGPIOPort_t port, bHalGPIOPin_t pin, bHalGPIOMode_t mode);
int bHalGpioWritePin(bHalGPIOPort_t port, bHalGPIOPin_t pin, uint8_t level);
int bHalGpioReadPin(bHalGPIOPort_t port, bHalGPIOPin_t pin);

// UART
int bHalUartSend(bHalUartNumber_t uart, const uint8_t *buf, uint16_t len);
int bHalUartReceive(bHalUartNumber_t uart, uint8_t *buf, uint16_t len);

// SPI
int bHalSpiInit(bHalSpiNumber_t spi, bHalSpiConfig_t *config);
int bHalSpiTransmitReceive(bHalSpiNumber_t spi, uint8_t *tx_buf, uint8_t *rx_buf, uint16_t len);

// I2C
int bHalI2cInit(bHalI2cNumber_t i2c, bHalI2cConfig_t *config);
int bHalI2cMasterTransmit(bHalI2cNumber_t i2c, uint8_t addr, uint8_t *buf, uint16_t len);
int bHalI2cMasterReceive(bHalI2cNumber_t i2c, uint8_t addr, uint8_t *buf, uint16_t len);

// Flash
int bHalFlashWrite(uint32_t addr, const uint8_t *buf, uint32_t len);
int bHalFlashRead(uint32_t addr, uint8_t *buf, uint32_t len);
int bHalFlashSectorErase(uint32_t addr);

// DMA
int bHalDmaInit(bHalDmaChlNumber_t chl, bHalDmaConfig_t *config);
int bHalDmaStart(bHalDmaChlNumber_t chl);
int bHalDmaStop(bHalDmaChlNumber_t chl);

// 中断
void bHalIntEnable(void);
void bHalIntDisable(void);
```

### 3. MCU 特定代码

```c
// bos/mcu/b_mcu_xxx/b_mcu_port.c

// 实现具体 MCU 的寄存器操作
void bHalDelayMs(uint32_t ms) {
    // 使用定时器实现延时
    // SysTick / 定时器中断
}

// 系统时钟初始化
void SystemClock_Config(void) {
    // 配置 PLL, 时钟分频等
}

// 中断处理
void SysTick_Handler(void) {
    // 更新系统时钟计数
}

// 可选: 提供 MCU 特定扩展
void bMcuSpecificInit(void) {
    // MCU 特定初始化
}
```

### 4. 链接器脚本

```ld
/* babyos.ld */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM (rwx)    : ORIGIN = 0x20000000, LENGTH = 128K
}

SECTIONS
{
    .text : {
        *(.isr_vector)
        *(.text)
        *(.rodata)
    } > FLASH

    .data : {
        *(.data)
    } > RAM

    /* BabyOS 特定段 */
    .driver_init : {
        PROVIDE(__driver_init_start = .);
        KEEP(*(.driver_init))
        PROVIDE(__driver_init_end = .);
    } > FLASH
}
```

### 5. 已有 MCU 移植参考

| MCU 系列 | 目录 | 说明 |
|---------|------|------|
| STM32F4 | `bos/mcu/b_mcu_stm32f4xx/` | STM32 标准库 |
| ESP32-C3 | `bos/mcu/b_mcu_esp32c3/` | IDF 框架 |
| Renesas RL78 | `bos/mcu/b_mcu_r5f100/` | 瑞萨 RL78 |

---

## 模块开发规范

---

## 模块开发规范

### 1. 创建新模块 (b_mod_xxx.c)

**文件结构**:
```c
/**
 * \file        b_mod_xxx.c
 * \brief       XXX module implementation
 * \author      Author
 *******************************************************************************
 * Copyright (c) 2024 Author
 * MIT License
 *******************************************************************************
 */

#include "modules/inc/b_mod_xxx.h"

#if (defined(_XXX_ENABLE) && (_XXX_ENABLE == 1))

// 模块私有变量
static xxx_context_t g_xxx_ctx = {0};

// 模块实现
int bXxxInit(void) {
    // 初始化逻辑
    return 0;
}

int bXxxFunc(int param) {
    // 功能实现
    return 0;
}

#endif
```

**头文件 (b_mod_xxx.h)**:
```c
#ifndef __B_MOD_XXX_H__
#define __B_MOD_XXX_H__

#include <stdint.h>
#include "b_config.h"

#if (defined(_XXX_ENABLE) && (_XXX_ENABLE == 1))

// 模块类型定义
typedef struct {
    // ...
} xxx_context_t;

// 模块函数
int bXxxInit(void);
int bXxxFunc(int param);

#endif
#endif
```

### 2. 添加 Kconfig 配置

在 `test/selftest/_config/Kconfig` 添加:
```
config _XXX_ENABLE
    bool "Enable XXX module"
    default n
    help
      Enable XXX module for ...
```

### 3. 关键设计模式

#### Device 抽象
```c
// 设备注册 (b_device.c)
int bDeviceRegister(bDriverInterface_t *drv, const char *name);

// 设备操作
int bOpen(int device_no, int flags);
int bRead(int fd, void *buf, int len);
int bWrite(int fd, const void *buf, int len);
int bCtl(int fd, int cmd, void *param);
int bClose(int fd);
```

#### Section 机制
```c
// 注册驱动初始化函数
bDRIVER_REG_INIT(driver_name);

// 注册轮询函数
BOS_REG_POLLING_FUNC(my_poll_func);

// 注册 Shell 命令
bSHELL_REG_CMD(my_cmd);
```

#### Task (Protothread)
```c
PT_THREAD(my_task(struct pt *pt, void *arg)) {
    PT_BEGIN(pt);

    // 等待条件
    PT_WAIT_UNTIL(pt, condition);

    // 延时
    bTaskDelayMs(pt, 100);

    PT_END(pt);
}
```

---

## 驱动开发规范

### 驱动结构 (bDriverInterface_t)

```c
static bDriverInterface_t g_xxx_driver = {
    .init   = _XxxInit,
    .read   = _XxxRead,
    .write  = _XxxWrite,
    .ctl    = _XxxCtl,
    .close  = _XxxClose,
};

static int _XxxInit(bDriverInterface_t *pdrv) {
    // 初始化
    return 0;
}

static int _XxxRead(bDriverInterface_t *pdrv, int pos, void *buf, int len) {
    // 读取
    return len;
}

// 注册驱动
bDRIVER_REG_INIT(g_xxx_driver);
```

### HAL 接口实现

```c
// port.c 中实现 HAL 函数
int bHalFlashWrite(uint32_t addr, const uint8_t *buf, uint32_t len) {
    // 具体实现
    return len;
}
```

---

## 已有模块参考

### 核心模块

| 模块 | 文件 | 用途 |
|------|------|------|
| Device | `b_device.c` | 统一设备抽象 |
| Task | `b_task.c` | 轻量级协程 (protothread) |
| Timer | `b_timer.c` | 软件定时器 |
| Queue | `b_queue.c` | 消息队列 |
| Semaphore | `b_sem.c` | 信号量 |

### 常用模块

| 模块 | 文件 | 用途 |
|------|------|------|
| FS | `b_mod_fs.c` | 文件系统抽象 (FatFS/LittleFS) |
| KV | `b_mod_kv.c` | 键值存储 |
| Shell | `b_mod_shell.c` | 命令行接口 |
| Select | `b_mod_select.c` | IO 多路复用 |

### 通信模块

| 模块 | 文件 | 用途 |
|------|------|------|
| TCP/IP | `b_mod_tcpip.c` | Socket 封装 |
| MQTT | `b_srv_mqtt.c` | MQTT 客户端 |
| HTTP | `b_srv_tcpip.c` | HTTP 客户端 |

---

## 64位平台兼容性

### 关键规则

```c
// 错误 - 指针截断
int bSocket(...) { return (int)ptrans; }

// 正确 - 使用 intptr_t
typedef intptr_t bSocketFd_t;
bSocketFd_t bSocket(...) { return (bSocketFd_t)(intptr_t)ptrans; }

// 恢复时
bTrans_t *ptrans = (bTrans_t *)(intptr_t)sockfd;
```

### 通用指针类型

| 类型 | 用途 |
|------|------|
| `intptr_t` | 可存储指针的整数 |
| `uintptr_t` | 无符号版本 |
| `bFSFd_t` | 文件描述符 |
| `bSocketFd_t` | Socket 描述符 |
| `bHttpFd_t` | HTTP 描述符 |

---

## 编译配置

### 配置项前缀

| 前缀 | 用途 |
|------|------|
| `_XXX_ENABLE` | 模块开关 |
| `_BOS_MODULES_ENABLE` | 模块总开关 |
| `_HALIF_VARIABLE_ENABLE` | HAL 变量接口 |

### 构建命令

```bash
cd test/selftest
make menuconfig    # 配置模块
make clean && make  # 编译
```

---

## 代码风格

1. **命名**: 小写下划线 `b_xxx_func`
2. **注释**: Doxygen 风格 `/** ... */`
3. **头文件**: `#ifndef __FILE_H__` 保护
4. **返回**: 0/-1 表示成功/失败
5. **错误码**: 负数表示错误

---

## 常见模式

### 回调模式
```c
typedef void (*callback_t)(int event, void *arg);

int register_callback(callback_t cb) {
    g_callback = cb;
    return 0;
}
```

### 状态机
```c
typedef enum {
    STATE_IDLE,
    STATE_WORKING,
    STATE_DONE,
} state_t;
```

### 链表节点
```c
struct list_head {
    struct list_head *next, *prev;
};
```

---

## 异步编程规范（重要）

### Protothread 必须规则

BabyOS 所有异步操作必须使用 Protothread 模式，**禁止使用 while 循环等待**：

```c
// ❌ 错误 - 阻塞等待，会卡死主循环
while (!condition) { }

// ✅ 正确 - 使用 PT 宏等待
PT_WAIT_UNTIL(pt, condition);

// ✅ 正确 - 超时等待
PT_WAIT_UNTIL(pt, condition || bTaskIsTimeout(pt, timeout_ms));
```

### API 函数 vs 状态机职责分离

| 操作 | 位置 | 说明 |
|------|------|------|
| 数据准备 | API 函数 | 解析参数、构建请求、分配内存 |
| Socket 创建 | 状态机 IDLE | 异步操作必须在任务中执行 |
| 连接操作 | 状态机 CONNECTING | bConnect 是异步的 |
| 发送数据 | 状态机 SENDING | 在状态机中执行 |
| 接收数据 | 状态机 RECVING | 使用 PT_WAIT_UNTIL |

```c
// API 函数 - 只做数据准备和重入检查
int bHttpClientRequest(void *handle, ...) {
    // 用状态判断重入
    if (ctx->state != HTTP_CLI_STA_IDLE) {
        return -1;
    }

    // 解析 URL
    // 构建请求头（动态分配）

    // 状态仍为 IDLE，由 send_buf != NULL 信号触发状态机
    return 0;
}

// 状态机 - HTTP内部逻辑都在这里
PT_THREAD(http_client_task(...)) {
    PT_BEGIN(pt);
    while (1) {
        switch (ctx->state) {
        case HTTP_CLI_STA_IDLE:
            // 等待请求信号
            PT_WAIT_UNTIL(pt, ctx->send_buf != NULL, 0);

            // HTTP内部逻辑：创建socket + bConnect
            ctx->sockfd = bSocket(...);
            bConnect(ctx->sockfd, ...);
            ctx->state = HTTP_CLI_STA_CONNECTING;
            break;

        case HTTP_CLI_STA_CONNECTING:
            PT_WAIT_UNTIL(pt, bSocketIsConnected(ctx->sockfd) == 1, 5000);
            if (PT_WAIT_IS_TIMEOUT(pt)) {
                ctx->state = HTTP_CLI_STA_IDLE;
                break;
            }
            ctx->state = HTTP_CLI_STA_SENDING;
            break;
        }
    }
    PT_END(pt);
}
```

**关键点**:
- API 只做数据准备 + 重入检查（用状态判断）
- Socket 创建和 bConnect 都在状态机 IDLE case 中
- API 返回后状态仍为 IDLE，由 `send_buf != NULL` 信号触发状态机

### MCU 栈空间保护

MCU 栈空间有限（通常 2KB-16KB），**禁止在函数内定义大数组**：

```c
// ❌ 错误 - 栈溢出风险
void func() {
    char buf[1024];      // 太大
    char req_header[256]; // 太大
    char host[64];       // 太大
}

// ✅ 正确 - 使用 static 或动态分配
static char s_buf[256];  // static 在 .data/.bss 段
// 或者
char *buf = bMalloc(size);  // 动态分配
```

### 异步指针有效期问题

API 函数接收的指针在异步执行时可能已失效，**必须复制数据到分配的缓冲区**：

```c
// ❌ 错误 - 用户指针可能在 API 返回后失效
int bHttpClientRequest(void *handle, const char *url) {
    ctx->url = url;  // 危险！用户可能释放了 url
}

// ✅ 正确 - 分配内存复制数据
int bHttpClientRequest(void *handle, const char *url) {
    int url_len = strlen(url) + 1;
    char *allocated_url = bMalloc(url_len);
    if (!allocated_url) return -1;
    memcpy(allocated_url, url, url_len);
    ctx->url = allocated_url;  // 安全的副本
}
```

### 重入检查必须切换状态

**错误写法**：判断状态但不切换，API 执行完状态没变
```c
// ❌ 错误 - 判断了状态但不切换，没有意义
if (ctx->state != HTTP_CLI_STA_IDLE) {
    return -1;  // 重入返回
}
// ... 数据准备 ...
return 0;  // 状态没变，重入检查形同虚设
```

**正确写法**：判断状态后立即切换
```c
// ✅ 正确 - 判断 + 切换
if (ctx->state != HTTP_CLI_STA_IDLE) {
    return -1;
}
// ... 数据准备 ...

// 在返回前切换状态，否则重入检查无效
ctx->state = HTTP_CLI_STA_PENDING;
return 0;
```

**状态机侧**：需要处理 PENDING 状态
```c
case HTTP_CLI_STA_IDLE:
    PT_WAIT_UNTIL(pt, ctx->send_buf != NULL, 0);
    /* fall through */  // 穿透到 PENDING

case HTTP_CLI_STA_PENDING:
    // HTTP内部逻辑
    ctx->state = HTTP_CLI_STA_CONNECTING;
    break;
```

### 代码审查检查清单

**异步代码必查项**:
- [ ] 没有使用 `while (...)` 阻塞等待
- [ ] 所有 socket 操作在状态机中，不在 API 函数中
- [ ] 没有大数组定义在栈上（>64 字节需谨慎）
- [ ] 用户指针已复制到分配缓冲区
- [ ] 每个 PT_WAIT_UNTIL 都有超时保护
- [ ] 状态转换配对（设置↔处理）
- [ ] 重入检查：判断状态后立即切换状态
- [ ] 资源释放路径完整（成功/失败都要释放）

---

## 参考文档

- `CLAUDE.md` - 项目总览
- `bos/b_os.h` - 主头文件
- `bos/b_section.h` - Section 机制
- `bos/core/inc/b_device.h` - 设备接口
- `test/selftest/docs/` - 模块设计文档
- `bos/thirdparty/pt/pt.h` - Protothread 头文件