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
#ifndef SECD_HEAP_H
#define SECD_HEAP_H

#include "secd/types.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Heap management for SECD machine.
 *
 * The heap is a single contiguous array of objects.
 * Objects are allocated by index, not by pointer.
 * Handles (secd_value_t) contain indices into this heap.
 *
 * Memory layout:
 *   [Object 0] [Object 1] ... [Object N-1]
 *
 * Each object is 4 bytes (2 x uint16_t).
 * Type information is stored in a separate array for compactness.
 */

/* Default heap configuration */
#define SECD_HEAP_DEFAULT_SIZE  1024    /* objects */
#define SECD_STACK_DEFAULT_SIZE 256     /* values */
#define SECD_SYMBOLS_DEFAULT_SIZE 128   /* symbols */

/* Heap statistics */
typedef struct {
    uint16_t total;     /* Total objects */
    uint16_t used;      /* Used objects */
    uint16_t free;      /* Free objects */
    uint16_t collections; /* Number of GC collections */
} secd_heap_stats_t;

/* Heap structure */
typedef struct {
    secd_object_t *objects;     /* Object array */
    secd_type_t *types;         /* Type array */
    uint16_t size;              /* Total capacity */
    uint16_t next_free;         /* Next free object index */
    secd_heap_stats_t stats;    /* Statistics */
} secd_heap_t;

/* Initialize heap */
int secd_heap_init(secd_heap_t *heap, uint16_t size);

/* Free heap resources */
void secd_heap_free(secd_heap_t *heap);

/* Allocate a new object (returns index, or 0 on failure) */
uint16_t secd_heap_alloc(secd_heap_t *heap, secd_type_t type);

/* Free an object (mark as free) */
void secd_heap_free_object(secd_heap_t *heap, uint16_t index);

/* Get object by index */
secd_object_t* secd_heap_get(secd_heap_t *heap, uint16_t index);

/* Get type by index */
secd_type_t secd_heap_get_type(secd_heap_t *heap, uint16_t index);

/* Set type by index */
void secd_heap_set_type(secd_heap_t *heap, uint16_t index, secd_type_t type);

/* Mark object as used (for GC) */
void secd_heap_mark(secd_heap_t *heap, uint16_t index);

/* Check if object is marked */
bool secd_heap_is_marked(secd_heap_t *heap, uint16_t index);

/* Get heap statistics */
void secd_heap_get_stats(secd_heap_t *heap, secd_heap_stats_t *stats);

/* Run garbage collection (returns number of freed objects) */
uint16_t secd_heap_gc(secd_heap_t *heap);

/*
 * Value operations (using heap)
 */

/* Get car of a pair */
secd_value_t secd_car(secd_heap_t *heap, secd_value_t pair);

/* Get cdr of a pair */
secd_value_t secd_cdr(secd_heap_t *heap, secd_value_t pair);

/* Set car of a pair */
void secd_set_car(secd_heap_t *heap, secd_value_t pair, secd_value_t val);

/* Set cdr of a pair */
void secd_set_cdr(secd_heap_t *heap, secd_value_t pair, secd_value_t val);

/* Create a new pair (cons) */
secd_value_t secd_cons(secd_heap_t *heap, secd_value_t car, secd_value_t cdr);

#endif /* SECD_HEAP_H */

#ifdef __cplusplus
}
#endif
