/*
 * SECD Machine — shared firmware boot core.
 *
 * Every target runs this same code: print the banner, bring up the heap and
 * the SECD machine, then hand off to the per-board bytecode loader. The only
 * things that differ between chips are the HAL bindings (secd_console_write,
 * the bytecode loader) and the artifact packed into the .machine file (UF2 or
 * raw binary). Banner content (version/platform/features) comes from per-build
 * -D macros; heap size from the caller.
 */
#include "secd/boot.h"
#include "hal/hal.h"
#include <stdint.h>
#include <string.h>

/* Write a NUL-terminated string without depending on snprintf. */
static void secd_put_str(const char *s) {
    secd_console_write((const uint8_t *)s, strlen(s));
}

/* Write a decimal uint32 without depending on snprintf. */
static void secd_put_u32(uint32_t v) {
    char buf[11];
    int i = 0;
    if (v == 0) {
        secd_console_write((const uint8_t *)"0", 1);
        return;
    }
    while (v) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        secd_console_write((const uint8_t *)&buf[--i], 1);
    }
}

void secd_print_banner(void) {
    secd_put_str("SECD Machine v" SECD_MACHINE_VERSION "\r\n");

#if defined(SECD_DEBUG_BUILD) && SECD_DEBUG_BUILD
    secd_put_str("Build: debug (serial + info)\r\n");
#else
    secd_put_str("Build: release\r\n");
#endif

    secd_put_str("Features: ");
    if (SECD_FEATURES_STR[0] == '\0') {
        secd_put_str("(none)");
    } else {
        secd_put_str(SECD_FEATURES_STR);
    }
    secd_put_str("\r\n");

    secd_put_str("Platform: " SECD_PLATFORM_NAME "\r\n");
}

int secd_machine_boot(secd_machine_t *machine, secd_heap_t *heap,
                      int heap_objects,
                      int (*load)(secd_machine_t *, secd_heap_t *)) {
    secd_print_banner();

    /* Heap element is 4 bytes (heap.h); report the live object count. */
    secd_put_str("Heap: ");
    secd_put_u32((uint32_t)heap_objects);
    secd_put_str(" objects (~");
    secd_put_u32((uint32_t)heap_objects * 4u);
    secd_put_str(" bytes)\r\n");

    if (secd_heap_init(heap, (uint16_t)heap_objects) != 0) {
        secd_put_str("Heap init failed\r\n");
        return 1;
    }
    secd_put_str("Heap initialized\r\n");

    if (secd_machine_init(machine, heap) != 0) {
        secd_put_str("Machine init failed\r\n");
        return 1;
    }
    secd_put_str("Machine initialized\r\n");

    secd_put_str("Loading bytecode...\r\n");
    if (load && load(machine, heap) != 0) {
        secd_put_str("No valid bytecode\r\n");
    }
    return 0;
}
