/*
 * SECD Machine USB for nRF52840 - CherryUSB class/core stack on top of
 * TinyUSB's proven nRF5x DCD.
 *
 * The low-level driver is third_party/cherryusb/port/nrf52840/usb_dc_nrf5x.c,
 * which is a direct port of TinyUSB's dcd_nrf5x.c (the driver that enumerates
 * reliably on this silicon). CherryUSB's usbd core + class drivers provide the
 * runtime "Lisp factory" composition: interfaces are chosen by Lisp before
 * %usb-start, the configuration descriptor is composed from that registry, and
 * CherryUSB binds its class drivers to whatever the descriptor carries.
 *
 * Endpoint map (nRF52840: 8 IN incl EP0 / 8 OUT):
 *   CDC0 console : notif 0x82, OUT 0x01, IN 0x81   (interfaces 0,1)
 *   CDC1 extra   : notif 0x84, OUT 0x03, IN 0x83   (interfaces 2,3)
 *   HID0         : IN 0x87                          (interface 4)
 *   HID1         : IN 0x88                          (interface 5)
 */

#include "usb.h"
#include "secd/boot.h"
#include "hal/hal.h"

#include <stdint.h>
#include <string.h>

extern "C" {
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usbd_hid.h"
#include "usb_dc.h"
#include "nrf5x_regs.h"
/* USBD register block (driver-internal header); base per nRF52840 TRM */
#ifndef NRF_USBD
#define NRF_USBD ((NRF_USBD_Type *)0x40027000UL)
#endif
/* provided by the nRF5x DCD port; drives USBD enable + HFCLK + pull-up */
void cherry_usb_hal_nrf_power_event(uint32_t event);
}

#define MAX_CDC 2
#define MAX_HID 2
#define RING_SIZE 256
#define CDC_OUT_MPS 64

/* ---------------- runtime registry ---------------- */
static uint16_t s_vid = 0xFFFE;
static uint16_t s_pid = 0x0001;
static char     s_mfc[128];
static char     s_product[128];
static char     s_serial[128];
uint16_t        g_secd_str_len[4] = { 2, 0, 0, 0 };

static uint8_t  s_cdc_count = 1;   /* console is instance 0 */
static uint8_t  s_hid_count = 0;
static volatile bool cdc_dtr[MAX_CDC];
static bool     s_started = false;
static bool     s_configured = false;

/* ---------------- console RX rings (per CDC port) ---------------- */
static volatile uint16_t cdc_rx_head[MAX_CDC], cdc_rx_tail[MAX_CDC];
static uint8_t cdc_rx_ring[MAX_CDC][RING_SIZE];
static uint8_t cdc_rx_dma[MAX_CDC][CDC_OUT_MPS] __attribute__((aligned(4)));   /* OUT read buffer */
static bool    cdc_in_busy[MAX_CDC];

/* ---------------- console TX buffers (per CDC port) ----------------
 * The nRF USBD EasyDMA can only source from RAM and requires 4-byte-aligned
 * buffers, so we stage outgoing bytes here and stream them in 64-byte packets. */
#define CDC_TX_MPS 64
#define CDC_TX_BUF 512
static uint8_t  cdc_tx_buf[MAX_CDC][CDC_TX_BUF] __attribute__((aligned(4)));
static volatile uint16_t cdc_tx_head[MAX_CDC];   /* bytes queued */
static volatile uint16_t cdc_tx_tail[MAX_CDC];   /* bytes sent   */
static uint16_t          cdc_last_len[MAX_CDC];  /* bytes sent per IN kick */
/* 4-byte-aligned staging buffer: nRF USBD EasyDMA requires aligned source and
 * cannot cross a wrap boundary, so we copy each outgoing chunk here first. */
static uint8_t  cdc_tx_scratch[MAX_CDC][CDC_TX_MPS] __attribute__((aligned(4)));
/* Observable transmit state for LED diagnostics (set in tx kick / IN cb). */
static volatile bool g_tx_primed = false;   /* usbd_ep_start_write returned 0 */
static volatile bool g_tx_done   = false;   /* IN transfer completed (cdc_in_cb) */

bool secd_tx_primed(void) { return g_tx_primed; }
bool secd_tx_done(void)   { return g_tx_done; }

