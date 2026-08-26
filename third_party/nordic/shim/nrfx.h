/* Minimal nrfx.h shim: TinyUSB's nordic port only needs the SoC register
 * layer, not the full nrfx driver framework. */
#pragma once
#include "nrf.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifndef NRF_STATIC_INLINE
#define NRF_STATIC_INLINE static inline
#endif

/* nrfx glue hook: normally from nrfx_glue.h (debug event read-back). */
static inline void nrf_event_readback(const volatile void *p_reg) { (void)p_reg; }
