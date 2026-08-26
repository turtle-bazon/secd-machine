/*
 * SECD Machine for Microcontrollers - STM32F1 HAL
 * Copyright (C) 2026  License: GPL3
 *
 * Bare-metal HAL for the Blue Pill STM32F103CBT6 (medium-density F1,
 * Cortex-M3 @72MHz). Implements the secd-machine HAL interface directly on
 * registers (no vendor SDK): HSE 8MHz -> PLL x9 -> SYSCLK 72MHz, APB1 36MHz
 * (I2C), APB2 72MHz (USART1/GPIO/ADC). SysTick drives the 1ms tick.
 *
 * Pin numbers use port*16+pin: PA0..15 = 0..15, PB = 16..31, PC = 32..47
 * (PC13 on-board LED = 45). Console is USART1 TX PA9 / RX PA10 (the same
 * pins as the ROM serial bootloader). I2C master on two buses:
 *   bus 0 = I2C1 SCL PB6 / SDA PB7,  bus 1 = I2C2 SCL PB10 / SDA PB11.
 */

#include "hal/hal.h"
#include <string.h>

#ifndef SECD_FEATURE_GPIO
#define SECD_FEATURE_GPIO 0
#endif
#ifndef SECD_FEATURE_UART
#define SECD_FEATURE_UART 0
#endif
#ifndef SECD_FEATURE_I2C
#define SECD_FEATURE_I2C 0
#endif

/* ---------------------------- Core memory map ---------------------------- */
#define RCC_BASE      0x40021000u
#define GPIOA_BASE    0x40010800u
#define GPIOB_BASE    0x40010C00u
#define GPIOC_BASE    0x40011000u
#define FLASH_BASE_IF 0x40022000u
#define USART1_BASE   0x40013800u
#define I2C1_BASE     0x40005400u
#define I2C2_BASE     0x40005800u
#define SCB_BASE      0xE000ED00u
#define SYST_BASE     0xE000E010u

/* ------------------------------ Register map ----------------------------- */
#define RCC_CR        (RCC_BASE + 0x00u)
#define RCC_CFGR      (RCC_BASE + 0x04u)
#define RCC_APB2ENR   (RCC_BASE + 0x18u)
#define RCC_APB1ENR   (RCC_BASE + 0x1Cu)

#define RCC_HSEON     (1u << 16)
#define RCC_HSERDY    (1u << 17)
#define RCC_PLLON     (1u << 24)
#define RCC_PLLRDY    (1u << 25)

/* CFGR: SW=PLL(10), PPRE1=/2 (100), ADCPRE=/6 (10x), PLLSRC=HSE, PLL x9 */
#define CFGR_SW_PLL   (2u << 0)
#define CFGR_SWS_PLL  (2u << 2)
#define CFGR_PPRE1_DIV2 (4u << 8)
#define CFGR_ADCPRE_DIV6 (2u << 14)
#define CFGR_PLLSRC_HSE (1u << 16)
#define CFGR_PLLMUL_X9  (7u << 18)

#define APB2_AFIOEN   (1u << 0)
#define APB2_IOPAEN   (1u << 2)
#define APB2_IOPBEN   (1u << 3)
#define APB2_IOPCEN   (1u << 4)
#define APB2_ADC1EN   (1u << 9)
#define APB2_USART1EN (1u << 14)
#define APB1_I2C1EN   (1u << 21)
#define APB1_I2C2EN   (1u << 22)

#define FLASH_ACR     (FLASH_BASE_IF + 0x00u)
#define FLASH_LATENCY2 (2u << 0)
#define FLASH_PRFTBE  (1u << 4)

/* System clock after bring-up */
#define SYSCLK_HZ     72000000u
#define PCLK1_HZ      36000000u
#define PCLK2_HZ      72000000u

/* GPIO block layout (identical offsets per port) */
#define GPIO_CRL      0x00u
#define GPIO_CRH      0x04u
#define GPIO_IDR      0x08u
#define GPIO_BSRR     0x10u
#define GPIO_BRR      0x14u

/* Config nibble values (MODE[1:0] | CNF[1:0] << 2) */
#define CFG_OUT_PP_2MHZ 0x2u   /* MODE=10 (2MHz), CNF=00 push-pull */
#define CFG_IN_FLOATING 0x4u   /* MODE=00 input,   CNF=01 floating */
#define CFG_AF_PP       0xAu   /* MODE=10,         CNF=10 alt push-pull */
#define CFG_AF_OD       0xBu   /* MODE=10,         CNF=11 alt open-drain */

