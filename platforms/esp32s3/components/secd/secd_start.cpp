/*
 * SECD VM startup for ESP32-S3.
 * Loads bytecode from raw flash right after the app image, which the linker
 * places there by simple concatenation:
 *   cat firmware.bin program.secd > final.bin
 * The program file carries the standard SECD header (magic "SECD" + version +
 * sizes).  No firmware rebuild is needed to run a new Lisp program, and the
 * bytecode location tracks the firmware size automatically (see
 * load_bytecode), so adding or removing firmware features never breaks it.
 *
 * Two firmware variants, selected at build time via SECD_DEBUG_BUILD:
 *  - debug   (default): boots its own USB console so the startup banner is
 *                        always printed, then tears USB down and lets the Lisp
 *                        program re-initialize it (%usb-init/%usb-start).
 *  - release          : starts bytecode immediately, no serial output, no waits.
 */
#include "secd/machine.h"
#include "secd/heap.h"
#include "secd/boot.h"
#include "hal/hal.h"
#include "usb.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef HEAP_OBJECTS
#define HEAP_OBJECTS 4095   /* 12-bit handle index limit */
#endif

#ifndef SECD_MACHINE_VERSION
#define SECD_MACHINE_VERSION "0.0.1.0"
#endif

#ifndef SECD_DEBUG_BUILD
#define SECD_DEBUG_BUILD 1
#endif

#ifndef SECD_FEATURE_HID
#define SECD_FEATURE_HID 0
#endif

/* The bytecode is glued right after the app image in flash
 * (cat firmware.bin program.secd > final.bin).  Its position is NOT a
 * compile-time constant: it tracks the actual firmware size, so adding or
 * removing features (which shifts the image) never breaks the offset.  The
 * loader locates it by parsing its own app image (ESP32 image header +
 * segments, same walk as the bootloader) and scanning a small window past the
 * image end for the SECD header magic, absorbing the checksum/hash tail and
 * flash-write alignment. */
#define ESP_IMAGE_MAGIC 0xE9
#define ESP_IMAGE_MAX_SEGMENTS 16
#define BYTECODE_SCAN_LIMIT 8192

static secd_heap_t heap;
static secd_machine_t machine;

#if SECD_DEBUG_BUILD && SECD_FEATURE_HID
/* Debug boots its own USB console, prints startup info, then tears it down so
 * the Lisp program can re-initialize USB (%usb-init/%usb-start) with the full
 * factory. This guarantees the boot banner is seen even if Lisp never starts
 * USB.
 *
 * SECD_S3_GATED_BOOT: when 1 the debug build waits for the host to open the
 * CD C console (DTR) before handing USB to Lisp. When 0 it skips the gate and
 * hands over immediately (matches the ESP32-C3 flow). The gate is what makes
 * a host-side port close (DTR drop) race with the DWC2 handoff, so the
 * default here is off for robustness. */
#ifndef SECD_S3_GATED_BOOT
#define SECD_S3_GATED_BOOT 0
#endif

static void secd_info(const char *fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    secd_console_write((const uint8_t *)buf, strlen(buf));
}
#define SECD_INFO(...) secd_info(__VA_ARGS__)
#define SECD_WAIT_MS(ms) hal_delay(ms)
#else
#define SECD_INFO(...) printf(__VA_ARGS__)
#define SECD_WAIT_MS(ms) hal_delay(ms)
#endif

/* Size in bytes of the app image on flash: ESP32 image header + all segments.
 * Mirrors the bootloader's esp_image_format.c segment walk. Returns 0 on any
 * malformed header. */
static uint32_t app_image_len(const esp_partition_t *part) {
    uint8_t hdr[24];
    if (esp_partition_read(part, 0, hdr, sizeof(hdr)) != ESP_OK) return 0;
    if (hdr[0] != ESP_IMAGE_MAGIC) return 0;
    uint8_t seg_count = hdr[1];
    if (seg_count == 0 || seg_count > ESP_IMAGE_MAX_SEGMENTS) return 0;

    uint32_t off = sizeof(hdr);
    for (uint8_t i = 0; i < seg_count; i++) {
        uint8_t seg_hdr[8];
        if (esp_partition_read(part, off, seg_hdr, sizeof(seg_hdr)) != ESP_OK) return 0;
        uint32_t data_len = (uint32_t)seg_hdr[4]
                          | ((uint32_t)seg_hdr[5] << 8)
                          | ((uint32_t)seg_hdr[6] << 16)
                          | ((uint32_t)seg_hdr[7] << 24);
        off += sizeof(seg_hdr) + data_len;
        if (off >= part->size) return 0;
    }
    return off;
}

