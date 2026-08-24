/*
 * SECD Machine for Microcontrollers - nRF52840 HAL
 * Copyright (C) 2026  License: GPL3
 *
 * Bare-metal HAL for Nordic nRF52840 (Cortex-M4F @64MHz). No vendor SDK.
 * Clock: internal RC HFINT 64MHz (no crystal needed for basic operation).
 * SysTick drives the 1ms tick.
 *
 * Pin encoding: port*32 + pin → P0.0..P0.31 = 0..31, P1.0..P1.15 = 32..47.
 * Console: UARTE0 TX P1.02 / RX P1.03 (typical for SuperMini).
 * I2C: TWIM0 SCL P0.27 / SDA P0.26.
 */

#include "hal/hal.h"
#include <string.h>
#include <stdlib.h>

#ifndef SECD_FEATURE_GPIO
#define SECD_FEATURE_GPIO 0
#endif
#ifndef SECD_FEATURE_UART
#define SECD_FEATURE_UART 0
#endif
#ifndef SECD_FEATURE_I2C
#define SECD_FEATURE_I2C 0
#endif

/* ------------------------- Core memory map --------------------------- */
#define NRF_CLOCK_BASE   0x40000000u
#define NRF_POWER_BASE   0x40000000u /* shared page */
#define NRF_UARTE0_BASE  0x40002000u
#define NRF_TWIM0_BASE   0x40003000u
#define NRF_P0_BASE      0x50000504u /* OUT register offset */
#define NRF_P1_BASE      0x50000804u
#define NRF_GPIO_BASE    0x50000000u /* common GPIO block for PIN_CNF */
#define SCB_AIRCR        0xE000ED0Cu
#define SYST_CSR         0xE000E010u
#define SYST_RVR         0xE000E014u
#define SYST_CVR         0xE000E018u

/* CLOCK registers */
#define CLK_TASKS_HFCLKSTART   (NRF_CLOCK_BASE + 0x000u)
#define CLK_EVENTS_HFCLKSTARTED (NRF_CLOCK_BASE + 0x100u)
#define CLK_TASKS_LFCLKSTART   (NRF_CLOCK_BASE + 0x008u)
#define CLK_EVENTS_LFCLKSTARTED (NRF_CLOCK_BASE + 0x104u)
#define CLK_LFCLKSRC           (NRF_CLOCK_BASE + 0x518u)
#define CLK_LFCLKSTAT          (NRF_CLOCK_BASE + 0x512u)

/* UARTE0 */
#define UARTE_TASKS_STARTRX  (NRF_UARTE0_BASE + 0x000u)
#define UARTE_TASKS_STOPRX   (NRF_UARTE0_BASE + 0x004u)
#define UARTE_TASKS_STARTTX  (NRF_UARTE0_BASE + 0x008u)
#define UARTE_TASKS_STOPTX   (NRF_UARTE0_BASE + 0x00Cu)
#define UARTE_EVENTS_RXDRDY  (NRF_UARTE0_BASE + 0x108u)
#define UARTE_EVENTS_TXDRDY  (NRF_UARTE0_BASE + 0x10Cu)
#define UARTE_SHORTS         (NRF_UARTE0_BASE + 0x104u)
#define UARTE_INTEN          (NRF_UARTE0_BASE + 0x300u)
#define UARTE_ENABLE         (NRF_UARTE0_BASE + 0x500u)
#define UARTE_PSELRTS        (NRF_UARTE0_BASE + 0x508u)
#define UARTE_PSELTXD        (NRF_UARTE0_BASE + 0x50Cu)
#define UARTE_PSELCTS        (NRF_UARTE0_BASE + 0x510u)
#define UARTE_PSELRXD        (NRF_UARTE0_BASE + 0x514u)
#define UARTE_BAUDRATE       (NRF_UARTE0_BASE + 0x524u)
#define UARTE_RXD_PTR        (NRF_UARTE0_BASE + 0x534u)
#define UARTE_RXD_MAXCNT     (NRF_UARTE0_BASE + 0x538u)
#define UARTE_RXD_AMOUNT     (NRF_UARTE0_BASE + 0x53Cu)
#define UARTE_TXD_PTR        (NRF_UARTE0_BASE + 0x544u)
#define UARTE_TXD_MAXCNT     (NRF_UARTE0_BASE + 0x548u)
#define UARTE_TXD_AMOUNT     (NRF_UARTE0_BASE + 0x54Cu)
#define UARTE_CONFIG         (NRF_UARTE0_BASE + 0x56Cu)

