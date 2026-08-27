/*
 * Minimal SoftDevice (S140 v6.1.1) shim for the nRF52840 unified firmware.
 *
 * We deliberately do NOT include the full Nordic headers (nrf.h / nrf52840.h /
 * core_cm4.h) here: they re-define the same peripheral structs (NRF_USBD_Type,
 * NVIC_Type, ...) that CherryUSB's nrf5x_regs.h already defines, which makes the
 * USB DCD (which includes nrf5x_regs.h) fail to compile. This shim declares ONLY
 * the SoftDevice entry points the firmware actually calls:
 *   - the SVC-based SD control / clock calls (via the standalone nrf_svc.h
 *     SVCALL macro, with the fixed S140 SVC numbers), and
 *   - the sd_nvic_* NVIC API, implemented as thin wrappers over the CMSIS-style
 *     NVIC accessors that operate on nrf5x_regs.h's NVIC_Type.
 *
 * The NVIC-dependent part is guarded by SECD_SD_NEED_NVIC so that translation
 * units that do not include nrf5x_regs.h (e.g. nrf52840_ble.cpp, which uses the
 * real Nordic BLE headers) do not pull in an NVIC_Type they don't have.
 */
#ifndef SECD_SD_SHIM_H
#define SECD_SD_SHIM_H

#include <stdint.h>
#include "nrf_svc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Types needed by the SoftDevice SVC signatures ---- */
typedef void (*nrf_fault_handler_t)(uint32_t id, uint32_t pc, uint32_t info);

typedef struct {
    uint8_t  source;        /* NRF_CLOCK_LF_SRC_RC = 0 */
    uint8_t  rc_ctiv;
    uint8_t  rc_temp_ctiv;
    uint8_t  accuracy;      /* NRF_CLOCK_LF_ACCURACY_500_PPM = 1 */
} nrf_clock_lf_cfg_t;

/* ---- SD info / magic (from nrf_mbr.h / nrf_sdm.h) ---- */
#ifndef SD_MAGIC_NUMBER
#define SD_MAGIC_NUMBER 0x51B1E5DB
#endif
#ifndef SOFTDEVICE_INFO_STRUCT_ADDRESS
#define MBR_SIZE 0
#define SOFTDEVICE_INFO_STRUCT_OFFSET (0x2000)
#define SOFTDEVICE_INFO_STRUCT_ADDRESS (SOFTDEVICE_INFO_STRUCT_OFFSET + MBR_SIZE)
#endif

/* ---- SoftDevice SVC entry points (S140 v6.1.1 SVC numbers) ----
 * sd_softdevice_*  live in the SDM SVC range (SDM_SVC_BASE = 0x10).
 * sd_clock_*      live in the SOC SVC range (SOC_SVC_BASE_NOT_AVAILABLE = 0x2C). */
SVCALL(0x10, uint32_t, sd_softdevice_enable(nrf_clock_lf_cfg_t const * p_clock_lf_cfg, nrf_fault_handler_t fault_handler));
SVCALL(0x12, uint32_t, sd_softdevice_is_enabled(uint8_t * p_softdevice_enabled));
SVCALL(0x13, uint32_t, sd_softdevice_vector_table_base_set(uint32_t address));
SVCALL(0x42, uint32_t, sd_clock_hfclk_request(void));
SVCALL(0x43, uint32_t, sd_clock_hfclk_release(void));
SVCALL(0x44, uint32_t, sd_clock_hfclk_is_running(uint32_t * p_is_running));

#ifdef SECD_SD_NEED_NVIC
/* ---- IRQ numbering (subset; only USBD/UARTE are used directly) ---- */
typedef enum IRQn_Type {
    UARTE0_IRQn = 2,
    USBD_IRQn   = 39
} IRQn_Type;

#ifndef NVIC
#define NVIC ((NVIC_Type *)0xE000E100UL)
#endif

/* ---- CMSIS-style NVIC accessors (operate on nrf5x_regs.h NVIC_Type) ---- */
static inline void NVIC_EnableIRQ(IRQn_Type IRQn) {
    NVIC->ISER[((uint32_t)IRQn) >> 5] = (uint32_t)(1UL << (((uint32_t)IRQn) & 0x1FUL));
}
static inline void NVIC_DisableIRQ(IRQn_Type IRQn) {
    NVIC->ICER[((uint32_t)IRQn) >> 5] = (uint32_t)(1UL << (((uint32_t)IRQn) & 0x1FUL));
}
static inline void NVIC_ClearPendingIRQ(IRQn_Type IRQn) {
    NVIC->ICPR[((uint32_t)IRQn) >> 5] = (uint32_t)(1UL << (((uint32_t)IRQn) & 0x1FUL));
}
static inline void NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority) {
    NVIC->IP[(uint32_t)IRQn] = (uint8_t)((priority << 5) & 0xFFUL);
}

/* ---- sd_nvic_* : the SoftDevice's NVIC API for app-owned IRQs ----
 * The real implementation just forwards to the CMSIS NVIC accessors, which is
 * exactly what Nordic's inline sd_nvic_* does for IRQs the application is
 * allowed to own (USBD, UARTE): the SD reserves RADIO/TIMER/RTC, not USBD. */
static inline uint32_t sd_nvic_EnableIRQ(IRQn_Type IRQn) {
    NVIC_EnableIRQ(IRQn);
    return 0;
}
static inline uint32_t sd_nvic_DisableIRQ(IRQn_Type IRQn) {
    NVIC_DisableIRQ(IRQn);
    return 0;
}
static inline uint32_t sd_nvic_ClearPendingIRQ(IRQn_Type IRQn) {
    NVIC_ClearPendingIRQ(IRQn);
    return 0;
}
static inline uint32_t sd_nvic_SetPriority(IRQn_Type IRQn, uint32_t priority) {
    NVIC_SetPriority(IRQn, priority);
    return 0;
}
#endif /* SECD_SD_NEED_NVIC */

#ifdef __cplusplus
}
#endif

#endif /* SECD_SD_SHIM_H */