/* Load bytecode appended right after the firmware image (cat firmware.bin
 * bytecode.secd > final.bin).  Finds the SECD header by scanning a window
 * past the image end, then executes code_size + const_size bytes after it. */
static int load_bytecode(void) {
    const esp_partition_t *part = esp_ota_get_running_partition();
    if (!part) {
        SECD_INFO("No running app partition\n");
        return -1;
    }

    uint32_t base = app_image_len(part);
    if (base == 0 || base >= part->size) {
        SECD_INFO("Bad app image\n");
        return -1;
    }

    size_t window = part->size - base;
    if (window > BYTECODE_SCAN_LIMIT) window = BYTECODE_SCAN_LIMIT;
    uint8_t *scan = (uint8_t *)malloc(window);
    if (!scan) {
        SECD_INFO("Out of memory\n");
        return -1;
    }
    if (esp_partition_read(part, base, scan, window) != ESP_OK) {
        SECD_INFO("Flash read failed\n");
        free(scan);
        return -1;
    }

    bool found = false;
    uint32_t magic = 0;
    for (uint32_t i = 0; i + 4 <= window; i++) {
        if (scan[i] == 'S' && scan[i + 1] == 'E' && scan[i + 2] == 'C'
            && scan[i + 3] == 'D') {
            magic = i;
            found = true;
            break;
        }
    }
    if (!found) {
        SECD_INFO("No SECD header after app image\n");
        free(scan);
        return -1;
    }

    uint8_t version_major = scan[magic + 4];
    uint8_t version_minor = scan[magic + 5];
    uint16_t code_size = (scan[magic + 8] << 8) | scan[magic + 9];
    uint16_t const_size = (scan[magic + 10] << 8) | scan[magic + 11];
    uint16_t sym_size = (scan[magic + 12] << 8) | scan[magic + 13];
    uint32_t total = 14 + (uint32_t)code_size + const_size + sym_size;
    free(scan);

    SECD_INFO("Bytecode v%d.%d, code=%d, const=%d, sym=%d\n",
              version_major, version_minor, code_size, const_size, sym_size);

    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) {
        SECD_INFO("Out of memory for bytecode\n");
        return -1;
    }
    if (esp_partition_read(part, base + magic, buf, total) != ESP_OK) {
        SECD_INFO("Flash read failed\n");
        free(buf);
        return -1;
    }

#if SECD_DEBUG_BUILD && SECD_FEATURE_HID
    SECD_INFO("Deinitializing boot console, Lisp takes over USB.\n");
    secd_usb_deinit();
    SECD_INFO("Boot console deinit done.\n");
#endif

    int rc = secd_execute(&machine, buf + 14, (size_t)code_size + const_size);
    SECD_INFO("secd_execute returned %d\n", rc);
    free(buf);
    return rc;
}

extern "C" int secd_start(void) {
    secd_hal_init();

#if SECD_DEBUG_BUILD && SECD_FEATURE_HID
    secd_usb_init();
    secd_usb_start();
#if SECD_S3_GATED_BOOT
    secd_console_write((const uint8_t *)"Waiting for host console...\n", 29);
    while (!secd_console_ready()) {
        hal_delay(20);
    }
#else
    /* No DTR gate: wait up to 5s for the host to open the console, so the
     * banner actually lands (CDC-ACM drops TX until DTR is set). Falls
     * through after the timeout so boot is never blocked. */
    for (int ms = 0; ms < 5000 && !secd_console_ready(); ms += 20) {
        hal_delay(20);
    }
#endif

    secd_print_banner();
    /* Hold the boot console a moment longer so the host can capture the
     * banner before control passes to the Lisp bytecode. */
    SECD_WAIT_MS(2000);
#endif

    if (secd_heap_init(&heap, HEAP_OBJECTS) != 0) {
        SECD_INFO("Heap init failed\n");
        return 1;
    }
    if (secd_machine_init(&machine, &heap) != 0) {
        SECD_INFO("Machine init failed\n");
        return 1;
    }

    /* Bytecode is loaded from flash (see load_bytecode). */
    load_bytecode();
    SECD_INFO("Done: steps=%u error=%u\n", (unsigned)machine.steps, (unsigned)machine.error);

    for (;;) {
        hal_delay(1000);
    }
}