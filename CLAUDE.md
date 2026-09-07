# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

BabyOS is a C-based embedded framework for MCU projects. Modular management of functional modules and peripheral drivers. Official docs: https://babyos.cn/doc/.

- **C99 required**
- **Custom linker sections**: `.driver_init`, `.bos_polling`, `.b_mod_shell`, `.b_mod_state` (see `bos/b_section.h`)
- **MCU-first**: never `#ifdef __linux__` etc. in common logic — write pure MCU code uniformly

## Build & Test

```bash
# Per-test menuconfig (writes _config/b_config.h)
cd test/<name> && make menuconfig
cd test/<name> && make           # build
cd test/<name> && make clean     # clean

# On-host regression suite (Linux + Unity)
cd test/selftest && make && ./build/BabyOS
```

Each `test/<name>/` has its own `Makefile` and `_config/` (with `b_device_list.h`, `b_hal_if.c`, `Kconfig`, generated `b_config.h`). Tests link against `test/babyos.ld` (a PC linker script, **not** for MCUs).

`tool/` holds Python utilities for protocol testing (`b_protocol.py`, `xmodem_ydmodem.py`, `http_server.py`) — **NOT compiled into firmware**.

## Architecture

```
Services (HTTP, MQTT, OTA, NTP, file transfer, config-web)
    ↓
Modules (KV, FS, GUI, WiFi, SSL, USB, Modbus, state, shell, button, IAP, tcpip, ...)
    ↓
Core (device abstraction, tasks, timers, queues, semaphores, select)
    ↓
HAL (GPIO/UART/SPI/I2C/DMA/QSPI/SDIO/Ethernet/Flash/Watchdog/RNG)
    ↓
Drivers / MCU-specific porting
```

## Architecture & Programming Constraints (READ FIRST)

### 1. `bInit()` / `bExec()` are USER-LEVEL only

`bInit()` and `bExec()` are called **only from user `main()`**. BabyOS-internal code MUST NOT call them — the framework does not call its own scheduler. To run a polling function, register it with `BOS_REG_POLLING_FUNC()` and let the user's `main()`'s `bExec()` loop drive it.

```c
// WRONG — framework calling its own scheduler
void bHttpServerStop(void) { bExec(); }   // ❌ never

// RIGHT — register a polling function for the user to drive
BOS_REG_POLLING_FUNC(my_task);            // ✅
```

### 2. PT (Protothread) API: yielding & delay

PT is a cooperative scheduler using `switch`-based state machines (`bos/thirdparty/pt/pt.h`, wrapped in `bos/core/inc/b_task.h`). Tasks declared with `PT_THREAD(name)(struct pt *pt, void *arg)` return a `char` and use `PT_BEGIN/PT_END/PT_WAIT_UNTIL/PT_YIELD/PT_DELAY_MS`.

