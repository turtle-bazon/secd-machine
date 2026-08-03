# SECD Machine Makefile
#
# Targets:
#   make              - Build library (libsecd.a)
#   make machines     - Build .machine files for all targets
#   make rp2040-pico  - Build .machine for RP2040 Pico
#   make esp32-devkit - Build .machine for ESP32 devkit
#   make atmega328p-uno - Build .machine for ATmega328P UNO
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
TARGETS = rp2040-pico esp32-devkit atmega328p-uno

# Pico SDK
PICO_SDK_PATH ?= /tmp/pico-sdk
PICO_BUILD_DIR ?= build-rp2040-pico
PICO_BUILD_DIR_RELEASE = build-rp2040-pico-release
PICO_BOARD ?= pico

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
$(OUTPUT_DIR)/%.metadata.json: targets/%.json
	@mkdir -p $(OUTPUT_DIR)
	@echo "Generating metadata for $< (version from secd-lisp build)..."
	@sbcl --non-interactive --load $(SECD_LISP_ASD) \
		--eval '(asdf:load-system :secd-lisp)' \
		--eval '(secd-lisp:write-target-metadata "$<" "$@")' \
		|| (echo "Error generating metadata for $<"; exit 1)

$(TARGETS): %: $(OUTPUT_DIR)/%.machine

# RP2040: build real firmware with pico-sdk (debug variant)
$(OUTPUT_DIR)/rp2040-pico.machine: $(PICO_BUILD_DIR)/secd-machine.uf2 $(OUTPUT_DIR)/rp2040-pico.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(PICO_BUILD_DIR)/secd-machine.uf2', 'firmware.uf2'); \
		zf.write('$(OUTPUT_DIR)/rp2040-pico.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.uf2 + metadata.json, debug)"

# RP2040: release variant (no serial, start immediately)
$(OUTPUT_DIR)/rp2040-pico.release.machine: $(PICO_BUILD_DIR_RELEASE)/secd-machine.uf2 $(OUTPUT_DIR)/rp2040-pico.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(PICO_BUILD_DIR_RELEASE)/secd-machine.uf2', 'firmware.uf2'); \
		zf.write('$(OUTPUT_DIR)/rp2040-pico.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@ (firmware.uf2 + metadata.json, release)"

rp2040-debug: $(OUTPUT_DIR)/rp2040-pico.machine
rp2040-release: $(OUTPUT_DIR)/rp2040-pico.release.machine

# Shared RP2040 cmake configure + build for a given build dir and variant.
#  $$1 = build dir, $$2 = SECD_DEBUG_BUILD (ON=debug / OFF=release)
define RP2040-CMAKE
	@echo "Building RP2040 firmware (SECD_DEBUG_BUILD=$(2)) with pico-sdk..."
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
	@cp CMakeLists.txt.rp2040 CMakeLists.txt
	@cd $(1) && cmake .. \
		-DCMAKE_TOOLCHAIN_FILE=$(PICO_SDK_PATH)/cmake/preload/toolchains/pico_arm_cortex_m0plus_gcc.cmake \
		-DPICO_SDK_PATH=$(PICO_SDK_PATH) \
		-DPICO_BOARD=$(PICO_BOARD) \
		-DSECD_DEBUG_BUILD=$(2)
	@cmake --build $(1) -- -j$$(nproc)
endef

# Debug firmware (default; current behavior: serial info + startup waits)
$(PICO_BUILD_DIR)/secd-machine.uf2: CMakeLists.txt.rp2040
	$(call RP2040-CMAKE,$(PICO_BUILD_DIR),ON)

# Release firmware (start bytecode immediately, no serial, no waits)
$(PICO_BUILD_DIR_RELEASE)/secd-machine.uf2: CMakeLists.txt.rp2040
	$(call RP2040-CMAKE,$(PICO_BUILD_DIR_RELEASE),OFF)

# ESP32: placeholder (real build with ESP-IDF later)
$(OUTPUT_DIR)/esp32-devkit.machine: $(OUTPUT_DIR)/esp32-devkit.uf2 $(OUTPUT_DIR)/esp32-devkit.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(OUTPUT_DIR)/esp32-devkit.uf2', 'firmware.uf2'); \
		zf.write('$(OUTPUT_DIR)/esp32-devkit.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@"

$(OUTPUT_DIR)/esp32-devkit.uf2: targets/esp32-devkit.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import struct, json; \
		m = json.load(open('$<')); \
		fl = m.get('flash_layout', {}); \
		bs = 512; nb = min(fl.get('firmware_size', 0x100000) // bs, 4); \
		data = b''.join([struct.pack('<IIIIII', 0x0A324655, 0x9E5D5157, i, nb, bs, i*bs) + bytes(bs) + struct.pack('<II', 0x04c31db1, 0x0AB16F30) for i in range(nb)]); \
		open('$@', 'wb').write(data)"
	@echo "Created placeholder $@"

# ATmega328P: placeholder (real build with avr-gcc later)
$(OUTPUT_DIR)/atmega328p-uno.machine: $(OUTPUT_DIR)/atmega328p-uno.uf2 $(OUTPUT_DIR)/atmega328p-uno.metadata.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import zipfile; \
		zf = zipfile.ZipFile('$@', 'w', zipfile.ZIP_DEFLATED); \
		zf.write('$(OUTPUT_DIR)/atmega328p-uno.uf2', 'firmware.uf2'); \
		zf.write('$(OUTPUT_DIR)/atmega328p-uno.metadata.json', 'metadata.json'); \
		zf.close()"
	@echo "Created $@"

$(OUTPUT_DIR)/atmega328p-uno.uf2: targets/atmega328p-uno.json
	@mkdir -p $(OUTPUT_DIR)
	@python3 -c "import struct, json; \
		m = json.load(open('$<')); \
		fl = m.get('flash_layout', {}); \
		bs = 512; nb = min(fl.get('firmware_size', 0x100000) // bs, 4); \
		data = b''.join([struct.pack('<IIIIII', 0x0A324655, 0x9E5D5157, i, nb, bs, i*bs) + bytes(bs) + struct.pack('<II', 0x04c31db1, 0x0AB16F30) for i in range(nb)]); \
		open('$@', 'wb').write(data)"
	@echo "Created placeholder $@"

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(LIB) run_tests
	rm -rf $(OUTPUT_DIR)
	rm -rf $(PICO_BUILD_DIR) $(PICO_BUILD_DIR_RELEASE)
