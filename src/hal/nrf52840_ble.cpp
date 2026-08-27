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
    /* Complete local name */
    uint8_t nlen = m_name_len;
    if (nlen > (31 - idx - 2)) nlen = (31 - idx - 2);
    if (nlen > 0) {
        adv_data[idx++] = nlen + 1;
        adv_data[idx++] = BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME;
        memcpy(&adv_data[idx], m_name, nlen);
        idx += nlen;
    }

    ble_gap_adv_data_t adv;
    memset(&adv, 0, sizeof(adv));
    adv.adv_data.p_data = adv_data;
    adv.adv_data.len = idx;

    ble_gap_adv_params_t params;
    memset(&params, 0, sizeof(params));
    params.properties.type = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED;
    params.primary_phy = BLE_GAP_PHY_1MBPS;
    params.secondary_phy = BLE_GAP_PHY_1MBPS;
    params.interval = 160;                 /* ~100 ms */
    params.duration = 0;                   /* no timeout */

    uint32_t err = sd_ble_gap_adv_set_configure(&m_adv_handle, &adv, &params);
    if (err) { DBG("adv_cfg err "); hal_print_int((int32_t)err); DBG("\r\n"); return; }

    err = sd_ble_gap_adv_start(m_adv_handle, BLE_CONN_CFG_TAG_DEFAULT);
    if (err) { DBG("adv_start err "); hal_print_int((int32_t)err); DBG("\r\n"); return; }
    m_adv_running = true;
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
/* Enable the SoftDevice at boot. Called once from main(), before the USB
 * stack is brought up. The SD owns the vector table and the bottom of RAM, and
 * forwards app-owned IRQs (USBD, UARTE) to our handlers once we point it at our
 * vector table. The BLE GATT stack itself is brought up lazily later, by
 * hal_ble_init() (driven from Lisp's %ble-init). */
extern "C" void ble_sd_enable(void) {
    static bool sd_on = false;
    if (sd_on) return;

    nrf_clock_lf_cfg_t lfcfg = {
        .source = 0,            /* NRF_CLOCK_LFCLK_RC */
        .rc_ctiv = 16,
        .rc_temp_ctiv = 2,
        .accuracy = 1           /* NRF_CLOCK_LF_ACCURACY_500_PPM */
    };
    uint32_t err = sd_softdevice_enable(&lfcfg, NULL);
    if (err) { DBG("sd_enable err "); hal_print_int((int32_t)err); DBG("\r\n"); }

    /* Point the SD at our vector table so USBD/UARTE IRQs reach our handlers. */
    err = sd_softdevice_vector_table_base_set((uint32_t)SECD_APP_BASE);
    if (err) { DBG("vtbase err "); hal_print_int((int32_t)err); DBG("\r\n"); }

    sd_on = true;
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
