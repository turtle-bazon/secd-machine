/*
 * SECD USB device for RP2040 (CherryUSB).
 *
 * Composite device whose interface set is built from Lisp at runtime
 * (Lisp "factory" model). USB enumeration is started only once secd_usb_start
 * (Lisp: %usb-start) runs, so all interfaces must be added beforehand:
 *     %usb-init          -> secd_usb_init():      register descriptor + console
 *     %usb-serial-add    -> secd_usb_serial_add():add a CDC-ACM serial port
 *     %usb-hid-add       -> secd_usb_hid_add():   add the HID keyboard
 *     %usb-start         -> secd_usb_start():     freeze + enumerate
 *
 * The CDC-ACM console (SECD print/format, port 0) is always present. Up to
 * SECD_USB_MAX_PORTS-1 further Lisp serial consoles can be added. The config
 * descriptor is built once at secd_usb_start() from the current registry and
 * stored (USB cannot add interfaces after enumeration).
 */

#include "usb.h"

extern "C" {
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usbd_hid.h"
}

#include <string.h>

/* Total CDC-ACM ports: 1 console + (SECD_USB_MAX_PORTS-1) Lisp serials. */
#ifndef SECD_USB_MAX_PORTS
#define SECD_USB_MAX_PORTS 6
#endif

#define USBD_VID   0xFFFF
#define USBD_PID   0x0001

/* Device identity, settable from Lisp via secd_usb_set_* (called before
 * %usb-start). Defaults match the historical "ffff:0001 SECD SECD Machine"
 * so behavior is unchanged until a Lisp program overrides them. */
static uint16_t s_vid = USBD_VID;
static uint16_t s_pid = USBD_PID;
/* Identity string buffers hold pre-encoded UTF-16LE bytes (from the Lisp
 * to-c-string helper); g_secd_str_len carries the byte length because
 * UTF-16LE contains embedded NUL bytes that strlen would truncate.
 * Indices match the USB string descriptor indices: 1=mfc, 2=product,
 * 3=serial. Consumed by CherryUSB's usbd_core when serving descriptors. */
static char  s_mfc[128];
static char  s_product[128];
static char  s_serial[128];
uint16_t     g_secd_str_len[4] = { 2, 0, 0, 0 };   /* [0]=langid length */

/* Fill DST (a UTF-16LE buffer) from an ASCII default and return its length. */
static uint16_t set_default_utf16(char *dst, const char *ascii)
{
    uint16_t n = 0;
    for (const char *p = ascii; *p && n + 1 < (int)sizeof(s_mfc); p++) {
        dst[n++] = *p;       /* low byte */
        dst[n++] = 0;        /* high byte (ASCII fits in one UTF-16 unit) */
    }
    return n;
}
static uint8_t device_descriptor[18];

#define USBD_MAX_POWER 100
#define CDC_MAX_MPS 64

#define HID_EP_SIZE     8
#define HID_EP_INTERVAL 1
#define HID_KEYBOARD_REPORT_DESC_SIZE 63

#define RING_SIZE 256
#define CDC_RX_BUF     64

/* Reserved HID IN endpoint; CDC data IN use odd 0x8x, notif even 0x8x. */
#define HID_EP 0x8F

static const uint8_t cdc_ep_out[SECD_USB_MAX_PORTS] = {0x01, 0x03, 0x05, 0x07, 0x09, 0x0B};
static const uint8_t cdc_ep_in[SECD_USB_MAX_PORTS]  = {0x81, 0x83, 0x85, 0x87, 0x89, 0x8B};
static const uint8_t cdc_ep_not[SECD_USB_MAX_PORTS] = {0x82, 0x84, 0x86, 0x88, 0x8A, 0x8C};

/* ------------------------------------------------------------- per-port state */
struct cdc_port {
    volatile bool   used;
    volatile bool   tx_busy;
    volatile uint8_t dtr;
    volatile uint16_t head, tail;
    uint8_t tx_buf[CDC_MAX_MPS];
    uint8_t rx_buf[CDC_RX_BUF];
    uint8_t ring[RING_SIZE];
};

static struct cdc_port cdc[SECD_USB_MAX_PORTS];
static struct usbd_interface  intfs[2 * SECD_USB_MAX_PORTS + 1];
static struct usbd_endpoint   ep_out[SECD_USB_MAX_PORTS], ep_in[SECD_USB_MAX_PORTS];
static struct usbd_endpoint   hid_ep;