static void cdc_tx_kick(uint8_t port)
{
    if (cdc_in_busy[port]) return;
    uint16_t pending = (uint16_t)(cdc_tx_head[port] - cdc_tx_tail[port]);
    if (pending == 0) return;
    uint16_t off  = (uint16_t)(cdc_tx_tail[port] % CDC_TX_BUF);
    uint16_t n    = pending > CDC_TX_MPS ? CDC_TX_MPS : pending;
    if (n > CDC_TX_BUF - off) n = (uint16_t)(CDC_TX_BUF - off);  /* don't cross wrap */
    /* Stage into the aligned scratch buffer (handles alignment + wrap). */
    for (uint16_t i = 0; i < n; i++)
        cdc_tx_scratch[port][i] = cdc_tx_buf[port][(uint16_t)(off + i)];
    cdc_in_busy[port] = true;
    cdc_last_len[port] = n;
    int ret = usbd_ep_start_write(0, (uint8_t)(0x81 + 2 * port), cdc_tx_scratch[port], n);
    if (ret == 0) {
        g_tx_primed = true;
    } else {
        /* Endpoint not ready yet (e.g. right after CONFIGURED): release the
         * busy flag so the TX loop below can retry the same buffered bytes. */
        cdc_in_busy[port] = false;
    }
}

/* ---------------- HID tap state ---------------- */
static bool    hid_in_busy[MAX_HID];

/* ---------------- descriptor buffers ---------------- */
static uint8_t device_desc[18];

#define CDC_BLOCK_LEN 65
#define HID_BLOCK_LEN 25
static uint8_t config_buf[9 + MAX_CDC * CDC_BLOCK_LEN + MAX_HID * HID_BLOCK_LEN];

static const uint8_t hid_kbd_report_desc[63] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07,
    0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
    0x75, 0x08, 0x81, 0x03, 0x95, 0x05, 0x75, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x03, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
    0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0 };

/* ---------------- interface + endpoint registration ---------------- */
static struct usbd_interface cdc_intf[2 * MAX_CDC];
static struct usbd_interface hid_intf[MAX_HID];
static struct usbd_endpoint  cdc_out_ep[MAX_CDC];
static struct usbd_endpoint  cdc_in_ep[MAX_CDC];
static struct usbd_endpoint  cdc_notif_ep[MAX_CDC];
static struct usbd_endpoint  hid_in_ep[MAX_HID];

static struct usb_descriptor g_secd_desc;

static void set_default_utf16(char *dst, uint16_t *len_slot, const char *ascii)
{
    uint16_t n = 0;
    for (const char *p = ascii; *p && n + 1 < 127u; p++) {
        dst[n++] = *p;
        dst[n++] = 0;
    }
    *len_slot = n;
}

static uint8_t *desc_device_cb(uint8_t speed)
{
    (void)speed;
    device_desc[0]  = 0x12;
    device_desc[1]  = 0x01;
    device_desc[2]  = 0x00; device_desc[3]  = 0x02;
    device_desc[4]  = 0xEF;                       /* IAD composite */
    device_desc[5]  = 0x02; device_desc[6]  = 0x01;
    device_desc[7]  = 0x40;
    device_desc[8]  = (uint8_t)(s_vid & 0xFF);
    device_desc[9]  = (uint8_t)(s_vid >> 8);
    device_desc[10] = (uint8_t)(s_pid & 0xFF);
    device_desc[11] = (uint8_t)(s_pid >> 8);
    device_desc[12] = 0x00; device_desc[13] = 0x01;
    device_desc[14] = 0x01; device_desc[15] = 0x02;
    device_desc[16] = 0x03; device_desc[17] = 0x01;
    return device_desc;
}

