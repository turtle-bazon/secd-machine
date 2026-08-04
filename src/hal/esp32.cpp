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

/* String output routed to the console. */
void hal_print(const char *str) { printf("%s", str); }
void hal_println(const char *str) { puts(str); }
void hal_print_int(int32_t value) { printf("%d", (int)value); }

/* Flash: not used (bytecode is merged into the app image). */
uint32_t hal_flash_size(void) { return 4194304; }
int hal_flash_read(uint32_t addr, uint8_t *buf, size_t len) { (void)addr; (void)buf; (void)len; return 0; }
int hal_flash_write(uint32_t addr, const uint8_t *buf, size_t len) { (void)addr; (void)buf; (void)len; return 0; }
int hal_flash_erase(uint32_t addr, size_t len) { (void)addr; (void)len; return 0; }

void hal_reset(void) { esp_restart(); }

} /* extern "C" */