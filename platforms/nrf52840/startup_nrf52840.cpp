/*
 * SECD Machine - nRF52840 bare-metal startup
 * Copyright (C) 2026  License: GPL3
 */
#include <stdint.h>

extern "C" void Reset_Handler(void);
extern "C" int main(void);
extern "C" volatile uint32_t secd_systick_ms;

extern "C" void SysTick_Handler(void) { secd_systick_ms++; }

/* Any unexpected IRQ/handler lands here LOUD: a silent spin is
 * indistinguishable from a healthy idle on the LED. */
extern "C" void HardFault_Handler(void);
extern "C" void Default_Handler(void) { HardFault_Handler(); }

/* HardFault marker: rapid blink on every candidate LED so a crash is
 * distinguishable from a hung-but-alive app.  Direct register GPIO only. */
extern "C" __attribute__((noreturn)) void HardFault_Handler(void) {
    const uint32_t pins[] = { 15u, 27u, 45u };
    for (int p = 0; p < 4; p++) {
        uint32_t base = (pins[p] < 32) ? 0x50000000u : 0x50000300u;
        uint32_t idx = pins[p] & 31u;
        *(volatile uint32_t *)(base + 0x700u + idx * 4u) = 0x3u; /* output */
        *(volatile uint32_t *)(base + 0x518u) = (1u << idx);     /* DIRSET */
    }
    for (;;) {
        for (int p = 0; p < 4; p++) {
            uint32_t base = (pins[p] < 32) ? 0x50000000u : 0x50000300u;
            uint32_t bit = 1u << (pins[p] & 31u);
            *(volatile uint32_t *)(base + 0x508u) = bit; /* OUTSET */
            for (volatile int d = 0; d < 300000; d++) {}
            *(volatile uint32_t *)(base + 0x50Cu) = bit; /* OUTCLR */
            for (volatile int d = 0; d < 300000; d++) {}
        }
    }
}

#define WEAK_DEFAULT(name) \
    extern "C" void name(void) __attribute__((weak, alias("Default_Handler")))

/* Fault handlers all share the loud strobe: a silent freeze is
 * indistinguishable from a healthy idle loop on the LED. */
WEAK_DEFAULT(NMI_Handler);
WEAK_DEFAULT(SVCall_Handler);
extern "C" void MemManage_Handler(void) __attribute__((alias("HardFault_Handler")));
extern "C" void BusFault_Handler(void) __attribute__((alias("HardFault_Handler")));
extern "C" void UsageFault_Handler(void) __attribute__((alias("HardFault_Handler")));
WEAK_DEFAULT(DebugMon_Handler); WEAK_DEFAULT(PendSV_Handler);

extern "C" void USBD_IRQHandler(uint8_t busid);
extern "C" void USBD_Handler(void) { USBD_IRQHandler(0); }

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
    Default_Handler, Default_Handler, Default_Handler, USBD_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler
};

extern unsigned int __data_load;
extern unsigned int __data_start;
extern unsigned int __data_end;
extern unsigned int __bss_start;
extern unsigned int __bss_end;
extern unsigned int __app_base__;

extern "C" void Reset_Handler(void) {
    /* When loaded above the bootloader/SoftDevice (e.g. at 0x26000) the
       vector table must be relocated from the default 0x00000000. */
    *(volatile uint32_t *)0xE000ED08 = (uint32_t)&__app_base__;

    unsigned int *src = &__data_load;
    unsigned int *dst = &__data_start;
    while (dst < &__data_end) *dst++ = *src++;
    unsigned int *b = &__bss_start;
    while (b < &__bss_end) *b++ = 0;
    main();
    for (;;) {}
}
