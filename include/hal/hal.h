#ifdef __cplusplus
extern "C" {
#endif
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
#ifndef SECD_HAL_H
#define SECD_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Hardware Abstraction Layer (HAL).
 *
 * Provides uniform interface for hardware-specific features.
 * Each target platform implements this interface.
 */

/* GPIO modes */
#define HAL_GPIO_INPUT   0
#define HAL_GPIO_OUTPUT  1
#define HAL_GPIO_PWM     2

/* Initialize HAL */
void hal_init(void);

/* Memory management */
void* hal_malloc(size_t size);
void* hal_realloc(void *ptr, size_t size);
void hal_free(void *ptr);

/* Timing */
uint32_t hal_millis(void);
void hal_delay(uint32_t ms);

/* GPIO */
int hal_gpio_init(uint8_t pin, uint8_t mode);
int hal_gpio_write(uint8_t pin, uint8_t value);
int hal_gpio_read(uint8_t pin);

/* Waveform player: drive `pin` through count segments, starting at
 * start_level and flipping level after each. Durations are in nanoseconds.
 * The HAL renders the whole packet natively (no interpreter jitter). */
void hal_wave_play(int pin, int start_level, const uint16_t *duration_ns, int count);

/* I2C master (available when SECD_FEATURE_I2C is enabled). */
#ifndef SECD_FEATURE_I2C
#define SECD_FEATURE_I2C 0
#endif
#if SECD_FEATURE_I2C
/* Init the single master bus on the given pins. Returns 0 or -1. */
int hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t hz);
/* Write `len` bytes to the 7-bit `addr`. Returns bytes sent, or -1 on NACK/timeout. */
int hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len);
/* Read `len` bytes from the 7-bit `addr`. Returns bytes received, or -1. */
int hal_i2c_read(uint8_t addr, uint8_t *data, size_t len);
/* Write `wlen` bytes then read `rlen` bytes (register access). Returns bytes received or -1. */
int hal_i2c_write_read(uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rdata, size_t rlen);
#endif

/* UART/Serial */
void hal_serial_init(uint32_t baud);
void hal_serial_write(uint8_t byte);
void hal_serial_write_bytes(const uint8_t *data, size_t len);
uint8_t hal_serial_read(void);
int hal_serial_available(void);

/* String output */
void hal_print(const char *str);
void hal_println(const char *str);
void hal_print_int(int32_t value);

/* USB HID keyboard (available when SECD_FEATURE_HID is enabled). */
#ifndef SECD_FEATURE_HID
#define SECD_FEATURE_HID 0
#endif
#if SECD_FEATURE_HID
void hal_usb_init(void);
void hal_usb_start(void);
int  hal_usb_serial_add(void);
int  hal_usb_hid_add(void);
int  hal_usb_mouse_add(void);
void hal_hid_keyboard_tap(uint8_t modifier, uint8_t usage);
int  hal_hid_mouse_send(int8_t dx, int8_t dy, uint8_t buttons, int8_t wheel);
int  hal_usb_serial_write(int port, uint8_t byte);
int  hal_usb_serial_read(int port);
int  hal_usb_serial_available(int port);
/* Lisp-settable USB device identity (VID/PID/strings), applied before
 * hal_usb_start(); the host reads these at enumeration time. */
void hal_usb_set_vid(uint16_t vid);
void hal_usb_set_pid(uint16_t pid);
/* String setters receive a pre-encoded UTF-16LE byte buffer (from the Lisp
 * to-c-string helper) plus its byte length. */
void hal_usb_set_manufacturer(const uint8_t *data, uint16_t len);
void hal_usb_set_product(const uint8_t *data, uint16_t len);
void hal_usb_set_serial(const uint8_t *data, uint16_t len);
#endif

/* Flash (for firmware storage) */
uint32_t hal_flash_size(void);
int hal_flash_read(uint32_t addr, uint8_t *buf, size_t len);
int hal_flash_write(uint32_t addr, const uint8_t *buf, size_t len);
int hal_flash_erase(uint32_t addr, size_t len);

/* System */
void hal_reset(void);
void hal_sleep(uint32_t ms);

#endif /* SECD_HAL_H */

#ifdef __cplusplus
}
#endif