Yielding / delay rules:
- **PT context** (`PT_BEGIN`/`PT_END`): use `bTaskYield(pt)` (=`PT_YIELD`), `bTaskDelayMs(pt, ms)` (=`PT_DELAY_MS`), `PT_WAIT_UNTIL(pt, cond, timeout_ms)`. These work because PT expands into `switch`+`case` labels that persist state across yields.
- **Non-PT context** (e.g., a public API like `bHttpServerStop()` called from main thread): **cannot** call any PT macro. Doing so produces "case label not within a switch" errors. Use only synchronous non-PT APIs (`bShutdown()`, `bSend()`, etc.) and let a registered polling task do the actual cleanup in its PT context.
- **Non-PT functions called from PT**: cannot use `bTaskYield()` (it references the PT context's `PT_YIELD_FLAG`). Either yield via `PT_YIELD(pt)` directly, or expose a separate PT version: `bHttpSendResponsePT(pt, fd, &resp)` invoked via `PT_WAIT_THREAD(pt, bHttpSendResponsePT(pt, fd, &resp))`.

### 3. PT state across yields

Variables inside a `PT_THREAD` function are **destroyed every yield** unless declared `static`. State that must survive between yields (recv buffer offset, send chunk index, timeout state machine) MUST be `static`. Local stack variables become garbage after the first yield.

```c
PT_THREAD(_bMyTask)(struct pt *pt, void *arg) {
    static int  s_off = 0;          // ✅ persists across yields
    static char s_buf[64];          // ✅
    int tmp = bHalGetSysTick();     // ⚠ tmp is local; recompute each entry if needed
    PT_BEGIN(pt);
    ...
    PT_END(pt);
}
```

### 4. MCU resource constraints

- **No large stack arrays**. Anything ≥64 B should be `static` or heap-allocated (`bMalloc`/`bRealloc`).
- **No VLAs**.
- **No busy-wait / spin**. Any "wait for X" loop must yield via PT (`PT_WAIT_UNTIL`, `PT_DELAY_MS`) with a timeout. Pure spin loops starve the entire PT scheduler on single-core MCUs.
- **No platform `#ifdef`** (`__linux__`, `__APPLE__`, `_WIN32`, etc.) in common logic code. If a function is unsafe for MCU, mark it deprecated / host-only via doc, or restructure into PT-safe + non-PT-safe variants instead of guarding it out.

### 5. Coding standards

- All `bHal*` `int`-returning functions must have their return value checked.
- All `bRead/bWrite/bCtl/bConnect/bSend/bRecv` must check returns (`!= expected_len`, `< 0`, etc.).
- Division by variables that could be zero must be guarded.
- GPIO wait loops must have timeout protection (`TICK_DIFF_BIT32` vs `MS2TICKS`).
- Never modify `bos/thirdparty/` — vendored code.

## Coding Standards — Error Handling Examples

```c
// HAL
if (bHalI2CMemRead(_if, reg, 1, data, len) != 0) { return -1; }

// Device I/O
if (bRead(fd, buf, len) != expected_len) { bClose(fd); return -1; }

// Network
if (bSend(sockfd, buf, len, NULL) <= 0) { /* handle */ }

// Guard divide-by-zero
if (sector_size == 0) { return -1; }

// GPIO timeout
uint32_t tick = bHalGetSysTick();
while (bHalGpioReadPin(port, pin)) {
    if (TICK_DIFF_BIT32(tick, bHalGetSysTick()) > MS2TICKS(1000)) return -1;
}
```

## Known Pitfalls

**Section scan stride mismatch**: `sizeof(struct)` is used as loop stride. Works for pointer-based sections; **does not** work for `.b_srv_protocol` (stores full structs). On x86_64 Linux, `.rodata` aligns to 32 B but `bProtocolInstance_t` is 24 B (3 pointers) — must pad with `reserved[8]` on 64-bit. Verify `sizeof(struct)` matches binary entry spacing on x86_64 before adding new struct-based sections. See `test/selftest/claude_task.md`.

**Encrypt loop silent failures** (`b_mod_kv.c`): return immediately on first error, do not continue.

**YMODEM CRC precedence**: `crc = (crc ^ pbuf[i]) << 8` (parens required).

**`bWifiModule.dev_no`**: must be wrapped with `#if (defined(_WIFI_ENABLE) && (_WIFI_ENABLE == 1))`.

**HTTP `bHttpSendResponse`**: deprecated on bare-metal. Use `bHttpSendResponsePT(pt, fd, resp)` from PT context instead (`bos/services/b_srv_http.h`).

**NTP socket fd leak**: `_bNtpTask` has no public close API; relies on `B_TRANS_DISCONNECT` callback to mark socket freed via `s_ntp_disconnected` before resetting `s_ntp.sockfd`.

**NTP 2036 era**: `b_srv_ntp.c` handles era rollover by checking high bit and ORing `0xFFFFFFFF00000000ULL` before subtracting the NTP-to-Unix offset.

**Config-web `_bParseParams(NULL, ...)`**: underflows `dest_len - 1`. Guard with `if (dest == NULL || dest_len == 0) return;`.

## Network Module Architecture

```
bTcpIpInit() → registers netcards via bNetCardInfo_t array
bWifiInit()  → initializes WiFi separately, stores dev_no in bWifiModule
```

Key types:
- **`bNetCardInfo_t`** (`bos/modules/inc/b_mod_tcpip.h`) — *registration* info: `dev_no`, `priority`, `ignore_ip`, `assigned_ip{ip, mask, gateway}`.
- **`bNetcardStaInfo_t`** (`bos/modules/inc/b_mod_tcpip.h`) — *runtime query* result: `dev_no`, `priority`, **bitfield** `is_linked:1 / is_ignore_ip:1 / is_dhcp:1 / is_ethnet:1 / is_wifi:1`, plus `ipaddr/netmask/gateway`.
- **`bNetcardType_t`** (`bos/drivers/inc/b_driver_cmd.h`) — driver-side enum: `B_NETCARD_TYPE_UNKNOWN=0`, `B_NETCARD_TYPE_WIFI=1`, `B_NETCARD_TYPE_ETH=2`.

Query API (`b_mod_tcpip.h`):
```c
uint8_t  bTcpIpGetNetcardCount(void);
int      bTcpIpGetNetcardInfo(uint8_t index, bNetcardStaInfo_t *info);
int      bTcpIpSetActiveNetcard(uint32_t dev_no);
uint32_t bTcpIpGetCurrentDevNo(void);
// bTcpIpGetNetcardTypeByDevNo() is NOT implemented. Distinguish via info.is_wifi / info.is_ethnet,
// or compare dev_no against bWifiModule.dev_no.
```

## TCP/IP Service (`b_srv_tcpip`)

Wrapper for netcard registration:
```c
#include "services/inc/b_srv_tcpip.h"
bTcpipSrvInit(netcard_info, 2);
```

## NTP Service (`b_srv_ntp`)

Function name is **`bSntpStart`** (with the `S`). Use `bSntpStop()` to stop synchronization before reboot.
```c
bSntpStart(3600);  // re-sync every hour
bSntpStop();        // stop NTP task and release socket
```
Server addresses: `_NTP_SERVER_1/2/3` in `bos/services/Kconfig` (defaults: `ntp1.aliyun.com`, etc.). Enable with `_NTP_SERVICE_ENABLE = 1`.

## HTTP Service (`b_srv_http`)

Unified client + server. **Use PT-aware API**: `bHttpSendResponsePT(pt, fd, &resp)` invoked via `PT_WAIT_THREAD`. The non-PT `bHttpSendResponse` is host-only (deprecated on bare-metal).

Server streaming constants (override per-project in `b_config.h` to size BSS for your MCU):
```c
_HTTP_MAX_REQ_SIZE          512   // BSS rbuf per connection
_HTTP_RECV_CHUNK_SIZE       256   // max bytes consumed per bExec()
_HTTP_SEND_CHUNK_TIMEOUT_MS 5000  // per-chunk send deadline
```

**`bHttpServerStop()`** is non-blocking: it sets `stop_requested` on the server, shuts down listen fd + client fds (synchronous), and returns immediately. The actual cleanup of sub-tasks / ctx happens in `_bHttpServerTask` (registered polling function) on the next `bExec()` round. The user's `main()` must keep running `bExec()` and must not free the server memory until `_bHttpServerTask` has finished cleanup (the global `s_http_server` pointer will be cleared to `NULL`).

## Config Web Service (`b_srv_config_web`)

> **Note:** The `b_srv_config_web` service is not yet merged into the current branch. The following API description is kept as a forward reference for the upcoming feature branch.

```c
bConfigWebServiceStart(80, config_cb, user_data);
```
Endpoints: `GET /`, `POST /api/wifi`, `POST /api/eth`, `GET /api/status`, `GET /api/netcards`, `POST /api/setnetcard`. **Note:** `_CONFIG_WEB_SERVICE_ENABLE` already `select`s `_HTTP_SERVICE_ENABLE` in Kconfig — enabling Config-Web implicitly enables HTTP.

Caveats (see `tool/review_config-web.md`): `/api/eth` does not validate IP octets; `/api/wifi` SSID/pass buffers can silently truncate long %XX values; `_bUrlDecode` accepts control/high-bit chars; always check `result` arg of `config_cb`.

## Code Review Reports

Each merged feature branch should produce a report under `tool/review_<branch>.md` listing bugs by severity with file:line, failure scenarios, and fix suggestions. New Claude instances working on a branch should read its report first.

- `tool/review_config-web.md` — bugs found in `feat: 添加配网Web服务`.

## HAL Porting

Implement HAL functions declared in `_config/b_hal_if.h` and `bos/hal/inc/b_hal.h`: `bHalInit()`, `bHalDelayMs()`, `bHalGetSysTick()`, and GPIO/UART/SPI/I2C/DMA/QSPI/SDIO/Ethernet/Flash/Watchdog/RNG interfaces.

## Usage Pattern

```c
#include "b_os.h"

int main(void) {
    bInit();
    int fd = bOpen(SPIFLASH, BCORE_FLAG_RW);
    bRead(fd, buf, len);
    bClose(fd);

    while (1) {
        bExec();   // drives all registered polling functions
    }
}
```