#define UARTE_ENABLE_ON      4u

/* Baud rate register values (from nRF52840 Product Spec) */
#define UARTE_BAUD_115200    0x01D7E000u
#define UARTE_BAUD_9600      0x00275000u

/* TWIM0 */
#define TWIM_TASKS_STARTTX   (NRF_TWIM0_BASE + 0x000u)
#define TWIM_TASKS_STARTRX   (NRF_TWIM0_BASE + 0x001u * 4) /* 0x004? no, separate */
#define TWIM_TASKS_STOP      (NRF_TWIM0_BASE + 0x014u)
#define TWIM_TASKS_SUSPEND   (NRF_TWIM0_BASE + 0x018u)
#define TWIM_TASKS_RESUME    (NRF_TWIM0_BASE + 0x01Cu)
#define TWIM_EVENTS_STOPPED  (NRF_TWIM0_BASE + 0x104u)
#define TWIM_EVENTS_ERROR    (NRF_TWIM0_BASE + 0x108u)
#define TWIM_EVENTS_RXSTARTED (NRF_TWIM0_BASE + 0x12Cu)
#define TWIM_EVENTS_TXSTARTED (NRF_TWIM0_BASE + 0x130u)
#define TWIM_EVENTS_LASTRX   (NRF_TWIM0_BASE + 0x148u)
#define TWIM_EVENTS_LASTTX   (NRF_TWIM0_BASE + 0x14Cu)
#define TWIM_SHORTS          (NRF_TWIM0_BASE + 0x200u)
#define TWIM_INTEN           (NRF_TWIM0_BASE + 0x300u)
#define TWIM_INTENSET        (NRF_TWIM0_BASE + 0x304u)
#define TWIM_INTENCLR        (NRF_TWIM0_BASE + 0x308u)
#define TWIM_ERRORSRC        (NRF_TWIM0_BASE + 0x474u)
#define TWIM_ENABLE          (NRF_TWIM0_BASE + 0x500u)
#define TWIM_PSELSCL         (NRF_TWIM0_BASE + 0x508u)
#define TWIM_PSELSDA         (NRF_TWIM0_BASE + 0x50Cu)
#define TWIM_FREQUENCY       (NRF_TWIM0_BASE + 0x524u)
#define TWIM_RXD_PTR         (NRF_TWIM0_BASE + 0x534u)
#define TWIM_RXD_MAXCNT      (NRF_TWIM0_BASE + 0x538u)
#define TWIM_RXD_AMOUNT      (NRF_TWIM0_BASE + 0x53Cu)
#define TWIM_RXD_LIST        (NRF_TWIM0_BASE + 0x540u)
#define TWIM_TXD_PTR         (NRF_TWIM0_BASE + 0x544u)
#define TWIM_TXD_MAXCNT      (NRF_TWIM0_BASE + 0x548u)
#define TWIM_TXD_AMOUNT      (NRF_TWIM0_BASE + 0x54Cu)
#define TWIM_TXD_LIST        (NRF_TWIM0_BASE + 0x550u)

#define TWIM_ENABLE_ON       5u
#define TWIM_FREQ_100K       0x01980000u
#define TWIM_FREQ_250K       0x04000000u
#define TWIM_FREQ_400K       0x06400000u

/* GPIO PIN_CNF fields */
#define GPIO_CNF_DIR_OUT     (1u << 0)
#define GPIO_CNF_INPUT_BUF   (1u << 1)
#define GPIO_CNF_PULLUP      (3u << 2)  /* PULL=3 is pullup in PIN_CNF[3:2]=11 */
#define GPIO_CNF_PULL_NONE   (0u << 2)

