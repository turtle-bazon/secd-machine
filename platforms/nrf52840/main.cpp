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

/* SoftDevice bring-up (enables S140 at boot) and BLE event polling; both defined
 * in nrf52840_ble.cpp and always linked into the unified image. */
extern "C" void ble_sd_enable(void);
extern "C" void hal_ble_poll(void);

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

static secd_heap_t heap;
static secd_machine_t machine;

/* LED: nice!nano blue on P0.15. */
static void led_cfg(void) {
    uint32_t pins[1] = { 15u };
    for (int k = 0; k < 1; k++) {
        uint32_t pin = pins[k];
        uint32_t port_base = (pin < 32) ? 0x50000000u : 0x50000300u;
        uint32_t idx = pin & 31u;
        *(volatile uint32_t *)(port_base + 0x700u + idx * 4u) = 0x3u;   /* OUT */
        *(volatile uint32_t *)(port_base + 0x518u) = (1u << idx);       /* DIRSET */
    }
}
static void led_set(int on) {
    uint32_t pins[1] = { 15u };
    for (int k = 0; k < 1; k++) {
        uint32_t pin = pins[k];
        uint32_t port_base = (pin < 32) ? 0x50000000u : 0x50000300u;
        uint32_t idx = pin & 31u;
        if (on) *(volatile uint32_t *)(port_base + 0x508u) = (1u << idx);
        else    *(volatile uint32_t *)(port_base + 0x50Cu) = (1u << idx);
    }
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
        SECD_INFO("No bytecode found\r\n");
        return -1;
    }

    uint16_t code_size = (ptr[8] << 8) | ptr[9];
    uint16_t const_size = (ptr[10] << 8) | ptr[11];

    SECD_INFO("Bytecode: code=");
    hal_print_int(code_size);
    SECD_INFO(" const=");
    hal_print_int(const_size);
    SECD_INFO("\r\n");

    secd_execute(m, ptr + 14, (size_t)code_size + (size_t)const_size);
    return 0;
}

int main(void) {
    led_cfg();

    /* Bring up the S140 SoftDevice first; it owns the clock + RAM bottom and
     * forwards USBD/UARTE IRQs to our handlers. USB and BLE then coexist in one
     * image (this is the whole point of the unified nRF52840 target). */
    ble_sd_enable();

    secd_hal_init();

    secd_usb_init();
    secd_usb_start();

    /* Wait for the host to open the CDC port (DTR) so the verbose boot log is
     * actually delivered. Also drain BLE events so a pairing started earlier is
     * processed. */
    uint32_t console_wait_ms = 0;
    while (!secd_console_ready()) {
        secd_usb_task();
        hal_ble_poll();
        hal_delay(1);
        if (++console_wait_ms > 10000u) break;
    }
    if (secd_console_ready())
        hal_delay(250);

    secd_machine_boot(&machine, &heap, SECD_HEAP_OBJECTS, load_bytecode);

    /* USB task + BLE event pump. For the keyboard examples the loaded Lisp
     * program loops inside secd_execute (and itself polls BLE from %gpio-read),
     * so this loop is normally reached only for non-Lisp / host interaction. */
    for (;;) {
        secd_usb_task();
        hal_ble_poll();
        hal_delay(1);
        led_set(secd_console_ready());
    }
}
