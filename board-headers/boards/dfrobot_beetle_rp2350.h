/*
 * Copyright (c) 2026 turtle-bazon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Custom board header for the DFRobot Beetle RP2350 (DFR1188, RP2350A QFN60).
 * This board is NOT in the pico-sdk board list, so it is supplied via
 * PICO_BOARD_HEADER_DIRS. Pins: 0/1/4/5/8/9/16/18/19/26/27.
 * On-board LED on IO25; user button on QSPI_SS; battery ADC on IO29.
 */

#ifndef _BOARDS_DFROBOT_BEETLE_RP2350_H
#define _BOARDS_DFROBOT_BEETLE_RP2350_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// For board detection
#define DFROBOT_BEETLE_RP2350

// --- RP2350 VARIANT ---
#define PICO_RP2350A 1

// --- UART ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- I2C ---
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 1
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 4
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 5
#endif

// --- SPI ---
#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 18
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 19
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 16
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 17
#endif

// --- On-board LED (IO25) ---
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

// --- FLASH (2MB) ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 3
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif