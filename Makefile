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
TARGETS = rp2040-pico rp2040-zero rp2350-zero rp2350-beetle seeed-xiao-samd21 esp32c3-supermini

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
	-Wall -Wextra -Iinclude \
	-DSECD_FEATURE_GPIO=1 -DSECD_FEATURE_UART=0 \
	-DSECD_MACHINE_VERSION='"0.0.1.0"' -DSECD_FEATURES_STR='"gpio"' -DSECD_DEBUG_BUILD=1
SAMD21_CFLAGS_RELEASE = -mcpu=cortex-m0plus -mthumb -Os -ffunction-sections -fdata-sections \
	-Wall -Wextra -Iinclude \
	-DSECD_FEATURE_GPIO=1 -DSECD_FEATURE_UART=0 \
	-DSECD_MACHINE_VERSION='"0.0.1.0"' -DSECD_FEATURES_STR='"gpio"' -DSECD_DEBUG_BUILD=0
SAMD21_SRCS = platforms/samd21/main.cpp platforms/samd21/startup_samd21.cpp \
	platforms/samd21/syscalls.cpp \
	src/hal/samd21.cpp \
	src/core/heap.cpp src/core/gc.cpp src/core/machine.cpp src/core/bytecode.cpp \
	src/core/primitives.cpp src/core/symbols.cpp

# ESP32-C3 SuperMini (ESP-IDF, single-core RISC-V). Bytecode merged into the app image.
ESP_IDF_DIR ?= /home/turtle/esp/esp-idf
ESP32C3_DIR = platforms/esp32
ESP32C3_BUILD = $(ESP32C3_DIR)/build
ESP32C3_BUILD_RELEASE = $(ESP32C3_DIR)/build-release
ESP32C3_BIN = $(ESP32C3_BUILD)/secd_machine.bin
ESP32C3_BIN_RELEASE = $(ESP32C3_BUILD_RELEASE)/secd_machine.bin
ESP32C3_SRCS = src/hal/esp32.cpp src/core/heap.cpp src/core/gc.cpp src/core/machine.cpp \
	src/core/bytecode.cpp src/core/primitives.cpp src/core/symbols.cpp \
	$(ESP32C3_DIR)/components/secd/secd_start.cpp \
	$(ESP32C3_DIR)/components/secd/secd_bytecode.cpp \
	$(ESP32C3_DIR)/CMakeLists.txt $(ESP32C3_DIR)/sdkconfig.defaults \
	$(ESP32C3_DIR)/components/secd/CMakeLists.txt

# Board features (peripherals linked into the firmware), comma-separated.
# The board's enabled capabilities; e.g. a bare chip target would build
# with SECD_FEATURES= (no GPIO/UART drivers linked).
SECD_FEATURES ?= gpio,uart

# secd-lisp (CL build that provides the version for .machine metadata)
SECD_LISP_DIR ?= ../secd-lisp
SECD_LISP_ASD = $(SECD_LISP_DIR)/secd-lisp.asd

# Test files
TEST_DIR = tests
TEST_SRCS = $(wild $(TEST_DIR)/*.c)
TEST_OBJS = $(TEST_SRCS:.c=.o)

.PHONY: all clean tests machines machine $(TARGETS) rp2040-debug rp2040-release

all: $(LIB)

$(LIB): $(OBJS)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

tests: $(LIB) $(TEST_OBJS)
	$(CC) $(TEST_OBJS) $(LIB) $(LDFLAGS) -o run_tests
	./run_tests

# Build .machine files (zip with firmware + metadata)
machines: $(TARGETS)
machine: machines

# Generate target metadata with the CL build version injected
# (board file in targets/boards/; its chip base is merged in by secd-lisp)
$(META_DIR)/%.metadata.json: targets/boards/%.json
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

$(OUTPUT_DIR)/esp32c3-supermini.machine: $(ESP32C3_BIN) $(META_DIR)/esp32c3-supermini.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(ESP32C3_BIN)', 'firmware.bin'); \
		zf.write('$(META_DIR)/esp32c3-supermini.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.bin + metadata.json)"

$(OUTPUT_DIR)/esp32c3-supermini.release.machine: $(ESP32C3_BIN_RELEASE) $(META_DIR)/esp32c3-supermini.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(ESP32C3_BIN_RELEASE)', 'firmware.bin'); \
		zf.write('$(META_DIR)/esp32c3-supermini.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.bin + metadata.json, release)"

# Merge a compiled Lisp program into the ESP32-C3 app image.
# $$1 = example source, $$2 = target board name.
define ESP32C3-MERGE
	@echo "Compiling $(1) for esp32c3-supermini and merging into the app image..."
	@sbcl --non-interactive --load $(SECD_LISP_ASD) \
		--eval '(asdf:load-system :secd-lisp)' \
		--eval '(secd-lisp:write-bytecode (secd-lisp:secd-compile-file "$(1)" :target :esp32c3-supermini) "$(2)")' \
		|| (echo "compile failed"; exit 1)
	@python3 -c "import sys; d=open('$(2)','rb').read(); c=', '.join('0x%02x'%b for b in d); open('$(ESP32C3_DIR)/components/secd/secd_bytecode.cpp','w').write('/* generated */\\n#include <stdint.h>\\nextern \"C\" const uint8_t secd_bytecode[] = { '+c+' };\\nextern \"C\" const uint32_t secd_bytecode_len = '+str(len(d))+';\\n')"
	@. $(ESP_IDF_DIR)/export.sh >/dev/null 2>&1 && cd $(ESP32C3_DIR) && idf.py build >/dev/null
	@echo "Merged $(1) into $(ESP32C3_BIN)"
endef

esp32c3-blink: $(OUTPUT_DIR)/esp32c3-supermini.machine
	$(call ESP32C3-MERGE,$(SECD_LISP_DIR)/examples/rp2040-blink.lisp,/tmp/esp32c3-blink.bin)

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(LIB) run_tests
	rm -rf $(OUTPUT_DIR)
	rm -rf $(PICO_BUILD_DIR) $(PICO_BUILD_DIR_RELEASE) \
		$(RP2040_ZERO_DIR) $(RP2040_ZERO_DIR_RELEASE) $(RP2350_ZERO_DIR) \
		$(RP2350_ZERO_DIR_RELEASE) $(RP2350_BEETLE_DIR) $(RP2350_BEETLE_DIR_RELEASE) \
		$(SAMD21_DIR) $(SAMD21_DIR_RELEASE) $(ESP32C3_BUILD) $(ESP32C3_BUILD_RELEASE)
