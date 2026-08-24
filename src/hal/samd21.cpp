/*
 * SECD Machine for Microcontrollers - SAMD21 HAL
 * Copyright (C) 2026  License: GPL3
 *
 * Bare-metal HAL for the Seeed XIAO SAMD21 (SAMD21G18A, Cortex-M0+).
 *
 * Implements the secd-machine HAL interface directly on SAMD21 registers
 * (no vendor SDK). Clock: GCLK0 from the internal 8MHz OSC8M (÷1).
 * GPIO digital pins map to XIAO D0..D13 (see pin table). UART (optional,
 * SECD_FEATURE_UART) is SERCOM4 on PB08(TX)/PB09(RX) - the XIAO's D6/D7.
 *
 * Register definitions follow the SAMD21 CMSIS/ASF component headers.
 */

#include "hal/hal.h"
#include <string.h>

/* ---------------------------- Core memory map ---------------------------- */
#define SYSCTRL_BASE  0x40000800u
#define GCLK_BASE     0x40000C00u
#define PM_BASE       0x40000400u
#define PORT_BASE     0x41004400u
#define NVMCTRL_BASE  0x41004000u
#define SERCOM4_BASE  0x42001400u
#define PAC0_WPCLR    0x40000000u
#define PAC1_WPCLR    0x41000000u
#define PAC2_WPCLR    0x42000000u

#define SCB_BASE      0xE000ED00u
#define SYST_BASE     0xE000E010u

/* ------------------------------ Clock bits ------------------------------- */
#define SYSCTRL_OSC8M       (SYSCTRL_BASE + 0x20u)
#define OSC8M_PRESC_DIV1    (0u << 8)
#define OSC8M_PRESC_DIV8    (3u << 8)

#define GCLK_CTRL           (GCLK_BASE + 0x00u)
#define GCLK_STATUS         (GCLK_BASE + 0x04u)
#define GCLK_CLKCTRL        (GCLK_BASE + 0x08u)
#define GCLK_GENCTRL        (GCLK_BASE + 0x0Cu)
#define GCLK_GENDIV         (GCLK_BASE + 0x10u)
#define GCLK_STATUS_SYNCBUSY (1u << 7)

#define GCLK_CLKCTRL_CLKEN  (1u << 14)
#define GCLK_GENCTRL_GENEN  (1u << 16)
#define GCLK_GENCTRL_IDC    (1u << 17)
#define GCLK_GENCTRL_SRC_OSC8M (0x06u << 8)
#define GCLK_GENDIV_DIV1    (1u << 8)

/* ------------------------------ NVMCTRL bits ----------------------------- */
#define NVMCTRL_CTRLB       (NVMCTRL_BASE + 0x04u)
#define NVMCTRL_STATUS      (NVMCTRL_BASE + 0x00u)

/* ------------------------------ SysTick bits ----------------------------- */
#define SYST_CSR            (SYST_BASE + 0x00u)
#define SYST_RVR            (SYST_BASE + 0x04u)
#define SYST_CVR            (SYST_BASE + 0x08u)
#define SYST_CSR_ENABLE     (1u << 0)
#define SYST_CSR_TICKINT    (1u << 1)
#define SYST_CSR_CLKSOURCE  (1u << 2)
#define SYST_CSR_COUNTFLAG  (1u << 16)

#define SCB_AIRCR           (SCB_BASE + 0x0Cu)
#define AIRCR_VECTKEY       (0x05FAu << 16)
#define AIRCR_SYSRESETREQ   (1u << 2)

/* ------------------------------- Pin mapping ----------------------------- */
/* Seeed XIAO SAMD21 digital pins: (port group, bit).
 * 0 = PA02, 1 = PA04, 2 = PA10, 3 = PA11, 4 = PA08, 5 = PA09,
 * 6 = PB08, 7 = PB09, 8 = PA07, 9 = PA05, 10 = PA06,
 * 11 = PA16, 12 = PA19, 13 = PA17 (on-board blue LED). */
static const uint8_t pin_group[] = { 0,0,0,0,0,0, 1,1, 0,0,0, 0,0,0 };
static const uint8_t pin_bit[]   = { 2,4,10,11,8,9, 8,9, 7,5,6, 16,19,17 };
#define SAMD21_NUM_PINS 14

#define PORT_GROUP0   (PORT_BASE + 0x00u)
#define PORT_GROUP1   (PORT_BASE + 0x80u)

