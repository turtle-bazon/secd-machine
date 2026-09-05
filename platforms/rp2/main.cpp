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
#include "usb.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include <cstdio>
#include <cstring>
#include <stdarg.h>

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
/* Debug boots its own USB console, prints startup info, then tears it down so
 * the Lisp program can re-initialize USB (%usb-init/%usb-start) with the full
 * factory. This guarantees the boot banner is seen even if Lisp never starts
 * USB. */
static void secd_info(const char *fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    secd_console_write((const uint8_t *)buf, strlen(buf));
}
#define SECD_INFO(...) secd_info(__VA_ARGS__)
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

    /* From here the Lisp program takes over USB: tear down the debug console
     * so its %usb-init/%usb-start can rebuild the factory fresh. */
#if SECD_DEBUG_BUILD
    SECD_INFO("Deinitializing boot console, Lisp takes over USB.\n");
    secd_usb_deinit();
#endif

    /* Execute bytecode (skip header) */
    return secd_execute(&machine, ptr + header_size, code_size + const_size);
}

/* Hold every GPIO at a defined low level from the first instruction so that
 * peripherals wired to a floating data line (e.g. a WS2812 LED on GPIO16) do
 * not latch garbage during boot, before the bytecode configures the pin.
 * Pins the bytecode later needs are re-initialized by %gpio-init.
 */
static void hold_all_gpios_low(void) {
    for (int pin = 0; pin < 30; pin++) {
        gpio_init(pin);
        gpio_pull_down(pin);
    }
}

/* Main entry point */
int main(void) {
    /* Hold all GPIOs low before anything else (stdio, heap, VM): a WS2812 on
       a floating data line would otherwise re-latch stale/garbage frames
       until the bytecode's %gpio-init drives the pin. */
    hold_all_gpios_low();

#if SECD_DEBUG_BUILD
    /* Debug boots its own USB console first so the banner is always printed,
       independently of whether Lisp later starts USB. */
    secd_usb_init();
    secd_usb_start();
    SECD_INFO("Waiting for host console...\n");
    {
        unsigned wait_ms = 0;
        while (!secd_console_ready() && wait_ms < 3000) {
            sleep_ms(20);
            wait_ms += 20;
        }
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
