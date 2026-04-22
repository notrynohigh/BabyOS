#!/usr/bin/env python3
"""
BabyOS Project Generator - Advanced Version
Supports: Makefile, Keil uVision, STM32CubeMX integration

Version: 2.0.0
Author: aiclaw
"""

import os
import sys
import shutil
import argparse
import xml.etree.ElementTree as ET
from pathlib import Path
from datetime import datetime

VERSION = "2.0.0"

# Vendor SDK URLs and configurations
VENDOR_SDKS = {
    "st": {
        "name": "STMicroelectronics",
        "cubemx": True,
        "pack_url": "https://www.st.com/content/st_com_af/products/develop...",
        "device_family": "STM32",
        "ide": "Keil",
    },
    "espressif": {
        "name": "Espressif",
        "idf": True,
        "idf_repo": "https://github.com/espressif/esp-idf.git",
        "ide": "ESP-IDF",
    },
    "nation": {
        "name": "Nationstech",
        "sdk_url": "https://www.nationstech.com/...",
        "ide": "Keil",
    },
    "mm": {
        "name": "Mindmotion",
        "pack_url": "https://www.mindmotion.com.cn/...",
        "ide": "Keil",
    },
}

# Chip to Keil device name mapping (partial list)
KEIL_DEVICES = {
    "STM32F10X_LD": ("STM32F103C8", "STMicroelectronics"),
    "STM32F10X_MD": ("STM32F103C8", "STMicroelectronics"),
    "STM32F10X_HD": ("STM32F103VC", "STMicroelectronics"),
    "STM32F10X_CL": ("STM32F105RC", "STMicroelectronics"),
    "STM32G0X0": ("STM32G071RB", "STMicroelectronics"),
    "STM32L41X_L46X": ("STM32L476JE", "STMicroelectronics"),
    "STM32F40X": ("STM32F407VG", "STMicroelectronics"),
    "STM32G4XX": ("STM32G474VE", "STMicroelectronics"),
    "STM32H5XX": ("STM32H563ZI", "STMicroelectronics"),
    "STM32L0X3": ("STM32L073VZ", "STMicroelectronics"),
    "STM32G0B1": ("STM32G0B1RE", "STMicroelectronics"),
    "NATION_L40X": ("N32L40x", "Nationstech"),
    "NATION_G45X": ("N32G45x", "Nationstech"),
    "MM32SPIN2X": ("MM32SPIN", "Mindmotion"),
    "CH32F103": ("CH32F103", "WCH"),
    "ESP32C3": ("ESP32C3", "Espressif"),
}

# Memory configurations for different chips
MEMORY_LAYOUTS = {
    "STM32F10X_LD": {"flash": "0x08000000:0x4000", "ram": "0x20000000:0x2000"},
    "STM32F10X_MD": {"flash": "0x08000000:0x10000", "ram": "0x20000000:0x5000"},
    "STM32F10X_HD": {"flash": "0x08000000:0x80000", "ram": "0x20000000:0x18000"},
    "STM32G0X0": {"flash": "0x08000000:0x20000", "ram": "0x20000000:0x6000"},
    "STM32L41X_L46X": {"flash": "0x08000000:0x80000", "ram": "0x20000000:0x20000"},
    "STM32F40X": {"flash": "0x08000000:0x100000", "ram": "0x20000000:0x40000"},
    "STM32G4XX": {"flash": "0x08000000:0x40000", "ram": "0x20000000:0x8000"},
    "STM32H5XX": {"flash": "0x08000000:0x100000", "ram": "0x20000000:0x30000"},
    "STM32L0X3": {"flash": "0x08000000:0x20000", "ram": "0x20000000:0x4000"},
    "STM32G0B1": {"flash": "0x08000000:0x20000", "ram": "0x20000000:0x6000"},
}


