/*
 * SECD Machine HAL - ESP32-C3 (ESP-IDF, single-core RISC-V)
 * Copyright (C) 2026  License: GPL3
 *
 * Implements the secd HAL on top of ESP-IDF drivers for the ESP32-C3.
 * Bytecode is merged into the app image, so flash read/write are no-ops
 * here (the VM reads its program from a const array).
 */

#include "hal/hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_cpu.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef SECD_FEATURE_GPIO
#define SECD_FEATURE_GPIO 0
#endif
#ifndef SECD_FEATURE_UART
#define SECD_FEATURE_UART 0
#endif
#ifndef SECD_FEATURE_I2C
#define SECD_FEATURE_I2C 0
#endif
#if SECD_FEATURE_I2C
#include "driver/i2c_master.h"
#endif

extern "C" {

void hal_init(void) {
    /* app_main already has the console and timer subsystems running. */
}

/* Memory: use the FreeRTOS/newlib heap provided by IDF. */
void *hal_malloc(size_t size) { return malloc(size); }
void *hal_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
void hal_free(void *ptr) { free(ptr); }

/* Timing */
uint32_t hal_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
void hal_delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
void hal_sleep(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

/* GPIO */
#if SECD_FEATURE_GPIO
int hal_gpio_init(uint8_t pin, uint8_t mode) {
    gpio_reset_pin((gpio_num_t)pin);
    gpio_set_direction((gpio_num_t)pin,
                       mode == HAL_GPIO_INPUT ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT);
    return 0;
}
int hal_gpio_write(uint8_t pin, uint8_t value) {
    return (gpio_set_level((gpio_num_t)pin, value) == ESP_OK) ? 0 : -1;
}
int hal_gpio_read(uint8_t pin) { return gpio_get_level((gpio_num_t)pin); }
#else
int hal_gpio_init(uint8_t pin, uint8_t mode) { (void)pin; (void)mode; return -1; }
int hal_gpio_write(uint8_t pin, uint8_t value) { (void)pin; (void)value; return -1; }
int hal_gpio_read(uint8_t pin) { (void)pin; return -1; }
#endif

/* UART (UART_NUM_0). */
#if SECD_FEATURE_UART
#define SECD_ENABLE_SERIAL 1
#else
#define SECD_ENABLE_SERIAL 0
#endif

void hal_serial_init(uint32_t baud) {
#if SECD_ENABLE_SERIAL
    uart_config_t cfg = {};
    cfg.baud_rate = (int)baud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    uart_driver_install(UART_NUM_0, 1024, 1024, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &cfg);
#endif
}

void hal_serial_write(uint8_t byte) {
#if SECD_ENABLE_SERIAL
    uart_write_bytes(UART_NUM_0, &byte, 1);
#else
    (void)byte;
#endif
}

void hal_serial_write_bytes(const uint8_t *data, size_t len) {
#if SECD_ENABLE_SERIAL
    uart_write_bytes(UART_NUM_0, data, len);
#else
    (void)data; (void)len;
#endif
}

uint8_t hal_serial_read(void) {
#if SECD_ENABLE_SERIAL
    uint8_t byte;
    if (uart_read_bytes(UART_NUM_0, &byte, 1, 0) == 1) return byte;
#endif
    return 0;
}

int hal_serial_available(void) {
#if SECD_ENABLE_SERIAL
    size_t available = 0;
    uart_get_buffered_data_len(UART_NUM_0, &available);
    return (int)available;
#else
    return 0;
#endif
}

/* I2C master (board feature), new i2c_master driver API. Up to
 * SECD_I2C_BUS_COUNT independent buses (2 on ESP32-S3/C3); %i2c-init picks
 * the first free controller and returns its index. */
#if SECD_FEATURE_I2C
#define SECD_I2C_BUS_COUNT 2
static i2c_master_bus_handle_t hal_i2c_bus[SECD_I2C_BUS_COUNT] = {NULL, NULL};
static uint32_t hal_i2c_hz[SECD_I2C_BUS_COUNT] = {100000, 100000};

/* Build a fully-zeroed device config (avoids -Wmissing-field-initializers). */
static i2c_device_config_t make_dev_cfg(uint8_t addr, uint32_t hz) {
    i2c_device_config_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = addr;
    dev.scl_speed_hz = hz;
    return dev;
}

/* Transfer timeout proportional to the byte count: at 100kHz each byte is
 * ~90us of wire time, so an 8KB BMI270 config upload needs ~740ms -- far
 * more than a fixed 100ms. Use 2x the worst-case wire time plus a 100ms
 * headroom. */
static int i2c_xfer_timeout(size_t len, uint32_t hz) {
    uint32_t us = (uint32_t)((len * 9u * 1000000u) / (hz ? hz : 100000u));
    return (int)((us / 1000u) * 2 + 100);
}

static i2c_master_bus_handle_t bus_handle(int bus) {
    if (bus < 0 || bus >= SECD_I2C_BUS_COUNT) return NULL;
    return hal_i2c_bus[bus];
}

int hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t hz) {
    int bus = -1;
    for (int i = 0; i < SECD_I2C_BUS_COUNT; i++) {
        if (!hal_i2c_bus[i]) { bus = i; break; }
    }
    if (bus < 0) return -1;
    i2c_master_bus_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.i2c_port = (i2c_port_t)bus;   /* I2C_NUM_0 / I2C_NUM_1 */
    cfg.sda_io_num = (gpio_num_t)sda_pin;
    cfg.scl_io_num = (gpio_num_t)scl_pin;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&cfg, &hal_i2c_bus[bus]) != ESP_OK) return -1;
    if (hz) hal_i2c_hz[bus] = hz;
    return bus;
}

