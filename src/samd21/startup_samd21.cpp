/*
 * SAMD21 startup: vector table + reset handler (Cortex-M0+).
 * Copyright (C) 2026  License: GPL3
 *
 * Minimal bare-metal startup for the Seeed XIAO SAMD21. No C runtime
 * library is used; we bring up .data/.bss ourselves and jump to main().
 */

#include <stdint.h>

extern "C" void Reset_Handler(void);
extern "C" int main(void);

extern "C" volatile uint32_t secd_systick_ms;

/* 1ms tick (SysTick is configured by hal_init). */
extern "C" void SysTick_Handler(void) {
    secd_systick_ms++;
}

extern "C" void Default_Handler(void) {
    for (;;) {
        /* trap */
    }
}

#define DEFAULT_IRQ(name) \
    extern "C" void name(void) __attribute__((weak, alias("Default_Handler")))

DEFAULT_IRQ(NMI_Handler);
DEFAULT_IRQ(HardFault_Handler);
DEFAULT_IRQ(SVCall_Handler);
DEFAULT_IRQ(PendSV_Handler);

DEFAULT_IRQ(POWER_Handler);
DEFAULT_IRQ(RTC_Handler);
DEFAULT_IRQ(RTC_ALARM_Handler);
DEFAULT_IRQ(RTC_TAMPER_Handler);
DEFAULT_IRQ(DMA_Handler);
DEFAULT_IRQ(DMA_ABORT_Handler);
DEFAULT_IRQ(USB_Handler);
DEFAULT_IRQ(EVSYS_Handler);
DEFAULT_IRQ(SERCOM0_Handler);
DEFAULT_IRQ(SERCOM1_Handler);
DEFAULT_IRQ(SERCOM2_Handler);
DEFAULT_IRQ(SERCOM3_Handler);
DEFAULT_IRQ(SERCOM4_Handler);
DEFAULT_IRQ(SERCOM5_Handler);
DEFAULT_IRQ(TC0_Handler);
DEFAULT_IRQ(TC1_Handler);
DEFAULT_IRQ(TC2_Handler);
DEFAULT_IRQ(TC3_Handler);
DEFAULT_IRQ(TC4_Handler);
DEFAULT_IRQ(TC5_Handler);
DEFAULT_IRQ(TC6_Handler);
DEFAULT_IRQ(TC7_Handler);
DEFAULT_IRQ(ADC_Handler);
DEFAULT_IRQ(AC_Handler);
DEFAULT_IRQ(DAC_Handler);
DEFAULT_IRQ(PTC_Handler);
DEFAULT_IRQ(I2S_Handler);
DEFAULT_IRQ(AC2_Handler);
DEFAULT_IRQ(TCC0_Handler);
DEFAULT_IRQ(TCC1_Handler);
DEFAULT_IRQ(TCC2_Handler);
DEFAULT_IRQ(WDT_Handler);

typedef void (*vector_fn)(void);

extern unsigned int __stack_top__;
extern unsigned int __flash_binary_end;

/* SAMD21 vector table (Cortex-M0+, 16 core + 32 external IRQs). */
__attribute__((section(".isr_vector"), used))
const vector_fn __isr_vector[16 + 32] = {
    /* Core exceptions */
    (vector_fn)&__stack_top__,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    Default_Handler,              /* 4: reserved */
    Default_Handler,              /* 5: reserved */
    Default_Handler,              /* 6: reserved */
    Default_Handler,              /* 7: reserved */
    Default_Handler,              /* 8: reserved */
    Default_Handler,              /* 9: reserved */
    Default_Handler,              /* 10: reserved */
    SVCall_Handler,
    Default_Handler,              /* 12: reserved */
    Default_Handler,              /* 13: reserved */
    PendSV_Handler,
    SysTick_Handler,
    /* External IRQs 0..31 */
    POWER_Handler,
    RTC_Handler,
    RTC_ALARM_Handler,
    RTC_TAMPER_Handler,
    DMA_Handler,
    DMA_ABORT_Handler,
    USB_Handler,
    EVSYS_Handler,
    SERCOM0_Handler,
    SERCOM1_Handler,
    SERCOM2_Handler,
    SERCOM3_Handler,
    SERCOM4_Handler,
    SERCOM5_Handler,
    TC0_Handler,
    TC1_Handler,
    TC2_Handler,
    TC3_Handler,
    TC4_Handler,
    TC5_Handler,
    TC6_Handler,
    TC7_Handler,
    ADC_Handler,
    AC_Handler,
    DAC_Handler,
    PTC_Handler,
    I2S_Handler,
    AC2_Handler,
    TCC0_Handler,
    TCC1_Handler,
    TCC2_Handler,
    WDT_Handler,
};

extern unsigned int __data_load;
extern unsigned int __data_start;
extern unsigned int __data_end;
extern unsigned int __bss_start;
extern unsigned int __bss_end;

extern "C" void Reset_Handler(void) {
    /* Copy .data from flash to RAM */
    unsigned int *src = &__data_load;
    unsigned int *dst = &__data_start;
    while (dst < &__data_end) {
        *dst++ = *src++;
    }

    /* Zero .bss */
    unsigned int *b = &__bss_start;
    while (b < &__bss_end) {
        *b++ = 0;
    }

    main();

    for (;;) {
        /* never return */
    }
}