static volatile uint8_t s_ports = 1;      /* count of used CDC ports (0 = console) */
static volatile bool    s_hid = false;
static volatile bool    s_started = false;

/* ---------------------------------------------------------------- descriptors */
/* device_descriptor[] is filled by device_descriptor_cb() from s_vid/s_pid. */

/* One CDC-ACM bridge descriptor per supported port (66 bytes each). */
static const uint8_t acm_desc[SECD_USB_MAX_PORTS][CDC_ACM_DESCRIPTOR_LEN] = {
    CDC_ACM_DESCRIPTOR_INIT(0, cdc_ep_not[0], cdc_ep_out[0], cdc_ep_in[0], CDC_MAX_MPS, 0),
    CDC_ACM_DESCRIPTOR_INIT(2, cdc_ep_not[1], cdc_ep_out[1], cdc_ep_in[1], CDC_MAX_MPS, 0),
    CDC_ACM_DESCRIPTOR_INIT(4, cdc_ep_not[2], cdc_ep_out[2], cdc_ep_in[2], CDC_MAX_MPS, 0),
    CDC_ACM_DESCRIPTOR_INIT(6, cdc_ep_not[3], cdc_ep_out[3], cdc_ep_in[3], CDC_MAX_MPS, 0),
    CDC_ACM_DESCRIPTOR_INIT(8, cdc_ep_not[4], cdc_ep_out[4], cdc_ep_in[4], CDC_MAX_MPS, 0),
    CDC_ACM_DESCRIPTOR_INIT(10, cdc_ep_not[5], cdc_ep_out[5], cdc_ep_in[5], CDC_MAX_MPS, 0),
};

static const uint8_t hid_kbd_report_desc[HID_KEYBOARD_REPORT_DESC_SIZE] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07,
    0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
    0x75, 0x08, 0x81, 0x03, 0x95, 0x05, 0x75, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x03, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
    0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0 };

static const uint8_t hid_kbd_desc[HID_KEYBOARD_DESCRIPTOR_LEN] = {
    HID_KEYBOARD_DESCRIPTOR_INIT(0, 0x01, HID_KEYBOARD_REPORT_DESC_SIZE,
                                 HID_EP, HID_EP_SIZE, HID_EP_INTERVAL)};

static const char *string_descriptors[] = {
    (const char[]){0x09, 0x04}, /* Langid */
    s_mfc,                      /* Manufacturer (settable from Lisp) */
    s_product,                  /* Product (settable from Lisp) */
    s_serial,                   /* Serial (settable from Lisp) */
};

static uint8_t config_buf[9 + CDC_ACM_DESCRIPTOR_LEN * SECD_USB_MAX_PORTS +
                          HID_KEYBOARD_DESCRIPTOR_LEN];

/* ------------------------------------------------------------- config build */
static uint16_t build_config_descriptor(void)
{
    uint8_t *p = config_buf;
    uint8_t n = (uint8_t)s_ports;              /* used CDC ports */
    uint16_t total = 9 + CDC_ACM_DESCRIPTOR_LEN * n + (s_hid ? 25 : 0);
    uint8_t ifaces = (uint8_t)(2 * n + (s_hid ? 1 : 0));

    *p++ = 9;                 /* bLength */
    *p++ = 0x02;              /* bDescriptorType: CONFIGURATION */
    *p++ = (uint8_t)(total);  *p++ = (uint8_t)(total >> 8);
    *p++ = ifaces;            /* bNumInterfaces */
    *p++ = 0x01;              /* bConfigurationValue */
    *p++ = 0x00;              /* iConfiguration */
    *p++ = 0x80;              /* bmAttributes: bus powered */
    *p++ = (uint8_t)USBD_MAX_POWER;

    for (uint8_t i = 0; i < n; i++) {
        memcpy(p, acm_desc[i], CDC_ACM_DESCRIPTOR_LEN);
        p += CDC_ACM_DESCRIPTOR_LEN;
    }
    if (s_hid) {
        memcpy(p, hid_kbd_desc, sizeof(hid_kbd_desc));
        p[2] = (uint8_t)(2 * n);   /* HID interface number follows the ACMs */
        p += sizeof(hid_kbd_desc);
    }
    return (uint8_t)(p - config_buf);
}