/* SysTick */
#define SYST_ENABLE    (1u << 0)
#define SYST_TICKINT   (1u << 1)
#define SYST_CLKSOURCE (1u << 2)

volatile uint32_t secd_systick_ms = 0;

static inline void reg_write(uint32_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}
static inline uint32_t reg_read(uint32_t addr) {
    return *(volatile uint32_t *)addr;
}

/* ------------------------------ Clock --------------------------------- */
static void clock_init(void) {
    /* Start high-frequency clock (internal RC, always available) */
    reg_write(CLK_TASKS_HFCLKSTART, 1);
    while (!(reg_read(CLK_EVENTS_HFCLKSTARTED) & 1)) {}

    /* 64MHz CPU clock from HFINT (no divider needed) */
    /* SysTick: 1ms tick at 64MHz HCLK */
    reg_write(SYST_RVR, 64000u - 1u);
    reg_write(SYST_CVR, 0);
    reg_write(SYST_CSR, SYST_ENABLE | SYST_TICKINT | SYST_CLKSOURCE);
}

void secd_hal_init(void) {
    clock_init();
#if SECD_FEATURE_UART
    hal_serial_init(115200);
#endif
}

void *hal_malloc(size_t size) { return malloc(size); }
void *hal_realloc(void *p, size_t s) { return realloc(p, s); }
void hal_free(void *p) { free(p); }

uint32_t hal_millis(void) { return secd_systick_ms; }
void hal_delay(uint32_t ms) {
    uint32_t start = secd_systick_ms;
    while ((uint32_t)(secd_systick_ms - start) < ms) {}
}
void hal_sleep(uint32_t ms) { hal_delay(ms); }

/* ------------------------------ GPIO ---------------------------------- */
#if SECD_FEATURE_GPIO

static inline uint32_t gpio_out_reg(int pin) {
    return (pin < 32) ? NRF_P0_BASE : NRF_P1_BASE;
}
static inline int gpio_pin_idx(int pin) { return pin & 31; }

int hal_gpio_init(uint8_t pin, uint8_t mode) {
    uint32_t cnf_addr = NRF_GPIO_BASE + 0x700u + ((uint32_t)pin * 4u);
    if (mode == HAL_GPIO_OUTPUT) {
        /* Output: dir=Output(1), input buffer=Connect(0), no pull,
           drive=S0S1(0), sense=Disabled(0) */
        reg_write(cnf_addr, GPIO_CNF_DIR_OUT);
        /* Set DIR bit in OUTSET */
        reg_write(gpio_out_reg(pin) + 0x08u, 1u << gpio_pin_idx(pin)); /* DIRSET */
    } else {
        /* Input: dir=Input(0), input buffer=Connect(1), pull-up */
        reg_write(cnf_addr, GPIO_CNF_INPUT_BUF | GPIO_CNF_PULLUP);
    }
    return 0;
}

int hal_gpio_write(uint8_t pin, uint8_t value) {
    uint32_t out_reg = gpio_out_reg(pin);
    uint32_t bit = 1u << gpio_pin_idx(pin);
    if (value)
        reg_write(out_reg + 0x04u, bit);  /* OUTSET offset 0x04 from OUT base */
    else
        reg_write(out_reg + 0x0Cu, bit);  /* OUTCLR offset 0x0C from OUT base */
    return 0;
}

int hal_gpio_read(uint8_t pin) {
    uint32_t in_reg = gpio_out_reg(pin) - 0x04u; /* IN is OUT-4 = base+0x504-4 */
    return (reg_read(in_reg) >> gpio_pin_idx(pin)) & 1u;
}
#endif

/* ------------------------------ Serial -------------------------------- */
#if SECD_FEATURE_UART

/* Console UART pins: TX=P1.02, RX=P1.03 (SuperMini default) */
#define CONSOLE_TX_PIN 34  /* P1.02 = port*32+pin = 1*32+2 */
#define CONSOLE_RX_PIN 35  /* P1.03 */

