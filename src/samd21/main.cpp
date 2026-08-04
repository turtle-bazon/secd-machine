/*
 * SECD Machine for Microcontrollers - SAMD21 Main
 * Copyright (C) 2026  License: GPL3
 *
 * Main entry point for the Seeed XIAO SAMD21 (bare metal).
 * Initializes the VM and runs bytecode glued right after the firmware in
 * flash (linked at __flash_binary_end, placed by the UF2 linker).
 */

#include "secd/machine.h"
#include "secd/heap.h"
#include "secd/bytecode.h"
#include "hal/hal.h"

#ifndef SECD_MACHINE_VERSION
#define SECD_MACHINE_VERSION "0.0.1.0"
#endif
#ifndef SECD_FEATURES_STR
#define SECD_FEATURES_STR ""
#endif
#ifndef SECD_DEBUG_BUILD
#define SECD_DEBUG_BUILD 1
#endif

/* Heap: the XIAO has 32KB SRAM; keep the VM object heap modest. */
#define HEAP_OBJECTS 2048

extern "C" char __flash_binary_end[];

static secd_heap_t heap;
static secd_machine_t machine;

#if SECD_DEBUG_BUILD
#define SECD_INFO(...) hal_print(__VA_ARGS__)
#else
#define SECD_INFO(...) ((void)0)
#endif

/* Load bytecode glued right after firmware in flash. */
static int load_bytecode(void) {
    const uint8_t *base = (const uint8_t *)(const void *)__flash_binary_end;
    const uint8_t *ptr = NULL;

    const uint8_t *limit = base + 4096;
    while (base < limit) {
        if (base[0] == 'S' && base[1] == 'E' && base[2] == 'C' && base[3] == 'D') {
            ptr = base;
            break;
        }
        base++;
    }

    if (ptr == NULL) {
        SECD_INFO("No bytecode found\n");
        return -1;
    }

    uint16_t code_size = (ptr[8] << 8) | ptr[9];
    uint16_t const_size = (ptr[10] << 8) | ptr[11];

    SECD_INFO("Bytecode: code=");
    hal_print_int(code_size);
    SECD_INFO(" const=");
    hal_print_int(const_size);
    SECD_INFO("\n");

    return secd_execute(&machine, ptr + 14, code_size + const_size);
}

int main(void) {
    hal_init();

#if SECD_DEBUG_BUILD
    SECD_INFO("SECD Machine v");
    SECD_INFO(SECD_MACHINE_VERSION);
    SECD_INFO("\n");
    SECD_INFO("Platform: XIAO SAMD21\n");
#endif

    if (secd_heap_init(&heap, HEAP_OBJECTS) != 0) {
        SECD_INFO("Heap init failed\n");
        return 1;
    }

    if (secd_machine_init(&machine, &heap) != 0) {
        SECD_INFO("Machine init failed\n");
        return 1;
    }

    if (load_bytecode() != 0) {
        SECD_INFO("No valid bytecode\n");
    }

    for (;;) {
        hal_delay(1000);
    }
}