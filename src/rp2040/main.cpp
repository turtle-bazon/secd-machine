/*
 * SECD Machine for Microcontrollers - RP2040 Main
 * Copyright (C) 2026
 * License: GPL3
 *
 * Main entry point for RP2040.
 * Initializes VM and runs bytecode from flash.
 */

#include "secd/machine.h"
#include "secd/heap.h"
#include "secd/bytecode.h"
#include "hal/rp2040.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include <cstdio>
#include <cstring>

/* Heap: ~80KB (after TinyUSB + pico-sdk take their share of 264KB RAM) */
#define HEAP_OBJECTS 4095   /* 12-bit handle index limit */

/* Bytecode stored contiguously after firmware in flash (via __flash_binary_end) */
extern "C" char __flash_binary_end;

/* Global state */
static secd_heap_t heap;
static secd_machine_t machine;

/* Load bytecode glued right after firmware in flash (via __flash_binary_end) */
int load_bytecode(void) {
    const uint8_t *base = (const uint8_t *)&__flash_binary_end;
    const uint8_t *ptr = NULL;

    printf("Scanning from __flash_binary_end = 0x%08X\n", (unsigned int)base);

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
        printf("No bytecode found\n");
        return -1;
    }
    printf("Found bytecode at 0x%08X\n", (unsigned int)ptr);

    /* Parse header */
    uint8_t version_major = ptr[4];
    uint8_t version_minor = ptr[5];
    uint16_t code_size = (ptr[8] << 8) | ptr[9];
    uint16_t const_size = (ptr[10] << 8) | ptr[11];
    uint16_t sym_size = (ptr[12] << 8) | ptr[13];
    uint16_t header_size = 14;

    printf("Bytecode v%d.%d, code=%d, const=%d, sym=%d\n",
           version_major, version_minor, code_size, const_size, sym_size);

    /* Execute bytecode (skip header) */
    return secd_execute(&machine, ptr + header_size, code_size + const_size);
}

/* Main entry point */
int main(void) {
    /* Init GPIO for LED */
    gpio_init(25);
    gpio_set_dir(25, GPIO_OUT);
    
    /* 3 blinks + 4s delay for USB enumeration */
    for (int i = 0; i < 3; i++) {
        gpio_put(25, 1); sleep_ms(200);
        gpio_put(25, 0); sleep_ms(200);
    }
    sleep_ms(4000);
    
    /* Now init USB */
    stdio_init_all();
    
    /* Debug: 5 fast blinks = stdio_init_all returned */
    for (int i = 0; i < 5; i++) {
        gpio_put(25, 1); sleep_ms(100);
        gpio_put(25, 0); sleep_ms(100);
    }
    sleep_ms(3000);
    
    printf("SECD Machine v0.1.0\n");
    printf("RP2040 Target\n");
    
    /* Initialize heap */
    if (secd_heap_init(&heap, HEAP_OBJECTS) != 0) {
        printf("Error: Failed to initialize heap\n");
        return 1;
    }
    printf("Heap initialized\n");
    
    /* Initialize machine */
    if (secd_machine_init(&machine, &heap) != 0) {
        printf("Error: Failed to initialize machine\n");
        return 1;
    }
    printf("Machine initialized\n");
    
    /* Load and execute bytecode from flash */
    printf("Loading bytecode...\n");
    if (load_bytecode() != 0) {
        printf("No valid bytecode - entering idle loop\n");
        /* Idle loop with LED blink */
        while (1) {
            gpio_put(25, 1); sleep_ms(100);
            gpio_put(25, 0); sleep_ms(100);
        }
    }
    
    printf("Done.\n");
    
    /* Main loop */
    while (1) {
        gpio_put(25, 1); sleep_ms(500);
        gpio_put(25, 0); sleep_ms(500);
    }
    
    return 0;
}
