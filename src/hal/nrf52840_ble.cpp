/*
 * SECD Machine for Microcontrollers - nRF52840 BLE HID (S140 SoftDevice)
 * Copyright (C) 2026  License: GPL3
 *
 * Implements the hal_ble_* HID-over-GATT (HOGP) keyboard profile directly on
 * the Nordic S140 v6.1.1 SoftDevice. No NimBLE / RTOS.
 *
 * Build only with -DSECD_FEATURE_BLE=1 (the BLE firmware variant). This file
 * replaces the stubs in nrf52840.cpp, which are guarded out in that build.
 *
 * SoftDevice notes:
 *   - The S140 occupies 0x1000..__app_base__ (0x26000) and reserves the bottom
 *     of RAM; the BLE linker script shifts RAM origin above it.
 *   - The SD owns NRF_CLOCK and the vector table. We must NOT touch the clock
 *     or enable app IRQs; BLE events are retrieved by polling sd_ble_evt_get().
 */

#include "hal/hal.h"
#include <string.h>
#include <stdint.h>

#include "ble.h"
#include "ble_gap.h"
#include "ble_gatts.h"
#include "ble_hci.h"
#include "nrf_error.h"
#include "nrf_sdm.h"

/* The SoftDevice forwards app-owned IRQs (USBD, UARTE) to our handlers once we
 * point it at our vector table via sd_softdevice_vector_table_base_set. The
 * table lives at the flash app base (SECD_APP_BASE), supplied by the Makefile. */

/* ---- GAP / GATT UUIDs (SIG 16-bit) ---- */
#define UUID_HID_SERVICE        0x1812
#define UUID_BATTERY_SERVICE    0x180F
#define UUID_DEVICE_INFO        0x180A
#define UUID_HID_REPORT         0x2A4D
#define UUID_HID_REPORT_MAP     0x2A4B
#define UUID_HID_INFORMATION    0x2A4A
#define UUID_HID_CONTROL_POINT  0x2A4C
#define UUID_HID_PROTOCOL_MODE  0x2A4E
#define UUID_BATTERY_LEVEL      0x2A19
#define UUID_MANUFACTURER_NAME  0x2A29
#define UUID_REPORT_REFERENCE   0x2908
#define UUID_GAP_APPEARANCE     0x2A01

#ifndef SECD_BLE_RADIOTX_TEST
/* Everything below is SoftDevice BLE HID plumbing; it is compiled out for the
 * bare-metal RADIOTX diagnostic, which has no S140 linked in.  The direct-
 * RADIO beacon (hal_ble_radio_beacon_test) lives outside this #ifndef and is
 * always compiled; in the production build it is dead code (never called). */

/* Local 16-bit UUID helper */
static ble_uuid_t sig_uuid(uint16_t u) {
    ble_uuid_t r;
    r.type = BLE_UUID_TYPE_BLE;
    r.uuid = u;
    return r;
}

#define DBG(...) hal_print(__VA_ARGS__)

/* ---- State ---- */
static bool              m_inited = false;
static uint16_t          m_conn_handle = BLE_CONN_HANDLE_INVALID;
static uint8_t           m_adv_handle  = BLE_GAP_ADV_SET_HANDLE_NOT_SET;
static bool              m_adv_running = false;
static uint8_t           m_input_notify = 0;     /* 1 = central enabled notify */

static uint16_t          m_hid_svc;
static uint16_t          m_kb_input_val;         /* HID Input Report value handle */
static uint16_t          m_kb_input_cccd;        /* HID Input Report CCCD handle */
static uint16_t          m_batt_val;

static uint8_t           m_name[32];
static uint8_t           m_name_len = 0;

/* HID report descriptor: boot-capable keyboard (8-byte input report). */
static const uint8_t hid_report_map[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop) */
    0x09, 0x06,        /* Usage (Keyboard) */
    0xA1, 0x01,        /* Collection (Application) */
    0x05, 0x07,        /*   Usage Page (Key Codes) */
    0x19, 0xE0,        /*   Usage Minimum (224) */
    0x29, 0xE7,        /*   Usage Maximum (231) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x25, 0x01,        /*   Logical Maximum (1) */
    0x75, 0x01,        /*   Report Size (1) */
    0x95, 0x08,        /*   Report Count (8) */
    0x81, 0x02,        /*   Input (Data,Var,Abs)  -- modifier byte */
    0x95, 0x01,        /*   Report Count (1) */
    0x75, 0x08,        /*   Report Size (8) */
    0x81, 0x03,        /*   Input (Const)        -- reserved byte */
    0x95, 0x06,        /*   Report Count (6) */
    0x75, 0x08,        /*   Report Size (8) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x25, 0x65,        /*   Logical Maximum (101) */
    0x05, 0x07,        /*   Usage Page (Key Codes) */
    0x19, 0x00,        /*   Usage Minimum (0) */
    0x29, 0x65,        /*   Usage Maximum (101) */
    0x81, 0x00,        /*   Input (Data,Array)   -- 6 keycodes */
    0xC0               /* End Collection */
};