/* ----------------------------------------------------------- USB callbacks */
static const uint8_t *device_descriptor_cb(uint8_t speed)
{
    (void)speed;
    device_descriptor[0]  = 0x12;                                /* bLength */
    device_descriptor[1]  = USB_DESCRIPTOR_TYPE_DEVICE;         /* bDescriptorType */
    device_descriptor[2]  = 0x00; device_descriptor[3]  = 0x02; /* bcdUSB 0x0200 */
    device_descriptor[4]  = 0x00;                               /* bDeviceClass */
    device_descriptor[5]  = 0x00;                               /* bDeviceSubClass */
    device_descriptor[6]  = 0x00;                               /* bDeviceProtocol */
    device_descriptor[7]  = 0x40;                               /* bMaxPacketSize0 */
    device_descriptor[8]  = (uint8_t)(s_vid & 0xFF);
    device_descriptor[9]  = (uint8_t)(s_vid >> 8);
    device_descriptor[10] = (uint8_t)(s_pid & 0xFF);
    device_descriptor[11] = (uint8_t)(s_pid >> 8);
    device_descriptor[12] = 0x00; device_descriptor[13] = 0x01; /* bcdDevice 0x0100 */
    device_descriptor[14] = USB_STRING_MFC_INDEX;
    device_descriptor[15] = USB_STRING_PRODUCT_INDEX;
    device_descriptor[16] = USB_STRING_SERIAL_INDEX;
    device_descriptor[17] = 0x01;                               /* bNumConfigurations */
    return device_descriptor;
}
static const uint8_t *config_descriptor_cb(uint8_t speed) { (void)speed; return config_buf; }
static const char *string_descriptor_cb(uint8_t speed, uint8_t index)
{
    (void)speed;
    if (index >= (sizeof(string_descriptors) / sizeof(char *))) return NULL;
    return string_descriptors[index];
}
static const uint8_t *device_quality_cb(uint8_t speed) { (void)speed; return NULL; }

static const struct usb_descriptor secd_descriptor = {
    .device_descriptor_callback         = device_descriptor_cb,
    .config_descriptor_callback         = config_descriptor_cb,
    .device_quality_descriptor_callback = device_quality_cb,
    .string_descriptor_callback         = string_descriptor_cb,
};

/* ------------------------------------------------------- endpoint plumbing */
static int port_from_out_ep(uint8_t ep)
{
    for (int i = 0; i < (int)s_ports; i++) if (cdc_ep_out[i] == ep) return i;
    return -1;
}
static int port_from_in_ep(uint8_t ep)
{
    for (int i = 0; i < (int)s_ports; i++) if (cdc_ep_in[i] == ep) return i;
    return -1;
}

static void cdc_out_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    int i = port_from_out_ep(ep);
    if (i < 0) return;
    for (uint32_t k = 0; k < nbytes; k++) {
        uint16_t next = (uint16_t)((cdc[i].head + 1) % RING_SIZE);
        if (next == cdc[i].tail) break;
        cdc[i].ring[cdc[i].head] = cdc[i].rx_buf[k];
        cdc[i].head = next;
    }
    usbd_ep_start_read(busid, ep, cdc[i].rx_buf, sizeof(cdc[i].rx_buf));
}

static void cdc_in_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid; (void)nbytes;
    int i = port_from_in_ep(ep);
    if (i < 0) return;
    cdc[i].tx_busy = false;
}

/* ------------------------------------------------------------------- HID */
static volatile bool hid_tx_busy = false;

static void hid_in_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid; (void)ep; (void)nbytes;
    hid_tx_busy = false;
}

/* Send a full report and wait for the interrupt transfer to complete before
 * returning, so consecutive sends (e.g. press then release) are not dropped. */
static void hid_ep_send(const uint8_t *buf)
{
    hid_tx_busy = true;
    usbd_ep_start_write(0, HID_EP, buf, HID_EP_SIZE);
    while (hid_tx_busy) { /* hid_in_cb completes the transfer */ }
}

