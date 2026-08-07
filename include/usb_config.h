/*
 * CherryUSB configuration for the SECD Machine (device only, bare metal).
 * No RTOS, no EP0 thread, no dcache (RP2040 has no cache).
 *
 * The console (CDC-ACM) and HID keyboard live in the device descriptors.
 */
#ifndef SECD_USB_CONFIG_H
#define SECD_USB_CONFIG_H

/* ---------------------------------------------------------------- usb common */
#define CONFIG_USB_PRINTF(...) ((void)0)
#define CONFIG_USB_DBG_LEVEL   0

/* No dcache on RP2040 -> 4-byte alignment, no special section for core structs */
#define CONFIG_USB_ALIGN_SIZE 4
#define USB_NOCACHE_RAM_SECTION

/* ================= USB Device Stack Configuration ================= */
#define CONFIG_USBDEV_MAX_BUS 1

/* Ep0 in and out transfer buffer */
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512

#define CONFIG_USBDEV_ADVANCE_DESC

#endif /* CHERRYUSB_CONFIG_H */