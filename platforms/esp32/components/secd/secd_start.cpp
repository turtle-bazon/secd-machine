/*
 * SECD VM startup for ESP32-C3.
 * Loads bytecode from raw flash right after the app image, which the linker
 * places there by simple concatenation:
 *   cat firmware.bin program.secd > final.bin
 * The program file carries the standard SECD header (magic "SECD" + version +
 * sizes).  No firmware rebuild is needed to run a new Lisp program, and the
 * bytecode location tracks the firmware size automatically (see
 * load_bytecode), so adding or removing firmware features never breaks it.
 *
 * Two firmware variants, selected at build time via SECD_DEBUG_BUILD:
 *  - debug   (default): prints a startup banner to the console.
 *  - release          : silent.
 * The ESP32-C3's USB is a fixed USB-Serial/JTAG console (no USB-OTG device
 * controller), so there is no Lisp-driven USB factory to hand over to.
 */
#include "secd/machine.h"
#include "secd/heap.h"
#include "hal/hal.h"
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

#if SECD_DEBUG_BUILD
#define SECD_INFO(...) printf(__VA_ARGS__)
#define SECD_WAIT_MS(ms) hal_delay(ms)
#else
#define SECD_INFO(...) ((void)0)
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

    int rc = secd_execute(&machine, buf + 14, (size_t)code_size + const_size);
    free(buf);
    return rc;
}

extern "C" int secd_start(void) {
    hal_init();

    /* The USB-Serial/JTAG console drops bytes printed before the host
     * re-attaches after a hard reset (data FIFO is small and the connection
     * re-enumerates).  Let it settle so the startup banner survives. */
    SECD_WAIT_MS(1000);

    SECD_INFO("SECD Machine v%s\n", SECD_MACHINE_VERSION);
    SECD_INFO("Platform: ESP32-C3\n");
    SECD_INFO("Features: %s\n", SECD_FEATURES_STR);

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