void hal_serial_init(uint32_t baud) {
    /* Configure TX pin as output, RX pin as input */
    uint32_t tx_cnf = NRF_GPIO_BASE + 0x700u + ((uint32_t)CONSOLE_TX_PIN * 4u);
    uint32_t rx_cnf = NRF_GPIO_BASE + 0x700u + ((uint32_t)CONSOLE_RX_PIN * 4u);
    reg_write(tx_cnf, 0x10601u); /* Out, Connect, Pull-up, S0S1 */
    reg_write(rx_cnf, 0x30001u); /* In, Connect, Pull-up, S0S1 */

    reg_write(UARTE_PSELTXD, CONSOLE_TX_PIN | (1u << 31)); /* connect */
    reg_write(UARTE_PSELRXD, CONSOLE_RX_PIN | (1u << 31));
    reg_write(UARTE_PSELRTS, 0xFFFFFFFFu); /* disconnect RTS */
    reg_write(UARTE_PSELCTS, 0xFFFFFFFFu); /* disconnect CTS */

    uint32_t baud_val = UARTE_BAUD_115200;
    if (baud >= 115200) baud_val = UARTE_BAUD_115200;
    else if (baud >= 9600) baud_val = UARTE_BAUD_9600;
    reg_write(UARTE_BAUDRATE, baud_val);

    reg_write(UARTE_CONFIG, 0); /* 8N1, no parity, no flow control */
    reg_write(UARTE_ENABLE, UARTE_ENABLE_ON);

    /* Pre-allocate a single-byte RX buffer and start RX */
    static uint8_t rx_byte;
    reg_write(UARTE_RXD_PTR, (uint32_t)(uintptr_t)&rx_byte);
    reg_write(UARTE_RXD_MAXCNT, 1);
    reg_write(UARTE_TASKS_STARTRX, 1);

    /* Enable ENDRX event to auto-restart via SHORTS */
    reg_write(UARTE_SHORTS, 0); /* no shortcuts */
}

void hal_serial_write(uint8_t byte) {
    static uint8_t tx_byte;
    tx_byte = byte;
    reg_write(UARTE_TXD_PTR, (uint32_t)(uintptr_t)&tx_byte);
    reg_write(UARTE_TXD_MAXCNT, 1);
    reg_write(UARTE_EVENTS_TXDRDY, 0);
    reg_write(UARTE_TASKS_STARTTX, 1);
    while (!(reg_read(UARTE_EVENTS_TXDRDY) & 1)) {}
}

void hal_serial_write_bytes(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) hal_serial_write(data[i]);
}

uint8_t hal_serial_read(void) {
    /* Read the last received byte from EasyDMA RX buffer */
    uint32_t amount = reg_read(UARTE_RXD_AMOUNT);
    if (amount > 0) {
        uint8_t *ptr = *(uint8_t **)UARTE_RXD_PTR;
        return ptr ? ptr[amount - 1] : 0;
    }
    return 0;
}

int hal_serial_available(void) {
    return (reg_read(UARTE_RXD_AMOUNT) > 0) ? 1 : 0;
}

#endif /* SECD_FEATURE_UART */


/* ------------------------------ I2C master ------------------------------- */
#if SECD_FEATURE_I2C
#define NRF_TWIM0_BASE 0x40003000u
#define TWIM_TASKS_STARTTX  0x000u
#define TWIM_TASKS_STARTRX  0x004u
#define TWIM_TASKS_STOP     0x014u
#define TWIM_EVENTS_STOPPED 0x104u
#define TWIM_EVENTS_ERROR   0x108u
#define TWIM_EVENTS_LASTRX  0x148u
#define TWIM_EVENTS_LASTTX  0x14Cu
#define TWIM_ERRORSRC       0x474u
#define TWIM_ENABLE         0x500u
#define TWIM_PSELSCL        0x508u
#define TWIM_PSELSDA        0x50Cu
#define TWIM_FREQUENCY      0x524u
#define TWIM_RXD_PTR        0x534u
#define TWIM_RXD_MAXCNT     0x538u
#define TWIM_RXD_AMOUNT     0x53Cu
#define TWIM_TXD_PTR        0x544u
#define TWIM_TXD_MAXCNT     0x548u
#define TWIM_TXD_AMOUNT     0x54Cu