static uint8_t *desc_config_cb(uint8_t speed)
{
    (void)speed;
    uint8_t *p = config_buf;
    uint8_t itf = 0;
    uint8_t total = 9;

    *p++ = 0x09; *p++ = 0x02;
    p += 7;

    for (uint8_t c = 0; c < s_cdc_count; c++) {
        const uint8_t notif = (uint8_t)(0x82 + 2 * c);
        const uint8_t bout  = (uint8_t)(0x01 + 2 * c);
        const uint8_t bin   = (uint8_t)(0x81 + 2 * c);
        *p++ = 0x08; *p++ = 0x0B; *p++ = itf; *p++ = 0x02; *p++ = 0x02; *p++ = 0x02; *p++ = 0x01; *p++ = 0x00;
        *p++ = 0x09; *p++ = 0x04; *p++ = itf++; *p++ = 0x00; *p++ = 0x01; *p++ = 0x02; *p++ = 0x02; *p++ = 0x01; *p++ = 0x00;
        *p++ = 0x05; *p++ = 0x24; *p++ = 0x00; *p++ = 0x10; *p++ = 0x01;
        *p++ = 0x04; *p++ = 0x24; *p++ = 0x01; *p++ = 0x00;
        *p++ = 0x04; *p++ = 0x24; *p++ = 0x02; *p++ = 0x06;
        *p++ = 0x05; *p++ = 0x24; *p++ = 0x06; *p++ = (uint8_t)(c * 2); *p++ = (uint8_t)(c * 2 + 1);
        *p++ = 0x07; *p++ = 0x05; *p++ = notif; *p++ = 0x03; *p++ = 0x08; *p++ = 0x00; *p++ = 0x10;
        *p++ = 0x09; *p++ = 0x04; *p++ = itf++; *p++ = 0x00; *p++ = 0x02; *p++ = 0x0A; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
        *p++ = 0x07; *p++ = 0x05; *p++ = bout; *p++ = 0x02; *p++ = 64; *p++ = 0x00; *p++ = 0x00;
        *p++ = 0x07; *p++ = 0x05; *p++ = bin;  *p++ = 0x02; *p++ = 64; *p++ = 0x00; *p++ = 0x00;
        total += CDC_BLOCK_LEN;
    }

    for (uint8_t h = 0; h < s_hid_count; h++) {
        *p++ = 0x09; *p++ = 0x04; *p++ = itf++; *p++ = 0x00; *p++ = 0x01; *p++ = 0x03; *p++ = 0x01; *p++ = 0x01; *p++ = 0x00;
        *p++ = 0x09; *p++ = 0x21; *p++ = 0x11; *p++ = 0x01; *p++ = 0x00; *p++ = 0x01; *p++ = 0x22; *p++ = 63; *p++ = 0x00;
        *p++ = 0x07; *p++ = 0x05; *p++ = (uint8_t)(0x87 + h); *p++ = 0x03; *p++ = 64; *p++ = 0x00; *p++ = 0x01;
        total += HID_BLOCK_LEN;
    }

    config_buf[2] = total;
    config_buf[3] = 0x00;
    config_buf[4] = itf;
    config_buf[5] = 0x01;     /* bConfigurationValue (must be non-zero) */
    config_buf[6] = 0x80;     /* bmAttributes: bus-powered */
    config_buf[7] = 0x32;     /* bMaxPower: 100 mA (0x32 * 2 mA) */
    return config_buf;
}

static const uint8_t langid_bytes[2] = { 0x09, 0x04 };  /* wLangID 0x0409 */

static const char *desc_string_cb(uint8_t speed, uint8_t index)
{
    (void)speed;
    if (index == 0) return (const char *)langid_bytes;
    const char *src = index == 1 ? s_mfc : index == 2 ? s_product
                     : index == 3 ? s_serial : NULL;
    return src;   /* UTF-16LE bytes; length via g_secd_str_len[index] */
}

/* ---------------- endpoint callbacks ---------------- */
static void cdc_out_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    uint8_t port = (uint8_t)(((ep & 0x7f) - 1) >> 1);
    if (port >= MAX_CDC) return;
    for (uint32_t i = 0; i < nbytes; i++) {
        uint16_t next = (uint16_t)((cdc_rx_head[port] + 1) % RING_SIZE);
        if (next == cdc_rx_tail[port]) break;   /* full: drop */
        cdc_rx_ring[port][cdc_rx_head[port]] = cdc_rx_dma[port][i];
        cdc_rx_head[port] = next;
    }
    usbd_ep_start_read(0, ep, cdc_rx_dma[port], CDC_OUT_MPS);  /* re-arm */
}

static void cdc_in_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid; (void)nbytes;
    uint8_t port = (uint8_t)(((ep & 0x7f) - 1) >> 1);
    if (port >= MAX_CDC) return;
    /* Advance by the length we actually submitted (the DCD reports nbytes=0
     * on IN completion here, so trusting it would never drain the buffer and
     * would re-kick the same bytes forever). */
    cdc_tx_tail[port] = (uint16_t)(cdc_tx_tail[port] + cdc_last_len[port]);
    g_tx_done = true;
    if (cdc_tx_tail[port] >= cdc_tx_head[port]) {
        cdc_tx_tail[port] = cdc_tx_head[port] = 0;   /* drained */
        cdc_in_busy[port] = false;
    } else {
        cdc_in_busy[port] = false;
        cdc_tx_kick(port);   /* send next packet */
    }
}