/* Security params (JustWorks, no bonding). */
static ble_gap_sec_params_t m_sec_params;
static ble_gap_sec_keyset_t m_keyset;            /* all-NULL pointers => no key storage */

/* Event buffer (default MTU small; 256 is ample). */
static uint8_t m_evt_buf[256];

/* ------------------------------------------------------------------ */
static void perm_open(ble_gap_conn_sec_mode_t *m) {
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(m);
}

/* Add a characteristic with a stack-owned value buffer and optional CCCD. */
static uint32_t add_characteristic(uint16_t svc, uint16_t uuid,
                                   ble_gatt_char_props_t props,
                                   uint8_t *val, uint16_t vlen,
                                   ble_gatts_char_handles_t *h,
                                   bool cccd) {
    ble_gatts_attr_md_t amd;
    memset(&amd, 0, sizeof(amd));
    perm_open(&amd.read_perm);
    perm_open(&amd.write_perm);
    amd.vloc = BLE_GATTS_VLOC_STACK;

    ble_uuid_t u = sig_uuid(uuid);
    ble_gatts_attr_t attr;
    memset(&attr, 0, sizeof(attr));
    attr.p_uuid = &u;
    attr.p_attr_md = &amd;
    attr.init_len = vlen;
    attr.max_len = vlen;
    attr.p_value = val;

    ble_gatts_char_md_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.char_props = props;

    ble_gatts_attr_md_t cccd_md;
    if (cccd) {
        memset(&cccd_md, 0, sizeof(cccd_md));
        perm_open(&cccd_md.read_perm);
        perm_open(&cccd_md.write_perm);
        cccd_md.vloc = BLE_GATTS_VLOC_STACK;
        cmd.p_cccd_md = &cccd_md;
    }

    return sd_ble_gatts_characteristic_add(svc, &cmd, &attr, h);
}

/* Add a Report Reference descriptor (id + type) to a value handle. */
static void add_report_reference(uint16_t value_handle, uint8_t id, uint8_t type) {
    ble_gatts_attr_md_t rmd;
    memset(&rmd, 0, sizeof(rmd));
    perm_open(&rmd.read_perm);
    perm_open(&rmd.write_perm);
    rmd.vloc = BLE_GATTS_VLOC_STACK;

    static uint8_t ref[2];
    ref[0] = id; ref[1] = type;

    ble_uuid_t u = sig_uuid(UUID_REPORT_REFERENCE);
    ble_gatts_attr_t rattr;
    memset(&rattr, 0, sizeof(rattr));
    rattr.p_uuid = &u;
    rattr.p_attr_md = &rmd;
    rattr.init_len = 2;
    rattr.max_len = 2;
    rattr.p_value = ref;

    uint16_t rh = 0;
    sd_ble_gatts_descriptor_add(value_handle, &rattr, &rh);
}

/* ------------------------------------------------------------------ */
static void start_advertising(void) {
    if (m_adv_running) return;

    /* Name to advertise: prefer the Lisp-configured one (%ble-name), fall back
     * to a sane default so the device is discoverable out of the box. */
    static const char default_name[] = "SECD Keyboard";
    const uint8_t *name = m_name_len ? m_name : (const uint8_t *)default_name;
    uint8_t nlen = m_name_len ? m_name_len : (uint8_t)sizeof(default_name) - 1;

    uint8_t adv_data[31];
    uint8_t idx = 0;
    adv_data[idx++] = 2;
    adv_data[idx++] = BLE_GAP_AD_TYPE_FLAGS;
    adv_data[idx++] = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    /* Complete list of 16-bit service UUIDs: HID (0x1812) */
    adv_data[idx++] = 3;
    adv_data[idx++] = BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_COMPLETE;
    adv_data[idx++] = (uint8_t)(UUID_HID_SERVICE & 0xFF);
    adv_data[idx++] = (uint8_t)(UUID_HID_SERVICE >> 8);
    /* Appearance (HID Keyboard 0x03C1): lets OS scanners categorize/filter
     * the device properly. */
    adv_data[idx++] = 3;
    adv_data[idx++] = BLE_GAP_AD_TYPE_APPEARANCE;
    adv_data[idx++] = (uint8_t)(BLE_APPEARANCE_HID_KEYBOARD & 0xFF);
    adv_data[idx++] = (uint8_t)(BLE_APPEARANCE_HID_KEYBOARD >> 8);

    ble_gap_adv_data_t adv;
    memset(&adv, 0, sizeof(adv));
    adv.adv_data.p_data = adv_data;
    adv.adv_data.len = idx;

    /* Scan response carries the name (Windows/Android use it to render the
     * entry in the Add-a-device list). */
    uint8_t srsp[31];
    uint8_t sidx = 0;
    if (nlen > 31 - sidx - 2) nlen = 31 - sidx - 2;
    if (nlen > 0) {
        srsp[sidx++] = nlen + 1;
        srsp[sidx++] = BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME;
        memcpy(&srsp[sidx], name, nlen);
        sidx += nlen;
    }
    adv.scan_rsp_data.p_data = srsp;
    adv.scan_rsp_data.len = sidx;

    ble_gap_adv_params_t params;
    memset(&params, 0, sizeof(params));
    params.properties.type = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED;
    params.primary_phy = BLE_GAP_PHY_1MBPS;
    params.secondary_phy = BLE_GAP_PHY_1MBPS;
    params.interval = 160;                 /* ~100 ms */
    params.duration = 0;                   /* no timeout */
    params.filter_policy = BLE_GAP_ADV_FP_ANY;

    uint32_t err = sd_ble_gap_adv_set_configure(&m_adv_handle, &adv, &params);
    if (err) { DBG("adv_cfg err "); hal_print_int((int32_t)err); DBG("\r\n"); return; }

    err = sd_ble_gap_adv_start(m_adv_handle, BLE_CONN_CFG_TAG_DEFAULT);
    if (err) { DBG("adv_start err "); hal_print_int((int32_t)err); DBG("\r\n"); return; }
    m_adv_running = true;
    DBG("advertising ok (handle ");
    hal_print_int((int32_t)m_adv_handle);
    DBG(", "); hal_print_int((int32_t)nlen); DBG(" byte name)\r\n");
}

