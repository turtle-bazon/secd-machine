/*
 * SECD Machine - nRF52840 bare-metal startup
 * Copyright (C) 2026  License: GPL3
 */
#include <stdint.h>

extern "C" void Reset_Handler(void);
extern "C" int main(void);
extern "C" volatile uint32_t secd_systick_ms;

extern "C" void SysTick_Handler(void) { secd_systick_ms++; }
extern "C" void Default_Handler(void) { for(;;){} }

#define WEAK_DEFAULT(name) \
    extern "C" void name(void) __attribute__((weak, alias("Default_Handler")))

WEAK_DEFAULT(NMI_Handler); WEAK_DEFAULT(HardFault_Handler);
WEAK_DEFAULT(MemManage_Handler); WEAK_DEFAULT(BusFault_Handler);
WEAK_DEFAULT(UsageFault_Handler); WEAK_DEFAULT(SVCall_Handler);
WEAK_DEFAULT(DebugMon_Handler); WEAK_DEFAULT(PendSV_Handler);

typedef void (*vector_fn)(void);
extern unsigned int __stack_top__;

/* nRF52840 vector table: 16 core + 48 external IRQs = 64 entries */
__attribute__((section(".isr_vector"), used))
const vector_fn __isr_vector[16 + 48] = {
    (vector_fn)&__stack_top__,
    Reset_Handler,
    NMI_Handler, HardFault_Handler,
    MemManage_Handler, BusFault_Handler, UsageFault_Handler,
    Default_Handler, Default_Handler, Default_Handler, /* 7-9 rsvd */
    Default_Handler,                                   /* 10 rsvd */
    SVCall_Handler, DebugMon_Handler,                  /* 11-12 */
    Default_Handler, PendSV_Handler, SysTick_Handler,   /* 13-15 */
    /* External IRQs 0..47 (all polled, trap on fire) */
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
    Default_Handler, Default_Handler, Default_Handler, Default_Handler
};

extern unsigned int __data_load;
extern unsigned int __data_start;
extern unsigned int __data_end;
extern unsigned int __bss_start;
extern unsigned int __bss_end;

extern "C" void Reset_Handler(void) {
    unsigned int *src = &__data_load;
    unsigned int *dst = &__data_start;
    while (dst < &__data_end) *dst++ = *src++;
    unsigned int *b = &__bss_start;
    while (b < &__bss_end) *b++ = 0;
    main();
    for (;;) {}
}