static void hid_in_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid; (void)nbytes;
    uint8_t port = (uint8_t)((ep & 0x7f) - 7);
    if (port < MAX_HID) hid_in_busy[port] = false;
}

/* ---------------- usbd event handler ---------------- */
static void secd_usbd_event_handler(uint8_t busid, uint8_t event)
{
    (void)busid;
    if (event == USBD_EVENT_CONFIGURED) {
        s_configured = true;
        g_tx_done = false;
        for (uint8_t c = 0; c < s_cdc_count; c++)
            usbd_ep_start_read(0, (uint8_t)(0x01 + 2 * c), cdc_rx_dma[c], CDC_OUT_MPS);
    } else if (event == USBD_EVENT_RESET || event == USBD_EVENT_DISCONNECTED) {
        s_configured = false;
    }
}

/* ---------------- DTR tracking (override weak) ---------------- */
 void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    if (intf < 2 * MAX_CDC)
        cdc_dtr[intf >> 1] = dtr;
}

/* ---------------- NVIC enable/disable (TinyUSB's tud_init did this) ---------------- */
#define USBD_IRQn 39
static void enable_usbd_irq(void)
{
    volatile uint8_t *ipr = (volatile uint8_t *)0xE000E400;
    ipr[USBD_IRQn] = 0x20;   /* preempt priority 2 */
    volatile uint32_t *iser = (volatile uint32_t *)0xE000E100;
    iser[USBD_IRQn >> 5] |= (1u << (USBD_IRQn & 0x1F));
}
static void disable_usbd_irq(void)
{
    volatile uint32_t *icer = (volatile uint32_t *)0xE000E180;
    icer[USBD_IRQn >> 5] = (1u << (USBD_IRQn & 0x1F));
}

/* ===================================================================== */
void secd_usb_init(void)
{
    /* Allow re-composition: when a Lisp program calls %usb-init after the
     * firmware already brought up the boot console, tear the device down and
     * start fresh so %usb-hid-add / %usb-start can add HID and re-enumerate.
     * (The firmware no longer de-inits the console itself.) */
    if (s_started)
        secd_usb_deinit();
    s_cdc_count = 1;
    s_hid_count = 0;
    s_started = false;
    s_configured = false;
    for (int i = 0; i < MAX_CDC; i++) {
        cdc_dtr[i] = false; cdc_in_busy[i] = false;
        cdc_rx_head[i] = cdc_rx_tail[i] = 0;
    }
    for (int i = 0; i < MAX_HID; i++) hid_in_busy[i] = false;
    set_default_utf16(s_mfc,     &g_secd_str_len[1], "SECD");
    set_default_utf16(s_product, &g_secd_str_len[2], "SECD Machine");
    set_default_utf16(s_serial,  &g_secd_str_len[3], "000000000001");
}

 void secd_usb_task(void)
 {
     if (!s_started) return;
     /* drive the power/connect state machine (replaces TinyUSB tud_task polling) */
     cherry_usb_hal_nrf_power_event(0);
     cherry_usb_hal_nrf_power_event(2);

     /* Drain any buffered console output once the IN endpoint is free. */
     if (cdc_tx_head[0] != cdc_tx_tail[0] && !cdc_in_busy[0])
         cdc_tx_kick(0);
 }

int secd_usb_serial_add(void)
{
    if (s_started || s_cdc_count >= MAX_CDC) return -1;
    return (int)(s_cdc_count++ - 1);   /* extra port index (>=1) */
}

int secd_usb_hid_add(void)
{
    if (s_started || s_hid_count >= MAX_HID) return -1;
    return (int)(s_hid_count++);        /* instance index 0/1 */
}