/* Build the GATT database: HID + Battery + Device Information. */
static uint32_t build_gatt_db(void) {
    uint32_t err;
    ble_uuid_t u;

    /* HID Service */
    u = sig_uuid(UUID_HID_SERVICE);
    err = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &u, &m_hid_svc);
    if (err) return err;

    ble_gatts_char_handles_t h;

    /* Protocol Mode (read, write w/o resp) init 0x01 (Report Protocol) */
    uint8_t proto = 1;
    err = add_characteristic(m_hid_svc, UUID_HID_PROTOCOL_MODE,
                             (ble_gatt_char_props_t){.read = 1, .write_wo_resp = 1},
                             &proto, 1, &h, false);
    if (err) return err;

    /* HID Input Report (read, notify) + Report Reference (id=0, type=Input) */
    uint8_t kb_init[8] = {0};
    err = add_characteristic(m_hid_svc, UUID_HID_REPORT,
                             (ble_gatt_char_props_t){.read = 1, .write_wo_resp = 1, .notify = 1},
                             kb_init, sizeof(kb_init), &h, true);
    if (err) return err;
    m_kb_input_val = h.value_handle;
    m_kb_input_cccd = h.cccd_handle;
    add_report_reference(h.value_handle, 0, 1);   /* 1 = Input */

    /* HID Output Report (read, write w/o resp, write) + Report Reference (id=0, type=Output) */
    uint8_t out_init[1] = {0};
    err = add_characteristic(m_hid_svc, UUID_HID_REPORT,
                             (ble_gatt_char_props_t){.read = 1, .write_wo_resp = 1, .write = 1},
                             out_init, sizeof(out_init), &h, false);
    if (err) return err;
    add_report_reference(h.value_handle, 0, 2);   /* 2 = Output */

    /* HID Report Map (read) */
    err = add_characteristic(m_hid_svc, UUID_HID_REPORT_MAP,
                             (ble_gatt_char_props_t){.read = 1},
                             (uint8_t *)hid_report_map, sizeof(hid_report_map), &h, false);
    if (err) return err;

    /* HID Information (read): bcdHID=1.11, country=0, flags=NormallyConnectable */
    uint8_t info[4] = {0x11, 0x01, 0x00, 0x02};
    err = add_characteristic(m_hid_svc, UUID_HID_INFORMATION,
                             (ble_gatt_char_props_t){.read = 1},
                             info, sizeof(info), &h, false);
    if (err) return err;

    /* HID Control Point (write w/o resp) */
    uint8_t ctl = 0;
    err = add_characteristic(m_hid_svc, UUID_HID_CONTROL_POINT,
                             (ble_gatt_char_props_t){.write_wo_resp = 1},
                             &ctl, 1, &h, false);
    if (err) return err;

    /* Battery Service */
    uint16_t batt_svc;
    u = sig_uuid(UUID_BATTERY_SERVICE);
    err = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &u, &batt_svc);
    if (err) return err;
    uint8_t level = 100;
    err = add_characteristic(batt_svc, UUID_BATTERY_LEVEL,
                             (ble_gatt_char_props_t){.read = 1, .write_wo_resp = 1, .notify = 1},
                             &level, 1, &h, true);
    if (err) return err;
    m_batt_val = h.value_handle;

    /* Device Information Service */
    uint16_t dis_svc;
    u = sig_uuid(UUID_DEVICE_INFO);
    err = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &u, &dis_svc);
    if (err) return err;
    static uint8_t mfr[] = "SECD";
    err = add_characteristic(dis_svc, UUID_MANUFACTURER_NAME,
                             (ble_gatt_char_props_t){.read = 1},
                             mfr, sizeof(mfr) - 1, &h, false);
    if (err) return err;

    return 0;
}

