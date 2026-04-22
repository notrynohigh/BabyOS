# BabyOS Project Generator
# babyos_project_generator.py
#
# A cross-platform project generator for BabyOS embedded framework.
# Primary target: Windows (with Linux/macOS support)
#
# Author: aiclaw (liklon)
# Created: 2026-04-22

import os
import sys
import shutil
import argparse
from pathlib import Path

# Version
VERSION = "1.0.0"

# Supported MCU vendors (matching BabyOS Kconfig)
VENDORS = {
    "st": "STMicroelectronics",
    "nation": "Nationstech",
    "mm": "Mindmotion",
    "hc": "HDSC",
    "wch": "WinChipHead",
    "holtek": "Holtek",
    "artery": "Artery",
    "hk": "HK",
    "espressif": "ESPRESSIF",
    "py": "PY",
    "cw": "CW",
}

# Vendor to chip map (key -> list of chips)
VENDOR_CHIPS = {
    "st": ["STM32F10X_LD", "STM32F10X_MD", "STM32F10X_HD", "STM32F10X_CL",
           "STM32G0X0", "STM32L41X_L46X", "STM32F40X", "STM32G4XX", "STM32H5XX",
           "STM32L0X3", "STM32G0B1"],
    "nation": ["NATION_L40X", "NATION_G45X"],
    "mm": ["MM32SPIN2X", "MM32SPIN0X"],
    "hc": ["HC32L13X", "HC32L07X", "HC32L17X"],
    "wch": ["CH32F103"],
    "holtek": ["HT32F", "HT66F", "HT68F"],
    "artery": ["AT32F415", "AT32F435", "AT32F437"],
    "hk": ["HK32F0", "HK32F1"],
    "espressif": ["ESP32C3", "ESP32S3", "ESP32"],
    "py": ["PY32F0", "PY32F4"],
    "cw": ["CW32F0", "CW32L"],
}

# BabyOS modules (for configuration)
BOS_MODULES = [
    "button", "kv", "fs", "gui", "iap", "menu", "modbus", "param",
    "protocol", "pwm", "qrcode", "shell", "ssl", "state", "tcpip",
    "trace", "usb", "wifi", "xm128", "xmodem", "adchub", "select",
]

# Algorithms available
BOS_ALGORITHMS = [
    "base64", "crc", "crc8", "crc16", "crc32", "hmac_sha1", "md5", "sha1", "sort", "utf8",
]

# Services
BOS_SERVICES = ["mqtt", "ota", "protocol", "tcpip", "transfile"]

# Drivers (subset of common ones)
BOS_DRIVERS = [
    "24cxx", "ads125x", "ds18b20", "esp12f", "fm25cl", "icm20948", "ili9320",
    "ili9341", "key", "lis3dh", "matrixkeys", "mcuflash", "oled", "paj7620u2",
    "pcf8574", "qmc5883l", "rs485", "sd", "spiflash", "ssd1289", "st7789",
    "tm1638", "xpt2046", "enc28j60",
]

def get_bos_root():
    """Find BabyOS root directory."""
    # Try environment variable first
    bos_root = os.environ.get("BABYOS_ROOT")
    if bos_root and os.path.isdir(bos_root):
        return bos_root

    # Try common locations relative to this script
    script_dir = Path(__file__).parent
    candidates = [
        script_dir / "BabyOS",
        script_dir.parent / "BabyOS",
        script_dir.parent.parent / "BabyOS",
    ]

    for candidate in candidates:
        if candidate.exists() and (candidate / "bos" / "b_os.h").exists():
            return str(candidate)

    return None

def check_bos_directory(bos_root):
    """Verify BabyOS directory structure."""
    required_paths = [
        "bos/b_os.h",
        "bos/core",
        "bos/hal",
        "bos/mcu",
        "bos/modules",
        "bos/drivers",
        "bos/thirdparty",
        "bos/utils",
        "_config/b_config.py",
    ]

    for path in required_paths:
        full_path = os.path.join(bos_root, path)
        if not os.path.exists(full_path):
            return False, f"Missing required path: {path}"

    return True, "OK"

