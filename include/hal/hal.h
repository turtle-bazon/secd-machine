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