int hal_i2c_write(uint8_t bus, uint8_t addr, const uint8_t *data, size_t len) {
    i2c_master_bus_handle_t h = bus_handle(bus);
    if (!h) return -1;
    i2c_device_config_t dev = make_dev_cfg(addr, hal_i2c_hz[bus]);
    i2c_master_dev_handle_t devh;
    if (i2c_master_bus_add_device(h, &dev, &devh) != ESP_OK) return -1;
    esp_err_t rc = i2c_master_transmit(devh, data, len, i2c_xfer_timeout(len, hal_i2c_hz[bus]));
    i2c_master_bus_rm_device(devh);
    return (rc == ESP_OK) ? (int)len : -1;
}

int hal_i2c_read(uint8_t bus, uint8_t addr, uint8_t *data, size_t len) {
    i2c_master_bus_handle_t h = bus_handle(bus);
    if (!h) return -1;
    i2c_device_config_t dev = make_dev_cfg(addr, hal_i2c_hz[bus]);
    i2c_master_dev_handle_t devh;
    if (i2c_master_bus_add_device(h, &dev, &devh) != ESP_OK) return -1;
    esp_err_t rc = i2c_master_receive(devh, data, len, i2c_xfer_timeout(len, hal_i2c_hz[bus]));
    i2c_master_bus_rm_device(devh);
    return (rc == ESP_OK) ? (int)len : -1;
}

int hal_i2c_write_read(uint8_t bus, uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rdata, size_t rlen) {
    i2c_master_bus_handle_t h = bus_handle(bus);
    if (!h) return -1;
    i2c_device_config_t dev = make_dev_cfg(addr, hal_i2c_hz[bus]);
    i2c_master_dev_handle_t devh;
    if (i2c_master_bus_add_device(h, &dev, &devh) != ESP_OK) return -1;
    esp_err_t rc = i2c_master_transmit_receive(devh, wdata, wlen, rdata, rlen,
                                               i2c_xfer_timeout(wlen + rlen, hal_i2c_hz[bus]));
    i2c_master_bus_rm_device(devh);
    return (rc == ESP_OK) ? (int)rlen : -1;
}
#endif

/* String output routed to the console. */
#if SECD_FEATURE_HID
/* USB HID builds route print/format output to the CDC-ACM console (port 0),
 * mirroring the RP2040 HAL; the UART console (printf) stays for non-HID. */
#include "usb.h"

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
#else
void hal_print(const char *str) { printf("%s", str); }
void hal_println(const char *str) { puts(str); }
void hal_print_int(int32_t value) { printf("%d", (int)value); }
#endif

/* USB HID keyboard / CDC serial (available when SECD_FEATURE_HID is enabled). */
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

int hal_usb_mouse_add(void) {
    return secd_usb_mouse_add();
}

void hal_hid_keyboard_tap(uint8_t modifier, uint8_t usage) {
    secd_hid_keyboard_tap(modifier, usage);
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
#endif

/* Flash: not used (bytecode is merged into the app image). */
uint32_t hal_flash_size(void) { return 4194304; }
int hal_flash_read(uint32_t addr, uint8_t *buf, size_t len) { (void)addr; (void)buf; (void)len; return 0; }
int hal_flash_write(uint32_t addr, const uint8_t *buf, size_t len) { (void)addr; (void)buf; (void)len; return 0; }
int hal_flash_erase(uint32_t addr, size_t len) { (void)addr; (void)len; return 0; }

void hal_reset(void) { esp_restart(); }

/* Waveform player: drive a pin through a precomputed pulse train.
 * Uses the RISC-V machine cycle counter for sub-microsecond timing.
 */
#include "esp_cpu.h"
void hal_wave_play(int pin, int start_level, const uint16_t *duration_ns, int count) {
    static uint32_t cpu_hz = 0;
    if (cpu_hz == 0) cpu_hz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000UL;
    int level = start_level;
    for (int i = 0; i < count; i++) {
        gpio_set_level((gpio_num_t)pin, level);
        uint32_t cyc = (uint32_t)(((uint64_t)duration_ns[i] * cpu_hz) / 1000000000ULL);
        uint32_t start = esp_cpu_get_cycle_count();
        while (((uint32_t)esp_cpu_get_cycle_count() - start) < cyc) {
        }
        level = 1 - level;
    }
}

} /* extern "C" */