/*
 * SECD Machine for Microcontrollers - STM32 Main
 * Copyright (C) 2026  License: GPL3
 *
 * Main entry point for the bare-metal STM32 targets (Blue Pill
 * STM32F103CBT6, Black Pill STM32F401RCT6). Initializes the VM and runs
 * bytecode glued right after the firmware in flash (concatenated by the
 * secd-lisp linker, located at runtime by scanning past __flash_binary_end).
 */

#include "secd/machine.h"
#include "secd/heap.h"
#include "secd/bytecode.h"
#include "secd/boot.h"
#include "hal/hal.h"

#ifndef SECD_MACHINE_VERSION
#define SECD_MACHINE_VERSION "0.0.1.0"
#endif
#ifndef SECD_FEATURES_STR
#define SECD_FEATURES_STR ""
#endif
#ifndef SECD_PLATFORM_NAME
#define SECD_PLATFORM_NAME "STM32"
#endif
#ifndef SECD_DEBUG_BUILD
#define SECD_DEBUG_BUILD 1
#endif

/* VM object heap; sized per board via -DSECD_HEAP_OBJECTS. */
#ifndef SECD_HEAP_OBJECTS
#define SECD_HEAP_OBJECTS 2048
#endif

extern "C" char __flash_binary_end[];

static secd_heap_t heap;
static secd_machine_t machine;

#if SECD_DEBUG_BUILD
#define SECD_INFO(...) hal_print(__VA_ARGS__)
#else
#define SECD_INFO(...) ((void)0)
#endif

/* Load bytecode glued right after firmware in flash (the linker pads the
 * image to a page boundary before appending, so scan a small window). */
static int load_bytecode(secd_machine_t *m, secd_heap_t *h) {
    (void)h;
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

    secd_execute(m, ptr + 14, (size_t)code_size + (size_t)const_size);
    return 0;
}

int main(void) {
    secd_hal_init();

    /* Shared boot: banner, heap, machine, then load+execute the bytecode
     * image via the per-board loader above. */
    secd_machine_boot(&machine, &heap, SECD_HEAP_OBJECTS, load_bytecode);

    for (;;) {
        hal_delay(1000);
    }
}
