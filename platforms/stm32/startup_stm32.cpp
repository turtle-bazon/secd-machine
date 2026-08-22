/*
 * SECD Machine for Microcontrollers - STM32 startup
 * Copyright (C) 2026  License: GPL3
 *
 * Minimal bare-metal startup for STM32 Cortex-M3/M4 parts (Blue Pill
 * STM32F103CBT6, Black Pill STM32F401RCT6). No vendor SDK and no C runtime;
 * .data/.bss are brought up here and main() is called directly.
 *
 * Only SysTick raises an interrupt (the HAL polls every peripheral), so the
 * external IRQ slots are filled with a trapping default handler; their order
 * does not matter for this firmware.
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
DEFAULT_IRQ(MemManage_Handler);
DEFAULT_IRQ(BusFault_Handler);
DEFAULT_IRQ(UsageFault_Handler);
DEFAULT_IRQ(SVCall_Handler);
DEFAULT_IRQ(DebugMon_Handler);
DEFAULT_IRQ(PendSV_Handler);

typedef void (*vector_fn)(void);

extern unsigned int __stack_top__;

/* Core exceptions (16) + 64 external IRQ slots: enough for any small/mid
 * F1/F4 part; unused slots simply never fire. Entry 0 is the initial SP,
 * entry 1 the reset vector (Cortex-M vector table layout). */
__attribute__((section(".isr_vector"), used))
const vector_fn __isr_vector[16 + 64] = {
    /* Core exceptions */
    (vector_fn)&__stack_top__,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    Default_Handler,              /* 7: reserved */
    Default_Handler,              /* 8: reserved */
    Default_Handler,              /* 9: reserved */
    Default_Handler,              /* 10: reserved */
    SVCall_Handler,
    DebugMon_Handler,
    Default_Handler,              /* 13: reserved */
    PendSV_Handler,
    SysTick_Handler,
    /* External IRQs 0..63 (all polled peripherals trap here if they ever
     * fire; none are enabled) */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
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