class KeilProjectGenerator:
    """Generate Keil uVision project files."""

    def __init__(self, project_name, chip, vendor, output_dir):
        self.project_name = project_name
        self.chip = chip
        self.vendor = vendor
        self.output_dir = Path(output_dir)
        self.device_name, self.vendor_name = KEIL_DEVICES.get(chip, ("STM32F103C8", "STMicroelectronics"))

    def generate_uvprojx(self):
        """Generate .uvprojx file (Keil project)."""
        # XML namespaces
        ns = {
            'c': 'http://schemas.microsoft.com/toolkit/2008/09/11/misc',
            'u': 'http://www.keil.com/schema/uv',
        }

        # Register namespaces to avoid ns0 prefix
        for prefix, uri in ns.items():
            ET.register_namespace(prefix, uri)

        # Root element
        Project = ET.Element('Project')
        Project.set('xmlns:i', 'http://www.w3.org/2001/XMLSchema-instance')

        # Targets
        Targets = ET.SubElement(Project, 'Targets')
        Target = ET.SubElement(Targets, 'Target')
        TargetName = ET.SubElement(Target, 'TargetName')
        TargetName.text = self.project_name

        # Target Option
        TargetOption = ET.SubElement(Target, 'TargetOption')
        TargetCommonOption = ET.SubElement(TargetOption, 'TargetCommonOption')

        Device = ET.SubElement(TargetCommonOption, 'Device')
        Device.text = self.device_name

        Vendor = ET.SubElement(TargetCommonOption, 'Vendor')
        Vendor.text = self.vendor_name

        # CPU IROM/IRAM
        IROMs = ET.SubElement(TargetCommonOption, 'IROMs')
        IROM = ET.SubElement(IROMs, 'IROM')
        IROMName = ET.SubElement(IROM, 'IROMName')
        IROMName.text = 'IROM1'
        IROMStart = ET.SubElement(IROM, 'Start')
        IROMStart.text = '0x08000000'
        IROMSize = ET.SubElement(IROM, 'Size')
        IROMSize.text = '0x10000'  # Default 64KB
        IROMAddress = ET.SubElement(IROM, 'Address')
        IROMAddress.text = '0x08000000'

        IRAMs = ET.SubElement(TargetCommonOption, 'IRAMs')
        IRAM = ET.SubElement(IRAMs, 'IRAM')
        IRAMName = ET.SubElement(IRAM, 'IRAMName')
        IRAMName.text = 'IRAM1'
        IRAMStart = ET.SubElement(IRAM, 'Start')
        IRAMStart.text = '0x20000000'
        IRAMSize = ET.SubElement(IRAM, 'Size')
        IRAMSize.text = '0x5000'  # Default 20KB

        # Groups
        Groups = ET.SubElement(Target, 'Groups')

        # Group 1: Source
        Group1 = ET.SubElement(Groups, 'Group')
        GroupName1 = ET.SubElement(Group1, 'GroupName')
        GroupName1.text = 'Source'
        Files1 = ET.SubElement(Group1, 'Files')
        File1 = ET.SubElement(Files1, 'File')
        FilePath1 = ET.SubElement(File1, 'FilePath')
        FilePath1.text = 'src/main.c'

        # Group 2: BabyOS Core
        Group2 = ET.SubElement(Groups, 'Group')
        GroupName2 = ET.SubElement(Group2, 'GroupName')
        GroupName2.text = 'BabyOS_Core'

        # Group 3: BabyOS HAL
        Group3 = ET.SubElement(Groups, 'Group')
        GroupName3 = ET.SubElement(Group3, 'GroupName')
        GroupName3.text = 'BabyOS_HAL'

        # Group 4: BabyOS Modules
        Group4 = ET.SubElement(Groups, 'Group')
        GroupName4 = ET.SubElement(Group4, 'GroupName')
        GroupName4.text = 'BabyOS_Modules'

        # Group 5: MCU
        Group5 = ET.SubElement(Groups, 'Group')
        GroupName5 = ET.SubElement(Group5, 'GroupName')
        GroupName5.text = 'MCU'

        # Build Options
        TargetOption = ET.SubElement(Target, 'TargetOption')
        for group in Groups.findall('Group'):
            pass  # Groups already added

        # Output directory
        Output = ET.SubElement(TargetOption, 'Output')
        OutputName = ET.SubElement(Output, 'OutputName')
        OutputName.text = self.project_name
        OutputDirectory = ET.SubElement(Output, 'OutputDirectory')
        OutputDirectory.text = 'Obj'

        # Listing
        Listing = ET.SubElement(TargetOption, 'Listing')
        ListingPath = ET.SubElement(Listing, 'ListingPath')
        ListingPath.text = 'List'

        # Create tree and write
        tree = ET.ElementTree(Project)
        tree.write(self.output_dir / f"{self.project_name}.uvprojx",
                   encoding='utf-8', xml_declaration=True)

        return True

    def generate_uvprojx_manual(self):
        """Generate .uvprojx file manually (string-based for complex structures)."""
        # Keil project file template
        template = f'''<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<Project xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_projx.xsd">
  <SchemaVersion>2.11</SchemaVersion>
  <Header>### uVision Project Header (DO NOT MODIFY) ###</Header>
  <Targets>
    <Target>
      <TargetName>{self.project_name}</TargetName>
      <ToolsetNumber>0x04</ToolsetNumber>
      <TargetOption>
        <TargetCommonOption>
          <Device>{self.device_name}</Device>
          <Vendor>{self.vendor_name}</Vendor>
          <PackID>Keil.STM32F1xx_DFP.2.3.0</PackID>
          <PackURL>https://www.keil.com/pack/</PackURL>
          <Cpu>IRAM(0x20000000,0x5000) IROM(0x08000000,0x10000) CPUTYPE("Cortex-M3") CLOCK(12000000) ELITTLE</Cpu>
          <FlashUtil>""</FlashUtil>
          <StartupFile>""</StartupFile>
          <FlashDriver>""</FlashDriver>
        </TargetCommonOption>
        <CommonProperty>
          <UseCPPCompiler>0</UseCPPCompiler>
          <RVCTCodeConst>0</RVCTCodeConst>
          <RVCTZI>0</RVCTZI>
          <RVCTOtherData>0</RVCTOtherData>
          <ModuleSelection>0</ModuleSelection>
          <IncludeInBuild>1</IncludeInBuild>
          <AlwaysBuild>0</AlwaysBuild>
          <GenerateXml>0</GenerateXml>
          <XClass>0</XClass>
          <Index>src/index.ppc</Index>
        </CommonProperty>
        <Tools>
          <ArmLink>
            <Optimization>1</Optimization>
            <RelaxFFP>1</RelaxFFP>
            <Warning>1</Warning>
            <LinkTimeUsage>0</LinkTimeUsage>
            <XSize>0</XSize>
            <CreateHexFile>1</CreateHexFile>
            <HexFormat>0</HexFormat>
            <ListingFile>1</ListingFile>
            <BrowseFile>0</BrowseFile>
            <UseCMSIS>1</UseCMSIS>
            <Options>-lf</Options>
          </ArmLink>
          <BInfo没有什么name>ArmAsm</BInfo>
          <BInfo没有什么name>CArmCc>
            <Mode>0</Mode>
            <OptLevel>1</OptLevel>
            <VariousControls>
              <MiscControls>--c99 -fdata-sections -ffunction-sections</MiscControls>
              <Define>{self.chip}=1</Define>
              <IncludePath>-I.; -I..\\inc; -I..\\bos; -I..\\bos\\core\\inc; -I..\\bos\\hal\\inc; -I..\\bos\\utils\\inc; -I..\\config</IncludePath>
            </VariousControls>
          </CArmCc>
          <BInfo没有什么name>CArmAsm>
          <BInfo没有什么name>CArmLd>
            <ScatterFile>..\\src\\{self.chip.lower()}_FLASH.scf</ScatterFile>
            <EditLogFile>0</EditLogFile>
          </CArmLd>
        </Tools>
      </TargetOption>
      <Groups>
        <Group>
          <GroupName>Source</GroupName>
          <Files>
            <File>
              <FileName>main.c</FileName>
              <FileType>1</FileType>
              <FilePath>src\\main.c</FilePath>
            </File>
            <File>
              <FileName>startup.c</FileName>
              <FileType>1</FileType>
              <FilePath>src\\startup.c</FilePath>
            </File>
          </Files>
        </Group>
        <Group>
          <GroupName>BabyOS_Core</GroupName>
        </Group>
        <Group>
          <GroupName>BabyOS_HAL</GroupName>
        </Group>
        <Group>
          <GroupName>BabyOS_Modules</GroupName>
        </Group>
        <Group>
          <GroupName>BabyOS_Drivers</GroupName>
        </Group>
        <Group>
          <GroupName>MCU</GroupName>
        </Group>
      </Groups>
    </Target>
  </Targets>
</Project>
'''
        uvprojx_path = self.output_dir / f"{self.project_name}.uvprojx"
        with open(uvprojx_path, 'w', encoding='utf-8') as f:
            f.write(template)
        return True