/* ------------------------------------------------------------------ */
static void on_ble_evt(ble_evt_t *evt) {
    uint16_t id = evt->header.evt_id;

    if (id == BLE_GAP_EVT_CONNECTED) {
        m_conn_handle = evt->evt.gap_evt.conn_handle;
        m_adv_running = false;
        m_input_notify = 0;
        DBG("BLE connected\r\n");
    } else if (id == BLE_GAP_EVT_DISCONNECTED) {
        DBG("BLE disconnected\r\n");
        m_conn_handle = BLE_CONN_HANDLE_INVALID;
        m_input_notify = 0;
        start_advertising();
    } else if (id == BLE_GAP_EVT_SEC_PARAMS_REQUEST) {
        uint32_t err = sd_ble_gap_sec_params_reply(
            evt->evt.gap_evt.conn_handle,
            BLE_GAP_SEC_STATUS_SUCCESS,
            &m_sec_params, &m_keyset);
        if (err) { DBG("sec_reply err "); hal_print_int((int32_t)err); DBG("\r\n"); }
    } else if (id == BLE_GAP_EVT_ADV_SET_TERMINATED) {
        if (m_conn_handle == BLE_CONN_HANDLE_INVALID)
            start_advertising();
    } else if (id == BLE_GATTS_EVT_WRITE) {
        ble_gatts_evt_write_t const *w = &evt->evt.gatts_evt.params.write;
        if (w->handle == m_kb_input_cccd) {
            m_input_notify = (w->len >= 1 && (w->data[0] & 0x01)) ? 1 : 0;
            DBG("CCCD -> "); hal_print_int((int32_t)m_input_notify); DBG("\r\n");
        }
    }
}

extern "C" void hal_ble_poll(void) {
    if (!m_inited) return;
    uint16_t len = sizeof(m_evt_buf);
    while (sd_ble_evt_get(m_evt_buf, &len) == NRF_SUCCESS) {
        on_ble_evt((ble_evt_t *)m_evt_buf);
        len = sizeof(m_evt_buf);
    }
}

/* ------------------------------------------------------------------ */
/* SoftDevice fault capture. sd_softdevice_enable() requires a non-NULL fault
 * handler (NULL just makes the call fail with NRF_ERROR_INVALID_ADDR). The
 * handler runs in HardFault context: it must not call SVCs, must not touch the
 * USB stack, and per nrf_sdm.h returning from it makes the SD perform a full
 * reset. We record id/pc/info in .noinit RAM (survives warm reset: it is
 * neither copied from flash nor zeroed by Reset_Handler) and print the report
 * on the following boot, once the console is up. */
#define SD_FAULT_MAGIC 0x53444654u   /* "SDFT" */

typedef struct {
    uint32_t magic;
    uint32_t id;
    uint32_t pc;
    uint32_t info;
} sd_fault_rec_t;

__attribute__((section(".noinit"), used))
static sd_fault_rec_t g_sd_fault;

extern "C" void sd_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
    g_sd_fault.magic = SD_FAULT_MAGIC;
    g_sd_fault.id    = id;
    g_sd_fault.pc    = pc;
    g_sd_fault.info  = info;
}

/* Outcome of this boot's enable attempt (reported by ble_sd_report()). */
static bool     g_sd_tried      = false;
static uint32_t g_sd_enable_ret = 0;
static uint32_t g_sd_vtbase_ret = 0;

/* Enable the SoftDevice at boot. Called once from main(), before the USB
 * stack is brought up. The SD owns the vector table and the bottom of RAM, and
 * forwards app-owned IRQs (USBD, UARTE) to our handlers once we point it at our
 * vector table. The BLE GATT stack itself is brought up lazily later, by
 * hal_ble_init() (driven from Lisp's %ble-init). */
