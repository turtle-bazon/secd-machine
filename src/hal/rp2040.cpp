/*
 * SECD Machine for Microcontrollers
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "hal/rp2040.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pico SDK includes */
#include "pico/stdlib.h"
/* Board feature gates (default off unless the build system enables them) */
#ifndef SECD_FEATURE_GPIO
#define SECD_FEATURE_GPIO 0
#endif
#ifndef SECD_FEATURE_UART
#define SECD_FEATURE_UART 0
#endif
/* Peripheral headers are only pulled in when the board feature is enabled */
#if SECD_FEATURE_GPIO
#include "hardware/gpio.h"
#endif
#if SECD_FEATURE_UART
#include "hardware/uart.h"
#endif
#include "hardware/flash.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"

/* Memory management (static allocation for embedded) */
static uint8_t heap_pool[HEAP_SIZE];
static uint8_t stack_pool[STACK_SIZE];
static size_t heap_used = 0;

void hal_init(void) {
    stdio_init_all();
    heap_used = 0;
}

void* hal_malloc(size_t size) {
    /* Simple bump allocator for now */
    size_t aligned = (size + 7) & ~7;  /* 8-byte align */
    if (heap_used + aligned > HEAP_SIZE) {
        return NULL;
    }
    void *ptr = &heap_pool[heap_used];
    heap_used += aligned;
    return ptr;
}

void* hal_realloc(void *ptr, size_t size) {
    /* Simple: allocate new, copy, free old */
    void *new_ptr = hal_malloc(size);
    if (new_ptr && ptr) {
        memcpy(new_ptr, ptr, size);
    }
    return new_ptr;
}

void hal_free(void *ptr) {
    /* Bump allocator - no real free */
    (void)ptr;
}

/* Timing */
uint32_t hal_millis(void) {
    return (uint32_t)(time_us_64() / 1000);
}

void hal_delay(uint32_t ms) {
    sleep_ms(ms);
}

/* GPIO (board feature) */
#if SECD_FEATURE_GPIO
int hal_gpio_init(uint8_t pin, uint8_t mode) {
    if (pin >= 30) return -1;
    gpio_init(pin);
    if (mode == HAL_GPIO_OUTPUT) {
        gpio_set_dir(pin, GPIO_OUT);
    } else {
        gpio_set_dir(pin, GPIO_IN);
    }
    return 0;
}

int hal_gpio_write(uint8_t pin, uint8_t value) {
    if (pin >= 30) return -1;
    gpio_put(pin, value);
    return 0;
}

int hal_gpio_read(uint8_t pin) {
    if (pin >= 30) return 0;
    return gpio_get(pin) ? 1 : 0;
}
#endif

/* UART/Serial (board feature) */
#if SECD_FEATURE_UART
void hal_serial_init(uint32_t baud) {
    uart_init(uart0, baud);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
}

void hal_serial_write(uint8_t byte) {
    uart_putc(uart0, byte);
}

void hal_serial_write_bytes(const uint8_t *data, size_t len) {
    uart_write_blocking(uart0, data, len);
}

uint8_t hal_serial_read(void) {
    return uart_getc(uart0);
}

int hal_serial_available(void) {
    return uart_is_readable(uart0) ? 1 : 0;
}
#endif

/* String output */
void hal_print(const char *str) {
    printf("%s", str);
}

void hal_println(const char *str) {
    printf("%s\n", str);
}

void hal_print_int(int32_t value) {
    printf("%d", value);
}

/* Flash */
uint32_t hal_flash_size(void) {
    return PICO_FLASH_SIZE_BYTES;
}

int hal_flash_read(uint32_t addr, uint8_t *buf, size_t len) {
    const uint8_t *flash_ptr = (const uint8_t *)(XIP_BASE + addr);
    memcpy(buf, flash_ptr, len);
    return 0;
}

int hal_flash_write(uint32_t addr, const uint8_t *buf, size_t len) {
    /* Flash must be written in 256-byte pages */
    uint32_t page_size = 256;
    uint32_t offset = addr % page_size;
    
    if (offset != 0 || len % page_size != 0) {
        /* Need page-aligned writes for simplicity */
        return -1;
    }
    
    flash_range_program(addr, buf, len);
    return 0;
}

int hal_flash_erase(uint32_t addr, size_t len) {
    /* Flash must be erased in 4KB sectors */
    uint32_t sector_size = 4096;
    uint32_t sectors = (len + sector_size - 1) / sector_size;
    
    flash_range_erase(addr, sectors * sector_size);
    return 0;
}

/* System */
void hal_reset(void) {
    watchdog_reboot(0, 0, 0);
    while(1) { __asm volatile ("wfi"); }
}

void hal_sleep(uint32_t ms) {
    sleep_ms(ms);
}

/*
 * Waveform player: drive a pin through a precomputed pulse train.
 * Cortex-M0+ has no cycle counter, so sub-microsecond timing uses a
 * busy loop calibrated against the 1us system timer at startup
 * (calibration happens once in hal_init; adjust on real hardware).
 */
static uint32_t wave_ns_per_loop = 0;

static void wave_calibrate(void) {
    /* Time 5000 NOP-ish loop iterations and derive ns/iteration. */
    uint32_t t0 = (uint32_t)time_us_64();
    volatile uint32_t n = 5000u;
    while (n--) { __asm volatile ("nop"); }
    uint32_t dt = (uint32_t)time_us_64() - t0;
    if (dt == 0) dt = 1;
    wave_ns_per_loop = (dt * 1000u) / 5000u;  /* ns per iteration */
    if (wave_ns_per_loop == 0) wave_ns_per_loop = 1;
}

void hal_wave_play(int pin, int start_level, const uint16_t *duration_ns, int count) {
    if (wave_ns_per_loop == 0) wave_calibrate();
    int level = start_level;
    for (int i = 0; i < count; i++) {
        gpio_put((uint)pin, level);
        uint32_t iters = duration_ns[i] / wave_ns_per_loop;
        while (iters--) { __asm volatile ("nop"); }
        level = 1 - level;
    }
}
