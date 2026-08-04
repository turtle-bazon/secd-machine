/*
 * SECD Machine for Microcontrollers - RP2040 Main
 * Copyright (C) 2026
 * License: GPL3
 *
 * Main entry point for RP2040.
 * Initializes VM and runs bytecode from flash.
 *
 * Two firmware variants, selected at build time via SECD_DEBUG_BUILD:
 *  - debug   (default): prints machine info over serial on startup, waits
 *                        for the USB host to enumerate the CDC serial.
 *  - release          : starts bytecode immediately, no serial output, no waits.
 */

#include "secd/machine.h"
#include "secd/heap.h"
#include "secd/bytecode.h"
#include "hal/rp2040.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/flash.h"
#include <cstdio>
#include <cstring>

/* Heap: ~80KB (after TinyUSB + pico-sdk take their share of 264KB RAM) */
#define HEAP_OBJECTS 4095   /* 12-bit handle index limit */

#ifndef SECD_DEBUG_BUILD
#define SECD_DEBUG_BUILD 1
#endif

/* Bytecode stored contiguously after firmware in flash (via __flash_binary_end) */
extern "C" char __flash_binary_end;

/* Global state */
static secd_heap_t heap;
static secd_machine_t machine;

#if SECD_DEBUG_BUILD
#define SECD_INFO(...) printf(__VA_ARGS__)
#define SECD_WAIT_MS(ms) sleep_ms(ms)
#else
#define SECD_INFO(...) ((void)0)
#define SECD_WAIT_MS(ms) ((void)0)
#endif

/* Load bytecode glued right after firmware in flash (via __flash_binary_end) */
static int load_bytecode(void) {
    const uint8_t *base = (const uint8_t *)&__flash_binary_end;
    const uint8_t *ptr = NULL;

    /* Scan forward (up to 4KB) looking for SECD magic */
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

    /* Parse header */
    uint8_t version_major = ptr[4];
    uint8_t version_minor = ptr[5];
    uint16_t code_size = (ptr[8] << 8) | ptr[9];
    uint16_t const_size = (ptr[10] << 8) | ptr[11];
    uint16_t sym_size = (ptr[12] << 8) | ptr[13];
    uint16_t header_size = 14;

    SECD_INFO("Bytecode v%d.%d, code=%d, const=%d, sym=%d\n",
              version_major, version_minor, code_size, const_size, sym_size);

    /* Execute bytecode (skip header) */
    return secd_execute(&machine, ptr + header_size, code_size + const_size);
}

/* Main entry point */
int main(void) {
    /* Bring up USB CDC for both builds. Debug then prints a banner (and waits
       briefly for host enumeration); release uses USB too so that SECD-Lisp
       programs can drive serial/mass-storage/other TinyUSB devices, but emits
       no debug output. Keeping stdio_init_all() unconditional also keeps the
       TinyUSB stack linked (it runs tud_task() on a periodic alarm). */
    stdio_init_all();

#if SECD_DEBUG_BUILD
    {
        /* Wait for the host to enumerate before printing. Waiting on
           stdio_usb_connected() (instead of a blind sleep) keeps the startup
           banner from being missed if the terminal is already open. */
        const uint32_t deadline = to_ms_since_boot(get_absolute_time()) + 5000;
        while (!stdio_usb_connected() &&
               to_ms_since_boot(get_absolute_time()) < deadline) {
            sleep_ms(50);
        }
        sleep_ms(500);
    }

    SECD_INFO("SECD Machine v%s\n", SECD_MACHINE_VERSION);
    SECD_INFO("Build: %s\n", SECD_DEBUG_BUILD ? "debug (serial + info)" : "release");
    SECD_INFO("Features: %s\n", SECD_FEATURES_STR);
    SECD_INFO("Platform: RP2040 (Pico)\n");
    SECD_INFO("Heap: %d objects (~%d bytes)\n", HEAP_OBJECTS, HEAP_OBJECTS * sizeof(secd_object_t));
#endif

    /* Initialize heap */
    if (secd_heap_init(&heap, HEAP_OBJECTS) != 0) {
        SECD_INFO("Error: Failed to initialize heap\n");
        return 1;
    }
    SECD_INFO("Heap initialized\n");

    /* Initialize machine */
    if (secd_machine_init(&machine, &heap) != 0) {
        SECD_INFO("Error: Failed to initialize machine\n");
        return 1;
    }
    SECD_INFO("Machine initialized\n");

    /* Load and execute bytecode from flash */
    SECD_INFO("Loading bytecode...\n");
    if (load_bytecode() != 0) {
        SECD_INFO("No valid bytecode\n");
    }

    /* Hold the VM (bytecode either ran to completion or was absent) */
    for (;;) {
        SECD_WAIT_MS(1000);
    }
}