class STM32CubeMXIntegrator:
    """Integrate with STM32CubeMX for vendor library management."""

    def __init__(self, chip, output_dir):
        self.chip = chip
        self.output_dir = Path(output_dir)
        self.cubemx_path = self.find_cubemx()

    def find_cubemx(self):
        """Find STM32CubeMX installation."""
        common_paths = [
            "C:\\Program Files\\STMicroelectronics\\STM32Cube\\STM32CubeMX\\STM32CubeMX.exe",
            "C:\\Program Files (x86)\\STMicroelectronics\\STM32Cube\\STM32CubeMX\\STM32CubeMX.exe",
            os.path.expanduser("~\\STCube\\STM32CubeMX\\STM32CubeMX.exe"),
        ]

        for path in common_paths:
            if os.path.exists(path):
                return path

        return None

    def generate_cubemx_script(self):
        """Generate a TCL script for STM32CubeMX automation."""
        tcl_script = f'''
# STM32CubeMX Auto-Configuration Script
# Generated by BabyOS Project Generator v{VERSION}

# Open STM32CubeMX (if path is set)
# set cube_mx_path "C:\\\\Program Files\\\\STMicroelectronics\\\\STM32Cube\\\\STM32CubeMX\\\\STM32CubeMX.exe"
# exec $cube_mx_path &

# Load chip database
# Example for STM32F103
set chip_model "{self.chip}"

# Configure pins (example)
# setGPIO PORTA PIN0 OUTPUT

# Generate code
# generate code to "{self.output_dir}/Cubemx_Code"

puts "STM32CubeMX configuration for {self.chip}"
puts "Please run STM32CubeMX manually and import this script"
'''
        tcl_path = self.output_dir / "cubemx_config.tcl"
        with open(tcl_path, 'w', encoding='utf-8') as f:
            f.write(tcl_script)
        return tcl_path

    def download_pack(self):
        """Attempt to download CMSIS pack for the chip."""
        # This would require actual ST server access
        print("[!] STM32CubeMX integration requires manual setup")
        print(f"    Please install STM32CubeMX from https://www.st.com/stm32cubemx")
        print(f"    Then open: {self.output_dir}/cubemx_config.tcl")
        return False