static void secd_usbd_event(uint8_t busid, uint8_t event)
{
    switch (event) {
        case USBD_EVENT_CONFIGURED:
            for (uint8_t i = 0; i < s_ports; i++) {
                cdc[i].tx_busy = false;
                usbd_ep_start_read(busid, cdc_ep_out[i], cdc[i].rx_buf, sizeof(cdc[i].rx_buf));
            }
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------- public API */
static void send_on(struct cdc_port *c, uint8_t ep_in, const uint8_t *buf, uint16_t len)
{
    c->tx_busy = true;
    usbd_ep_start_write(0, ep_in, buf, len);
    while (c->tx_busy) { /* ISR completes the transfer */ }
}

/* Device identity setters (called from Lisp before %usb-start). The values
 * are read when the host enumerates, so changing them after start has no
 * effect until the next re-enumeration. */
void secd_usb_set_vid(uint16_t vid) { s_vid = vid; }
void secd_usb_set_pid(uint16_t pid) { s_pid = pid; }
void secd_usb_set_manufacturer(const uint8_t *data, uint16_t len) {
    if (!data) return;
    uint16_t n = len < sizeof(s_mfc) ? len : (uint16_t)(sizeof(s_mfc) - 1);
    for (uint16_t i = 0; i < n; i++) s_mfc[i] = (char)data[i];
    g_secd_str_len[1] = n;
}
void secd_usb_set_product(const uint8_t *data, uint16_t len) {
    if (!data) return;
    uint16_t n = len < sizeof(s_product) ? len : (uint16_t)(sizeof(s_product) - 1);
    for (uint16_t i = 0; i < n; i++) s_product[i] = (char)data[i];
    g_secd_str_len[2] = n;
}
void secd_usb_set_serial(const uint8_t *data, uint16_t len) {
    if (!data) return;
    uint16_t n = len < sizeof(s_serial) ? len : (uint16_t)(sizeof(s_serial) - 1);
    for (uint16_t i = 0; i < n; i++) s_serial[i] = (char)data[i];
    g_secd_str_len[3] = n;
}

void secd_usb_init(void)
{
    /* Default identity strings (ASCII, encoded as UTF-16LE). */
    g_secd_str_len[1] = set_default_utf16(s_mfc, "SECD");
    g_secd_str_len[2] = set_default_utf16(s_product, "SECD Machine");
    g_secd_str_len[3] = set_default_utf16(s_serial, "000000000001");
    usbd_desc_register(0, &secd_descriptor);
    /* Console always present (port 0). */
    s_ports = 1;
    memset(&cdc[0], 0, sizeof(cdc[0]));
    cdc[0].used = true;
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intfs[0]));
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intfs[1]));
    ep_out[0].ep_addr = cdc_ep_out[0]; ep_out[0].ep_cb = cdc_out_cb;
    ep_in[0].ep_addr  = cdc_ep_in[0];  ep_in[0].ep_cb  = cdc_in_cb;
    usbd_add_endpoint(0, &ep_out[0]);
    usbd_add_endpoint(0, &ep_in[0]);
}

int secd_usb_serial_add(void)
{
    if (s_started || s_ports >= SECD_USB_MAX_PORTS) return -1;
    uint8_t i = s_ports;
    memset(&cdc[i], 0, sizeof(cdc[i]));
    cdc[i].used = true;
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intfs[2 * i]));
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intfs[2 * i + 1]));
    ep_out[i].ep_addr = cdc_ep_out[i]; ep_out[i].ep_cb = cdc_out_cb;
    ep_in[i].ep_addr  = cdc_ep_in[i];  ep_in[i].ep_cb  = cdc_in_cb;
    usbd_add_endpoint(0, &ep_out[i]);
    usbd_add_endpoint(0, &ep_in[i]);
    s_ports = (uint8_t)(i + 1);
    return (int)i;
}

int secd_usb_hid_add(void)
{
    if (s_started || s_hid) return -1;
    s_hid = true;
    usbd_add_interface(0, usbd_hid_init_intf(0, &intfs[2 * SECD_USB_MAX_PORTS],
                                             hid_kbd_report_desc,
                                             HID_KEYBOARD_REPORT_DESC_SIZE));
    hid_ep.ep_addr = HID_EP;
    hid_ep.ep_cb   = hid_in_cb;
    usbd_add_endpoint(0, &hid_ep);
    return 0;
}

/* RP2040 has a single HID interface (the keyboard); a second HID mouse
 * interface is not supported, so the mouse add is a hard failure and the
 * mouse send is a no-op. Both exist only so the HAL glue stays uniform. */
int secd_usb_mouse_add(void)
{
    (void)0;
    return -1;
}

int secd_hid_mouse_send(int8_t dx, int8_t dy, uint8_t buttons, int8_t wheel)
{
    (void)dx; (void)dy; (void)buttons; (void)wheel;
    return -1;
}

