/*
 * SECD USB device for ESP32-S3 (CherryUSB DWC2).
 * Composite device whose interface set is built from Lisp at runtime
 * (Lisp "factory" model). USB enumeration is started only once secd_usb_start
 * (Lisp: %usb-start) runs, so all interfaces must be added beforehand:
 *     %usb-init          -> secd_usb_init():      register descriptor + console
 *     %usb-serial-add    -> secd_usb_serial_add():add a CDC-ACM serial port
 *     %usb-hid-add       -> secd_usb_hid_add():   add the HID keyboard
 *     %usb-start         -> secd_usb_start():     freeze + enumerate
 */
#ifndef SECD_ESP32S3_USB_H
#define SECD_ESP32S3_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register the device + console (call before any add). */
void secd_usb_init(void);

/* Add a Lisp serial console; returns its port index (>=1) or -1 (full/started). */
int secd_usb_serial_add(void);

/* Add the HID keyboard interface. Returns 0 or -1. */
int secd_usb_hid_add(void);

/* Add the HID mouse interface (relative X/Y + 3 buttons + wheel).
   Returns 0 or -1. Must be called before secd_usb_start(). */
int secd_usb_mouse_add(void);

/* Freeze the interface set and start USB; call it after all secd_*_add()s. */
void secd_usb_start(void);

/* Tear down USB (debug boot banner path). After this, a fresh
 * secd_usb_init()+adds()+secd_usb_start() (as done by Lisp) re-enumerates. */
void secd_usb_deinit(void);

/* Lisp-settable USB device identity. Call before secd_usb_start(); the host
 * reads these at enumeration time. Strings are NUL-terminated C strings. */
void secd_usb_set_vid(uint16_t vid);
void secd_usb_set_pid(uint16_t pid);
/* String setters take a pre-encoded UTF-16LE byte buffer (as produced by the
 * Lisp to-c-string helper) and its byte length. A length is required because
 * UTF-16LE contains embedded NUL bytes that strlen would truncate. */
void secd_usb_set_manufacturer(const uint8_t *data, uint16_t len);
void secd_usb_set_product(const uint8_t *data, uint16_t len);
void secd_usb_set_serial(const uint8_t *data, uint16_t len);

/* True once the host has configured the device. */
bool secd_usb_configured(void);

/* True once the console (port 0) is live enough to print: started,
 * configured by the host, and DTR set. */
bool secd_console_ready(void);

/* Console (port 0). Returns bytes written. */
size_t secd_console_write(const uint8_t *data, size_t len);
void   secd_console_write_char(char c);
int    secd_console_read(void);     /* -1 if empty */
int    secd_console_available(void);

/* Lisp serial consoles (ports >= 1). */
size_t secd_serial_write(int port, const uint8_t *data, size_t len);
int    secd_serial_read(int port);  /* -1 if empty */
int    secd_serial_available(int port);

/* HID keyboard tap (press+release). MODIFIER 0 = none. */
void secd_hid_keyboard_tap(uint8_t modifier, uint8_t usage);

/* HID mouse move: relative X/Y, wheel scroll, button mask. Each of
   X/Y/WHEEL is a signed byte (-128..127). Sends one report. */
int  secd_hid_mouse_send(int8_t dx, int8_t dy, uint8_t buttons, int8_t wheel);

#ifdef __cplusplus
}
#endif

#endif /* SECD_ESP32S3_USB_H */