class BabyOSProjectGeneratorV2:
    """Enhanced project generator with Keil and CubeMX support."""

    def __init__(self, bos_root, output_dir, vendor, chip, project_name=None):
        self.bos_root = Path(bos_root)
        self.output_dir = Path(output_dir)
        self.vendor = vendor.lower()
        self.chip = chip
        self.project_name = project_name or f"babyos_{self.vendor}_{self.chip.lower()}"
        self.timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    def create_directory_structure(self):
        """Create enhanced project directory structure."""
        dirs = [
            self.output_dir / "src",
            self.output_dir / "include",
            self.output_dir / "bos",
            self.output_dir / "build",
            self.output_dir / "config",
            self.output_dir / "List",
            self.output_dir / "Obj",
        ]
        for d in dirs:
            d.mkdir(parents=True, exist_ok=True)

    def generate_makefile(self):
        """Generate Makefile."""
        mcu_path = self.bos_root / "bos" / "mcu" / self.vendor

        makefile = f'''# Makefile for {self.project_name}
# Generated by BabyOS Project Generator v{VERSION}
# Date: {self.timestamp}

PROJECT_NAME = {self.project_name}
BOS_ROOT = BabyOS
BUILD_DIR = build
SRC_DIR = src
INC_DIR = include

# Target chip
CHIP = {self.chip}

# Toolchain
CC = arm-none-eabi-gcc
AR = arm-none-eabi-ar
SIZE = arm-none-eabi-size
OBJCOPY = arm-none-eabi-objcopy
OBJDUMP = arm-none-eabi-objdump

# CPU flags
CPU = cortex-m3
FPU =

# C flags
CFLAGS = -mcpu=$(CPU)$(FPU) -mthumb -std=c99 -fdata-sections -ffunction-sections
CFLAGS += -Wall -Werror -Wextra
CFLAGS += -D{self.chip} -DUSE_HAL_DRIVER -DUSE_FULL_LL_DRIVER
CFLAGS += -DAUTO_GENERATED
CFLAGS += -I. -I$(INC_DIR) -I$(BOS_ROOT) -I$(BOS_ROOT)/bos
CFLAGS += -I$(BOS_ROOT)/_config -I$(BOS_ROOT)/bos/core/inc
CFLAGS += -I$(BOS_ROOT)/bos/hal/inc -I$(BOS_ROOT)/bos/utils/inc

# Warning flags
WFLAGS = -Wno-unused-parameter -Wno-unused-function

# LDFLAGS
LDFLAGS = -mcpu=$(CPU)$(FPU) -mthumb -nostartfiles
LDFLAGS += -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(PROJECT_NAME).map
LDFLAGS += -lc -lm -lstdc++

# Linker script
LDSCRIPT = src/{self.chip.lower()}_FLASH.ld

# Source files
CSRCS = $(wildcard $(SRC_DIR)/*.c)
CSRCS += $(wildcard $(BOS_ROOT)/bos/core/*.c)
CSRCS += $(wildcard $(BOS_ROOT)/bos/hal/*.c)
CSRCS += $(wildcard $(BOS_ROOT)/bos/utils/*.c)
CSRCS += $(wildcard $(BOS_ROOT)/bos/algorithm/*.c)
'''

        if mcu_path.exists():
            mcu_c_files = list(mcu_path.rglob("mcu_*.c"))
            for mcu_file in mcu_c_files[:20]:
                rel_path = mcu_file.relative_to(self.bos_root)
                makefile += f"CSRCS += $(BOS_ROOT)/{rel_path}\n"

        makefile += f'''
# Object files
OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(CSRCS)))

# Target
TARGET = $(BUILD_DIR)/$(PROJECT_NAME).elf
TARGET_BIN = $(BUILD_DIR)/$(PROJECT_NAME).bin
TARGET_HEX = $(BUILD_DIR)/$(PROJECT_NAME).hex

.PHONY: all clean config help project keil

all: $(BUILD_DIR) config $(TARGET)
	$(SIZE) $(TARGET)
	$(OBJCOPY) -O binary $(TARGET) $(TARGET_BIN)
	$(OBJCOPY) -O ihex $(TARGET) $(TARGET_HEX)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

config:
	@echo "Run BabyOS Kconfig for module selection..."
	@cd $(BOS_ROOT) && python _config/b_config.py $(BOS_ROOT)
	@cp $(BOS_ROOT)/_config/b_config.h config/ 2>/dev/null || true

$(TARGET): $(OBJS) $(LDSCRIPT) Makefile
	@echo "Linking $@"
	$(CC) $(LDFLAGS) -T $(LDSCRIPT) -o $@ $(OBJS)

$(BUILD_DIR)/%.o: %.c Makefile config/b_config.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFLAGS) -include config/b_config.h -o $@ $<

clean:
	rm -rf $(BUILD_DIR) List Obj

rebuild: clean all

# Keil project generation
project: $(PROJECT_NAME).uvprojx
	@echo "Keil project generated: $(PROJECT_NAME).uvprojx"

$(PROJECT_NAME).uvprojx:
	python3 $(BOS_ROOT)/../babyos-project-generator/generate_keil.py $(ARGS)

# Flash targets
flash: $(TARGET_BIN)
	st-flash write $(TARGET_BIN) 0x08000000

openocd:
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program $(TARGET) verify reset exit"

jlink:
	JLink.exe -device {self.chip} -if SWD -speed 1000 -commandfile scripts/jlink.cmd

help:
	@echo "=== BabyOS Project: {self.project_name} ==="
	@echo "  make all      - Build project"
	@echo "  make clean    - Clean build artifacts"
	@echo "  make config   - Run BabyOS Kconfig"
	@echo "  make rebuild  - Clean and rebuild"
	@echo "  make flash    - Flash via ST-Link"
	@echo "  make openocd  - Debug via OpenOCD"
'''

        with open(self.output_dir / "Makefile", 'w', encoding='utf-8') as f:
            f.write(makefile)

    def generate_linker_script(self):
        """Generate linker script with correct memory layout."""
        memory = MEMORY_LAYOUTS.get(self.chip, {"flash": "0x08000000:0x20000", "ram": "0x20000000:0x4000"})
        flash_start, flash_size = memory["flash"].split(":")
        ram_start, ram_size = memory["ram"].split(":")

        linker = f'''/* Linker script for {self.chip} */
/* Generated by BabyOS Project Generator v{VERSION} */

ENTRY(Reset_Handler)

MEMORY
{{
  FLASH (rx) : ORIGIN = {flash_start}, LENGTH = {flash_size}
  RAM (rwx)  : ORIGIN = {ram_start}, LENGTH = {ram_size}
}}

/* Bootstrap info - placed at start of flash */
_boot_infos :
{{
  . = ALIGN(4);
  _sboot_info = .;
}} > FLASH

/* Code and read-only data */
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

/* BabyOS custom sections */
_bos_polling :
{{
  . = ALIGN(4);
  __start_bos_polling = .;
  KEEP(*(.bos_polling))
  __stop_bos_polling = .;
}} > FLASH

_bos_driver_init :
{{
  . = ALIGN(4);
  __start_bos_driver_init = .;
  KEEP(*(.driver_init))
  __stop_bos_driver_init = .;
}} > FLASH

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

.ARM.exidx :
{{
  *(.ARM.exidx*)
  *(.ARM.exidx)
}} > FLASH

/* Initialized data - copy from flash to ram */
.data :
{{
  . = ALIGN(4);
  _sdata = .;
  *(.data)
  *(.data*)
  _edata = .;
}} > RAM AT > FLASH

_sidata = LOADADDR(.data);

/* Uninitialized data */
.bss :
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

        with open(self.output_dir / "src" / f"{self.chip.lower()}_FLASH.ld", 'w', encoding='utf-8') as f:
            f.write(linker)

    def generate_startup(self):
        """Generate startup code."""
        startup = f'''/**
 * Startup code for {self.chip}
 * Generated by BabyOS Project Generator v{VERSION}
 */