extern "C" void ble_sd_enable(void) {
    static bool sd_on = false;
    if (sd_on) return;

    /* A fault from a previous boot is pending. Skip re-enabling so the reset
     * loop cannot swallow the report; main() prints and clears it later. */
    if (g_sd_fault.magic == SD_FAULT_MAGIC) return;

    nrf_clock_lf_cfg_t lfcfg = {
        .source = 0,            /* NRF_CLOCK_LF_SRC_RC */
        .rc_ctiv = 16,
        .rc_temp_ctiv = 2,
        .accuracy = 1           /* NRF_CLOCK_LF_ACCURACY_500_PPM */
    };
    g_sd_tried = true;
    uint32_t err = sd_softdevice_enable(&lfcfg, sd_fault_handler);
    g_sd_enable_ret = err;
    if (err) return;

    /* Point the SD at our vector table so USBD/UARTE IRQs reach our handlers. */
    err = sd_softdevice_vector_table_base_set((uint32_t)SECD_APP_BASE);
    g_sd_vtbase_ret = err;
    if (err) return;

    sd_on = true;
}

/* Print this boot's SD bring-up outcome. Called from main() after the console
 * is up (before that, secd_console_write() drops everything). */
static void sd_print_hex(uint32_t v) {
    const char h[] = "0123456789ABCDEF";
    char b[11] = { '0', 'x' };
    for (int i = 0; i < 8; i++) b[2 + i] = h[(v >> ((7 - i) * 4)) & 0xF];
    hal_print(b);
}

extern "C" void ble_sd_report(void) {
    if (g_sd_tried) {
        uint8_t enabled = 0;
        (void)sd_softdevice_is_enabled(&enabled);
        /* DCD is_sd_existed() reads the SD info struct magic here (MBR 0x1000
         * + info offset 0x2000 + 4); print it so we can confirm the DCD will
         * actually take the SD clock path rather than write CLOCK directly. */
        uint32_t sdmagic = *(volatile uint32_t *)0x3004;

        DBG("sd_enable ret ");
        hal_print_int((int32_t)g_sd_enable_ret);
        DBG(" vtbase ");
        hal_print_int((int32_t)g_sd_vtbase_ret);
        DBG(" enabled=");
        hal_print_int((int32_t)enabled);
        DBG(" sdmagic=");
        sd_print_hex(sdmagic);
        DBG("\r\n");

        /* Identity of the resident SoftDevice (MBR info block at 0x3000): the
         * version word must be S140 6.1.1 (sd_fw_version) or EVERYTHING the
         * app assumes about struct layouts / SVC behavior is suspect. One word
         * per line: the CDC console drops tail bytes while BLE init takes over,
         * so no field ever depends on surviving to the end of a long line. */
        for (int k = 0; k < 6; k++) {
            volatile uint32_t *p = (volatile uint32_t *)(0x3000UL + (uint32_t)k * 4u);
            DBG("sdinfo");
            hal_print_int((int32_t)k);
            DBG("=");
            sd_print_hex(*p);
            DBG("\r\n");
        }

        /* The SoftDevice's own image carries its build string as ASCII in flash.
         * Read it straight out of the SD region (flash reads are always legal)
         * to see exactly which SoftDevice is resident. */
        DBG("sdstrings:\r\n");
        uint8_t run[36];
        uint8_t run_len = 0;
        for (uint32_t a = 0x1004; a < 0x3000; a++) {
            uint8_t c = *(volatile uint8_t *)a;
            if (c >= 0x20 && c <= 0x7E) {
                if (run_len < 34) run[run_len] = c;
                run_len++;
                continue;
            }
            if (run_len >= 4) {
                if (run_len > 34) run_len = 34;
                run[run_len] = 0;
                DBG(" "); DBG((const char *)run); DBG("\r\n");
            }
            run_len = 0;
        }
        if (run_len >= 4) {
            if (run_len > 34) run_len = 34;
            run[run_len] = 0;
            DBG(" "); DBG((const char *)run); DBG("\r\n");
        }

        /* Radio health probe: BLE's radio cannot transmit without the 64 MHz
         * HFXO. Request HFCLK through the SD and read the real hardware state
         * (reads of CLOCK are allowed; only writes fault with the SD on).
         * HFCLKSTAT @ 0x4000040C: STATE = bit0, SRC = bit16 (0=HFINT, 1=HFXO).
         * Healthy: 0x00010001. Stuck on internal oscillator (missing/cracked
         * crystal): 0x00000001 or 0x00000000. */
        (void)sd_clock_hfclk_request();
        hal_delay(5);
        uint32_t hfclkstat = *(volatile uint32_t *)0x4000040CUL;
        (void)sd_clock_hfclk_release();
        DBG("hfclkstat=");
        sd_print_hex(hfclkstat);
        DBG("\r\n");

        /* The SD's radio scheduler ticks from LFCLK -> RTC0. If either is down,
         * adv_start queues forever and the radio never transmits (no fault, no
         * error). LFCLKSTAT @0x40000418 (STATE=bit0, SRC RC=00), LFCLKRUN
         * @0x40000414, RTC0.COUNTER @0x4000B504 sampled across a delay. */
        uint32_t lfclkstat = *(volatile uint32_t *)0x40000418UL;
        uint32_t lfclkrun  = *(volatile uint32_t *)0x40000414UL;
        uint32_t rtc0_a    = *(volatile uint32_t *)0x4000B504UL;
        hal_delay(10);
        uint32_t rtc0_b    = *(volatile uint32_t *)0x4000B504UL;
        DBG("lfclkstat=");
        sd_print_hex(lfclkstat);
        DBG("\r\n");
        DBG("lfclkrun=");
        sd_print_hex(lfclkrun);
        DBG("\r\n");
        DBG("rtc0cnt=");
        sd_print_hex(rtc0_a);
        DBG("->");
        sd_print_hex(rtc0_b);
        DBG("\r\n");

        /* Is the SD's radio-scheduler interrupt machinery actually armed?
         * VTOR must point into the SD region (not our app table at 0x26000),
         * NVIC ISER0 must show RTC0 (IRQ 11) enabled, RTC0 INTENSET must have
         * COMPARE0 (bit16) set, PRESCALER 0. If all four hold yet the radio
         * hears nothing, the RF front end itself is dead. */
        uint32_t vtor   = *(volatile uint32_t *)0xE000ED08UL;
        uint32_t iser0  = *(volatile uint32_t *)0xE000E100UL;
        uint32_t rtc0_inten = *(volatile uint32_t *)0x4000B304UL;
        uint32_t rtc0_presc = *(volatile uint32_t *)0x4000B508UL;
        DBG("vtor=");
        sd_print_hex(vtor);
        DBG("\r\n");
        DBG("iser0=");
        sd_print_hex(iser0);
        DBG("\r\n");
        DBG("rtc0inten=");
        sd_print_hex(rtc0_inten);
        DBG("\r\n");
        DBG("rtc0presc=");
        sd_print_hex(rtc0_presc);
        DBG("\r\n");
    }
    if (g_sd_fault.magic == SD_FAULT_MAGIC) {
        DBG("SD_FAULT id=");
        hal_print_int((int32_t)g_sd_fault.id);
        DBG(" pc=");
        hal_print_int((int32_t)g_sd_fault.pc);
        DBG(" info=");
        hal_print_int((int32_t)g_sd_fault.info);
        DBG("\r\n");
        g_sd_fault.magic = 0;   /* consume the report */
    }
}

