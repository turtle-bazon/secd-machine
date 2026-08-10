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
#include "secd/heap.h"
#include "secd/machine.h"

/*
 * Garbage Collection - Mark Phase.
 *
 * The sweep phase is in heap.c (secd_heap_gc).
 * This file implements the mark phase, which traverses from roots.
 */

/* Mark an object and all objects it references */
static void mark_object(secd_heap_t *heap, secd_value_t val) {
    if (secd_is_nil(val) || secd_is_bool(val)) {
        return; /* No object to mark */
    }
    
    /* Immediate values (fixnums) are not heap references */
    if (secd_is_fixnum(val)) {
        return;
    }
    
    /* Byte-vector handles reference the descriptor table, not heap objects */
    if (secd_is_bytevec(val)) {
        return;
    }
    
    uint16_t index = secd_get_index(val);
    if (index == 0 || index > heap->size) {
        return; /* Invalid index */
    }
    
    /* Check if already marked (avoid infinite loops) */
    if (secd_heap_is_marked(heap, index)) {
        return;
    }
    
    /* Mark this object */
    secd_heap_mark(heap, index);
    
    /* Get object type */
    secd_type_t type = secd_heap_get_type(heap, index);
    
    /* Recursively mark referenced objects */
    switch (type) {
        case SECD_TYPE_PAIR: {
            secd_object_t *obj = secd_heap_get(heap, index);
            if (obj) {
                mark_object(heap, obj->car);
                mark_object(heap, obj->cdr);
            }
            break;
        }
        
        case SECD_TYPE_CLOSURE: {
            /* Closure is a pair: (bytecode-address . environment) */
            secd_object_t *obj = secd_heap_get(heap, index);
            if (obj) {
                /* cdr holds the environment, which is a heap reference */
                mark_object(heap, obj->cdr);
            }
            break;
        }
        
        case SECD_TYPE_BIGNUM: {
            /* Bignum contains pointer to data (stored in car as handle) */
            secd_object_t *obj = secd_heap_get(heap, index);
            if (obj) {
                mark_object(heap, obj->car);
            }
            break;
        }
        
        default:
            /* Other types don't reference heap objects */
            break;
    }
}

/* Mark the stack (a list of pairs held in the heap) */
static void mark_stack(secd_machine_t *machine) {
    mark_object(machine->heap, machine->S);
}

/* Mark the environment (a list of bindings) */
static void mark_env(secd_machine_t *machine) {
    mark_object(machine->heap, machine->E);
}

/* Mark the dump stack (a list of saved environments and return addresses) */
static void mark_dump(secd_machine_t *machine) {
    mark_object(machine->heap, machine->dump_stack);
}

/* Run full GC cycle */
uint16_t secd_gc_run(secd_machine_t *machine) {
    if (!machine || !machine->heap) {
        return 0;
    }
    
    /* Mark phase: traverse from roots */
    mark_stack(machine);
    mark_env(machine);
    mark_dump(machine);
    
    /* Sweep phase: free unmarked objects */
    return secd_heap_gc(machine->heap);
}