#define WEAK __attribute__((weak))

extern unsigned long _sboot_info;
extern unsigned long _eboot_info;
extern unsigned long _stext;
extern unsigned long _etext;
extern unsigned long _sdata;
extern unsigned long _edata;
extern unsigned long _sbss;
extern unsigned long _ebss;
extern unsigned long _heap_start;
extern unsigned long _sidata;

extern int main(void);
extern void SystemInit(void);

void Reset_Handler(void) __attribute__((weak, alias("Default_Handler")));
void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

__attribute__((section(".isr_vector"), used))
void (* const g_pfnVectors[])(void) = {{
    (void *)(&_heap_start),
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,
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

    while(dest < &_edata) {{
        *dest++ = *src++;
    }}

    dest = &_sbss;
    while(dest < &_ebss) {{
        *dest++ = 0;
    }}

    SystemInit();
    main();

    while(1) {{ }}
}}
'''

        with open(self.output_dir / "src" / "startup.c", 'w', encoding='utf-8') as f:
            f.write(startup)

    def generate_main(self):
        """Generate main.c template."""
        main = f'''/**
 * {self.project_name}.c
 * Generated by BabyOS Project Generator v{VERSION}
 * Chip: {self.chip} | Vendor: {self.vendor}
 */

#include "b_os.h"

