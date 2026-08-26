#ifndef SECD_BOOT_H
#define SECD_BOOT_H

#include <stdint.h>
#include <stddef.h>
#include "secd/machine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Print the one shared boot banner (CRLF-terminated) to the console binding.
 * Identical on every target; only SECD_MACHINE_VERSION / SECD_PLATFORM_NAME
 * (per-build -D macros) vary. */
void secd_print_banner(void);

/* Shared boot sequence: print banner, initialize heap + machine, then load
 * and execute the per-platform bytecode image. `load` is the HAL/board binding
 * that finds the glued image in flash and runs secd_execute(); it returns 0 on
 * success. Returns 0 on success, 1 on a fatal init failure. */
int secd_machine_boot(secd_machine_t *machine, secd_heap_t *heap,
                      int heap_objects,
                      int (*load)(secd_machine_t *, secd_heap_t *));

#ifdef __cplusplus
}
#endif

#endif /* SECD_BOOT_H */
