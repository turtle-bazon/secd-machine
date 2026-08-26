/*
 * SECD Machine for Microcontrollers - SAMD21 Main
 * Copyright (C) 2026  License: GPL3
 *
 * Main entry point for the Seeed nRF52840 SuperMini (bare metal).
 * Initializes the VM and runs bytecode glued right after the firmware in
 * flash (linked at __flash_binary_end, placed by the UF2 linker).
 */

#include "secd/machine.h"
#include "secd/heap.h"
#include "secd/bytecode.h"
#include "secd/boot.h"
#include "hal/hal.h"
#include "usb.h"
#include <string.h>

#ifndef SECD_MACHINE_VERSION
#define SECD_MACHINE_VERSION "0.0.1.0"
#endif
#ifndef SECD_FEATURES_STR
#define SECD_FEATURES_STR ""
#endif
#ifndef SECD_DEBUG_BUILD
#define SECD_DEBUG_BUILD 1
#endif

/* Heap size comes from the per-board -DSECD_HEAP_OBJECTS. The nice!nano is an
 * nRF52840 with 256KB RAM, so the generous 4096 is comfortable. */
#ifndef SECD_HEAP_OBJECTS
#define SECD_HEAP_OBJECTS 2048
#endif

extern "C" char __flash_binary_end[];

/* USB ISR diagnostic counters (defined in usb_dc_nrf52840.c). */

static secd_heap_t heap;
static secd_machine_t machine;

/* ---------------------------------------------------------------------
 * DIAGNOSTIC LED SCHEME (P0.15, nice!nano blue) - FINAL
 *
 * Unit blink : 250 ms on / 250 ms off.   Pause after each event: ~1.5 s dark.
 * Group A (system):      1 boot, 2 hal_init, 3 usb_registered.
 * Group B (USB hw):      counter restarts -
 *   1 USBD_READY, 2 pullup_connected, 3 init_returned,
 *   4 usb_start_done, 5 banner/VM.  Then solid ON = healthy idle loop.
 * Error bursts (fast 60 ms, USB aborted): 2 = HFXO fail, 3 = no READY,
 * 4 = no VBUS.  Continuous strobing = CPU fault.
 * ------------------------------------------------------------------- */
#define DIAG_BLINK_ITERS 2500000u
#define DIAG_GAP_ITERS  15000000u

static void hb_pin(uint32_t pin, int cycles) {
    uint32_t port_base = (pin < 32) ? 0x50000000u : 0x50000300u;
    uint32_t idx = pin & 31u;
    *(volatile uint32_t *)(port_base + 0x700u + idx * 4u) = 0x00000003u; /* out */
    *(volatile uint32_t *)(port_base + 0x518u) = (1u << idx);           /* DIRSET */
    for (int i = 0; i < cycles; i++) {
        *(volatile uint32_t *)(port_base + 0x508u) = (1u << idx);       /* OUTSET */
        for (volatile uint32_t d = 0; d < DIAG_BLINK_ITERS; d++) {}
        *(volatile uint32_t *)(port_base + 0x50Cu) = (1u << idx);       /* OUTCLR */
        for (volatile uint32_t d = 0; d < DIAG_BLINK_ITERS; d++) {}
    }
    *(volatile uint32_t *)(port_base + 0x50Cu) = (1u << idx);
}

static void hb_gap(void) {
    for (volatile uint32_t d = 0; d < DIAG_GAP_ITERS; d++) {}
}

static void diag_heartbeat(void) {
    /* Single-board build: nice!nano's blue LED is P0.15. */
    hb_pin(15u, 1);
    hb_gap();
}

/* Runtime USB state LED: drive both the nice!nano blue (P0.15) and the board
 * LED (P1.10 / pin 42) so at least one is visible regardless of board variant.
 *   off        -> USB not configured
 *   fast blink -> configured, banner not yet delivered (host not open / TX stall)
 *   solid on   -> banner delivered (healthy idle) */
static void led_cfg(void) {
    uint32_t pins[2] = { 15u, 42u };
    for (int k = 0; k < 2; k++) {
        uint32_t pin = pins[k];
        uint32_t port_base = (pin < 32) ? 0x50000000u : 0x50000300u;
        uint32_t idx = pin & 31u;
        *(volatile uint32_t *)(port_base + 0x700u + idx * 4u) = 0x3u;   /* OUT */
        *(volatile uint32_t *)(port_base + 0x518u) = (1u << idx);       /* DIRSET */
    }
}
static void led_set(int on) {
    uint32_t pins[2] = { 15u, 42u };
    for (int k = 0; k < 2; k++) {
        uint32_t pin = pins[k];
        uint32_t port_base = (pin < 32) ? 0x50000000u : 0x50000300u;
        uint32_t idx = pin & 31u;
        if (on) *(volatile uint32_t *)(port_base + 0x508u) = (1u << idx);
        else    *(volatile uint32_t *)(port_base + 0x50Cu) = (1u << idx);
    }
}
static void led_blink(int n) {
    for (int i = 0; i < n; i++) {
        led_set(1);
        for (volatile uint32_t d = 0; d < DIAG_BLINK_ITERS; d++) {}
        led_set(0);
        for (volatile uint32_t d = 0; d < DIAG_BLINK_ITERS; d++) {}
    }
}

/* Milestone marker: blink P0.15 `n` times so a single flash run tells us
 * the last stage reached before a crash/reset:
 *   1 = boot | 5 = hal_init | 6 = usb_init | 7 = usb_start
 *   8 = boot banner reached (main loop) */
static void diag_milestone(int n) {
    hb_pin(15u, n);
    hb_gap();
}

#if SECD_DEBUG_BUILD
#define SECD_INFO(...) hal_print(__VA_ARGS__)
#else
#define SECD_INFO(...) ((void)0)
#endif

/* Load bytecode glued right after firmware in flash (linked at
 * __flash_binary_end). Returns 0 on success, -1 if no image is present. */
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
    led_cfg();

    secd_hal_init();
    SECD_INFO("[M] hal_init ok\n");

    /* Bring up USB CDC so the host can observe boot and we get a console.
     * The composite device (CDC console + optional HID) is built by Lisp
     * via secd_usb_init/serial_add/hid_add/%usb-start; here we just register
     * the console and enumerate, which is enough for the debug banner. */
    secd_usb_init();
    SECD_INFO("[M] usb_init ok\n");
    secd_usb_start();
    SECD_INFO("[M] usb_start ok\n");

    /* Wait for the host to open the CDC port (DTR) so the verbose boot log
     * is actually delivered. A prime fired on the exact DTR edge is dropped
     * on nRF (the same pre-read loss as a pre-open write), so settle ~250ms
     * after ready before emitting. The shared boot then prints the identical
     * banner/log every other target gets. */
    while (!secd_console_ready()) {
        secd_usb_task();
        hal_delay(1);
    }
    hal_delay(250);

    secd_machine_boot(&machine, &heap, SECD_HEAP_OBJECTS, load_bytecode);

    for (;;) {
        secd_usb_task();
        hal_delay(1);
        led_set(secd_console_ready());
    }
}