#define TWIM_ENABLE_ON  5u
#define TWIM_FREQ_100K  0x01980000u

static uint8_t i2c_ready[2] = {0, 0};

static int i2c_wait_event(uint32_t addr, uint32_t evt_off, uint32_t mask) {
    for (volatile uint32_t i = 0; i < 200000; i++) {
        if (reg_read(addr + evt_off) & mask) return 0;
    }
    return -1;
}

int hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t hz) {
    /* TODO: implement TWIM-based I2C master */
    return -1;
}
int hal_i2c_write(uint8_t bus, uint8_t addr, const uint8_t *data, size_t len) { return -1; }
int hal_i2c_read(uint8_t bus, uint8_t addr, uint8_t *data, size_t len) { return -1; }
int hal_i2c_write_read(uint8_t bus, uint8_t addr, const uint8_t *w, size_t wl, uint8_t *r, size_t rl) { return -1; }

#endif /* SECD_FEATURE_I2C */


/* ----------------------------- String output ---------------------------- */
static void uart_put(const char *c) {
#if SECD_FEATURE_UART
    hal_serial_write((uint8_t)*c);
#else
    (void)c;
#endif
}

void hal_print(const char *str) {
    while (*str) uart_put(str++);
}
void hal_println(const char *str) {
    hal_print(str); uart_put("\n");
}
void hal_print_int(int32_t value) {
    char buf[12]; int i = 0;
    if (!value) { uart_put("0"); return; }
    if (value < 0) { uart_put("-"); value = -value; }
    while (value > 0) { buf[i++] = '0' + value % 10; value /= 10; }
    while (i > 0) { char c[2] = { buf[--i], 0 }; uart_put(c); }
}

/* ------------------------------ Wave player ------------------------------ */
static uint32_t wave_ns_per_loop = 0;

static void wave_calibrate(void) {
    uint32_t t0 = secd_systick_ms;
    volatile uint32_t n = 100000u;
    while (n--) { __asm volatile ("nop"); }
    uint32_t dt = secd_systick_ms - t0;
    if (dt == 0) dt = 1;
    wave_ns_per_loop = (dt * 1000000u) / 100000u;
    if (!wave_ns_per_loop) wave_ns_per_loop = 1;
}

void hal_wave_play(int pin, int start_level,
                   const uint16_t *duration_ns, int count) {
    if (!wave_ns_per_loop) wave_calibrate();
    int level = start_level;
    for (int i = 0; i < count; i++) {
        hal_gpio_write((uint8_t)pin, (uint8_t)level);
        uint32_t iters = duration_ns[i] / wave_ns_per_loop;
        while (iters--) { __asm volatile ("nop"); }
        level = 1 - level;
    }
}


/* ------------------------------ Radio (ESB) ---------------------------- */
/* ------------------------------ Radio (ESB) ---------------------------- */
/* Enhanced ShockBurst — direct register access, no SoftDevice needed. */

#define NRF_RADIO_BASE   0x40001000u
#define R_TASKS_TXEN     0x000u
#define R_TASKS_RXEN     0x004u
#define R_TASKS_START    0x008u
#define R_TASKS_DISABLE  0x00Cu
#define R_EVT_READY      0x100u
#define R_EVT_END        0x120u
#define R_EVT_DISABLED   0x128u
#define R_MODE           0x510u
#define R_POWER          0x514u
#define R_PACKETPTR      0x51Cu
#define R_BASE0          0x520u
#define R_PREFIX0        0x528u
#define R_TXADDRESS      0x530u
#define R_RXADDRESSES    0x534u
#define R_PCNF0          0x538u
#define R_PCNF1          0x53Cu
#define R_FREQUENCY      0x508u
#define R_CRCINIT        0x560u
#define R_CRCPOLY        0x564u
#define R_CRCCNF         0x568u

static uint8_t esb_buf[33];