/* ------------------------------------------------------------------ */
int hal_ble_init(void) {
    if (m_inited) return 0;
    uint32_t err;

    /* 1. The SoftDevice was already enabled at boot by ble_sd_enable(); only the
     *    BLE portion of the stack is brought up here (lazily, on %ble-init). */

    /* 2. Configure role counts (default peripheral count is 0!). */
    ble_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.gap_cfg.role_count_cfg.adv_set_count = 1;
    cfg.gap_cfg.role_count_cfg.periph_role_count = 1;
    cfg.gap_cfg.role_count_cfg.central_role_count = 0;
    err = sd_ble_cfg_set(BLE_GAP_CFG_ROLE_COUNT, &cfg, 0x20004000);
    if (err) { DBG("role_cfg err "); hal_print_int((int32_t)err); DBG("\r\n"); return -1; }

    /* 3. Attribute table size for our HID service. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.gatts_cfg.attr_tab_size.attr_tab_size = 0x600;
    err = sd_ble_cfg_set(BLE_GATTS_CFG_ATTR_TAB_SIZE, &cfg, 0x20004000);
    if (err) { DBG("gatts_cfg err "); hal_print_int((int32_t)err); DBG("\r\n"); return -1; }

    /* 4. Enable BLE stack (reserve RAM at the bottom for the SD). */
    uint32_t ram_base = 0x20004000;
    err = sd_ble_enable(&ram_base);
    if (err) {
        DBG("ble_enable err "); hal_print_int((int32_t)err);
        DBG(" ram_base="); hal_print_int((int32_t)ram_base); DBG("\r\n");
        return -1;
    }

    /* 5. Device name / appearance / connection params. */
    ble_gap_conn_sec_mode_t nm;
    perm_open(&nm);
    uint8_t nm_buf[32];
    uint8_t nm_len = m_name_len ? m_name_len : 13;
    if (m_name_len) memcpy(nm_buf, m_name, m_name_len);
    else { memcpy(nm_buf, "SECD Keyboard", 13); }
    sd_ble_gap_device_name_set(&nm, nm_buf, nm_len);
    sd_ble_gap_appearance_set(BLE_APPEARANCE_HID_KEYBOARD);

    ble_gap_conn_params_t cp;
    memset(&cp, 0, sizeof(cp));
    cp.min_conn_interval = 12;     /* ~15 ms */
    cp.max_conn_interval = 24;     /* ~30 ms */
    cp.slave_latency = 0;
    cp.conn_sup_timeout = 400;     /* 4 s */
    sd_ble_gap_ppcp_set(&cp);

    /* 6. Build GATT database. */
    err = build_gatt_db();
    if (err) { DBG("gatt_db err "); hal_print_int((int32_t)err); DBG("\r\n"); return -1; }

    /* 7. Security params (JustWorks, no bonding). */
    memset(&m_sec_params, 0, sizeof(m_sec_params));
    m_sec_params.bond = 0;
    m_sec_params.mitm = 0;
    m_sec_params.lesc = 0;
    m_sec_params.keypress = 0;
    m_sec_params.io_caps = BLE_GAP_IO_CAPS_NONE;
    m_sec_params.oob = 0;
    m_sec_params.min_key_size = 7;
    m_sec_params.max_key_size = 16;
    memset(&m_keyset, 0, sizeof(m_keyset));

    /* 8. Start advertising. */
    start_advertising();

    m_inited = true;
    DBG("BLE HID ready\r\n");
    return 0;
}

