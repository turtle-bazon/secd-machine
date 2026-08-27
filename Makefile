# SECD Machine Makefile
#
# Targets:
#   make              - Build library (libsecd.a)
#   make machines     - Build .machine files for all targets
#   make rp2040-pico  - Build .machine for RP2040 Pico
#   make clean        - Clean build artifacts

CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -Iinclude
LDFLAGS = -lm

# Source files
SRC_DIR = src/core
HAL_DIR = src/hal
SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(HAL_DIR)/*.c)
OBJS = $(SRCS:.c=.o)

# Library
LIB = libsecd.a

# Output
OUTPUT_DIR = output
# Scratch dir for generated target metadata (zipped into .machine, not delivered)
META_DIR = $(OUTPUT_DIR)/.build
TARGETS = rp2040-pico rp2040-zero rp2350-zero rp2350-beetle seeed-xiao-samd21 esp32c3-supermini stamp-s3a blue-pill black-pill-f401 esp32s3-devkit lolin-s3-mini lolin-s2-mini nrf52840-promicro

# Pico SDK
PICO_SDK_PATH ?= /tmp/pico-sdk
PICO_TOOLCHAIN_M0PLUS = $(PICO_SDK_PATH)/cmake/preload/toolchains/pico_arm_cortex_m0plus_gcc.cmake
PICO_TOOLCHAIN_M33 = $(PICO_SDK_PATH)/cmake/preload/toolchains/pico_arm_cortex_m33_gcc.cmake

# Per-board build config: build dir, toolchain file, pico board, header dirs.
# RP2350 boards use the Cortex-M33 toolchain; RP2040 boards use M0+.
PICO_BUILD_DIR ?= build/rp2040-pico
PICO_BUILD_DIR_RELEASE = build/rp2040-pico-release
PICO_BOARD ?= pico

# RP2040 Zero (M0+, 2MB flash, WS2812 on GPIO16)
RP2040_ZERO_DIR = build/rp2040-zero
RP2040_ZERO_DIR_RELEASE = build/rp2040-zero-release
RP2040_ZERO_TOOLCHAIN = $(PICO_TOOLCHAIN_M0PLUS)
RP2040_ZERO_BOARD = waveshare_rp2040_zero

# RP2350 Zero (M33, 4MB flash, WS2812 on GPIO16)
RP2350_ZERO_DIR = build/rp2350-zero
RP2350_ZERO_DIR_RELEASE = build/rp2350-zero-release
RP2350_ZERO_TOOLCHAIN = $(PICO_TOOLCHAIN_M33)
RP2350_ZERO_BOARD = waveshare_rp2350_zero

# RP2350 Beast (M33, 2MB flash, LED on GPIO25). Custom header in board-headers/.
RP2350_BEETLE_DIR = build/rp2350-beetle
RP2350_BEETLE_DIR_RELEASE = build/rp2350-beetle-release
RP2350_BEETLE_TOOLCHAIN = $(PICO_TOOLCHAIN_M33)
RP2350_BEETLE_BOARD = dfrobot_beetle_rp2350
RP2350_BEETLE_HEADERS = $(CURDIR)/board-headers/boards

# Seeed XIAO SAMD21 (bare metal, Cortex-M0+)
SAMD21_DIR = build/samd21
SAMD21_DIR_RELEASE = build/samd21-release
SAMD21_CC = arm-none-eabi-g++
SAMD21_CFLAGS = -mcpu=cortex-m0plus -mthumb -Os -ffunction-sections -fdata-sections \
	-Wall -Wextra -Iinclude -DSECD_BYTEVEC_ARENA_SIZE=8192 \
	-DSECD_FEATURE_GPIO=1 -DSECD_FEATURE_UART=0 \
	-DSECD_MACHINE_VERSION='"0.0.1.0"' -DSECD_FEATURES_STR='"gpio"' -DSECD_DEBUG_BUILD=1
SAMD21_CFLAGS_RELEASE = -mcpu=cortex-m0plus -mthumb -Os -ffunction-sections -fdata-sections \
	-Wall -Wextra -Iinclude \
	-DSECD_FEATURE_GPIO=1 -DSECD_FEATURE_UART=0 \
	-DSECD_MACHINE_VERSION='"0.0.1.0"' -DSECD_FEATURES_STR='"gpio"' -DSECD_DEBUG_BUILD=0
SAMD21_SRCS = platforms/samd21/main.cpp platforms/samd21/startup_samd21.cpp \
	platforms/samd21/syscalls.cpp \
	src/firmware/secd_boot.cpp \
	src/hal/samd21.cpp \
	src/core/heap.cpp src/core/gc.cpp src/core/machine.cpp src/core/bytecode.cpp \
	src/core/primitives.cpp src/core/symbols.cpp

# STM32 bare-metal boards: Blue Pill F103CBT6 (Cortex-M3 @72MHz) and Black
# Pill F401RCT6 (Cortex-M4 @84MHz, WeAct "stm32f401/411 v1204"). Shared
# platform glue; only -mcpu, HAL file and linker script differ. Console is
# USART1 PA9/PA10; I2C master on two buses (I2C1 PB6/PB7, I2C2 PB10/PB11).
STM32_CC = arm-none-eabi-g++
STM32_CORE_SRCS = platforms/stm32/main.cpp platforms/stm32/startup_stm32.cpp \
	platforms/stm32/syscalls.cpp \
	src/firmware/secd_boot.cpp \
	src/core/heap.cpp src/core/gc.cpp src/core/machine.cpp src/core/bytecode.cpp \
	src/core/primitives.cpp src/core/symbols.cpp

STM32F103_DIR = build/stm32f103
STM32F103_DIR_RELEASE = build/stm32f103-release
STM32F103_CFLAGS = -mcpu=cortex-m3 -mthumb -Os -ffunction-sections -fdata-sections \
	-Wall -Wextra -Iinclude -DSECD_BYTEVEC_ARENA_SIZE=8192 \
	-DSECD_FEATURE_GPIO=1 -DSECD_FEATURE_UART=1 -DSECD_FEATURE_I2C=1 \
	-DSECD_MACHINE_VERSION='"0.0.1.0"' -DSECD_PLATFORM_NAME='"Blue Pill F103CB"' \
	-DSECD_HEAP_OBJECTS=1024 -DSECD_FEATURES_STR='"gpio,uart,i2c"' -DSECD_DEBUG_BUILD=1
STM32F103_CFLAGS_RELEASE = $(filter-out -DSECD_DEBUG_BUILD=1,$(STM32F103_CFLAGS)) -DSECD_DEBUG_BUILD=0
STM32F103_SRCS = $(STM32_CORE_SRCS) src/hal/stm32f1.cpp

STM32F401_DIR = build/stm32f401
STM32F401_DIR_RELEASE = build/stm32f401-release
STM32F401_CFLAGS = -mcpu=cortex-m4 -mthumb -Os -ffunction-sections -fdata-sections \
	-Wall -Wextra -Iinclude -DSECD_BYTEVEC_ARENA_SIZE=49152 \
	-DSECD_FEATURE_GPIO=1 -DSECD_FEATURE_UART=1 -DSECD_FEATURE_I2C=1 \
	-DSECD_MACHINE_VERSION='"0.0.1.0"' -DSECD_PLATFORM_NAME='"Black Pill F401RC"' \
	-DSECD_HEAP_OBJECTS=4096 -DSECD_FEATURES_STR='"gpio,uart,i2c"' -DSECD_DEBUG_BUILD=1
STM32F401_CFLAGS_RELEASE = $(filter-out -DSECD_DEBUG_BUILD=1,$(STM32F401_CFLAGS)) -DSECD_DEBUG_BUILD=0
STM32F401_SRCS = $(STM32_CORE_SRCS) src/hal/stm32f4.cpp

# nRF52840 SuperMini / XIAO nRF52840 BLE (bare metal, Cortex-M4F)
NRF52840_DIR = build/nrf52840
NRF52840_DIR_RELEASE = build/nrf52840-release
NRF52840_CC = arm-none-eabi-g++
NRF52840_CORE_SRCS = platforms/nrf52840/main.cpp platforms/nrf52840/startup_nrf52840.cpp \
	platforms/nrf52840/syscalls.cpp \
	src/firmware/secd_boot.cpp \
	src/core/heap.cpp src/core/gc.cpp src/core/machine.cpp src/core/bytecode.cpp \
	src/core/primitives.cpp src/core/symbols.cpp

NRF52840_CFLAGS = -mcpu=cortex-m4 -mthumb -Os -ffunction-sections -fdata-sections \
	-Wall -Wextra -Iinclude -DSECD_FEATURE_GPIO=1 -DSECD_FEATURE_UART=1 -DSECD_FEATURE_I2C=1 \
	-DSECD_FEATURE_HID=1 -DSECD_FEATURE_BLE=1 -DSOFTDEVICE_PRESENT \
	-DSECD_MACHINE_VERSION='"0.0.1.0"' -DSECD_PLATFORM_NAME='"nRF52840 SuperMini"' \
	-DSECD_HEAP_OBJECTS=4096 -DSECD_FEATURES_STR='"gpio,uart,i2c,ble"' -DSECD_DEBUG_BUILD=1 \
	-DSECD_APP_BASE=$(NRF52840_APP_BASE) \
	-Iplatforms/nrf52840 \
	-Ithird_party/cherryusb/common -Ithird_party/cherryusb/core \
	-Ithird_party/cherryusb/class/cdc -Ithird_party/cherryusb/class/hid \
	-Ithird_party/cherryusb/port/nrf52840 \
	-Ithird_party/nordic/shim -Ithird_party/nordic/mdk -Ithird_party/nordic/hal \
	-Ithird_party/nordic/drivers/src -Ithird_party/cmsis \
	-Ithird_party/nordic/s140/headers -Ithird_party/nordic/s140/headers/nrf52 \
	-DNRF52840_XXAA -fpermissive
NRF52840_CFLAGS_RELEASE = $(filter-out -DSECD_DEBUG_BUILD=1,$(NRF52840_CFLAGS)) -DSECD_DEBUG_BUILD=0
NRF52840_SRCS = $(NRF52840_CORE_SRCS) src/hal/nrf52840.cpp \
	src/hal/nrf52840_ble.cpp \
	platforms/nrf52840/usb.cpp

# CherryUSB device stack on top of TinyUSB's proven nRF5x DCD. Compiled as C++
# (whole image is g++) with -fpermissive so the stack's C sources build cleanly.
NRF52840_CHERRY_SRCS = \
	third_party/cherryusb/core/usbd_core.c \
	third_party/cherryusb/class/cdc/usbd_cdc_acm.c \
	third_party/cherryusb/class/hid/usbd_hid.c \
	third_party/cherryusb/port/nrf52840/usb_dc_nrf5x.c
NRF52840_CHERRY_OBJS := $(patsubst %.c,build/nrf52840/cherry/%.o,$(NRF52840_CHERRY_SRCS))
NRF52840_SRCS += $(NRF52840_CHERRY_OBJS)

$(NRF52840_DIR)/cherry/%.o: %.c
	@mkdir -p $(dir $@)
	@arm-none-eabi-g++ -c $(NRF52840_CFLAGS) $< -o $@

# Application flash base for nRF52840 comes from the board metadata (the
# Adafruit nRF52 bootloader + S140 SoftDevice occupy 0x0000..0x26000), and is
# injected into the linker as __app_base__. Single source of truth: the board JSON.
NRF52840_BOARD = targets/boards/nrf52840-promicro.json
NRF52840_APP_BASE = $(shell python3 -c "import json; print(json.load(open('$(NRF52840_BOARD)'))['output']['base_address'])")
NRF52840_UF2_FAMILY = $(shell python3 -c "import json; print(json.load(open('$(NRF52840_BOARD)'))['output']['uf2_family_id'])")
# Extra link flags (the comma in -Wl,--defsym must live inside a $(...) so
# $(call) doesn't split it on the comma).
# --nmagic disables 64KB ELF page alignment so the PT_LOAD base equals the true
# app address (0x26000); otherwise objcopy pads a 0x6000 hole and the UF2 would
# flash the vector table to the wrong address.
NRF52840_LINKFLAGS = -Wl,--defsym=__app_base__=$(NRF52840_APP_BASE) -Wl,--nmagic

define STM32-LINK
	@mkdir -p $(1)
	@echo "void __cxa_pure_virtual(void) {}" > $(1)/dummy.cpp
	@arm-none-eabi-g++ -c $(1)/dummy.cpp -o $(1)/dummy.o
	@arm-none-eabi-ar rcs $(1)/libstdc++.a $(1)/dummy.o
	@$(2) $(3) $(5) $(6) -Wl,-T,$(4) -Wl,--gc-sections -nostartfiles \
		-L$(1) -lm -o $(1)/secd-machine.elf
endef

$(STM32F103_DIR)/secd-machine.elf: $(STM32F103_SRCS)
	$(call STM32-LINK,$(STM32F103_DIR),$(STM32_CC),$(STM32F103_CFLAGS),platforms/stm32/stm32f103cb.ld,$(STM32F103_SRCS))

$(STM32F103_DIR_RELEASE)/secd-machine.elf: $(STM32F103_SRCS)
	$(call STM32-LINK,$(STM32F103_DIR_RELEASE),$(STM32_CC),$(STM32F103_CFLAGS_RELEASE),platforms/stm32/stm32f103cb.ld,$(STM32F103_SRCS))

$(STM32F401_DIR)/secd-machine.elf: $(STM32F401_SRCS)
	$(call STM32-LINK,$(STM32F401_DIR),$(STM32_CC),$(STM32F401_CFLAGS),platforms/stm32/stm32f401rc.ld,$(STM32F401_SRCS))

$(STM32F401_DIR_RELEASE)/secd-machine.elf: $(STM32F401_SRCS)
	$(call STM32-LINK,$(STM32F401_DIR_RELEASE),$(STM32_CC),$(STM32F401_CFLAGS_RELEASE),platforms/stm32/stm32f401rc.ld,$(STM32F401_SRCS))

$(NRF52840_DIR)/secd-machine.elf: $(NRF52840_SRCS)
	$(call STM32-LINK,$(NRF52840_DIR),$(STM32_CC),$(NRF52840_CFLAGS),platforms/nrf52840/nrf52840-ble.ld,$(NRF52840_SRCS),$(NRF52840_LINKFLAGS))

$(NRF52840_DIR_RELEASE)/secd-machine.elf: $(NRF52840_SRCS)
	$(call STM32-LINK,$(NRF52840_DIR_RELEASE),$(STM32_CC),$(NRF52840_CFLAGS_RELEASE),platforms/nrf52840/nrf52840-ble.ld,$(NRF52840_SRCS),$(NRF52840_LINKFLAGS))






build/%/firmware.bin: build/%/secd-machine.elf
	@arm-none-eabi-objcopy -O binary $< $@

define STM32-PACK
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$(3)', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(1)/firmware.bin', 'firmware.bin'); \
		zf.write('$(META_DIR)/$(2).metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $(3)"
endef

$(OUTPUT_DIR)/blue-pill.machine: $(STM32F103_DIR)/firmware.bin $(META_DIR)/blue-pill.metadata.json
	$(call STM32-PACK,$(STM32F103_DIR),blue-pill,$@)

# nRF52840 board .machine packaging (UF2 for the Adafruit nRF52 bootloader)
build/nrf52840/firmware.uf2: build/nrf52840/firmware.bin
	@python3 tools/uf2.py $< $@ $(NRF52840_APP_BASE) $(NRF52840_UF2_FAMILY)

build/nrf52840-release/firmware.uf2: build/nrf52840-release/firmware.bin
	@python3 tools/uf2.py $< $@ $(NRF52840_APP_BASE) $(NRF52840_UF2_FAMILY)

$(OUTPUT_DIR)/nrf52840-promicro.machine: build/nrf52840/firmware.uf2 $(META_DIR)/nrf52840-promicro.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('build/nrf52840/firmware.uf2', 'firmware.uf2'); \
		zf.write('$(META_DIR)/nrf52840-promicro.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.uf2 + metadata.json)"

$(OUTPUT_DIR)/nrf52840-promicro.release.machine: build/nrf52840-release/firmware.uf2 $(META_DIR)/nrf52840-promicro.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('build/nrf52840-release/firmware.uf2', 'firmware.uf2'); \
		zf.write('$(META_DIR)/nrf52840-promicro.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.uf2 + metadata.json)"

$(OUTPUT_DIR)/blue-pill.release.machine: $(STM32F103_DIR_RELEASE)/firmware.bin $(META_DIR)/blue-pill.metadata.json
	$(call STM32-PACK,$(STM32F103_DIR_RELEASE),blue-pill,$@)

$(OUTPUT_DIR)/black-pill-f401.machine: $(STM32F401_DIR)/firmware.bin $(META_DIR)/black-pill-f401.metadata.json
	$(call STM32-PACK,$(STM32F401_DIR),black-pill-f401,$@)

$(OUTPUT_DIR)/black-pill-f401.release.machine: $(STM32F401_DIR_RELEASE)/firmware.bin $(META_DIR)/black-pill-f401.metadata.json
	$(call STM32-PACK,$(STM32F401_DIR_RELEASE),black-pill-f401,$@)


# ESP32-C3 SuperMini (ESP-IDF, single-core RISC-V). Bytecode is appended to the
# app image at runtime (never compiled in); see link-machine-esp32.
ESP_IDF_DIR ?= /home/turtle/esp/esp-idf
ESP32C3_DIR = platforms/esp32
ESP32C3_BUILD = $(ESP32C3_DIR)/build
ESP32C3_BUILD_RELEASE = $(ESP32C3_DIR)/build-release
ESP32C3_BIN = $(ESP32C3_BUILD)/secd_machine.bin
ESP32C3_BIN_RELEASE = $(ESP32C3_BUILD_RELEASE)/secd_machine.bin
ESP32C3_SRCS = src/hal/esp32.cpp src/core/heap.cpp src/core/gc.cpp src/core/machine.cpp \
	src/core/bytecode.cpp src/core/primitives.cpp src/core/symbols.cpp \
	$(ESP32C3_DIR)/components/secd/secd_start.cpp \
	$(ESP32C3_DIR)/CMakeLists.txt $(ESP32C3_DIR)/sdkconfig.defaults \
	$(ESP32C3_DIR)/components/secd/CMakeLists.txt

# ESP32-S3 Stamp-S3A (ESP-IDF, dual-core Xtensa LX7). Bytecode merged into the app image.
ESP32S3_DIR = platforms/esp32s3
ESP32S3_BUILD = $(ESP32S3_DIR)/build
ESP32S3_BUILD_RELEASE = $(ESP32S3_DIR)/build-release
ESP32S3_BIN = $(ESP32S3_BUILD)/secd_machine.bin
ESP32S3_BIN_RELEASE = $(ESP32S3_BUILD_RELEASE)/secd_machine.bin
ESP32S3_SRCS = src/hal/esp32.cpp src/core/heap.cpp src/core/gc.cpp src/core/machine.cpp \
	src/core/bytecode.cpp src/core/primitives.cpp src/core/symbols.cpp \
	$(ESP32S3_DIR)/components/secd/secd_start.cpp \
	$(ESP32S3_DIR)/components/secd/usb.cpp \
	$(ESP32S3_DIR)/CMakeLists.txt $(ESP32S3_DIR)/sdkconfig.defaults \
	$(ESP32S3_DIR)/components/secd/CMakeLists.txt

# Board features (peripherals linked into the firmware), comma-separated.
# The board's enabled capabilities; e.g. a bare chip target would build
# with SECD_FEATURES= (no GPIO/UART drivers linked).
SECD_FEATURES ?= gpio,uart,i2c

# secd-lisp (CL build that provides the version for .machine metadata)
SECD_LISP_DIR ?= ../secd-lisp
SECD_LISP_ASD = $(SECD_LISP_DIR)/secd-lisp.asd

# Test files
TEST_DIR = tests
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS = $(TEST_SRCS:.c=.o)

.PHONY: all clean tests host-bn-test machines machine $(TARGETS) rp2040-debug rp2040-release

all: $(LIB)

$(LIB): $(OBJS)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Host-side bignum engine test (no HAL needed; stubbed). Runs with host g++.
host-bn-test:
	@g++ -O1 -DSECD_BYTEVEC_MAX=4096 -Iinclude -o build/bn_test \
		tests/host/bignum_test.cpp tests/host/hal_stub.cpp \
		src/core/heap.cpp src/core/gc.cpp src/core/bytecode.cpp \
		src/core/symbols.cpp src/core/machine.cpp src/core/primitives.cpp -lm
	@./build/bn_test

tests: $(LIB) $(TEST_OBJS)
	$(CC) $(TEST_OBJS) $(LIB) $(LDFLAGS) -o run_tests
	./run_tests

# Build .machine files (zip with firmware + metadata)
machines: $(TARGETS)
machine: machines

# Generate target metadata with the CL build version injected
# (board file in targets/boards/; its chip base is merged in by secd-lisp).
# Depends on every board+chip json: editing a chip's capabilities/primitives
# must invalidate the cached metadata, not just the board file.
CHIP_JSONS = $(wildcard targets/chips/*.json)
$(META_DIR)/%.metadata.json: targets/boards/%.json $(CHIP_JSONS)
	@mkdir -p $(META_DIR)
	@echo "Generating metadata for $< (version from secd-lisp build)..."
	@sbcl --non-interactive --load $(SECD_LISP_ASD) \
		--eval '(asdf:load-system :secd-lisp)' \
		--eval '(secd-lisp:write-target-metadata "$<" "$@")' \
		|| (echo "Error generating metadata for $<"; exit 1)

$(TARGETS): %: $(OUTPUT_DIR)/%.machine

# RP2040: build real firmware with pico-sdk (debug variant)
$(OUTPUT_DIR)/rp2040-pico.machine: $(PICO_BUILD_DIR)/secd-machine.uf2 $(META_DIR)/rp2040-pico.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(PICO_BUILD_DIR)/secd-machine.uf2', 'firmware.uf2'); \
		zf.write('$(META_DIR)/rp2040-pico.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.uf2 + metadata.json, debug)"

# RP2040: release variant (no serial, start immediately)
$(OUTPUT_DIR)/rp2040-pico.release.machine: $(PICO_BUILD_DIR_RELEASE)/secd-machine.uf2 $(META_DIR)/rp2040-pico.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(PICO_BUILD_DIR_RELEASE)/secd-machine.uf2', 'firmware.uf2'); \
		zf.write('$(META_DIR)/rp2040-pico.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.uf2 + metadata.json, release)"

rp2040-debug: $(OUTPUT_DIR)/rp2040-pico.machine
rp2040-release: $(OUTPUT_DIR)/rp2040-pico.release.machine

# Shared RP2 (RP2040/RP2350) cmake configure + build for a given build dir.
#  $$1 = build dir, $$2 = toolchain file, $$3 = SECD_DEBUG_BUILD (ON=debug/OFF=release),
#  $$4 = PICO_BOARD, $$5 = PICO_BOARD_HEADER_DIRS (may be empty)
define RP2-CMAKE
	@echo "Building RP2 firmware (SECD_DEBUG_BUILD=$(3), board=$(4)) with pico-sdk..."
	@mkdir -p $(1)/cxx-shim
	@for h in cassert cstdlib cstdint cstring cstddef cstdio; do \
		case $$h in \
			cassert) printf '#ifndef _CASSERT_H\n#define _CASSERT_H\n#include <assert.h>\n#endif\n' > $(1)/cxx-shim/$$h ;; \
			cstdlib) printf '#ifndef _CSTDLIB_H\n#define _CSTDLIB_H\n#include <stdlib.h>\nnamespace std { using ::size_t; using ::malloc; using ::realloc; using ::free; using ::calloc; using ::exit; using ::abort; }\n#endif\n' > $(1)/cxx-shim/$$h ;; \
			cstdint) printf '#ifndef _CSTDINT_H\n#define _CSTDINT_H\n#include <stdint.h>\n#endif\n' > $(1)/cxx-shim/$$h ;; \
			cstring) printf '#ifndef _CSTRING_H\n#define _CSTRING_H\n#include <string.h>\nnamespace std { using ::memcpy; using ::memset; using ::memcmp; using ::strlen; using ::strcmp; using ::strcpy; using ::strncpy; }\n#endif\n' > $(1)/cxx-shim/$$h ;; \
			cstddef) printf '#ifndef _CSTDDEF_H\n#define _CSTDDEF_H\n#include <stddef.h>\nnamespace std { using ::size_t; using ::ptrdiff_t; using ::nullptr_t; }\n#endif\n' > $(1)/cxx-shim/$$h ;; \
			cstdio) printf '#ifndef _CSTDIO_H\n#define _CSTDIO_H\n#include <stdio.h>\n#endif\n' > $(1)/cxx-shim/$$h ;; \
		esac; \
	done
	@cd $(1) && \
		echo "void __cxa_pure_virtual(void) {}" > /tmp/dummy.cpp && \
		arm-none-eabi-g++ -c /tmp/dummy.cpp -o /tmp/dummy.o && \
		arm-none-eabi-ar rcs libstdc++.a /tmp/dummy.o
	@cd $(1) && cmake -S ../../platforms/rp2 -B . \
		-DCMAKE_TOOLCHAIN_FILE=$(2) \
		-DPICO_SDK_PATH=$(PICO_SDK_PATH) \
		-DPICO_BOARD=$(4) \
		$(if $(5),-DPICO_BOARD_HEADER_DIRS=$(5),) \
		-DSECD_DEBUG_BUILD=$(3) \
		-DSECD_FEATURES=$(SECD_FEATURES)
	@cmake --build $(1) -- -j$$(nproc)
endef

# Shared RP2 source list (RP2040 and RP2350 use the same firmware sources).
# Depends on the sources so a core/HAL change triggers a CMake rebuild
# (CMake itself re-tracks per-file deps; this just forces the build step).
RP2040_SRCS = platforms/rp2/CMakeLists.txt \
	platforms/rp2/main.cpp platforms/rp2/usb.cpp \
	src/core/heap.cpp src/core/gc.cpp src/core/machine.cpp \
	src/core/bytecode.cpp src/core/primitives.cpp src/core/symbols.cpp \
	src/hal/rp2040.cpp

# rp2040-pico (keeps existing debug+release behavior)
$(PICO_BUILD_DIR)/secd-machine.uf2: $(RP2040_SRCS)
	$(call RP2-CMAKE,$(PICO_BUILD_DIR),$(PICO_TOOLCHAIN_M0PLUS),ON,$(PICO_BOARD))

$(PICO_BUILD_DIR_RELEASE)/secd-machine.uf2: $(RP2040_SRCS)
	$(call RP2-CMAKE,$(PICO_BUILD_DIR_RELEASE),$(PICO_TOOLCHAIN_M0PLUS),OFF,$(PICO_BOARD))

# rp2040-zero (M0+, WS2812 on GPIO16)
$(RP2040_ZERO_DIR)/secd-machine.uf2: $(RP2040_SRCS)
	$(call RP2-CMAKE,$(RP2040_ZERO_DIR),$(RP2040_ZERO_TOOLCHAIN),ON,$(RP2040_ZERO_BOARD))

$(RP2040_ZERO_DIR_RELEASE)/secd-machine.uf2: $(RP2040_SRCS)
	$(call RP2-CMAKE,$(RP2040_ZERO_DIR_RELEASE),$(RP2040_ZERO_TOOLCHAIN),OFF,$(RP2040_ZERO_BOARD))

# rp2350-zero (M33, WS2812 on GPIO16)
$(RP2350_ZERO_DIR)/secd-machine.uf2: $(RP2040_SRCS)
	$(call RP2-CMAKE,$(RP2350_ZERO_DIR),$(RP2350_ZERO_TOOLCHAIN),ON,$(RP2350_ZERO_BOARD))

$(RP2350_ZERO_DIR_RELEASE)/secd-machine.uf2: $(RP2040_SRCS)
	$(call RP2-CMAKE,$(RP2350_ZERO_DIR_RELEASE),$(RP2350_ZERO_TOOLCHAIN),OFF,$(RP2350_ZERO_BOARD))

# rp2350-beetle (M33, LED on GPIO25, custom board header)
$(RP2350_BEETLE_DIR)/secd-machine.uf2: $(RP2040_SRCS)
	$(call RP2-CMAKE,$(RP2350_BEETLE_DIR),$(RP2350_BEETLE_TOOLCHAIN),ON,$(RP2350_BEETLE_BOARD),$(RP2350_BEETLE_HEADERS))

$(RP2350_BEETLE_DIR_RELEASE)/secd-machine.uf2: $(RP2040_SRCS)
	$(call RP2-CMAKE,$(RP2350_BEETLE_DIR_RELEASE),$(RP2350_BEETLE_TOOLCHAIN),OFF,$(RP2350_BEETLE_BOARD),$(RP2350_BEETLE_HEADERS))

# Generic machine packing for an RP2 board.
#  $$1 = target name, $$2 = firmware build dir
define RP2-PACK
$(OUTPUT_DIR)/$(1).machine: $(2)/secd-machine.uf2 $(META_DIR)/$(1).metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(2)/secd-machine.uf2', 'firmware.uf2'); \
		zf.write('$(META_DIR)/$(1).metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $$@ (firmware.uf2 + metadata.json)"
endef
$(eval $(call RP2-PACK,rp2040-zero,$(RP2040_ZERO_DIR)))
$(eval $(call RP2-PACK,rp2350-zero,$(RP2350_ZERO_DIR)))
$(eval $(call RP2-PACK,rp2350-beetle,$(RP2350_BEETLE_DIR)))

# rp2040-zero release variant (same metadata as debug, release firmware)
$(OUTPUT_DIR)/rp2040-zero.release.machine: $(RP2040_ZERO_DIR_RELEASE)/secd-machine.uf2 $(META_DIR)/rp2040-zero.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(RP2040_ZERO_DIR_RELEASE)/secd-machine.uf2', 'firmware.uf2'); \
		zf.write('$(META_DIR)/rp2040-zero.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.uf2 + metadata.json, release)"

# Generic release packing for an RP2 board.
#  $$1 = target name, $$2 = release firmware build dir
define RP2-PACK-RELEASE
$(OUTPUT_DIR)/$(1).release.machine: $(2)/secd-machine.uf2 $(META_DIR)/$(1).metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(2)/secd-machine.uf2', 'firmware.uf2'); \
		zf.write('$(META_DIR)/$(1).metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $$@ (firmware.uf2 + metadata.json, release)"
endef
$(eval $(call RP2-PACK-RELEASE,rp2350-zero,$(RP2350_ZERO_DIR_RELEASE)))
$(eval $(call RP2-PACK-RELEASE,rp2350-beetle,$(RP2350_BEETLE_DIR_RELEASE)))

# Seeed XIAO SAMD21: bare-metal build with arm-none-eabi-g++.
define SAMD21-COMPILE
	@mkdir -p $(1)
	@echo "void __cxa_pure_virtual(void) {}" > $(1)/dummy.cpp
	@arm-none-eabi-g++ -c $(1)/dummy.cpp -o $(1)/dummy.o
	@arm-none-eabi-ar rcs $(1)/libstdc++.a $(1)/dummy.o
	@$(2) $(3) $(SAMD21_SRCS) \
		-Wl,-T,platforms/samd21/samd21g18a.ld -Wl,--gc-sections -nostartfiles \
		-L$(1) -lm -o $(1)/secd-machine.elf
endef

$(SAMD21_DIR)/secd-machine.elf: $(SAMD21_SRCS)
	$(call SAMD21-COMPILE,$(SAMD21_DIR),$(SAMD21_CC),$(SAMD21_CFLAGS))

$(SAMD21_DIR_RELEASE)/secd-machine.elf: $(SAMD21_SRCS)
	$(call SAMD21-COMPILE,$(SAMD21_DIR_RELEASE),$(SAMD21_CC),$(SAMD21_CFLAGS_RELEASE))

$(SAMD21_DIR)/firmware.bin: $(SAMD21_DIR)/secd-machine.elf
	@arm-none-eabi-objcopy -O binary $< $@

$(SAMD21_DIR_RELEASE)/firmware.bin: $(SAMD21_DIR_RELEASE)/secd-machine.elf
	@arm-none-eabi-objcopy -O binary $< $@

# The XIAO's UF2 bootloader expects the app at 0x2000 (family 0x68ED2B88).
$(SAMD21_DIR)/firmware.uf2: $(SAMD21_DIR)/firmware.bin tools/uf2.py
	@python3 tools/uf2.py $< $@ 0x2000 0x68ED2B88

$(SAMD21_DIR_RELEASE)/firmware.uf2: $(SAMD21_DIR_RELEASE)/firmware.bin tools/uf2.py
	@python3 tools/uf2.py $< $@ 0x2000 0x68ED2B88

$(OUTPUT_DIR)/seeed-xiao-samd21.machine: $(SAMD21_DIR)/firmware.uf2 $(META_DIR)/seeed-xiao-samd21.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(SAMD21_DIR)/firmware.uf2', 'firmware.uf2'); \
		zf.write('$(META_DIR)/seeed-xiao-samd21.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.uf2 + metadata.json)"

$(OUTPUT_DIR)/seeed-xiao-samd21.release.machine: $(SAMD21_DIR_RELEASE)/firmware.uf2 $(META_DIR)/seeed-xiao-samd21.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(SAMD21_DIR_RELEASE)/firmware.uf2', 'firmware.uf2'); \
		zf.write('$(META_DIR)/seeed-xiao-samd21.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.uf2 + metadata.json, release)"

# ESP32-C3 SuperMini: build firmware with ESP-IDF, then zip .machine.
# Debug vs release via SECD_DEBUG_BUILD (set with -D override).
$(ESP32C3_BIN): $(ESP32C3_SRCS)
	@. $(ESP_IDF_DIR)/export.sh >/dev/null 2>&1 && cd $(ESP32C3_DIR) && idf.py build >/dev/null

$(ESP32C3_BIN_RELEASE): $(ESP32C3_SRCS)
	@. $(ESP_IDF_DIR)/export.sh >/dev/null 2>&1 && cd $(ESP32C3_DIR) && idf.py -B build-release -DSECD_DEBUG_BUILD=0 build >/dev/null

# Pack the ESP32-C3 app into the .machine: firmware.bin is the raw app image;
# bootloader + partition table are bundled so the board flashes in one esptool
# command. Bytecode is glued after the app image by simple concatenation
# (cat firmware.bin program.secd > final.bin) and located at runtime by
# scanning past the image end (see load_bytecode in secd_start.cpp).
$(OUTPUT_DIR)/esp32c3-supermini.machine: $(ESP32C3_BIN) $(ESP32C3_BUILD)/bootloader/bootloader.bin $(ESP32C3_BUILD)/partition_table/partition-table.bin $(META_DIR)/esp32c3-supermini.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(ESP32C3_BIN)', 'firmware.bin'); \
		zf.write('$(ESP32C3_BUILD)/bootloader/bootloader.bin', 'bootloader.bin'); \
		zf.write('$(ESP32C3_BUILD)/partition_table/partition-table.bin', 'partition-table.bin'); \
		zf.write('$(META_DIR)/esp32c3-supermini.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.bin + bootloader + partition table)"

$(OUTPUT_DIR)/esp32c3-supermini.release.machine: $(ESP32C3_BIN_RELEASE) $(ESP32C3_BUILD_RELEASE)/bootloader/bootloader.bin $(ESP32C3_BUILD_RELEASE)/partition_table/partition-table.bin $(META_DIR)/esp32c3-supermini.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(ESP32C3_BIN_RELEASE)', 'firmware.bin'); \
		zf.write('$(ESP32C3_BUILD_RELEASE)/bootloader/bootloader.bin', 'bootloader.bin'); \
		zf.write('$(ESP32C3_BUILD_RELEASE)/partition_table/partition-table.bin', 'partition-table.bin'); \
		zf.write('$(META_DIR)/esp32c3-supermini.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.bin + bootloader + partition table, release)"

# ESP32-S3 Stamp-S3A: build firmware with ESP-IDF, then zip .machine.
$(ESP32S3_BIN): $(ESP32S3_SRCS)
	@. $(ESP_IDF_DIR)/export.sh >/dev/null 2>&1 && cd $(ESP32S3_DIR) && idf.py build >/dev/null

$(ESP32S3_BIN_RELEASE): $(ESP32S3_SRCS)
	@. $(ESP_IDF_DIR)/export.sh >/dev/null 2>&1 && cd $(ESP32S3_DIR) && idf.py -B build-release -DSECD_DEBUG_BUILD=0 build >/dev/null

# Pack the ESP32-S3 app into the .machine: firmware.bin is the raw app image;
# bytecode is glued right after it by simple concatenation (cat firmware.bin
# program.secd > final.bin) and located at runtime by scanning past the image
# end (see load_bytecode) — so firmware size changes never break programs.
# The bootloader + partition table are bundled so the board flashes in one
# esptool command.
$(OUTPUT_DIR)/stamp-s3a.machine: $(ESP32S3_BIN) $(ESP32S3_BUILD)/bootloader/bootloader.bin $(ESP32S3_BUILD)/partition_table/partition-table.bin $(META_DIR)/stamp-s3a.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(ESP32S3_BIN)', 'firmware.bin'); \
		zf.write('$(ESP32S3_BUILD)/bootloader/bootloader.bin', 'bootloader.bin'); \
		zf.write('$(ESP32S3_BUILD)/partition_table/partition-table.bin', 'partition-table.bin'); \
		zf.write('$(META_DIR)/stamp-s3a.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.bin + bootloader + partition table)"

# Other ESP32-S3 boards reuse the same chip firmware; only metadata differs.
define S3-BOARD-PACK
$(OUTPUT_DIR)/$(1).machine: $(ESP32S3_BIN) $(ESP32S3_BUILD)/bootloader/bootloader.bin $(ESP32S3_BUILD)/partition_table/partition-table.bin $$(META_DIR)/$(1).metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(ESP32S3_BIN)', 'firmware.bin'); \
		zf.write('$(ESP32S3_BUILD)/bootloader/bootloader.bin', 'bootloader.bin'); \
		zf.write('$(ESP32S3_BUILD)/partition_table/partition-table.bin', 'partition-table.bin'); \
		zf.write('$$(META_DIR)/$(1).metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $$@ (firmware.bin + bootloader + partition table)"

$(OUTPUT_DIR)/$(1).release.machine: $(ESP32S3_BIN_RELEASE) $(ESP32S3_BUILD_RELEASE)/bootloader/bootloader.bin $(ESP32S3_BUILD_RELEASE)/partition_table/partition-table.bin $$(META_DIR)/$(1).metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(ESP32S3_BIN_RELEASE)', 'firmware.bin'); \
		zf.write('$(ESP32S3_BUILD_RELEASE)/bootloader/bootloader.bin', 'bootloader.bin'); \
		zf.write('$(ESP32S3_BUILD_RELEASE)/partition_table/partition-table.bin', 'partition-table.bin'); \
		zf.write('$$(META_DIR)/$(1).metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $$@ (firmware.bin + bootloader + partition table, release)"
endef
$(eval $(call S3-BOARD-PACK,esp32s3-devkit))
$(eval $(call S3-BOARD-PACK,lolin-s3-mini))

# Wemos/Lolin S2 Mini (ESP32-S2FH4): same shared esp32 HAL via its own IDF
# project (set-target esp32s2). UART0 console; no USB HID in this build.
ESP32S2_DIR = platforms/esp32s2
ESP32S2_BUILD = $(ESP32S2_DIR)/build
ESP32S2_BUILD_RELEASE = $(ESP32S2_DIR)/build-release
ESP32S2_BIN = $(ESP32S2_BUILD)/secd_machine.bin
ESP32S2_BIN_RELEASE = $(ESP32S2_BUILD_RELEASE)/secd_machine.bin
ESP32S2_SRCS = src/hal/esp32.cpp src/core/heap.cpp src/core/gc.cpp src/core/machine.cpp \
	src/core/bytecode.cpp src/core/primitives.cpp src/core/symbols.cpp \
	$(ESP32S2_DIR)/components/secd/secd_start.cpp \
	$(ESP32S2_DIR)/CMakeLists.txt $(ESP32S2_DIR)/sdkconfig.defaults \
	$(ESP32S2_DIR)/components/secd/CMakeLists.txt

$(ESP32S2_BIN): $(ESP32S2_SRCS)
	@. $(ESP_IDF_DIR)/export.sh >/dev/null 2>&1 && cd $(ESP32S2_DIR) && idf.py -B build build >/dev/null

$(ESP32S2_BIN_RELEASE): $(ESP32S2_SRCS)
	@. $(ESP_IDF_DIR)/export.sh >/dev/null 2>&1 && cd $(ESP32S2_DIR) && idf.py -B build-release -DSECD_DEBUG_BUILD=0 build >/dev/null

define S2-PACK
$(OUTPUT_DIR)/lolin-s2-mini.machine: $(ESP32S2_BIN) $$(ESP32S2_BUILD)/bootloader/bootloader.bin $$(ESP32S2_BUILD)/partition_table/partition-table.bin $$(META_DIR)/lolin-s2-mini.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(ESP32S2_BIN)', 'firmware.bin'); \
		zf.write('$$(ESP32S2_BUILD)/bootloader/bootloader.bin', 'bootloader.bin'); \
		zf.write('$$(ESP32S2_BUILD)/partition_table/partition-table.bin', 'partition-table.bin'); \
		zf.write('$$(META_DIR)/lolin-s2-mini.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $$@ (firmware.bin + bootloader + partition table)"

$(OUTPUT_DIR)/lolin-s2-mini.release.machine: $(ESP32S2_BIN_RELEASE) $$(ESP32S2_BUILD_RELEASE)/bootloader/bootloader.bin $$(ESP32S2_BUILD_RELEASE)/partition_table/partition-table.bin $$(META_DIR)/lolin-s2-mini.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(ESP32S2_BIN_RELEASE)', 'firmware.bin'); \
		zf.write('$$(ESP32S2_BUILD_RELEASE)/bootloader/bootloader.bin', 'bootloader.bin'); \
		zf.write('$$(ESP32S2_BUILD_RELEASE)/partition_table/partition-table.bin', 'partition-table.bin'); \
		zf.write('$$(META_DIR)/lolin-s2-mini.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $$@ (firmware.bin + bootloader + partition table, release)"
endef
$(eval $(call S2-PACK))

$(OUTPUT_DIR)/stamp-s3a.release.machine: $(ESP32S3_BIN_RELEASE) $(ESP32S3_BUILD_RELEASE)/bootloader/bootloader.bin $(ESP32S3_BUILD_RELEASE)/partition_table/partition-table.bin $(META_DIR)/stamp-s3a.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(ESP32S3_BIN_RELEASE)', 'firmware.bin'); \
		zf.write('$(ESP32S3_BUILD_RELEASE)/bootloader/bootloader.bin', 'bootloader.bin'); \
		zf.write('$(ESP32S3_BUILD_RELEASE)/partition_table/partition-table.bin', 'partition-table.bin'); \
		zf.write('$(META_DIR)/stamp-s3a.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.bin + bootloader + partition table, release)"

# Link a compiled Lisp program into a final ESP32 flash image without rebuilding
# the firmware: the CLI pads+concats firmware.bin with the SECD-header bytecode
# (final .bin) and also writes the standalone .secd so the same program can be
# re-cat'd later. Bytecode is located at runtime by scanning past the app image
# end, so firmware size changes never break programs.
# $$1 = target board, $$2 = example source, $$3 = entry point, $$4 = output base.
define ESP32-LINK
	@$(SECD_LISP_DIR)/build/secd-lisp -t $(1) --entry "$(3)" -o $(4).bin $(2)
	@echo "Linked $(4).bin (+ $(4).secd)"
endef

esp32c3-blink: $(OUTPUT_DIR)/esp32c3-supermini.machine
	$(call ESP32-LINK,esp32c3-supermini,$(SECD_LISP_DIR)/examples/rp2040-blink.lisp,SECD:MAIN,/tmp/esp32c3-blink)

esp32s3-blink: $(OUTPUT_DIR)/stamp-s3a.machine
	$(call ESP32-LINK,stamp-s3a,$(SECD_LISP_DIR)/examples/rp2040-blink.lisp,SECD:MAIN,/tmp/stamp-s3a-final)

esp32s3-cardputer: $(OUTPUT_DIR)/stamp-s3a.machine
	$(call ESP32-LINK,stamp-s3a,$(SECD_LISP_DIR)/examples/cardputer-input.lisp,CARDPUTER-INPUT:MAIN,/tmp/stamp-s3a-cardputer)

# --- nRF52840 unified firmware (USB + BLE, single image) -----------------
# One image that brings up the S140 SoftDevice at boot and runs both the
# CherryUSB (CDC + HID) stack and the BLE HID (S140 GATT) stack. The RAM
# origin is shifted above the SD's reserved 16 KB (nrf52840-ble.ld) and the
# app tells the SD where its vector table lives so USBD/UARTE IRQs are
# forwarded. nrf52840_ble.cpp enables the SD at boot (ble_sd_enable) and the
# BLE GATT is brought up lazily by %ble-init from Lisp.

# --- nRF52840 example firmware (Lisp glued into the UF2 app image) -------
# Builds the unified firmware .machine first, then compiles each example with
# secd-lisp and links it with the firmware to a flashable UF2. The bundled
# linker extracts the firmware from the .machine, appends the bytecode past
# the app image end (where load_bytecode scans for it), and re-wraps as UF2.
# All three examples share the one unified nrf52840-promicro firmware.
nrf-examples: nrf52840-promicro
	$(SECD_LISP_DIR)/build/secd-lisp $(SECD_LISP_DIR)/examples/portable-blink.lisp -t nrf52840-promicro --entry "PORTABLE-BLINK:MAIN" -o $(OUTPUT_DIR)/portable-blink-nrf52840.uf2
	$(SECD_LISP_DIR)/build/secd-lisp $(SECD_LISP_DIR)/examples/usb-keyboard.lisp -t nrf52840-promicro --entry "USB-KEYBOARD:MAIN" -o $(OUTPUT_DIR)/usb-keyboard-nrf52840.uf2
	$(SECD_LISP_DIR)/build/secd-lisp $(SECD_LISP_DIR)/examples/ble-keyboard.lisp -t nrf52840-promicro --entry "BLE-KEYBOARD:MAIN" -o $(OUTPUT_DIR)/ble-keyboard-nrf52840.uf2

.PHONY: nrf-examples

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(LIB) run_tests
	rm -rf $(OUTPUT_DIR)
	rm -rf $(PICO_BUILD_DIR) $(PICO_BUILD_DIR_RELEASE) \
		$(RP2040_ZERO_DIR) $(RP2040_ZERO_DIR_RELEASE) $(RP2350_ZERO_DIR) \
		$(RP2350_ZERO_DIR_RELEASE) $(RP2350_BEETLE_DIR) $(RP2350_BEETLE_DIR_RELEASE) \
		$(SAMD21_DIR) $(SAMD21_DIR_RELEASE) $(ESP32C3_BUILD) $(ESP32C3_BUILD_RELEASE) \
		$(ESP32S3_BUILD) $(ESP32S3_BUILD_RELEASE)