/* ----------------------------- Static allocator -------------------------- */
/* SAMD21 has 32KB SRAM; keep the HAL pools small. */
#define HEAP_SIZE   (8 * 1024)

static uint8_t heap_pool[HEAP_SIZE];
static size_t heap_used = 0;

/* SysTick millisecond counter, incremented by SysTick_Handler in startup. */
extern "C" volatile uint32_t secd_systick_ms = 0;

static inline void reg_write(uint32_t addr, uint32_t value) {
    *(volatile uint32_t *)addr = value;
}

static inline uint32_t reg_read(uint32_t addr) {
    return *(volatile uint32_t *)addr;
}

static void clk_wait_sync(void) {
    while (reg_read(GCLK_STATUS) & GCLK_STATUS_SYNCBUSY) {
    }
}

/* SysTick_Handler lives in startup_samd21.cpp and increments secd_systick_ms. */

void hal_init(void) {
    /* Disable write-protection for all peripherals (PAC0/PAC1/PAC2) */
    reg_write(PAC0_WPCLR, 0xFFFFFFFEu);
    reg_write(PAC1_WPCLR, 0xFFFFFFFEu);
    reg_write(PAC2_WPCLR, 0xFFFFFFFEu);

    /* Put the 8MHz oscillator on GCLK0 (÷1) for a known CPU frequency.
     * Preserve the factory calibration field in OSC8M. */
    uint32_t osc8m = reg_read(SYSCTRL_OSC8M);
    osc8m = (osc8m & ~(3u << 8)) | OSC8M_PRESC_DIV1;
    reg_write(SYSCTRL_OSC8M, osc8m);

    reg_write(GCLK_GENDIV, GCLK_GENDIV_DIV1);            /* GEN0, div 1 */
    clk_wait_sync();
    reg_write(GCLK_GENCTRL, GCLK_GENCTRL_SRC_OSC8M | GCLK_GENCTRL_GENEN | GCLK_GENCTRL_IDC);
    clk_wait_sync();
    reg_write(GCLK_CLKCTRL, GCLK_CLKCTRL_CLKEN);         /* GCLK0 <- GEN0 */
    clk_wait_sync();

    /* 1ms SysTick at 8MHz */
    secd_systick_ms = 0;
    reg_write(SYST_RVR, 8000 - 1);
    reg_write(SYST_CVR, 0);
    reg_write(SYST_CSR, SYST_CSR_ENABLE | SYST_CSR_TICKINT | SYST_CSR_CLKSOURCE);

    heap_used = 0;
}

/* --------------------------- Memory management --------------------------- */
void *hal_malloc(size_t size) {
    size_t aligned = (size + 7) & ~((size_t)7);
    if (heap_used + aligned > HEAP_SIZE) {
        return NULL;
    }
    void *ptr = &heap_pool[heap_used];
    heap_used += aligned;
    return ptr;
}

void *hal_realloc(void *ptr, size_t size) {
    void *new_ptr = hal_malloc(size);
    if (new_ptr && ptr) {
        memcpy(new_ptr, ptr, size);
    }
    return new_ptr;
}

void hal_free(void *ptr) {
    (void)ptr;
}

/* -------------------------------- Timing -------------------------------- */
uint32_t hal_millis(void) {
    return secd_systick_ms;
}

void hal_delay(uint32_t ms) {
    uint32_t start = secd_systick_ms;
    while ((uint32_t)(secd_systick_ms - start) < ms) {
    }
}

/* -------------------------------- GPIO ---------------------------------- */
#if SECD_FEATURE_GPIO
static int samd21_pin_to_group(uint8_t pin, uint32_t *group_base, uint8_t *bit) {
    if (pin >= SAMD21_NUM_PINS) {
        return -1;
    }
    *group_base = (pin_group[pin] == 0) ? PORT_GROUP0 : PORT_GROUP1;
    *bit = pin_bit[pin];
    return 0;
}

int hal_gpio_init(uint8_t pin, uint8_t mode) {
    uint32_t group;
    uint8_t bit;
    if (samd21_pin_to_group(pin, &group, &bit) != 0) {
        return -1;
    }
    if (mode == HAL_GPIO_OUTPUT) {
        reg_write(group + 0x08, (1u << bit));   /* DIRSET */
    } else {
        reg_write(group + 0x04, (1u << bit));   /* DIRCLR */
        /* enable input + pull-down */
        reg_write(group + 0x40 + bit, (1u << 1) | (1u << 2)); /* PINCFG: INEN|PULLEN */
    }
    return 0;
}