void secd_usb_start(void)
{
    if (s_started) return;
    build_config_descriptor();
    usbd_initialize(0, 0, secd_usbd_event);
    s_started = true;
}

bool secd_usb_configured(void)
{
    return usb_device_is_configured(0);
}

void secd_usb_deinit(void)
{
    if (!s_started) return;
    /* Tear down the DCD (disable IRQ, clear pull-up, reset RP2040 EP state)
     * and notifies DEINIT. The core registry (interfaces/endpoints) is reset
     * again by the next secd_usb_init() -> usbd_desc_register(), so Lisp can
     * rebuild the factory cleanly. */
    usbd_deinitialize(0);
    s_started = false;
    s_hid = false;
    s_ports = 1;
}

bool secd_console_ready(void)
{
    return s_started && usb_device_is_configured(0) && (cdc[0].dtr != 0);
}

/* ---- console (SECD print/format) = port 0 ---- */
size_t secd_console_write(const uint8_t *data, size_t len)
{
    if (!s_started || !usb_device_is_configured(0) || !cdc[0].dtr) return 0;
    /* Serial terminals expect CRLF; '\n' alone moves down a line but leaves
     * the cursor wherever it was, so each line overprints the previous. */
    size_t sent = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (c == '\n') {
            static const uint8_t crlf[2] = {'\r', '\n'};
            send_on(&cdc[0], cdc_ep_in[0], crlf, 2);
            sent += 2;
        } else {
            send_on(&cdc[0], cdc_ep_in[0], &c, 1);
            sent += 1;
        }
    }
    return sent;
}

void secd_console_write_char(char c)
{
    (void)secd_console_write((const uint8_t *)&c, 1);
}

int secd_console_read(void)
{
    if (cdc[0].tail == cdc[0].head) return -1;
    uint8_t c = cdc[0].ring[cdc[0].tail];
    cdc[0].tail = (uint16_t)((cdc[0].tail + 1) % RING_SIZE);
    return (int)c;
}

int secd_console_available(void)
{
    return (int)((cdc[0].head + RING_SIZE - cdc[0].tail) % RING_SIZE);
}

/* ---- Lisp serial consoles (ports >= 1, added by secd_usb_serial_add) ---- */
size_t secd_serial_write(int port, const uint8_t *data, size_t len)
{
    if (port < 1 || port >= (int)s_ports || !s_started) return 0;
    if (!usb_device_is_configured(0) || !cdc[port].dtr) return 0;
    size_t written = 0;
    while (written < len) {
        size_t chunk = len - written;
        if (chunk > sizeof(cdc[port].tx_buf)) chunk = sizeof(cdc[port].tx_buf);
        memcpy(cdc[port].tx_buf, data + written, chunk);
        send_on(&cdc[port], cdc_ep_in[port], cdc[port].tx_buf, (uint16_t)chunk);
        written += chunk;
    }
    return written;
}

int secd_serial_read(int port)
{
    if (port < 1 || port >= (int)s_ports) return -1;
    if (cdc[port].tail == cdc[port].head) return -1;
    uint8_t c = cdc[port].ring[cdc[port].tail];
    cdc[port].tail = (uint16_t)((cdc[port].tail + 1) % RING_SIZE);
    return (int)c;
}

int secd_serial_available(int port)
{
    if (port < 1 || port >= (int)s_ports) return 0;
    return (int)((cdc[port].head + RING_SIZE - cdc[port].tail) % RING_SIZE);
}

/* ---- HID keyboard ---- */
void secd_hid_keyboard_tap(uint8_t modifier, uint8_t usage)
{
    static uint8_t rep[HID_EP_SIZE];
    if (!s_hid || !s_started || !usb_device_is_configured(0)) return;

    rep[0] = modifier; rep[1] = 0; rep[2] = usage; rep[3] = 0;
    rep[4] = 0; rep[5] = 0; rep[6] = 0; rep[7] = 0;
    hid_ep_send(rep);

    memset(rep, 0, sizeof(rep));
    hid_ep_send(rep);
}

/* CDC control hooks: track per-port DTR (intf number = 2 * port). */
extern "C" void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    uint8_t port = (uint8_t)(intf / 2);
    if (port < (uint8_t)s_ports) cdc[port].dtr = dtr ? 1 : 0;
}