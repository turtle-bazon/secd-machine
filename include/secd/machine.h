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
#ifndef SECD_MACHINE_H
#define SECD_MACHINE_H

#include "secd/types.h"
#include "secd/heap.h"
#include <stdint.h>
#include <stdbool.h>

/*
 * SECD Machine State.
 *
 * Registers:
 *   S - Stack (holds intermediate results)
 *   E - Environment (variable bindings)
 *   C - Control (current instruction pointer)
 *   D - Dump (saved state for calls/returns)
 *
 * All registers are secd_value_t, which can be:
 *   - A handle to a pair (list) in the heap
 *   - NIL for empty list
 */

/* Machine configuration */
#define SECD_STACK_MAX_SIZE  512
#define SECD_DUMP_MAX_SIZE   64

/* Auto-GC trigger: free-object floor (must stay below the 12-bit handle
 * index limit of 4096 so every allocated slot is addressable). */
#define SECD_GC_THRESHOLD    1024

/* Machine state */
typedef struct {
    secd_value_t S;     /* Stack */
    secd_value_t E;     /* Environment */
    secd_value_t G;     /* Global environment (flat list of global cells) */
    secd_value_t C;     /* Control (instruction pointer) */
    secd_value_t D;     /* Dump (saved state) */
    
    secd_heap_t *heap;  /* Pointer to heap */
    
    /* Dump stack */
    secd_value_t dump_stack;
    uint16_t dump_size;
    
    /* Error state */
    uint8_t error;      /* Error code (0 = no error) */
    bool running;       /* Is machine running? */
    uint32_t steps;     /* Instructions executed */
    uint32_t max_steps; /* Step limit (0 = unlimited) */
    bool pool_loaded;   /* ROM byte-vector pool registered for this buffer */
} secd_machine_t;

/* Error codes */
#define SECD_ERROR_NONE              0
#define SECD_ERROR_STACK_OVERFLOW    1
#define SECD_ERROR_STACK_UNDERFLOW   2
#define SECD_ERROR_HEAP_FULL         3
#define SECD_ERROR_INVALID_HANDLE    4
#define SECD_ERROR_TYPE_ERROR        5
#define SECD_ERROR_DIVISION_BY_ZERO  6
#define SECD_ERROR_UNDEFINED_SYMBOL  7
#define SECD_ERROR_PRIMITIVE_NOT_FOUND 8

/* Initialize machine */
int secd_machine_init(secd_machine_t *machine, secd_heap_t *heap);

/* Free machine resources */
void secd_machine_free(secd_machine_t *machine);

/* Reset machine to initial state */
void secd_machine_reset(secd_machine_t *machine);

/* Stack operations */
int secd_push(secd_machine_t *machine, secd_value_t value);
secd_value_t secd_pop(secd_machine_t *machine);
secd_value_t secd_peek(secd_machine_t *machine);

/* Environment operations */
secd_value_t secd_make_env(secd_machine_t *machine, secd_value_t parent);
secd_value_t secd_env_lookup(secd_machine_t *machine, secd_value_t env, secd_value_t symbol);
int secd_env_define(secd_machine_t *machine, secd_value_t env, secd_value_t symbol, secd_value_t value);

/* Dump operations */
int secd_dump_push(secd_machine_t *machine, secd_value_t value);
secd_value_t secd_dump_pop(secd_machine_t *machine);

/* Error handling */
void secd_set_error(secd_machine_t *machine, uint8_t error_code);
uint8_t secd_get_error(secd_machine_t *machine);
const char* secd_error_string(uint8_t error_code);

/* Execute bytecode */
int secd_execute(secd_machine_t *machine, const uint8_t *bytecode, size_t length);

/* Single step execution (for debugging) */
int secd_step(secd_machine_t *machine, const uint8_t *bytecode, size_t length);

/* Garbage collection */
uint16_t secd_gc_run(secd_machine_t *machine);

#endif /* SECD_MACHINE_H */

#ifdef __cplusplus
}
#endif