/* USART1 */
#define USR_SR        0x00u
#define USR_DR        0x04u
#define USR_BRR       0x08u
#define USR_CR1       0x0Cu
#define USART_TXE     (1u << 7)
#define USART_RXNE    (1u << 5)
#define USART_UE      (1u << 13)
#define USART_TE      (1u << 3)
#define USART_RE      (1u << 2)

/* I2C */
#define II_CR1        0x00u
#define II_CR2        0x04u
#define II_OAR1       0x08u
#define II_DR         0x0Cu
#define II_SR1        0x14u
#define II_SR2        0x18u
#define II_CCR        0x1Cu
#define II_TRISE      0x20u

#define I2C_PE        (1u << 0)
#define I2C_START     (1u << 8)
#define I2C_STOP      (1u << 9)
#define I2C_ACK       (1u << 10)
#define I2C_FM        (1u << 15)
#define I2C_SB        (1u << 0)
#define I2C_ADDR      (1u << 1)
#define I2C_BTF       (1u << 2)
#define I2C_RXNE      (1u << 6)
#define I2C_TXE       (1u << 7)
#define I2C_AF        (1u << 10)
#define I2C_MSL       (1u << 0)
#define I2C_BUSY      (1u << 1)

/* SysTick / SCB */
#define SYST_CSR      (SYST_BASE + 0x00u)
#define SYST_RVR      (SYST_BASE + 0x04u)
#define SYST_CVR      (SYST_BASE + 0x08u)
#define SYST_ENABLE   (1u << 0)
#define SYST_TICKINT  (1u << 1)
#define SYST_CLKSOURCE (1u << 2)
#define SCB_AIRCR     (SCB_BASE + 0x0Cu)
#define AIRCR_VECTKEY (0x5FAu << 16)
#define AIRCR_SYSRESETREQ (1u << 2)

static inline void reg_write(uint32_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}
static inline uint32_t reg_read(uint32_t addr) {
    return *(volatile uint32_t *)addr;
}

volatile uint32_t secd_systick_ms = 0;

/* ------------------------------ Clock setup ------------------------------ */
static void clock_init(void) {
    /* Flash: two wait states + prefetch for 72MHz @3.3V */
    reg_write(FLASH_ACR, FLASH_PRFTBE | FLASH_LATENCY2);

    reg_write(RCC_CR, reg_read(RCC_CR) | RCC_HSEON);
    while (!(reg_read(RCC_CR) & RCC_HSERDY)) {
    }
    uint32_t cfgr = reg_read(RCC_CFGR);
    cfgr |= CFGR_PPRE1_DIV2 | CFGR_ADCPRE_DIV6 | CFGR_PLLSRC_HSE | CFGR_PLLMUL_X9;
    reg_write(RCC_CFGR, cfgr);
    reg_write(RCC_CR, reg_read(RCC_CR) | RCC_PLLON);
    while (!(reg_read(RCC_CR) & RCC_PLLRDY)) {
    }
    reg_write(RCC_CFGR, (reg_read(RCC_CFGR) & ~3u) | CFGR_SW_PLL);
    while ((reg_read(RCC_CFGR) & (3u << 2)) != CFGR_SWS_PLL) {
    }

    /* Clocks: AFIO + all used GPIO ports + USART1 (APB2), I2C (APB1) */
    uint32_t apb2 = APB2_AFIOEN | APB2_IOPAEN | APB2_IOPBEN | APB2_IOPCEN;
#if SECD_FEATURE_UART
    apb2 |= APB2_USART1EN;
#endif
    reg_write(RCC_APB2ENR, apb2);
    uint32_t apb1 = 0;
#if SECD_FEATURE_I2C
    apb1 |= APB1_I2C1EN | APB1_I2C2EN;
#endif
    reg_write(RCC_APB1ENR, apb1);

    /* 1ms SysTick from the processor clock */
    reg_write(SYST_RVR, (SYSCLK_HZ / 1000u) - 1u);
    reg_write(SYST_CVR, 0);
    reg_write(SYST_CSR, SYST_ENABLE | SYST_TICKINT | SYST_CLKSOURCE);
}

void secd_hal_init(void) {
    clock_init();
#if SECD_FEATURE_UART
    hal_serial_init(115200);
#endif
}

/* --------------------------- Memory management --------------------------- */
#include <stdlib.h>

void *hal_malloc(size_t size) {
    return malloc(size);
}
void *hal_realloc(void *ptr, size_t size) {
    return realloc(ptr, size);
}
void hal_free(void *ptr) {
    free(ptr);
}

/* -------------------------------- Timing --------------------------------- */
uint32_t hal_millis(void) {
    return secd_systick_ms;
}

