# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

BabyOS is a C-based embedded framework for MCU projects. It provides modular management of functional modules and peripheral drivers to shorten development cycles and reduce repetitive work. The official documentation is at https://babyos.cn/doc/.

**Compiler requirement:** C99 support required.
**Custom linker sections required:** `.driver_init`, `.bos_polling`, `.b_mod_shell`, `.b_mod_state` (and others defined in `bos/b_section.h`).

## Build Commands

```bash
# Run configuration (Kconfig menuconfig, generates b_config.h)
# Per-test: from within a test directory
cd test/<name> && make menuconfig

# Build a single test
cd test/<name> && make

# Clean a single test
cd test/<name> && make clean
```

Each test in `test/` has its own `Makefile` and `_config/` directory. The `_config/` contains: `b_device_list.h`, `b_hal_if.c`, `Kconfig`, and generated `b_config.h` — all specific to that test. Tests link against `test/babyos.ld` (a PC linker script, not for MCUs).

## Architecture

BabyOS uses a layered architecture:

```
Services (MQTT, OTA, TCP/IP, HTTP client, NTP, file transfer)
    ↓
Modules (KV, FS, GUI, WiFi, SSL, USB, Modbus, state machine, shell, button, IAP, etc.)
    ↓
Core (device abstraction, tasks, timers, queues, semaphores, select/I/O multiplexing)
    ↓
HAL (GPIO, UART, SPI, I2C, DMA, QSPI, SDIO, Ethernet, Flash, Watchdog, RNG)
    ↓
Drivers / MCU-specific code
```

## Key Design Patterns

**Device abstraction** (`bOpen`/`bRead`/`bWrite`/`bCtl`/`bClose`): All hardware and virtual devices are accessed through a unified file-descriptor API in `bos/core/inc/b_device.h`. Devices are enumerated via `B_DEVICE_REG()` macros in `b_device_list.h` (per-project) and driven by `bDriverInterface_t` structs.

**Section-based registration** (`bos/b_section.h`): Drivers and modules register via `bDRIVER_REG_INIT()` and `BOS_REG_POLLING_FUNC()` which place structs/function pointers into custom linker sections. The core scans these sections at boot. Supports GCC, Keil, IAR, and Renesas compilers.

> **⚠ Known pitfall — sizeof vs binary entry spacing**: The section scan loop uses `sizeof(struct)` as its stride. This works for all existing sections because they store **pointers** (pointer size == binary spacing). The only exception is `.b_srv_protocol` which stores full `bProtocolInstance_t` structs. On x86_64 Linux, `.rodata` aligns to 32 bytes, but the struct's natural sizeof is 24 (3× pointer), causing the loop to skip every other entry. The fix adds `reserved[8]` on 64-bit platforms. When adding a new struct-based section, always verify that `sizeof(struct)` matches the binary entry spacing on x86_64 before merging. See `test/selftest/claude_task.md` for the full investigation and verification method.

**Protothreads** (`bos/thirdparty/pt/pt.h`): Task scheduling uses lightweight protothreads (cooperative, no separate stacks), wrapped in `b_task.h`.

**Polling functions** (`bExec()`): Polling-based main loop — `bExec()` iterates through registered functions in the `.bos_polling` section. All time-driven logic (tasks, timers, buttons, GUI, etc.) is driven by `bExec()`, not by interrupts directly.

## Configuration System

`b_config.py` generates `b_config.h` from Kconfig rules. Each test directory has its own Kconfig+`b_config.py` setup. Key config prefixes: `_BOS_MODULES_ENABLE`, `_BOS_SERVICES_ENABLE`, `_BOS_ALGO_ENABLE`, `TICK_FRZ_HZ`.

## Source Layout

| Directory | Purpose |
|-----------|---------|
| `bos/algorithm/` | CRC, MD5, SHA1, Base64, sort, UTF-8 |
| `bos/core/` | Device abstraction, tasks, timers, queues, semaphores, select |
| `bos/hal/` | Hardware Abstraction Layer interfaces |
| `bos/drivers/` | 40+ hardware drivers (sensors, displays, storage, network, etc.) |
| `bos/mcu/` | MCU-specific porting code (ST, ESP32-C3, Renesas R5F, etc.) |
| `bos/modules/` | Software modules (KV, FS, GUI, TCP/IP, SSL, WiFi, USB, IAP, Modbus, shell, state, button, etc.) |
| `bos/modules/b_mod_netif/` | Network interface: lwIP integration, TCP/UDP client/server, ping |
| `bos/services/` | High-level services (MQTT, OTA, NTP, HTTP client, protocol, file transfer) |
| `bos/thirdparty/` | Bundled third-party libs (lwIP, mbedtls, LVGL, LittleFS, FatFS, TinyUSB, Unity, cJSON, Lua, etc.) |
| `bos/utils/` | FIFO, logging, memory pool, UTC time |
| `_config/` | Shared config tool, HAL interface definitions |
| `test/` | Unit tests for hardware-independent modules (queue, KV, cJSON, UTC, MQTT, SSL, coremark, etc.) |

Hardware-independent tests live in `test/` and run on Linux with GCC + pthread. Hardware-dependent examples are in the companion repository: https://gitee.com/notrynohigh/BabyOS_Example

## HAL Porting

To port BabyOS to a new MCU, implement the HAL functions declared in `_config/b_hal_if.h` and `bos/hal/inc/b_hal.h`: `bHalInit()`, `bHalDelayMs()`, `bHalGetSysTick()`, and GPIO/UART/SPI/I2C/DMA/QSPI/SDIO/Ethernet/Flash/Watchdog/RNG interfaces.

## Usage Pattern

```c
#include "b_os.h"

int main(void) {
    bInit();                         // Initialize BabyOS
    int fd = bOpen(SPIFLASH, BCORE_FLAG_RW);
    bRead(fd, buf, len);
    bWrite(fd, buf, len);
    bCtl(fd, CMD, param);
    bClose(fd);

    while (1) {
        bExec();                      // Poll registered functions
    }
}
```
