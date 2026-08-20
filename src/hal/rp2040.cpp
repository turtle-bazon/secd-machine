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
#include "usb.h"
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
#if SECD_FEATURE_I2C
#include "hardware/i2c.h"
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
        /* Input with internal pull-up, so an active-low button (pin to GND)
         * reads a stable 1 when released and 0 when pressed, regardless of
         * the boot hold-all-low state. */
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
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

/* I2C master (board feature): up to two buses (I2C0/I2C1), pins as given.
 * %i2c-init picks the first free controller and returns its index. */
#if SECD_FEATURE_I2C
#define SECD_I2C_BUS_COUNT 2
static i2c_inst_t *hal_i2c_bus[SECD_I2C_BUS_COUNT] = {NULL, NULL};

int hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t hz) {
    static i2c_inst_t *const ctrl[SECD_I2C_BUS_COUNT] = {i2c0, i2c1};
    int bus = -1;
    for (int i = 0; i < SECD_I2C_BUS_COUNT; i++) {
        if (!hal_i2c_bus[i]) { bus = i; break; }
    }
    if (bus < 0) return -1;
    hal_i2c_bus[bus] = ctrl[bus];
    i2c_init(hal_i2c_bus[bus], hz);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);
    return bus;
}

static i2c_inst_t *bus_handle(int bus) {
    if (bus < 0 || bus >= SECD_I2C_BUS_COUNT) return NULL;
    return hal_i2c_bus[bus];
}

int hal_i2c_write(uint8_t bus, uint8_t addr, const uint8_t *data, size_t len) {
    i2c_inst_t *h = bus_handle(bus);
    if (!h) return -1;
    int rc = i2c_write_blocking(h, addr, data, len, false);
    return (rc == (int)len) ? (int)len : -1;
}

int hal_i2c_read(uint8_t bus, uint8_t addr, uint8_t *data, size_t len) {
    i2c_inst_t *h = bus_handle(bus);
    if (!h) return -1;
    int rc = i2c_read_blocking(h, addr, data, len, false);
    return (rc == (int)len) ? (int)len : -1;
}

int hal_i2c_write_read(uint8_t bus, uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rdata, size_t rlen) {
    i2c_inst_t *h = bus_handle(bus);
    if (!h) return -1;
    int rc = i2c_write_blocking(h, addr, wdata, wlen, true);
    if (rc != (int)wlen) return -1;
    rc = i2c_read_blocking(h, addr, rdata, rlen, false);
    return (rc == (int)rlen) ? (int)rlen : -1;
}
#endif

/* String output - routes to the USB CDC console (instance 0). */
void hal_print(const char *str) {
    secd_console_write((const uint8_t *)str, strlen(str));
}

void hal_println(const char *str) {
    secd_console_write((const uint8_t *)str, strlen(str));
    secd_console_write((const uint8_t *)"\n", 1);
}

void hal_print_int(int32_t value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)value);
    secd_console_write((const uint8_t *)buf, strlen(buf));
}

/* USB composite device: console always; serial + HID added from Lisp before start */
#if SECD_FEATURE_HID
void hal_usb_init(void) {
    secd_usb_init();
}

void hal_usb_start(void) {
    secd_usb_start();
}

int hal_usb_serial_add(void) {
    return secd_usb_serial_add();
}

int hal_usb_hid_add(void) {
    return secd_usb_hid_add();
}

void hal_hid_keyboard_tap(uint8_t modifier, uint8_t usage) {
    secd_hid_keyboard_tap(modifier, usage);
}

int hal_usb_mouse_add(void) {
    return secd_usb_mouse_add();
}

int hal_hid_mouse_send(int8_t dx, int8_t dy, uint8_t buttons, int8_t wheel) {
    return secd_hid_mouse_send(dx, dy, buttons, wheel);
}

int hal_usb_serial_write(int port, uint8_t byte) {
    return (int)secd_serial_write(port, &byte, 1);
}

int hal_usb_serial_read(int port) {
    return secd_serial_read(port);
}

int hal_usb_serial_available(int port) {
    return secd_serial_available(port);
}

void hal_usb_set_vid(uint16_t vid) { secd_usb_set_vid(vid); }
void hal_usb_set_pid(uint16_t pid) { secd_usb_set_pid(pid); }
void hal_usb_set_manufacturer(const char *s) { secd_usb_set_manufacturer(s); }
void hal_usb_set_product(const char *s) { secd_usb_set_product(s); }
void hal_usb_set_serial(const char *s) { secd_usb_set_serial(s); }
#endif

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