void hal_delay(uint32_t ms) {
    uint32_t start = secd_systick_ms;
    while ((uint32_t)(secd_systick_ms - start) < ms) {
    }
}

void hal_sleep(uint32_t ms) {
    uint32_t start = secd_systick_ms;
    while ((uint32_t)(secd_systick_ms - start) < ms) {
        __asm volatile ("wfi");
    }
}

/* --------------------------------- GPIO ---------------------------------- */
#if SECD_FEATURE_GPIO
static uint32_t gpio_base(int pin) {
    switch (pin >> 4) {
        case 0: return GPIOA_BASE;
        case 1: return GPIOB_BASE;
        case 2: return GPIOC_BASE;
        default: return GPIOA_BASE;
    }
}

int hal_gpio_init(uint8_t pin, uint8_t mode) {
    uint32_t base = gpio_base(pin);
    int pos = pin & 0xF;
    volatile uint32_t *cr = (pos < 8) ? (volatile uint32_t *)(base + GPIO_CRL)
                                      : (volatile uint32_t *)(base + GPIO_CRH);
    int shift = (pos & 7) * 4;
    uint32_t cfg = (mode == HAL_GPIO_OUTPUT) ? CFG_OUT_PP_2MHZ : CFG_IN_FLOATING;
    *cr = (*cr & ~(0xFu << shift)) | (cfg << shift);
    return 0;
}

int hal_gpio_write(uint8_t pin, uint8_t value) {
    uint32_t base = gpio_base(pin);
    int pos = pin & 0xF;
    if (value) {
        reg_write(base + GPIO_BSRR, 1u << pos);
    } else {
        reg_write(base + GPIO_BRR, 1u << pos);
    }
    return 0;
}

int hal_gpio_read(uint8_t pin) {
    uint32_t base = gpio_base(pin);
    return (reg_read(base + GPIO_IDR) >> (pin & 0xF)) & 1u;
}
#endif /* SECD_FEATURE_GPIO */

/* ------------------------------- Serial ---------------------------------- */
#if SECD_FEATURE_UART
void hal_serial_init(uint32_t baud) {
    /* USART1 on APB2 (PCLK2); default pins PA9 TX (AF push-pull) /
     * PA10 RX (input floating). Pins 8..15 are configured in CRH. */
    volatile uint32_t *crh = (volatile uint32_t *)(GPIOA_BASE + GPIO_CRH);
    *crh = (*crh & ~(0xFu << ((9 - 8) * 4))) | ((uint32_t)CFG_AF_PP << ((9 - 8) * 4));
    *crh = (*crh & ~(0xFu << ((10 - 8) * 4))) | ((uint32_t)CFG_IN_FLOATING << ((10 - 8) * 4));

    uint32_t div = (PCLK2_HZ + (baud / 2u)) / baud;   /* mantissa<<4|frac */
    reg_write(USART1_BASE + USR_BRR, div);
    reg_write(USART1_BASE + USR_CR1, USART_UE | USART_TE | USART_RE);
}

void hal_serial_write(uint8_t byte) {
    while (!(reg_read(USART1_BASE + USR_SR) & USART_TXE)) {
    }
    reg_write(USART1_BASE + USR_DR, byte);
}

void hal_serial_write_bytes(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        hal_serial_write(data[i]);
    }
}

uint8_t hal_serial_read(void) {
    return (uint8_t)(reg_read(USART1_BASE + USR_DR) & 0xFFu);
}

int hal_serial_available(void) {
    return (reg_read(USART1_BASE + USR_SR) & USART_RXNE) ? 1 : 0;
}
#endif /* SECD_FEATURE_UART */

/* ----------------------------- String output ----------------------------- */
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
    while (value > 0) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        char one[2] = { buf[--i], 0 };
        uart_put(one);
    }
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

/* ------------------------------ I2C master ------------------------------- */
#if SECD_FEATURE_I2C
#define SECD_I2C_BUS_COUNT 2

struct stm32_i2c_bus {
    volatile uint32_t *regs;
    uint32_t en_bit;          /* RCC_APB1ENR bit */
    uint8_t scl_pin;
    uint8_t sda_pin;
    uint8_t scl_cfg_pos;      /* CR position of SCL within its port */
    uint8_t sda_cfg_pos;
};

/* bus 0 = I2C1 PB6/PB7, bus 1 = I2C2 PB10/PB11 */
static struct stm32_i2c_bus buses[SECD_I2C_BUS_COUNT] = {
    { (volatile uint32_t *)I2C1_BASE, APB1_I2C1EN, 22, 23, 6, 7 },
    { (volatile uint32_t *)I2C2_BASE, APB1_I2C2EN, 26, 27, 10, 11 },
};
static uint8_t i2c_ready[SECD_I2C_BUS_COUNT] = {0, 0};

