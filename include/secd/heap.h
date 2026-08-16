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

/* Byte-vector configuration.
 *
 * Byte-vectors are a separate value kind (SECD_TYPE_BYTEVEC) whose handle
 * index is a slot in this descriptor table, NOT a heap object index. A slot
 * either points at read-only data (ROM literals from the bytecode pool) or
 * at a region of a RAM arena (vectors created with make-vector).
 */
#ifndef SECD_BYTEVEC_MAX
#define SECD_BYTEVEC_MAX 64              /* max simultaneously live byte-vectors */
#endif
#ifndef SECD_BYTEVEC_ARENA_SIZE
#define SECD_BYTEVEC_ARENA_SIZE 8192     /* RAM bytes for make-vector */
#endif
#define SECD_BYTEVEC_INVALID 0xFFFF      /* sentinel slot value */

/* Heap statistics */
typedef struct {
    uint16_t total;     /* Total objects */
    uint16_t used;      /* Used objects */
    uint16_t free;      /* Free objects */
    uint16_t collections; /* Number of GC collections */
} secd_heap_stats_t;

/* Byte-vector descriptor */
typedef struct {
    const uint8_t *data;    /* byte data (ROM pool or RAM arena) */
    uint16_t len;           /* number of bytes */
    uint8_t writable;       /* 1 = RAM arena (make-vector), 0 = ROM literal */
} secd_bytevec_t;

/* Heap structure */
typedef struct {
    secd_object_t *objects;     /* Object array */
    secd_type_t *types;         /* Type array */
    uint16_t size;              /* Total capacity */
    uint16_t next_free;         /* Next free object index */
    secd_heap_stats_t stats;    /* Statistics */

    /* Byte-vector descriptor table + bump arena (see SECD_BYTEVEC_*) */
    secd_bytevec_t bytevecs[SECD_BYTEVEC_MAX];
    uint16_t bytevec_count;
    uint8_t *byte_arena;
    uint32_t byte_arena_size;
    uint32_t byte_arena_pos;

    /* GC mark bits for bytevec slots: bit N set = slot N reachable from the
     * machine roots on the last collection. ROM slots (bytecode pool) are
     * never freed, so only RAM (make-vector / read) slots use these. */
    uint64_t bytevec_marks;
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

/*
 * Byte-vector operations.
 *
 * A byte-vector handle's index is a slot in the descriptor table, not a
 * heap object index, so these manipulate the table + arena directly.
 */

/* Register a read-only byte vector (e.g. ROM literal from the bytecode pool).
   Returns the descriptor slot, or SECD_BYTEVEC_INVALID on overflow. */
uint16_t secd_bytevec_register(secd_heap_t *heap, const uint8_t *data, uint16_t len);

/* Allocate a writable byte vector of LEN bytes from the RAM arena.
   Returns the descriptor slot, or SECD_BYTEVEC_INVALID on failure. */
uint16_t secd_bytevec_alloc(secd_heap_t *heap, uint16_t len);

/* Get a descriptor by slot; NULL if the slot is out of range. */
secd_bytevec_t* secd_bytevec_get(secd_heap_t *heap, uint16_t slot);

/* Read one byte; returns -1 on bad slot/index, else 0..255. */
int secd_bytevec_read(secd_heap_t *heap, uint16_t slot, uint16_t index);

/* Write one byte; returns 0 on success, -1 on bad slot/index or a
   read-only (ROM) vector. */
int secd_bytevec_write(secd_heap_t *heap, uint16_t slot, uint16_t index, uint8_t byte);

#endif /* SECD_HEAP_H */

#ifdef __cplusplus
}
#endif