/* System clock configuration - implement per chip */
void SystemInit(void) {{
    /* Configure system clock */
    /* Example for STM32F1: SystemInit() sets HSI as clock source */
}}

/* Hardware initialization */
static void hardware_init(void) {{
    bHalInit();

    /* LED on PC13 (common on STM32F103C8T6 blue pill) */
    #ifdef B_HAL_GPIOC
    bHalGpioConfig(B_HAL_GPIOC, B_HAL_PIN13, B_HAL_GPIO_OUTPUT, B_HAL_GPIO_NOPULL);
    #endif

    /* UART for debug (on PA9/PA10) */
    /* bHalUartConfig(1, 115200); */
}}

/* Application task - LED blinking */
BOS_REG_POLLING_FUNC(led_task);

static uint32_t led_tick = 0;
void led_task(void) {{
    if (TICK_DIFF_BIT32(led_tick, bHalGetSysTick()) > MS2TICKS(500)) {{
        led_tick = bHalGetSysTick();
        #ifdef B_HAL_GPIOC
        static uint8_t state = 0;
        bHalGpioWritePin(B_HAL_GPIOC, B_HAL_PIN13, state);
        state = !state;
        #endif
    }}
}}

int main(void) {{
    bInit();
    hardware_init();

    /* Open devices - examples */
    /*
    int fd_flash = bOpen(SPIFLASH, BCORE_FLAG_RW);
    int fd_key = bOpen(KEY, BCORE_FLAG_RW);
    int fd_oled = bOpen(OLED, BCORE_FLAG_RW);
    */

    while (1) {{
        bExec();
    }}

    return 0;
}}
'''

        with open(self.output_dir / "src" / "main.c", 'w', encoding='utf-8') as f:
            f.write(main)

    def generate_b_config(self):
        """Generate initial b_config.h."""
        config = f'''/* b_config.h - Generated by BabyOS Project Generator v{VERSION} */