void hal_ble_set_name(const char *name) {
    uint8_t n = 0;
    if (name) {
        while (name[n] && n < 31) { m_name[n] = (uint8_t)name[n]; n++; }
    }
    m_name_len = n;
    /* If already initialised, push the new name live. */
    if (m_inited && n) {
        ble_gap_conn_sec_mode_t nm;
        perm_open(&nm);
        sd_ble_gap_device_name_set(&nm, m_name, n);
    }
}

int hal_ble_connected(void) {
    hal_ble_poll();
    return (m_conn_handle != BLE_CONN_HANDLE_INVALID) ? 1 : 0;
}

void hal_ble_key_report(uint8_t mods, const uint8_t keys[6]) {
    hal_ble_poll();
    if (m_conn_handle == BLE_CONN_HANDLE_INVALID) return;

    uint8_t report[8];
    report[0] = mods;
    report[1] = 0;
    memcpy(&report[2], keys, 6);

    /* Keep the stored attribute value in sync. */
    ble_gatts_value_t val;
    memset(&val, 0, sizeof(val));
    val.len = sizeof(report);
    val.p_value = report;
    sd_ble_gatts_value_set(m_conn_handle, m_kb_input_val, &val);

    if (!m_input_notify) return;

    uint16_t len = sizeof(report);
    ble_gatts_hvx_params_t hvx;
    memset(&hvx, 0, sizeof(hvx));
    hvx.handle = m_kb_input_val;
    hvx.type = BLE_GATT_HVX_NOTIFICATION;
    hvx.p_len = &len;
    hvx.p_data = report;
    sd_ble_gatts_hvx(m_conn_handle, &hvx);
}

void hal_ble_mouse_report(int8_t dx, int8_t dy, uint8_t btns) {
    (void)dx; (void)dy; (void)btns;
    hal_ble_poll();
    /* No mouse report characteristic in the keyboard profile. */
}
#endif /* SECD_BLE_RADIOTX_TEST */

/* ---- TEMP: direct-RADIO BLE ADV beacon, NO SoftDevice --------------------
 * Bypasses sd_* entirely; drives NRF_RADIO by bare register writes to prove
 * whether the RF front end (die PA + matching network + antenna) can transmit
 * on this unit at all, independent of the S140 (which ZMK also avoids).
 * Build with the SECD_BLE_RADIOTX_TEST guard in main.cpp. Watch a phone
 * scanner for an "SDT" beacon; the LED blinks once per 3-packet channel hop
 * cycle as a live proxy for the RADIO state machine actually executing.
 *
 * BLE 1Mbit air format (PCNF0 = 8-bit preamble, 1-byte S0, 1-byte LENGTH,
 * SKIPADDR=1 so the 4-byte access address we put in BASE0 is consumed and
 * not included in the CRC):
 *   <1B preamble> <4B AA = 0x8E89BED6> <S0 = PDU type> <LENGTH> <LENGTH-byte payload> <3B CRC>
 * PACKETPTR points to { S0, LENGTH, payload... }; the radio inserts preamble,
 * AA, and CRC. */
