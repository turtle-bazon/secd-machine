#ifdef __cplusplus
extern "C" {
#endif
/*
 * SECD Machine for Microcontrollers
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef SECD_PRIMITIVES_H
#define SECD_PRIMITIVES_H

#include "secd/types.h"
#include "secd/heap.h"
#include <stdint.h>

/*
 * C Primitive Interface.
 *
 * Primitives are C functions exposed to the SECD VM.
 * They receive arguments as a list and return a value.
 *
 * Usage:
 *   1. Implement primitive function
 *   2. Register with secd_register_prim()
 *   3. Call from bytecode with PRIM instruction
 */

/* Primitive function signature */
typedef secd_value_t (*secd_prim_fn)(secd_heap_t *heap, secd_value_t args);

/* Primitive entry */
typedef struct {
    const char *name;       /* Primitive name */
    secd_prim_fn fn;        /* Function pointer */
    uint8_t id;             /* Numeric ID for bytecode */
} secd_prim_entry_t;

/* Maximum number of primitives (uint8_t id space, so <= 255).
 *
 * HAL primitives are assigned sequential ids starting at 15 (see the
 * per-chip JSON metadata). A small set of UNIVERSAL runtime primitives
 * (present in every firmware image) is assigned FIXED high ids outside
 * the HAL range so that programs compiled against them are portable
 * across every target. utf16-enc/utf16-dec use 200/201. The registry is
 * therefore sized to cover the fixed universal range. */
#define SECD_PRIMITIVES_MAX 255

/* Primitive registry */
typedef struct {
    secd_prim_entry_t entries[SECD_PRIMITIVES_MAX];
    uint8_t count;
} secd_prim_registry_t;

/* Initialize primitive registry */
void secd_prim_init(secd_prim_registry_t *registry);

/* Register a primitive */
int secd_register_prim(secd_prim_registry_t *registry, const char *name, secd_prim_fn fn);

/* Register a primitive at a FIXED id (used for universal runtime
 * primitives whose ids must be stable across every firmware image,
 * e.g. utf16-enc/utf16-dec at 200/201). The registry high-water mark is
 * advanced to cover id so subsequent id-based lookups stay valid. */
int secd_register_prim_at(secd_prim_registry_t *registry, const char *name,
                          secd_prim_fn fn, uint8_t id);

/* Find primitive by name */
secd_prim_entry_t* secd_find_prim(secd_prim_registry_t *registry, const char *name);

/* Find primitive by ID */
secd_prim_entry_t* secd_find_prim_by_id(secd_prim_registry_t *registry, uint8_t id);

/* Call a primitive */
secd_value_t secd_call_prim(secd_prim_registry_t *registry, uint8_t id, secd_heap_t *heap, secd_value_t args);

/*
 * Built-in primitives
 */

/* List operations */
secd_value_t prim_car(secd_heap_t *heap, secd_value_t args);
secd_value_t prim_cdr(secd_heap_t *heap, secd_value_t args);
secd_value_t prim_cons(secd_heap_t *heap, secd_value_t args);
secd_value_t prim_list(secd_heap_t *heap, secd_value_t args);

/* Arithmetic operations */
secd_value_t prim_add(secd_heap_t *heap, secd_value_t args);
secd_value_t prim_sub(secd_heap_t *heap, secd_value_t args);
secd_value_t prim_mul(secd_heap_t *heap, secd_value_t args);
secd_value_t prim_div(secd_heap_t *heap, secd_value_t args);
secd_value_t prim_mod(secd_heap_t *heap, secd_value_t args);

/* Comparison operations */
secd_value_t prim_eq(secd_heap_t *heap, secd_value_t args);
secd_value_t prim_lt(secd_heap_t *heap, secd_value_t args);
secd_value_t prim_gt(secd_heap_t *heap, secd_value_t args);

/* Type checking */
secd_value_t prim_null(secd_heap_t *heap, secd_value_t args);
secd_value_t prim_pair(secd_heap_t *heap, secd_value_t args);
secd_value_t prim_atom(secd_heap_t *heap, secd_value_t args);

/* Register all built-in primitives */
void secd_register_builtins(secd_prim_registry_t *registry);

#endif /* SECD_PRIMITIVES_H */

#ifdef __cplusplus
}
#endif