/* Chip: {self.chip} | Vendor: {self.vendor} */
/* Generated: {self.timestamp} */

#ifndef __B_CONFIG_H__
#define __B_CONFIG_H__

#define {self.chip} (1)
#define USE_HAL_DRIVER (1)

/* BabyOS Core */
#define _BOS_MODULES_ENABLE (1)
#define _BOS_ALGO_ENABLE (1)
#define _BOS_SERVICES_ENABLE (1)

/* System */
#define TICK_FRZ_HZ (1000)
#define _BOS_PRINTF_ENABLE (1)

/* HAL */
#define _BOS_HAL_GPIO_ENABLE (1)
#define _BOS_HAL_UART_ENABLE (1)
#define _BOS_HAL_SPI_ENABLE (1)
#define _BOS_HAL_I2C_ENABLE (1)

/* Default algorithms */
#define _ALGO_CRC_ENABLE (1)
#define _ALGO_CRC16_CCITT_ENABLE (1)
#define _ALGO_CRC8_ENABLE (1)
#define _ALGO_MD5_ENABLE (1)

/* Default modules */
#define _BOS_MOD_KV_ENABLE (1)
#define _BOS_MOD_SHELL_ENABLE (1)
#define _BOS_MOD_BUTTON_ENABLE (1)
#define _BOS_MOD_FS_ENABLE (1)

#include "b_type.h"

#endif /* __B_CONFIG_H__ */
'''

        with open(self.output_dir / "config" / "b_config.h", 'w', encoding='utf-8') as f:
            f.write(config)

    def generate_keil_project(self):
        """Generate Keil .uvprojx project."""
        keil = KeilProjectGenerator(
            project_name=self.project_name,
            chip=self.chip,
            vendor=self.vendor,
            output_dir=self.output_dir
        )
        return keil.generate_uvprojx_manual()

    def generate_cubemx_script(self):
        """Generate STM32CubeMX TCL script."""
        cubemx = STM32CubeMXIntegrator(self.chip, self.output_dir)
        return cubemx.generate_cubemx_script()

    def generate_readme(self):
        """Generate comprehensive README."""
        readme = f'''# {self.project_name}

BabyOS embedded project - {self.vendor}/{self.chip}

**Generator**: BabyOS Project Generator v{VERSION}  
**Date**: {self.timestamp}

## Project Structure

```
{self.project_name}/
├── Makefile                 # Build system
├── {self.project_name}.uvprojx  # Keil uVision project
├── README.md               # This file
├── config/
│   └── b_config.h         # BabyOS configuration
├── cubemx_config.tcl       # STM32CubeMX script
├── src/
│   ├── main.c             # Application entry
│   ├── startup.c          # Startup code
│   └── {self.chip.lower()}_FLASH.ld  # Linker script
└── bos/ -> BabyOS/        # Symlink to BabyOS
```

## Quick Start

### 1. Create BabyOS symlink
Windows (Git Bash / MSYS2):
```bash
ln -s /path/to/BabyOS bos
```

Windows CMD (requires admin):
```bat
mklink /J bos C:\\path\\to\\BabyOS
```

### 2. Build with Makefile
```bash
make          # Build project
make clean    # Clean
make rebuild  # Clean + build
make config   # Run Kconfig
make flash    # Flash to device
```

### 3. Build with Keil uVision
1. Open `{self.project_name}.uvprojx`
2. Select project -> Options for Target
3. Configure device, path, toolchain
4. Build (F7)

### 4. STM32CubeMX Integration
1. Install STM32CubeMX from st.com
2. Open STM32CubeMX
3. Load `cubemx_config.tcl` script
4. Generate code and merge

## Features

### BabyOS Core
- Device abstraction: `bOpen/bRead/bWrite/bCtl/bClose`
- Protothread tasks
- Timers, queues, semaphores