int hal_gpio_write(uint8_t pin, uint8_t value) {
    uint32_t group;
    uint8_t bit;
    if (samd21_pin_to_group(pin, &group, &bit) != 0) {
        return -1;
    }
    if (value) {
        reg_write(group + 0x18, (1u << bit));   /* OUTSET */
    } else {
        reg_write(group + 0x14, (1u << bit));   /* OUTCLR */
    }
    return 0;
}

int hal_gpio_read(uint8_t pin) {
    uint32_t group;
    uint8_t bit;
    if (samd21_pin_to_group(pin, &group, &bit) != 0) {
        return 0;
    }
    return (reg_read(group + 0x20) >> bit) & 1u;   /* IN */
}
#endif

/* ------------------------------ UART (SERCOM4) --------------------------- */
#if SECD_FEATURE_UART
#define SERCOM4_CTRLA   (SERCOM4_BASE + 0x00u)
#define SERCOM4_CTRLB   (SERCOM4_BASE + 0x04u)
#define SERCOM4_BAUD    (SERCOM4_BASE + 0x0Cu)
#define SERCOM4_INTFLAG (SERCOM4_BASE + 0x18u)
#define SERCOM4_DATA    (SERCOM4_BASE + 0x28u)
#define SERCOM4_SYNCBUSY (SERCOM4_BASE + 0x20u)
#define SERCOM4_STATUS  (SERCOM4_BASE + 0x14u)

#define SERCOM_USART_MODE (0x1u << 2)
#define SERCOM_USART_FORM (0x0u << 24)   /* UART, no parity */
#define SERCOM_USART_TXPO (0x0u << 16)   /* PAD0 = TX */
#define SERCOM_USART_RXPO (0x1u << 20)   /* PAD1 = RX */
#define SERCOM_SYNCBUSY_MASK (0x3Fu)

static void sercom4_wait_sync(void) {
    while (reg_read(SERCOM4_SYNCBUSY) & SERCOM_SYNCBUSY_MASK) {
    }
}

void hal_serial_init(uint32_t baud) {
    /* Enable SERCOM4 APB clock */
    uint32_t apbcmask = reg_read(PM_BASE + 0x20);
    reg_write(PM_BASE + 0x20, apbcmask | (1u << 6));   /* APBCMASK.SERCOM4 */

    /* GCLK for SERCOM4 core: ID 22 = SERCOM4_CORE, from GEN0 */
    reg_write(GCLK_CLKCTRL, (22u) | (0u << 8) | GCLK_CLKCTRL_CLKEN);
    clk_wait_sync();

    /* Pin mux: PB08 -> SERCOM4 PAD0 (function D), PB09 -> PAD1 (function D) */
    /* Group 1 (PB) base = PORT_GROUP1; PMUX[4] covers pins 8/9 */
    uint32_t pmux = reg_read(PORT_GROUP1 + 0x30 + 4);
    pmux &= ~(0xFu << 0);         /* pin 8 even nibble */
    pmux |= (3u << 0);            /* function D */
    pmux &= ~(0xFu << 4);         /* pin 9 odd nibble */
    pmux |= (3u << 4);            /* function D */
    reg_write(PORT_GROUP1 + 0x30 + 4, pmux);
    reg_write(PORT_GROUP1 + 0x40 + 8, (1u << 0) | (1u << 1)); /* PINCFG8: PMUXEN|INEN */
    reg_write(PORT_GROUP1 + 0x40 + 9, (1u << 0) | (1u << 1)); /* PINCFG9: PMUXEN|INEN */

    /* Reset + configure USART (internal clock, 8N1) */
    reg_write(SERCOM4_CTRLA, 1u);  /* SWRST */
    sercom4_wait_sync();
    reg_write(SERCOM4_CTRLA, SERCOM_USART_MODE | SERCOM_USART_FORM | SERCOM_USART_TXPO | SERCOM_USART_RXPO);
    sercom4_wait_sync();

    /* BAUD for 8MHz ref, 115200 (16x oversampling, arithmetic):
     * BAUD = 65536 * (1 - 16*baud/fref) */
    uint32_t baud_reg = (uint32_t)(65536u - 16u * ((uint64_t)baud * 65536u / 8000000u));
    reg_write(SERCOM4_BAUD, baud_reg & 0xFFFFu);

    reg_write(SERCOM4_CTRLB, (1u << 16) | (1u << 17));  /* TXEN | RXEN */
    sercom4_wait_sync();
    reg_write(SERCOM4_CTRLA, reg_read(SERCOM4_CTRLA) | (1u << 1));  /* ENABLE */
    sercom4_wait_sync();
}