static inline void esb_disable(void) {
    reg_write(NRF_RADIO_BASE + R_TASKS_DISABLE, 1);
    while (!(reg_read(NRF_RADIO_BASE + R_EVT_DISABLED) & 1)) {}
}

int hal_radio_init(void) {
    reg_write(NRF_RADIO_BASE + R_MODE, 1);
    reg_write(NRF_RADIO_BASE + R_POWER, 4);
    reg_write(NRF_RADIO_BASE + R_PCNF0, 8);
    reg_write(NRF_RADIO_BASE + R_PCNF1, (3 << 16) | 32);
    reg_write(NRF_RADIO_BASE + R_CRCINIT, 0xFFFF);
    reg_write(NRF_RADIO_BASE + R_CRCPOLY, 0x11021 & 0xFFFF);
    reg_write(NRF_RADIO_BASE + R_CRCCNF, 2);
    reg_write(NRF_RADIO_BASE + R_BASE0, 0xE7E7E7E7);
    reg_write(NRF_RADIO_BASE + R_PREFIX0, 0xE7);
    reg_write(NRF_RADIO_BASE + R_TXADDRESS, 0);
    reg_write(NRF_RADIO_BASE + R_RXADDRESSES, 1);
    reg_write(NRF_RADIO_BASE + R_FREQUENCY, 2);
    return 0;
}

void hal_radio_set_address(const uint8_t *addr) {
    uint32_t base_addr = ((uint32_t)(uint8_t)addr[1] << 24)
                       | ((uint32_t)(uint8_t)addr[2] << 16)
                       | ((uint32_t)(uint8_t)addr[3] << 8)
                       | (uint32_t)(uint8_t)addr[4];
    reg_write(NRF_RADIO_BASE + R_BASE0, base_addr);
    reg_write(NRF_RADIO_BASE + R_PREFIX0, addr[0]);
}

void hal_radio_set_channel(uint8_t ch) {
    reg_write(NRF_RADIO_BASE + R_FREQUENCY, ch);
}

int hal_radio_send(const uint8_t *data, size_t len) {
    if (!len || len > 32) return -1;
    esb_disable();
    esb_buf[0] = len;
    memcpy(&esb_buf[1], data, len);
    reg_write(NRF_RADIO_BASE + R_PACKETPTR, (uint32_t)(uintptr_t)esb_buf);
    reg_write(NRF_RADIO_BASE + R_TXADDRESS, 0);
    reg_write(NRF_RADIO_BASE + R_TASKS_TXEN, 1);
    while (!(reg_read(NRF_RADIO_BASE + R_EVT_READY) & 1)) {}
    reg_write(NRF_RADIO_BASE + R_TASKS_START, 1);
    while (!(reg_read(NRF_RADIO_BASE + R_EVT_END) & 1)) {}
    esb_disable();
    return (int)len;
}

/* ------------------------------- BLE HID -------------------------------- */
/* SoftDevice-based BLE HID deferred. Stubs for now so the build links;
 * %ble-* primitives return failure codes until real stack is added. */
int hal_ble_init(void) { return -1; }
void hal_ble_set_name(const char *name) {}
int hal_ble_connected(void) { return 0; }
void hal_ble_key_report(uint8_t mods, uint8_t keys[6]) {}
void hal_ble_mouse_report(int8_t dx, int8_t dy, uint8_t btns) {}

/* ------------------------------- Flash ----------------------------------- */
uint32_t hal_flash_size(void) { return 1024u * 1024u; }
int hal_flash_read(uint32_t addr, uint8_t *buf, size_t len) {
    memcpy(buf, (const void *)addr, len); return 0;
}
int hal_flash_write(uint32_t addr, const uint8_t *buf, size_t len) {
    (void)addr; (void)buf; (void)len; return 0; /* not supported */
}
int hal_flash_erase(uint32_t addr, size_t len) {
    (void)addr; (void)len; return 0;
}

void hal_reset(void) {
    reg_write(SCB_AIRCR, (0x5FAu << 16) | (1u << 2));
    for (;;) {}
}