### HAL Support
- GPIO, UART, SPI, I2C
- DMA, QSPI, SDIO
- Flash, Ethernet, Watchdog

### Modules
- KV storage, File system, GUI
- Shell, State machine, Modbus
- MQTT, SSL, WiFi, USB, OTA

### Drivers
40+ drivers including sensors, displays, storage

## Development

### Debug with OpenOCD
```bash
make openocd
# In another terminal
arm-none-eabi-gdb build/{self.project_name}.elf
target remote localhost:3333
```

### Debug with J-Link
```bash
make jlink
```

## Documentation

- BabyOS: https://babyos.cn/doc/
- BabyOS Source: https://gitee.com/notrynohigh/BabyOS
- Keil: https://www.keil.com/
- STM32CubeMX: https://www.st.com/stm32cubemx

## Supported Chips

- ST: STM32F10X, STM32G0, STM32L4, STM32F4, STM32G4, STM32H5
- Nation: N32L40x, N32G45x
- Mindmotion: MM32SPIN
- WinChipHead: CH32F103
- Espressif: ESP32C3

## License

MIT License - BabyOS Project Generator by aiclaw
'''

        with open(self.output_dir / "README.md", 'w', encoding='utf-8') as f:
            f.write(readme)

    def generate(self):
        """Generate complete project."""
        print(f"[*] BabyOS Project Generator v{VERSION}")
        print(f"    Project: {self.project_name}")
        print(f"    Vendor: {self.vendor}, Chip: {self.chip}")
        print(f"    Output: {self.output_dir}")

        self.create_directory_structure()
        self.generate_makefile()
        self.generate_linker_script()
        self.generate_startup()
        self.generate_main()
        self.generate_b_config()
        self.generate_readme()
        self.generate_keil_project()
        self.generate_cubemx_script()

        # Create bos symlink reference
        bos_placeholder = self.output_dir / "bos_README.txt"
        with open(bos_placeholder, 'w') as f:
            f.write(f"BabyOS path: {self.bos_root}\n")
            f.write("Create symlink: ln -s /path/to/BabyOS bos\n")

        print(f"\n[✓] Project generated successfully!")
        print(f"\nFiles created:")
        print(f"  - Makefile (build system)")
        print(f"  - {self.project_name}.uvprojx (Keil project)")
        print(f"  - src/main.c, startup.c, *_FLASH.ld")
        print(f"  - config/b_config.h")
        print(f"  - cubemx_config.tcl (STM32CubeMX)")
        print(f"  - README.md")

        return True


def main():
    parser = argparse.ArgumentParser(
        description=f"BabyOS Project Generator v{VERSION} - Enhanced with Keil and CubeMX support",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )

    parser.add_argument("--version", action="version", version=f"%(prog)s {VERSION}")
    parser.add_argument("--bos-root", help="Path to BabyOS directory")
    parser.add_argument("-o", "--output", default="./babyos_project", help="Output directory")
    parser.add_argument("--vendor", "-v", required=True, help="MCU vendor")
    parser.add_argument("--chip", "-c", required=True, help="MCU chip")
    parser.add_argument("--name", "-n", help="Project name")
    parser.add_argument("--keil-only", action="store_true", help="Generate only Keil project")
    parser.add_argument("--makefile-only", action="store_true", help="Generate only Makefile")

    args = parser.parse_args()

    # Find BabyOS root
    script_dir = Path(__file__).parent
    bos_root = args.bos_root or os.environ.get("BABYOS_ROOT")

    if not bos_root:
        candidates = [
            script_dir.parent / "BabyOS",
            script_dir.parent.parent / "BabyOS",
            Path("/Users/liklon/.openclaw/workspace/BabyOS"),
        ]
        for candidate in candidates:
            if (candidate / "bos" / "b_os.h").exists():
                bos_root = str(candidate)
                break

    if not bos_root:
        print("Error: BabyOS directory not found. Use --bos-root or set BABYOS_ROOT")
        return 1

    print(f"[*] Using BabyOS at: {bos_root}")

    output_dir = Path(args.output).absolute()

    generator = BabyOSProjectGeneratorV2(
        bos_root=bos_root,
        output_dir=output_dir,
        vendor=args.vendor,
        chip=args.chip,
        project_name=args.name
    )

    try:
        generator.generate()
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())