void hal_serial_write(uint8_t byte) {
    while (!(reg_read(SERCOM4_INTFLAG) & (1u << 0))) { /* wait DRE */
    }
    reg_write(SERCOM4_DATA, byte);
}

void hal_serial_write_bytes(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        hal_serial_write(data[i]);
    }
}

uint8_t hal_serial_read(void) {
    while (!(reg_read(SERCOM4_INTFLAG) & (1u << 2))) { /* wait RXC */
    }
    return (uint8_t)reg_read(SERCOM4_DATA);
}

int hal_serial_available(void) {
    return (reg_read(SERCOM4_INTFLAG) & (1u << 2)) ? 1 : 0;
}
#endif

/* ----------------------------- String output ---------------------------- */
static void uart_put(const char *c) {
#if SECD_FEATURE_UART
    hal_serial_write((uint8_t)*c);
#else
    (void)c;
#endif
}

void hal_print(const char *str) {
    while (*str) {
        uart_put(str++);
    }
}

void hal_println(const char *str) {
    hal_print(str);
    uart_put("\n");
}

void hal_print_int(int32_t value) {
    char buf[12];
    int i = 0;
    if (value == 0) {
        uart_put("0");
        return;
    }
    if (value < 0) {
        uart_put("-");
        value = -value;
    }
    while (value > 0 && i < (int)sizeof(buf) - 1) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        uart_put(&buf[--i]);
    }
}

/* -------------------------------- Flash --------------------------------- */
uint32_t hal_flash_size(void) {
    return 256u * 1024u;
}

int hal_flash_read(uint32_t addr, uint8_t *buf, size_t len) {
    memcpy(buf, (const uint8_t *)addr, len);
    return 0;
}

int hal_flash_write(uint32_t addr, const uint8_t *buf, size_t len) {
    /* Bytecode is flashed by the UF2 bootloader, not written at runtime. */
    (void)addr;
    (void)buf;
    (void)len;
    return 0;
}

int hal_flash_erase(uint32_t addr, size_t len) {
    (void)addr;
    (void)len;
    return 0;
}

/* -------------------------------- System -------------------------------- */
void hal_reset(void) {
    reg_write(SCB_AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);
    for (;;) {
    }
}

void hal_sleep(uint32_t ms) {
    hal_delay(ms);
}

/* Waveform player: same calibrated-NOP approach as RP2040 (M0+, no cycle
 * counter), but the SAMD21 runs at only 8MHz so its per-iteration cost is
 * large (~500ns) - it is the least suitable target for WS2812 bit timing.
 * Calibrate on real hardware).
 */
static uint32_t wave_ns_per_loop = 0;

static void wave_calibrate(void) {
    uint32_t t0 = secd_systick_ms;
    volatile uint32_t n = 100000u;
    while (n--) { __asm volatile ("nop"); }
    uint32_t dt = secd_systick_ms - t0;
    if (dt == 0) dt = 1;
    wave_ns_per_loop = (dt * 1000000u) / 100000u;
    if (wave_ns_per_loop == 0) wave_ns_per_loop = 1;
}

void hal_wave_play(int pin, int start_level, const uint16_t *duration_ns, int count) {
    if (wave_ns_per_loop == 0) wave_calibrate();
    int level = start_level;
    for (int i = 0; i < count; i++) {
        hal_gpio_write((uint8_t)pin, (uint8_t)level);
        uint32_t iters = duration_ns[i] / wave_ns_per_loop;
        while (iters--) { __asm volatile ("nop"); }
        level = 1 - level;
    }
}

/* Radio / BLE stubs — implemented on nRF52840 (SoftDevice) */
int hal_radio_init(void) { return -1; }
void hal_radio_set_address(const uint8_t *addr) {}
void hal_radio_set_channel(uint8_t ch) {}
int hal_radio_send(const uint8_t *data, size_t len) { return -1; }
void hal_radio_on_receive(void (*cb)(const uint8_t *, size_t)) {}
int hal_ble_init(void) { return -1; }
void hal_ble_set_name(const char *name) {}
int hal_ble_connected(void) { return 0; }
void hal_ble_key_report(uint8_t mods, uint8_t keys[6]) {}
void hal_ble_mouse_report(int8_t dx, int8_t dy, uint8_t btns) {}