void secd_usb_start(void)
{
    if (s_started) return;
    s_started = true;

    set_default_utf16(s_mfc,     &g_secd_str_len[1], "SECD");
    set_default_utf16(s_product, &g_secd_str_len[2], "SECD Machine");
    set_default_utf16(s_serial,  &g_secd_str_len[3], "000000000001");

    g_secd_desc.device_descriptor_callback  = desc_device_cb;
    g_secd_desc.config_descriptor_callback  = desc_config_cb;
    g_secd_desc.string_descriptor_callback  = desc_string_cb;
    usbd_desc_register(0, &g_secd_desc);

    for (uint8_t c = 0; c < s_cdc_count; c++) {
        memset(&cdc_intf[2 * c],     0, sizeof(struct usbd_interface));
        memset(&cdc_intf[2 * c + 1], 0, sizeof(struct usbd_interface));
        usbd_cdc_acm_init_intf(0, &cdc_intf[2 * c]);   /* comm iface gets CDC handler */
    }
    for (uint8_t h = 0; h < s_hid_count; h++) {
        memset(&hid_intf[h], 0, sizeof(struct usbd_interface));
        usbd_hid_init_intf(0, &hid_intf[h], hid_kbd_report_desc,
                           (uint32_t)sizeof(hid_kbd_report_desc));
    }

    /* register interfaces in descriptor order so intf_num == bInterfaceNumber */
    for (uint8_t c = 0; c < s_cdc_count; c++) {
        usbd_add_interface(0, &cdc_intf[2 * c]);
        usbd_add_interface(0, &cdc_intf[2 * c + 1]);
    }
    for (uint8_t h = 0; h < s_hid_count; h++)
        usbd_add_interface(0, &hid_intf[h]);

    /* register endpoint callbacks (core opens endpoints from descriptor) */
    for (uint8_t c = 0; c < s_cdc_count; c++) {
        cdc_out_ep[c].ep_addr = (uint8_t)(0x01 + 2 * c); cdc_out_ep[c].ep_cb = cdc_out_cb;
        usbd_add_endpoint(0, &cdc_out_ep[c]);
        cdc_in_ep[c].ep_addr  = (uint8_t)(0x81 + 2 * c); cdc_in_ep[c].ep_cb  = cdc_in_cb;
        usbd_add_endpoint(0, &cdc_in_ep[c]);
        cdc_notif_ep[c].ep_addr = (uint8_t)(0x82 + 2 * c); cdc_notif_ep[c].ep_cb = NULL;
        usbd_add_endpoint(0, &cdc_notif_ep[c]);
    }
    for (uint8_t h = 0; h < s_hid_count; h++) {
        hid_in_ep[h].ep_addr = (uint8_t)(0x87 + h); hid_in_ep[h].ep_cb = hid_in_cb;
        usbd_add_endpoint(0, &hid_in_ep[h]);
    }

    /* Init CherryUSB core + DC while USBD peripheral is still off.
     * usb_dc_init() zeroes endpoint state and sets INTEN — this must happen
     * before the peripheral is enabled so we don't corrupt a mid-flight
     * enumeration triggered by the pull-up. */
    usbd_initialize(0, 0, secd_usbd_event_handler);

    /* Force a CLEAN USB disconnect (pull-up off + peripheral disable) and hold it
     * for a few tens of ms, so the host reliably detects a disconnect and re-enumerates
     * after any kind of reset (BMP soft-reset, bootloader->app jump, brown-out). Without
     * this the D+ line can stay high and Linux keeps talking to a dead device address.
     *
     * HFCLK is started and waited for INSIDE the DETECTED handler so USBD has a
     * stable clock when it activates — this is the fix for intermittent boot failures. */
    NRF_USBD->USBPULLUP = 0;
    NRF_USBD->ENABLE = 0;
    for (volatile uint32_t i = 0; i < 3000000; i++) { /* ~50ms @ 64MHz */ }
    cherry_usb_hal_nrf_power_event(0);  /* DETECTED -> start HFCLK, enable USBD */
    cherry_usb_hal_nrf_power_event(2);  /* READY    -> wait EVENTCAUSE, set pull-up */
    enable_usbd_irq();
}

void secd_usb_deinit(void)
{
    if (!s_started) return;
    disable_usbd_irq();         /* stop ISR before tearing down state */
    for (volatile uint32_t i = 0; i < 100000; i++) {}  /* ~1.5ms let any in-flight ISR finish */
    usbd_deinitialize(0);
    s_started = false;
    s_configured = false;
}

void secd_usb_set_vid(uint16_t vid) { s_vid = vid; }
void secd_usb_set_pid(uint16_t pid) { s_pid = pid; }

void secd_usb_set_manufacturer(const uint8_t *data, uint16_t len)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < len && n + 1 < 127u; i++) { s_mfc[n++] = (char)data[i]; s_mfc[n++] = 0; }
    g_secd_str_len[1] = n;
}
void secd_usb_set_product(const uint8_t *data, uint16_t len)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < len && n + 1 < 127u; i++) { s_product[n++] = (char)data[i]; s_product[n++] = 0; }
    g_secd_str_len[2] = n;
}
void secd_usb_set_serial(const uint8_t *data, uint16_t len)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < len && n + 1 < 127u; i++) { s_serial[n++] = (char)data[i]; s_serial[n++] = 0; }
    g_secd_str_len[3] = n;
}