extern "C" void hal_ble_radio_beacon_test(void) {
    /* HFXO on, directly (the SD is never enabled in this build). On nRF52840
     * HFCLK is always the crystal; there is no HFCLKSRC switch. */
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    while (!(NRF_CLOCK->EVENTS_HFCLKSTARTED & 1u)) {}

    /* ADV_IND PDU, transmitted verbatim: 16-bit BLE header (type 0, len 0x0D)
     * + AdvA + flags + "SDT" name.  0x0D = 13-byte payload (6 + 3 + 4 - 1? no
     * LEN field is total PDU bytes after the 2-byte header, so PDU=AdvA 6 +
     * flags 3 + name 4 = 13, header byte 1 = 0x0D). */
    static uint8_t adv[] = {
        0x00, 0x0D,                                  /* PDU header: ADV_IND, len 13 */
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x11,          /* AdvA (random, doesn't matter) */
        0x02, 0x01, 0x06,                            /* Flags: LE General Discoverable */
        0x04, 0x09, 'S', 'D', 'T'                    /* Short Local Name "SDT" (4 chars) */
    };

    NRF_RADIO->MODE        = RADIO_MODE_MODE_Ble_1Mbit;
    NRF_RADIO->TXPOWER     = RADIO_TXPOWER_TXPOWER_Pos8dBm;
    NRF_RADIO->TXADDRESS   = 0;                      /* logical address 0 -> BASE0/PREFIX0 */
    NRF_RADIO->RXADDRESSES = 1;                      /* only address 0 enabled */
    NRF_RADIO->PREFIX0     = 0u;                     /* not used: BALEN=4 covers full AA */
    NRF_RADIO->BASE0       = 0x8E89BED6u;            /* BLE 1Mbit advertising access address */
    /* BLE 1Mbit on-air frame from the radio's perspective:
     *   <8-bit preamble> <4B AA=0x8E89BED6> <S0=PDU type 1B> <LENGTH 1B> <payload> <3B CRC>
     * The radio reads S0 from PACKETPTR+0, LENGTH from PACKETPTR+1, then
     * LENGTH payload bytes from PACKETPTR+2..  Our adv[] encodes the PDU
     * exactly that way: byte 0 = PDU type (0x00 ADV_IND), byte 1 = LENGTH
     * (0x0D), bytes 2..14 = the 13-byte PDU payload. */
    NRF_RADIO->PCNF0       = (0u << RADIO_PCNF0_PLEN_Pos)              /* 8-bit preamble */
                            | (1u << RADIO_PCNF0_S0LEN_Pos)             /* 1-byte S0 (bit is 0/1) */
                            | (0u << RADIO_PCNF0_S1LEN_Pos)
                            | (8u << RADIO_PCNF0_LFLEN_Pos);            /* 8-bit (1-byte) LENGTH */
    /* BALEN = 4: the full 4-byte access address comes from BASE0 (PREFIX0 is
     * ignored). MAXLEN = 255: any BLE advertising payload fits easily.
     * ENDIAN_Big: BLE transmits each byte MSB-first, matching the air format. */
    NRF_RADIO->PCNF1       = (255u << RADIO_PCNF1_MAXLEN_Pos)
                            | (4u << RADIO_PCNF1_BALEN_Pos)
                            | (1u << RADIO_PCNF1_WHITEEN_Pos)
                            | (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos);
    NRF_RADIO->CRCCNF      = RADIO_CRCCNF_LEN_Three
                            | (1u << RADIO_CRCCNF_SKIPADDR_Pos); /* 3-byte CRC, skip AA */
    NRF_RADIO->CRCPOLY     = 0x0000065Bu;            /* BLE CRC-24 polynomial */
    NRF_RADIO->CRCINIT     = 0x555555u;              /* BLE CRC init value */
    /* SHORTS: [0] READY->START, [1] END->DISABLE, [2] DISABLED->TXEN.
     * After every END, the radio disables, then re-enters TX, READY auto-
     * fires START for the next packet.  We poll EVENTS_END for LED timing. */
    NRF_RADIO->SHORTS      = (1u << RADIO_SHORTS_READY_START_Pos)
                            | (1u << RADIO_SHORTS_END_DISABLE_Pos)
                            | (1u << RADIO_SHORTS_DISABLED_TXEN_Pos);
    NRF_RADIO->PACKETPTR   = (uint32_t)adv;
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->EVENTS_END      = 0;
    NRF_RADIO->TASKS_TXEN  = 1;

    /* ch37=2402 MHz (freq reg 2), ch38=2426 (26), ch39=2480 (80) per nRF52840
     * product spec: FREQUENCY = (2400 + N) for N in [0..100]. */
    const uint32_t chan_freq[3] = { 2u, 26u, 80u };
    uint32_t pkts = 0;
    for (;;) {
        while (!(NRF_RADIO->EVENTS_END & 1u)) {}
        NRF_RADIO->EVENTS_END = 0;
        NRF_RADIO->FREQUENCY   = chan_freq[pkts % 3u];
        /* BLE whitening IV = channel index (37, 38, 39).  Per spec IV starts
         * at the channel number; on the nRF52840 the hardware re-whitens for
         * each new packet only if you re-write DATAWHITEIV. */
        NRF_RADIO->DATAWHITEIV = 37u + (pkts % 3u);
        pkts++;
        if ((pkts & 1u) == 1u) NRF_P0->OUTSET = (1u << 15u);  /* LED blinks once */
        else                   NRF_P0->OUTCLR = (1u << 15u);  /* per packet pair  */
    }
}