def parse_device_list(bos_root):
    """Parse b_device_list.h to get available devices."""
    device_list_path = os.path.join(bos_root, "_config/b_device_list.h")
    devices = []

    if os.path.exists(device_list_path):
        with open(device_list_path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
            # Extract device names from B_DEVICE_REG macros
            import re
            pattern = r'B_DEVICE_REG\s*\(\s*(\w+)\s*,'
            devices = re.findall(pattern, content)

    return devices

def parse_hal_if(bos_root):
    """Parse b_hal_if.h to understand HAL interface structures."""
    hal_if_path = os.path.join(bos_root, "_config/b_hal_if.h")
    hal_defs = {}

    if os.path.exists(hal_if_path):
        with open(hal_if_path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
            import re
            # Find HAL interface definitions: #define HAL_<NAME>_IF ...
            pattern = r'#define\s+HAL_(\w+)_IF\s+([^{]+)'
            matches = re.findall(pattern, content, re.DOTALL)
            for name, definition in matches:
                hal_defs[name] = definition.strip()

    return hal_defs

class BabyOSProjectGenerator:
    """Main project generator class."""

    def __init__(self, bos_root, output_dir, vendor, chip, project_name=None):
        self.bos_root = Path(bos_root)
        self.output_dir = Path(output_dir)
        self.vendor = vendor.lower()
        self.chip = chip
        self.project_name = project_name or f"babyos_{self.vendor}_{self.chip.lower()}"
        self.devices = []
        self.hal_defs = {}
        self.config = {
            "vendor": self.vendor,
            "chip": self.chip,
            "project_name": self.project_name,
            "bos_root": str(self.bos_root),
            "modules": [],
            "algorithms": [],
            "services": [],
            "drivers": [],
        }

    def parse(self):
        """Parse BabyOS source to understand structure."""
        self.devices = parse_device_list(str(self.bos_root))
        self.hal_defs = parse_hal_if(str(self.bos_root))
        return True

    def create_directory_structure(self):
        """Create project directory structure."""
        dirs = [
            self.output_dir / "src",
            self.output_dir / "include",
            self.output_dir / "bos",
            self.output_dir / "build",
            self.output_dir / "config",
        ]

        for d in dirs:
            d.mkdir(parents=True, exist_ok=True)

    def generate_makefile(self):
        """Generate Makefile for the project."""
        mcu_path = self.bos_root / "bos" / "mcu" / self.vendor
        has_mcu = mcu_path.exists()

        makefile_content = f'''# Makefile for {self.project_name}
# Generated by BabyOS Project Generator v{VERSION}

# Project name
PROJECT_NAME = {self.project_name}

# Directories
BOS_ROOT = {self.bos_root.name}
BUILD_DIR = build
SRC_DIR = src
INC_DIR = include

# C Flags
CFLAGS = -mcpu=cortex-m0 -mthumb -Wall -fdata-sections -ffunction-sections
CFLAGS += -mapcs -std=c99 -c
CFLAGS += -I. -I$(INC_DIR) -I$(BOS_ROOT) -I$(BOS_ROOT)/bos
CFLAGS += -I$(BOS_ROOT)/_config -I$(BOS_ROOT)/bos/core/inc
CFLAGS += -I$(BOS_ROOT)/bos/hal/inc -I$(BOS_ROOT)/bos/utils/inc

# Linker Flags
LDFLAGS = -mcpu=cortex-m0 -mthumb -nostartfiles -Wl,--gc-sections
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(PROJECT_NAME).map,--cref
LDFLAGS += -lc -lm -lstdc++

# Source files
CSRCS = $(wildcard $(SRC_DIR)/*.c)
CSRCS += $(wildcard $(BOS_ROOT)/bos/core/*.c)
CSRCS += $(wildcard $(BOS_ROOT)/bos/hal/*.c)
CSRCS += $(wildcard $(BOS_ROOT)/bos/utils/*.c)
CSRCS += $(wildcard $(BOS_ROOT)/bos/utils/inc/*.c) 2>/dev/null || true

# MCU-specific sources
'''

        if has_mcu:
            mcu_c_files = list(mcu_path.rglob("*.c"))
            if mcu_c_files:
                makefile_content += f"# MCU-specific sources ({self.vendor}/{self.chip})\n"
                for mcu_file in mcu_c_files[:15]:  # Limit to avoid too long line
                    rel_path = mcu_file.relative_to(self.bos_root)
                    makefile_content += f"CSRCS += $(BOS_ROOT)/{rel_path}\n"

        makefile_content += f'''
# Object files
OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(CSRCS)))

# Target
TARGET = $(BUILD_DIR)/$(PROJECT_NAME).elf

# Toolchain (adjust for your setup)
CC = arm-none-eabi-gcc
AR = arm-none-eabi-ar
SIZE = arm-none-eabi-size
OBJCOPY = arm-none-eabi-objcopy
OBJDUMP = arm-none-eabi-objdump

# Configuration header
CONFIG_H = config/b_config.h

.PHONY: all clean config

all: $(BUILD_DIR) config $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

config:
	@echo "Running BabyOS configuration..."
	@cd $(BOS_ROOT) && python _config/b_config.py $(BOS_ROOT)
	@cp $(BOS_ROOT)/_config/b_config.h config/ 2>/dev/null || cp $(BOS_ROOT)/_config/b_config.h . 2>/dev/null || true

$(TARGET): $(OBJS) Makefile
	@echo "Linking $@"
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	$(SIZE) $@
	$(OBJCOPY) -O binary $@ $(BUILD_DIR)/$(PROJECT_NAME).bin
	$(OBJCOPY) -O ihex $@ $(BUILD_DIR)/$(PROJECT_NAME).hex

$(BUILD_DIR)/%.o: %.c Makefile $(CONFIG_H)
	@echo "Compiling $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -D{self.chip} -DAUTO_GENERATED -include $(CONFIG_H) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

rebuild: clean all

debug:
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program $(TARGET) verify reset exit"

flash: $(TARGET)
	st-flash write $(BUILD_DIR)/$(PROJECT_NAME).bin 0x08000000

help:
	@echo "BabyOS Project - Available targets:"
	@echo "  all      - Build the entire project"
	@echo "  config   - Run BabyOS Kconfig"
	@echo "  clean    - Remove build artifacts"
	@echo "  rebuild  - Clean and rebuild"
	@echo "  debug    - Debug with OpenOCD"
	@echo "  flash    - Flash to device"
'''

        makefile_path = self.output_dir / "Makefile"
        with open(makefile_path, "w", encoding="utf-8") as f:
            f.write(makefile_content)

    def generate_linker_script(self):
        """Generate linker script for the chip."""
        # Different memory layouts for different chips
        chip_memories = {
            "STM32F10X_LD": {"FLASH": "0x08000000:0x4000", "RAM": "0x20000000:0x2000"},
            "STM32F10X_MD": {"FLASH": "0x08000000:0x10000", "RAM": "0x20000000:0x5000"},
            "STM32F10X_HD": {"FLASH": "0x08000000:0x80000", "RAM": "0x20000000:0x18000"},
            "STM32G0X0": {"FLASH": "0x08000000:0x20000", "RAM": "0x20000000:0x6000"},
            "STM32L0X3": {"FLASH": "0x08000000:0x20000", "RAM": "0x20000000:0x4000"},
            "ESP32C3": {"FLASH": "0x10000000:0x40000", "RAM": "0x3FF00000:0x30000"},
        }

        memory = chip_memories.get(self.chip, {"FLASH": "0x08000000:0x20000", "RAM": "0x20000000:0x4000"})
        flash_start, flash_size = memory["FLASH"].split(":")
        ram_start, ram_size = memory["RAM"].split(":")

        linker_script = f'''/* Linker script for {self.chip} */
/* Generated by BabyOS Project Generator */

ENTRY(Reset_Handler)

/* Memory regions */
MEMORY
{{
  FLASH (rx)  : ORIGIN = {flash_start}, LENGTH = {flash_size}
  RAM (rwx)   : ORIGIN = {ram_start}, LENGTH = {ram_size}
}}

/* BabyOS custom sections - place in flash */
_boot_infos :
{{
  . = ALIGN(4);
  _sboot_info = .;
  /* Driver init section - placed at start of flash */
  . = ALIGN(4);
}} > FLASH

/* Main text section */
.text :
{{
  . = ALIGN(4);
  _stext = .;
  *(.isr_vector)
  *(.text)
  *(.text*)
  *(.rodata)
  *(.rodata*)
  _etext = .;
}} > FLASH

/* BabyOS polling section - keep in flash */
_bos_polling :
{{
  . = ALIGN(4);
  __start_bos_polling = .;
  KEEP(*(.bos_polling))
  __stop_bos_polling = .;
}} > FLASH

/* BabyOS driver init section */
_bos_driver_init :
{{
  . = ALIGN(4);
  __start_bos_driver_init = .;
  KEEP(*(.driver_init))
  __stop_bos_driver_init = .;
}} > FLASH

/* Other BabyOS sections */
_bos_mod_shell :
{{
  . = ALIGN(4);
  KEEP(*(.b_mod_shell))
}} > FLASH

_bos_mod_state :
{{
  . = ALIGN(4);
  KEEP(*(.b_mod_state))
}} > FLASH

/* Read-only data */
.ARM.exidx :
{{
  *(.ARM.exidx*)
  *(.ARM.exidx)
}} > FLASH

/* Initialized data */
.data :
{{
  . = ALIGN(4);
  _sdata = .;
  *(.data)
  *(.data*)
  _edata = .;
}} > RAM AT > FLASH

/* Uninitialized data */
_bss :
{{
  . = ALIGN(4);
  _sbss = .;
  *(.bss)
  *(.bss*)
  *(COMMON)
  _ebss = .;
}} > RAM

_heap_start = .;
_estack = ORIGIN(RAM) + LENGTH(RAM);
'''

        linker_path = self.output_dir / "src" / f"{self.chip.lower()}_FLASH.ld"
        with open(linker_path, "w", encoding="utf-8") as f:
            f.write(linker_script)

    def generate_startup(self):
        """Generate startup code for the chip."""
        startup = f'''/* Startup code for {self.chip} */
/* Generated by BabyOS Project Generator */

#define WEAK __attribute__((weak))

/* Minimal startup - Full startup should come from vendor SDK */
extern unsigned long _sboot_info;
extern unsigned long _eboot_info;
extern unsigned long _stext;
extern unsigned long _etext;
extern unsigned long _sdata;
extern unsigned long _edata;
extern unsigned long _sbss;
extern unsigned long _ebss;
extern unsigned long _heap_start;

extern int main(void);

void Reset_Handler(void);
WEAK void Default_Handler(void);

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

/* Interrupt vector table */
__attribute__((section(".isr_vector")))
void (* const g_pfnVectors[])(void) = {{
    (void *)((unsigned long)&_heap_start + ((unsigned long)&_ebss - (unsigned long)&_sbss)),
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0,
    0,
    0,
    0,
    SVC_Handler,
    0,
    0,
    PendSV_Handler,
    SysTick_Handler,
}};

void Default_Handler(void) {{
    while(1) {{ }}
}}

void Reset_Handler(void) {{
    unsigned long *src = &_etext;
    unsigned long *dest = &_sdata;
    
    /* Copy data from flash to ram */
    while(dest < &_edata) {{
        *dest++ = *src++;
    }}
    
    /* Zero bss */
    while(dest < &_ebss) {{
        *dest++ = 0;
    }}
    
    /* Call main */
    main();
    
    /* If main returns, loop forever */
    while(1) {{ }}
}}
'''

        startup_path = self.output_dir / "src" / "startup.c"
        with open(startup_path, "w", encoding="utf-8") as f:
            f.write(startup)

    def generate_main(self):
        """Generate template main.c."""
        main_content = f'''/**
 * main.c - {self.project_name}
 * Generated by BabyOS Project Generator
 */

#include "b_os.h"

/* Hardware initialization */
static void hardware_init(void) {{
    /* System clock init - implement based on your chip */
    bHalInit();
    
    /* Enable peripherals - add your peripherals here */
    
    /* Example: Configure LED on PC13 (common on STM32F103C8T6) */
    #if defined(B_HAL_GPIOC)
    bHalGpioConfig(B_HAL_GPIOC, B_HAL_PIN13, B_HAL_GPIO_OUTPUT, B_HAL_GPIO_NOPULL);
    #endif
}}

/* Application task */
static void app_task(void) {{
    static uint32_t tick = 0;
    
    /* Toggle LED every 500ms */
    if (TICK_DIFF_BIT32(tick, bHalGetSysTick()) > MS2TICKS(500)) {{
        tick = bHalGetSysTick();
        
        #if defined(B_HAL_GPIOC)
        static uint8_t led_state = 0;
        bHalGpioWritePin(B_HAL_GPIOC, B_HAL_PIN13, led_state);
        led_state = !led_state;
        #endif
    }}
}}

/* Register task to polling function */
BOS_REG_POLLING_FUNC(app_task);

int main(void) {{
    /* Initialize BabyOS */
    bInit();
    
    /* Hardware init */
    hardware_init();
    
    /* Open devices example - uncomment as needed */
    /*
    int fd_spi_flash = bOpen(SPIFLASH, BCORE_FLAG_RW);
    int fd_key = bOpen(KEY, BCORE_FLAG_RW);
    int fd_oled = bOpen(OLED, BCORE_FLAG_RW);
    */
    
    /* Main loop */
    while (1) {{
        bExec();  /* Poll BabyOS registered functions */
    }}
    
    return 0;
}}
'''

        main_path = self.output_dir / "src" / "main.c"
        with open(main_path, "w", encoding="utf-8") as f:
            f.write(main_content)

    def generate_b_config(self):
        """Generate initial b_config.h."""
        config_content = f'''/* b_config.h - Initial configuration */
/* Generated by BabyOS Project Generator */
/* Run 'make config' to customize via Kconfig */

#ifndef __B_CONFIG_H__
#define __B_CONFIG_H__

/* Chip configuration */
#define {self.chip} (1)

/* Algorithm configuration */
#define _BOS_ALGO_ENABLE (1)
#define _ALGO_CRC_ENABLE (1)
#define _ALGO_CRC16_CCITT_ENABLE (1)
#define _ALGO_CRC8_ENABLE (1)

/* Module configuration */
#define _BOS_MODULES_ENABLE (1)
#define _BOS_MOD_KV_ENABLE (1)
#define _BOS_MOD_SHELL_ENABLE (1)
#define _BOS_MOD_BUTTON_ENABLE (1)

/* Service configuration */
#define _BOS_SERVICES_ENABLE (1)

/* System configuration */
#define TICK_FRZ_HZ (1000)
#define _BOS_PRINTF_ENABLE (1)

/* HAL configuration - adjust for your hardware */
#define _BOS_HAL_GPIO_ENABLE (1)
#define _BOS_HAL_UART_ENABLE (1)
#define _BOS_HAL_SPI_ENABLE (1)
#define _BOS_HAL_I2C_ENABLE (1)

/* Include BabyOS type definitions */
#include "b_type.h"

#endif /* __B_CONFIG_H__ */
'''

        config_path = self.output_dir / "config" / "b_config.h"
        with open(config_path, "w", encoding="utf-8") as f:
            f.write(config_content)

    def generate_readme(self):
        """Generate project README."""
        readme = f'''# {self.project_name}

BabyOS-based embedded project for {self.vendor}/{self.chip}

## Generated by BabyOS Project Generator v{VERSION}

## Project Structure

```
{self.project_name}/
├── Makefile                 # Build configuration
├── README.md                # This file
├── config/
│   └── b_config.h          # BabyOS configuration (run 'make config')
├── src/
│   ├── main.c              # Application entry point
│   ├── startup.c           # Startup code
│   └── {self.chip.lower()}_FLASH.ld   # Linker script
└── bos/                    # Symlink to BabyOS (or copy)
```

## Quick Start

1. **First time setup**: Create symlink to BabyOS
   ```bash
   # On Windows (Git Bash / MSYS2)
   ln -s /path/to/BabyOS bos
   
   # On Linux/macOS
   ln -s /path/to/BabyOS bos
   ```

2. **Configure BabyOS** (select modules, drivers, algorithms)
   ```bash
   make config
   ```

3. **Build**
   ```bash
   make
   ```

4. **Flash**
   ```bash
   make flash
   ```

## BabyOS Components

### Core
- Device abstraction (bOpen/bRead/bWrite/bCtl/bClose)
- Tasks (protothreads-based)
- Timers, Queues, Semaphores

### HAL (Hardware Abstraction Layer)
- GPIO, UART, SPI, I2C
- DMA, QSPI, SDIO
- Flash, Ethernet, Watchdog, RNG

### Modules
- KV storage, File system, GUI, WiFi, USB
- Modbus, State machine, IAP, Shell
- MQTT, SSL/TLS, OTA

### Drivers (40+)
- Sensors: DS18B20, APDS9930, ICM20948, QMC5883L, LIS3DH
- Displays: OLED, SSD1289, ILI9341, ST7789, XPT2046
- Storage: SPI Flash, SD card, 24Cxx EEPROM
- Network: ENC28J60, ESP12F

## Documentation

- Official BabyOS documentation: https://babyos.cn/doc/
- BabyOS source: https://gitee.com/notrynohigh/BabyOS

## Vendor

| Item | Value |
|------|-------|
| Vendor | {self.vendor} |
| Chip | {self.chip} |
| Project | {self.project_name} |
| Generator | BabyOS Project Generator v{VERSION} |

## Available Devices

''' + ", ".join(self.devices[:20]) + f'''

... (total {len(self.devices)} devices)

## HAL Interfaces Available

''' + ", ".join(list(self.hal_defs.keys())[:15])

        readme_path = self.output_dir / "README.md"
        with open(readme_path, "w", encoding="utf-8") as f:
            f.write(readme)

    def generate(self):
        """Generate the complete project."""
        print(f"[*] Generating BabyOS project: {self.project_name}")
        print(f"    Vendor: {self.vendor}, Chip: {self.chip}")
        print(f"    Output: {self.output_dir}")

        # Create directories
        self.create_directory_structure()
        print(f"[+] Created directory structure")

        # Parse BabyOS source
        self.parse()
        print(f"[+] Parsed BabyOS ({len(self.devices)} devices, {len(self.hal_defs)} HAL interfaces)")

        # Generate files
        self.generate_makefile()
        print(f"[+] Generated Makefile")

        self.generate_linker_script()
        print(f"[+] Generated linker script")

        self.generate_startup()
        print(f"[+] Generated startup code")

        self.generate_main()
        print(f"[+] Generated main.c template")

        self.generate_b_config()
        print(f"[+] Generated initial b_config.h")

        self.generate_readme()
        print(f"[+] Generated README.md")

        # Copy BabyOS as bos symlink or reference
        bos_link = self.output_dir / "bos"
        if not bos_link.exists():
            try:
                if os.name == 'nt':  # Windows
                    # On Windows, create a junction or copy
                    import subprocess
                    subprocess.run(['cmd', '/c', 'mklink', '/J', str(bos_link), str(self.bos_root)],
                                   shell=True, capture_output=True)
                else:
                    os.symlink(self.bos_root, bos_link)
                print(f"[+] Created bos -> {self.bos_root.name} symlink")
            except:
                # If symlink fails, create a placeholder
                with open(bos_link.with_suffix('.txt'), 'w') as f:
                    f.write(f"Link to: {self.bos_root}\nReplace this with actual BabyOS directory or symlink\n")
                print(f"[!] Created bos.txt reference (manually create bos symlink on Windows)")

        print(f"\n[✓] Project generated successfully!")
        print(f"\nNext steps:")
        print(f"  1. cd {self.output_dir}")
        print(f"  2. On Windows: manually create 'bos' symlink to BabyOS directory")
        print(f"  3. make config  (optional: customize via Kconfig)")
        print(f"  4. make         (build)")
        print(f"  5. make flash   (flash to device)")

        return True


def main():
    parser = argparse.ArgumentParser(
        description="BabyOS Project Generator - Create BabyOS-based MCU projects",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Generate for STM32F103
  python babyos_project_generator.py --vendor st --chip STM32F10X_MD -o myproject

  # Use custom BabyOS path
  python babyos_project_generator.py --bos-root /path/to/BabyOS --vendor st --chip STM32F10X_MD

  # List available vendors
  python babyos_project_generator.py --list-vendors

  # List chips for a vendor
  python babyos_project_generator.py --list-chips --vendor st

Supported vendors: st, nation, mm, hc, wch, holtek, artery, hk, espressif, py, cw
        """
    )

    parser.add_argument("--version", action="version", version=f"%(prog)s {VERSION}")
    parser.add_argument("--bos-root", help="Path to BabyOS directory")
    parser.add_argument("-o", "--output", default="./babyos_project", help="Output directory")
    parser.add_argument("--vendor", "-v", help="MCU vendor (st, espressif, etc.)")
    parser.add_argument("--chip", "-c", help="MCU chip (e.g., STM32F10X_MD, ESP32C3)")
    parser.add_argument("--name", "-n", help="Project name")
    parser.add_argument("--list-vendors", action="store_true", help="List available vendors")
    parser.add_argument("--list-chips", action="store_true", help="List chips for vendor")

    args = parser.parse_args()

    # List vendors
    if args.list_vendors:
        print("Available MCU vendors:")
        for key, name in VENDORS.items():
            print(f"  {key:12} - {name}")
        print(f"\nChips per vendor: python {sys.argv[0]} --list-chips --vendor <vendor>")
        return 0

    # List chips
    if args.list_chips:
        if not args.vendor:
            print("Error: --vendor required for --list-chips")
            return 1
        vendor = args.vendor.lower()
        if vendor in VENDOR_CHIPS:
            print(f"Chips for {vendor}:")
            for chip in VENDOR_CHIPS[vendor]:
                print(f"  {chip}")
        else:
            print(f"Unknown vendor: {vendor}")
        return 0

    # Validate required arguments
    if not args.vendor or not args.chip:
        parser.print_help()
        print("\nError: --vendor and --chip are required unless using --list-*")
        return 1

    # Find BabyOS root
    bos_root = args.bos_root or get_bos_root()
    if not bos_root:
        print("Error: BabyOS directory not found.")
        print("Please specify --bos-root or set BABYOS_ROOT environment variable.")
        return 1

    # Validate BabyOS
    valid, msg = check_bos_directory(bos_root)
    if not valid:
        print(f"Error: Invalid BabyOS directory - {msg}")
        return 1

    print(f"[*] Using BabyOS at: {bos_root}")

    # Create output directory
    output_dir = Path(args.output).absolute()

    # Generate project
    generator = BabyOSProjectGenerator(
        bos_root=bos_root,
        output_dir=output_dir,
        vendor=args.vendor,
        chip=args.chip,
        project_name=args.name
    )

    try:
        generator.generate()
    except Exception as e:
        print(f"Error generating project: {e}")
        import traceback
        traceback.print_exc()
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())