static int i2c_bus_ok(int bus) {
    return (bus >= 0 && bus < SECD_I2C_BUS_COUNT && i2c_ready[bus]);
}

int hal_i2c_init(uint8_t sda_pin, uint8_t scl_pin, uint32_t hz) {
    int bus = -1;
    for (int i = 0; i < SECD_I2C_BUS_COUNT; i++) {
        if (!i2c_ready[i]) { bus = i; break; }
    }
    if (bus < 0) return -1;
    struct stm32_i2c_bus *b = &buses[bus];
    (void)sda_pin; (void)scl_pin;             /* fixed board wiring per bus */

    /* Pins to alternate-function open drain */
    for (int p = 0; p < 2; p++) {
        int pin = p ? b->sda_pin : b->scl_pin;
        int cfg_pos = p ? b->sda_cfg_pos : b->scl_cfg_pos;
        volatile uint32_t *cr = (cfg_pos < 8)
            ? (volatile uint32_t *)(gpio_base(pin) + GPIO_CRL)
            : (volatile uint32_t *)(gpio_base(pin) + GPIO_CRH);
        int shift = (cfg_pos & 7) * 4;
        *cr = (*cr & ~(0xFu << shift)) | ((uint32_t)CFG_AF_OD << shift);
    }

    uint32_t freq_mhz = PCLK1_HZ / 1000000u;
    if (hz == 0) hz = 100000u;
    uint32_t ccr, trise = freq_mhz + 1;
    if (hz <= 100000u) {
        ccr = PCLK1_HZ / (2u * hz);           /* standard mode */
    } else {
        ccr = I2C_FM | (PCLK1_HZ / (3u * hz));   /* fast mode, duty 0 */
    }
    if ((ccr & ~I2C_FM) < 4) ccr = 4;

    volatile uint32_t *r = b->regs;
    reg_write(RCC_APB1ENR, reg_read(RCC_APB1ENR) | b->en_bit);
    r[II_CR1 / 4] = 0;                        /* PE off during config */
    r[II_CR2 / 4] = freq_mhz;                 /* FREQ = PCLK1 MHz */
    r[II_CCR / 4] = ccr;
    r[II_TRISE / 4] = trise;
    r[II_CR1 / 4] = I2C_PE | I2C_ACK;
    i2c_ready[bus] = 1;
    return bus;
}

static int i2c_start_addr(volatile uint32_t *r, uint8_t addr, int read) {
    uint32_t guard = 200000u;
    while ((r[II_SR2 / 4] & I2C_BUSY) && --guard) {
    }
    if (!guard) return -1;
    r[II_CR1 / 4] |= I2C_START;
    guard = 200000u;
    while (!(r[II_SR1 / 4] & I2C_SB) && --guard) {
    }
    if (!guard) return -1;
    (void)r[II_SR1 / 4];                      /* clear SB by reading SR1 */
    r[II_DR / 4] = (uint32_t)((addr << 1) | (read ? 1 : 0));
    guard = 200000u;
    while (!(r[II_SR1 / 4] & I2C_ADDR) && --guard) {
    }
    if (!guard) { r[II_CR1 / 4] |= I2C_STOP; return -1; }
    (void)r[II_SR1 / 4];                      /* clear ADDR: SR1 then SR2 */
    (void)r[II_SR2 / 4];
    return 0;
}

int hal_i2c_write(uint8_t bus, uint8_t addr, const uint8_t *data, size_t len) {
    if (!i2c_bus_ok(bus)) return -1;
    volatile uint32_t *r = buses[bus].regs;
    if (i2c_start_addr(r, addr, 0) != 0) return -1;
    for (size_t i = 0; i < len; i++) {
        uint32_t guard = 200000u;
        while (!(r[II_SR1 / 4] & I2C_TXE) && --guard) {
        }
        if (!guard) { r[II_CR1 / 4] |= I2C_STOP; return -1; }
        r[II_DR / 4] = data[i];
        if (r[II_SR1 / 4] & I2C_AF) { r[II_CR1 / 4] |= I2C_STOP; return -1; }
    }
    uint32_t guard = 200000u;
    while (!(r[II_SR1 / 4] & I2C_BTF) && --guard) {
    }
    r[II_CR1 / 4] |= I2C_STOP;
    return (int)len;
}

