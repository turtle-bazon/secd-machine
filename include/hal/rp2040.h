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
#ifndef HAL_RP2040_H
#define HAL_RP2040_H

#include "hal/hal.h"

/*
 * RP2040 Hardware Abstraction Layer.
 *
 * Implements HAL interface for Raspberry Pi Pico (RP2040).
 */

/* RP2040 specific constants */
#define RP2040_SYS_CLK_HZ   125000000  /* 125 MHz */
#define RP2040_UART_CLK_HZ  125000000
#define RP2040_SPI_CLK_HZ   10000000   /* 10 MHz */
#define RP2040_I2C_CLK_HZ   400000     /* 400 kHz */

/* Memory sizes */
#define HEAP_SIZE  (128 * 1024)  /* 128KB heap — TinyUSB needs RAM headroom */
#define STACK_SIZE (8 * 1024)    /* 8KB stack per core */

/* GPIO pin assignments */
#define RP2040_GPIO_LED     25  /* Built-in LED */
#define RP2040_GPIO_BUTTON  0   /* User button */
#define RP2040_GPIO_TX      0   /* UART0 TX */
#define RP2040_GPIO_RX      1   /* UART0 RX */
#define RP2040_GPIO_I2C_SDA 4   /* I2C0 SDA */
#define RP2040_GPIO_I2C_SCL 5   /* I2C0 SCL */

#endif /* HAL_RP2040_H */

#ifdef __cplusplus
}
#endif
