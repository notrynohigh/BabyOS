# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

BabyOS is a C-based embedded framework for MCU projects. It provides modular management of functional modules and peripheral drivers to shorten development cycles and reduce repetitive work. The official documentation is at https://babyos.cn/doc/.

**Compiler requirement:** C99 support required.
**Custom linker sections required:** `.driver_init`, `.bos_polling`, `.b_mod_shell`, `.b_mod_state` (and others defined in `bos/b_section.h`).

## Build Commands

```bash
# Configure (launches Kconfig menuconfig, generates b_config.h)
python _config/b_config.py <bos_path>    # Linux/macOS
python _config/b_config.bat               # Windows

# Build (standard GNU Make)
make
make clean
```

Each test module in `test/` also has its own `Makefile` — build them individually from their directories.

## Architecture

BabyOS uses a layered architecture:

```
Services (MQTT, OTA, TCP/IP, HTTP, file transfer)
    ↓
Modules (KV, FS, GUI, WiFi, USB, Modbus, state machine, etc.)
    ↓
Core (device abstraction, protothread tasks, timers, queues, semaphores)
    ↓
HAL (GPIO, UART, SPI, I2C, DMA, QSPI, SDIO, Ethernet, Flash, Watchdog, RNG)
    ↓
Drivers / MCU-specific code
```

## Key Design Patterns

**Device abstraction** (`bOpen`/`bRead`/`bWrite`/`bCtl`/`bClose`): All hardware and virtual devices are accessed through a unified file-descriptor API defined in `bos/core/inc/b_device.h`.

**Section-based registration** (`bos/b_section.h`): Drivers and modules register themselves via macros (`bDRIVER_REG_INIT()`, `BOS_REG_POLLING_FUNC()`) that place structs/function pointers into custom linker sections. The core scans these sections at boot. Supports GCC, Keil, IAR, and Renesas compilers.

**Protothreads** (`bos/thirdparty/pt/pt.h`): Task scheduling is based on lightweight protothreads (cooperative, no separate stacks), defined in `b_task.h`.

## Configuration System

`b_config.py` generates `b_config.h` from Kconfig rules. Enable/disable modules, algorithms, services, drivers, and hardware-specific options through `make menuconfig` or the Python script. Key config prefixes: `_BOS_MODULES_ENABLE`, `_BOS_SERVICES_ENABLE`, `_BOS_ALGO_ENABLE`, `TICK_FRZ_HZ`.

## Source Layout

| Directory | Purpose |
|-----------|---------|
| `bos/algorithm/` | CRC, MD5, SHA1, Base64, sort, UTF-8 |
| `bos/core/` | Device abstraction, tasks, timers, queues, semaphores |
| `bos/hal/` | Hardware Abstraction Layer interfaces |
| `bos/drivers/` | 40+ hardware drivers (sensors, displays, etc.) |
| `bos/mcu/` | MCU-specific porting code (ST, ESP32-C3, Renesas R5F, etc.) |
| `bos/modules/` | Software modules (KV, FS, GUI, TCP/IP, SSL, WiFi, USB, IAP, Modbus) |
| `bos/services/` | High-level services (MQTT, OTA, NTP, HTTP client) |
| `bos/thirdparty/` | Bundled third-party libs (lwIP, mbedtls, LVGL, LittleFS, FatFS, TinyUSB, Unity, etc.) |
| `bos/utils/` | FIFO, logging, AT command parser, memory pool, UTC time |
| `_config/` | Configuration tool (`b_config.py`), HAL interface definitions, generated `b_config.h` |
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