int hal_i2c_read(uint8_t bus, uint8_t addr, uint8_t *data, size_t len) {
    if (!i2c_bus_ok(bus) || len == 0) return -1;
    volatile uint32_t *r = buses[bus].regs;
    if (len == 1) r[II_CR1 / 4] &= ~I2C_ACK;  /* NACK last byte */
    if (i2c_start_addr(r, addr, 1) != 0) {
        r[II_CR1 / 4] |= I2C_ACK;
        return -1;
    }
    for (size_t i = 0; i < len; i++) {
        uint32_t guard = 200000u;
        while (!(r[II_SR1 / 4] & I2C_RXNE) && --guard) {
        }
        if (!guard) break;
        data[i] = (uint8_t)(r[II_DR / 4] & 0xFFu);
        if (i + 2 == len) r[II_CR1 / 4] &= ~I2C_ACK;   /* NACK before last */
    }
    r[II_CR1 / 4] |= I2C_STOP;
    r[II_CR1 / 4] |= I2C_ACK;
    return (int)len;
}

int hal_i2c_write_read(uint8_t bus, uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rdata, size_t rlen) {
    if (!i2c_bus_ok(bus) || rlen == 0) return -1;
    volatile uint32_t *r = buses[bus].regs;
    /* Register write phase without STOP, then repeated-START read. */
    if (i2c_start_addr(r, addr, 0) != 0) return -1;
    for (size_t i = 0; i < wlen; i++) {
        uint32_t guard = 200000u;
        while (!(r[II_SR1 / 4] & I2C_TXE) && --guard) {
        }
        if (!guard) { r[II_CR1 / 4] |= I2C_STOP; return -1; }
        r[II_DR / 4] = wdata[i];
        if (r[II_SR1 / 4] & I2C_AF) { r[II_CR1 / 4] |= I2C_STOP; return -1; }
    }
    uint32_t guard = 200000u;
    while (!(r[II_SR1 / 4] & I2C_BTF) && --guard) {
    }
    if (rlen == 1) r[II_CR1 / 4] &= ~I2C_ACK;
    if (i2c_start_addr(r, addr, 1) != 0) {    /* repeated start */
        r[II_CR1 / 4] |= I2C_ACK;
        return -1;
    }
    for (size_t i = 0; i < rlen; i++) {
        guard = 200000u;
        while (!(r[II_SR1 / 4] & I2C_RXNE) && --guard) {
        }
        if (!guard) break;
        rdata[i] = (uint8_t)(r[II_DR / 4] & 0xFFu);
        if (i + 2 == rlen) r[II_CR1 / 4] &= ~I2C_ACK;
    }
    r[II_CR1 / 4] |= I2C_STOP;
    r[II_CR1 / 4] |= I2C_ACK;
    return (int)rlen;
}
#endif /* SECD_FEATURE_I2C */

/* -------------------------------- Flash ----------------------------------- */
uint32_t hal_flash_size(void) {
    return 128u * 1024u;
}
int hal_flash_read(uint32_t addr, uint8_t *buf, size_t len) {
    memcpy(buf, (const uint8_t *)addr, len);
    return 0;
}
int hal_flash_write(uint32_t addr, const uint8_t *buf, size_t len) {
    /* Bytecode is flashed over SWD/bootloader, never written at runtime. */
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

void hal_reset(void) {
    reg_write(SCB_AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);
    for (;;) {
    }
}

/* --------------------------- Radio / BLE stubs ------------------------- */
int hal_radio_init(void) { return -1; }
void hal_radio_set_address(const uint8_t *addr, size_t addr_len) { (void)addr; (void)addr_len; }
void hal_radio_set_channel(uint8_t ch) {}
int hal_radio_recv(uint8_t *out, size_t maxlen) { (void)out; (void)maxlen; return 0; }
int hal_radio_send(const uint8_t *data, size_t len) { return -1; }
void hal_radio_on_receive(void (*cb)(const uint8_t *, size_t)) {}
int hal_ble_init(void) { return -1; }
void hal_ble_set_name(const char *name) {}
int hal_ble_connected(void) { return 0; }
void hal_ble_key_report(uint8_t mods, const uint8_t keys[6]) { (void)mods; (void)keys; }
void hal_ble_mouse_report(int8_t dx, int8_t dy, uint8_t btns) {}

/* Console binding (UART-backed) */
size_t secd_console_write(const uint8_t *data, size_t len) {
    hal_serial_write_bytes(data, len);
    return len;
}
void secd_console_write_char(char c) { secd_console_write((const uint8_t *)&c, 1); }
int secd_console_read(void) {
    if (hal_serial_available() <= 0) return -1;
    return (int)hal_serial_read();
}
int secd_console_available(void) { return hal_serial_available(); }
bool secd_console_ready(void) { return true; }