bool secd_usb_configured(void) { return s_started && s_configured; }

bool secd_console_ready(void) { return s_started && s_configured && cdc_dtr[0]; }

size_t secd_console_write(const uint8_t *data, size_t len)
{
    if (!secd_usb_configured()) return 0;
    size_t added = 0;
    while (added < len) {
        uint16_t used = (uint16_t)(cdc_tx_head[0] - cdc_tx_tail[0]);
        if (used >= CDC_TX_BUF) break;   /* buffer full: drop rest */
        cdc_tx_buf[0][cdc_tx_head[0] % CDC_TX_BUF] = data[added++];
        cdc_tx_head[0]++;
    }
    cdc_tx_kick(0);
    return added;
}

void secd_console_write_char(char c) { secd_console_write((const uint8_t *)&c, 1); }

int secd_console_read(void)
{
    if (cdc_rx_head[0] == cdc_rx_tail[0]) return -1;
    uint8_t c = cdc_rx_ring[0][cdc_rx_tail[0]];
    cdc_rx_tail[0] = (uint16_t)((cdc_rx_tail[0] + 1) % RING_SIZE);
    return (int)c;
}

int secd_console_available(void)
{
    return (int)((cdc_rx_head[0] + RING_SIZE - cdc_rx_tail[0]) % RING_SIZE);
}

size_t secd_serial_write(int port, const uint8_t *data, size_t len)
{
    if (port < 1 || port >= (int)s_cdc_count) return 0;
    if (!secd_usb_configured()) return 0;
    size_t added = 0;
    while (added < len) {
        uint16_t used = (uint16_t)(cdc_tx_head[port] - cdc_tx_tail[port]);
        if (used >= CDC_TX_BUF) break;
        cdc_tx_buf[port][cdc_tx_head[port] % CDC_TX_BUF] = data[added++];
        cdc_tx_head[port]++;
    }
    cdc_tx_kick((uint8_t)port);
    return added;
}

int secd_serial_read(int port)
{
    if (port < 1 || port >= (int)s_cdc_count) return -1;
    if (cdc_rx_head[port] == cdc_rx_tail[port]) return -1;
    uint8_t c = cdc_rx_ring[port][cdc_rx_tail[port]];
    cdc_rx_tail[port] = (uint16_t)((cdc_rx_tail[port] + 1) % RING_SIZE);
    return (int)c;
}

int secd_serial_available(int port)
{
    if (port < 1 || port >= (int)s_cdc_count) return 0;
    return (int)((cdc_rx_head[port] + RING_SIZE - cdc_rx_tail[port]) % RING_SIZE);
}

void secd_hid_keyboard_tap_port(int hid_port, uint8_t modifier, uint8_t usage)
{
    if (hid_port < 0 || hid_port >= (int)s_hid_count) return;
    if (!secd_usb_configured()) return;
    uint8_t rep[8] = { modifier, 0, usage, 0, 0, 0, 0, 0 };
    uint8_t ep = (uint8_t)(0x87 + hid_port);
    for (int phase = 0; phase < 2; phase++) {
        uint32_t guard = 0;
        while (hid_in_busy[hid_port]) {
            cherry_usb_hal_nrf_power_event(0); cherry_usb_hal_nrf_power_event(2);
            if (++guard > 2000000u) break;
        }
        hid_in_busy[hid_port] = true;
        usbd_ep_start_write(0, ep, rep, 8);
        guard = 0;
        while (hid_in_busy[hid_port]) {
            cherry_usb_hal_nrf_power_event(0); cherry_usb_hal_nrf_power_event(2);
            if (++guard > 2000000u) break;
        }
        rep[0] = 0; rep[2] = 0;   /* release */
    }
}

void secd_hid_keyboard_tap(uint8_t modifier, uint8_t usage)
{
    secd_hid_keyboard_tap_port(0, modifier, usage);
}

int secd_usb_mouse_add(void) { return -1; }
int secd_hid_mouse_send(int8_t dx, int8_t dy, uint8_t buttons, int8_t wheel)
{
    (void)dx; (void)dy; (void)buttons; (void)wheel;
    return -1;
}
