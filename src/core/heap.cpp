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
#include <stdlib.h>
#include <string.h>

/*
 * Heap management implementation.
 *
 * Uses a simple free-list allocator.
 * Objects are allocated sequentially, freed objects are marked as free.
 * GC runs mark-sweep to reclaim free objects.
 */

/* Free object marker in type array */
#define SECD_TYPE_FREE_MARKER 0xFF

int secd_heap_init(secd_heap_t *heap, uint16_t size) {
    if (!heap || size == 0) {
        return -1;
    }
    
    /* Allocate object array */
    heap->objects = (secd_object_t*)calloc(size, sizeof(secd_object_t));
    if (!heap->objects) {
        return -1;
    }
    
    /* Allocate type array */
    heap->types = (secd_type_t*)calloc(size, sizeof(secd_type_t));
    if (!heap->types) {
        free(heap->objects);
        return -1;
    }
    
    /* Initialize all objects as free */
    memset(heap->types, SECD_TYPE_FREE_MARKER, size);
    
    heap->size = size;
    heap->next_free = 0;
    
    /* Initialize statistics */
    heap->stats.total = size;
    heap->stats.used = 0;
    heap->stats.free = size;
    heap->stats.collections = 0;
    
    /* Initialize byte-vector table + arena */
    memset(heap->bytevecs, 0, sizeof(heap->bytevecs));
    heap->bytevec_count = 0;
    heap->byte_arena = NULL;
    heap->byte_arena_size = 0;
    heap->byte_arena_pos = 0;
    
    return 0;
}

void secd_heap_free(secd_heap_t *heap) {
    if (!heap) return;
    
    if (heap->objects) {
        free(heap->objects);
        heap->objects = NULL;
    }
    
    if (heap->types) {
        free(heap->types);
        heap->types = NULL;
    }
    
    if (heap->byte_arena) {
        free(heap->byte_arena);
        heap->byte_arena = NULL;
    }
    
    heap->size = 0;
    heap->next_free = 0;
}

uint16_t secd_heap_alloc(secd_heap_t *heap, secd_type_t type) {
    if (!heap || !heap->objects || !heap->types) {
        return 0;
    }
    
    /* Find next free slot */
    uint16_t start = heap->next_free;
    uint16_t index = start;
    
    do {
        if (heap->types[index] == SECD_TYPE_FREE_MARKER) {
            /* Found free slot */
            heap->types[index] = type;
            heap->objects[index].car = SECD_NIL;
            heap->objects[index].cdr = SECD_NIL;
            
            /* Update next_free */
            heap->next_free = (index + 1) % heap->size;
            
            /* Update statistics */
            heap->stats.used++;
            heap->stats.free--;
            
            return index + 1; /* 1-based index (0 is reserved) */
        }
        
        index = (index + 1) % heap->size;
    } while (index != start);
    
    /* No free slot found */
    return 0;
}

void secd_heap_free_object(secd_heap_t *heap, uint16_t index) {
    if (!heap || !heap->types || index == 0 || index > heap->size) {
        return;
    }
    
    uint16_t idx = index - 1; /* Convert to 0-based */
    
    if (heap->types[idx] != SECD_TYPE_FREE_MARKER) {
        heap->types[idx] = SECD_TYPE_FREE_MARKER;
        heap->objects[idx].car = SECD_NIL;
        heap->objects[idx].cdr = SECD_NIL;
        
        heap->stats.used--;
        heap->stats.free++;
    }
}

secd_object_t* secd_heap_get(secd_heap_t *heap, uint16_t index) {
    if (!heap || !heap->objects || index == 0 || index > heap->size) {
        return NULL;
    }
    
    return &heap->objects[index - 1]; /* Convert to 0-based */
}

/* Mark bit is stored in the upper bit of type */
#define SECD_MARK_BIT 0x80

secd_type_t secd_heap_get_type(secd_heap_t *heap, uint16_t index) {
    if (!heap || !heap->types || index == 0 || index > heap->size) {
        return SECD_TYPE_FREE;
    }
    
    /* Strip the GC mark bit so callers see the real type */
    return heap->types[index - 1] & ~SECD_MARK_BIT;
}

void secd_heap_set_type(secd_heap_t *heap, uint16_t index, secd_type_t type) {
    if (!heap || !heap->types || index == 0 || index > heap->size) {
        return;
    }
    
    heap->types[index - 1] = type; /* Convert to 0-based */
}

/* Mark bit is stored in the upper bit of type */


void secd_heap_mark(secd_heap_t *heap, uint16_t index) {
    if (!heap || !heap->types || index == 0 || index > heap->size) {
        return;
    }
    
    uint16_t idx = index - 1;
    if (heap->types[idx] != SECD_TYPE_FREE_MARKER) {
        heap->types[idx] |= SECD_MARK_BIT;
    }
}

bool secd_heap_is_marked(secd_heap_t *heap, uint16_t index) {
    if (!heap || !heap->types || index == 0 || index > heap->size) {
        return false;
    }
    
    return (heap->types[index - 1] & SECD_MARK_BIT) != 0;
}

/* Clear all marks */
static void clear_marks(secd_heap_t *heap) {
    for (uint16_t i = 0; i < heap->size; i++) {
        if (heap->types[i] != SECD_TYPE_FREE_MARKER) {
            heap->types[i] &= ~SECD_MARK_BIT;
        }
    }
}

uint16_t secd_heap_gc(secd_heap_t *heap) {
    if (!heap) return 0;
    
    uint16_t freed = 0;
    
    /* Mark phase is done externally (by machine) */
    /* Sweep phase: free unmarked objects */
    for (uint16_t i = 0; i < heap->size; i++) {
        if (heap->types[i] != SECD_TYPE_FREE_MARKER) {
            if (!(heap->types[i] & SECD_MARK_BIT)) {
                /* Not marked, free it */
                heap->types[i] = SECD_TYPE_FREE_MARKER;
                heap->objects[i].car = SECD_NIL;
                heap->objects[i].cdr = SECD_NIL;
                freed++;
            }
        }
    }
    
    /* Clear marks for next collection */
    clear_marks(heap);
    
    /* Update statistics */
    heap->stats.used -= freed;
    heap->stats.free += freed;
    heap->stats.collections++;
    
    return freed;
}

void secd_heap_get_stats(secd_heap_t *heap, secd_heap_stats_t *stats) {
    if (!heap || !stats) return;
    
    *stats = heap->stats;
}

/*
 * Value operations
 */

secd_value_t secd_car(secd_heap_t *heap, secd_value_t pair) {
    if (!secd_is_pair(pair)) {
        return SECD_NIL;
    }
    
    secd_object_t *obj = secd_heap_get(heap, secd_get_index(pair));
    return obj ? obj->car : SECD_NIL;
}

secd_value_t secd_cdr(secd_heap_t *heap, secd_value_t pair) {
    if (!secd_is_pair(pair)) {
        return SECD_NIL;
    }
    
    secd_object_t *obj = secd_heap_get(heap, secd_get_index(pair));
    return obj ? obj->cdr : SECD_NIL;
}

void secd_set_car(secd_heap_t *heap, secd_value_t pair, secd_value_t val) {
    if (!secd_is_pair(pair)) return;
    
    secd_object_t *obj = secd_heap_get(heap, secd_get_index(pair));
    if (obj) {
        obj->car = val;
    }
}

void secd_set_cdr(secd_heap_t *heap, secd_value_t pair, secd_value_t val) {
    if (!secd_is_pair(pair)) return;
    
    secd_object_t *obj = secd_heap_get(heap, secd_get_index(pair));
    if (obj) {
        obj->cdr = val;
    }
}

secd_value_t secd_cons(secd_heap_t *heap, secd_value_t car, secd_value_t cdr) {
    uint16_t index = secd_heap_alloc(heap, SECD_TYPE_PAIR);
    if (index == 0) {
        return SECD_NIL; /* Heap full */
    }
    
    secd_object_t *obj = secd_heap_get(heap, index);
    if (!obj) {
        return SECD_NIL;
    }
    
    obj->car = car;
    obj->cdr = cdr;
    
    return secd_make_pair(index);
}

/*
 * Byte-vector operations.
 */

/* Ensure the RAM byte arena is allocated (lazily, on first make-vector). */
static int bytevec_ensure_arena(secd_heap_t *heap) {
    if (!heap) return -1;
    if (heap->byte_arena) return 0;
    if (SECD_BYTEVEC_ARENA_SIZE == 0) return -1;

    heap->byte_arena = (uint8_t*)malloc(SECD_BYTEVEC_ARENA_SIZE);
    if (!heap->byte_arena) return -1;
    heap->byte_arena_size = SECD_BYTEVEC_ARENA_SIZE;
    heap->byte_arena_pos = 0;
    return 0;
}

uint16_t secd_bytevec_register(secd_heap_t *heap, const uint8_t *data, uint16_t len) {
    if (!heap || !data) return SECD_BYTEVEC_INVALID;
    if (heap->bytevec_count >= SECD_BYTEVEC_MAX) return SECD_BYTEVEC_INVALID;

    secd_bytevec_t *slot = &heap->bytevecs[heap->bytevec_count];
    slot->data = data;
    slot->len = len;
    slot->writable = 0;
    return heap->bytevec_count++;
}

uint16_t secd_bytevec_alloc(secd_heap_t *heap, uint16_t len) {
    if (!heap || len == 0) return SECD_BYTEVEC_INVALID;
    if (heap->bytevec_count >= SECD_BYTEVEC_MAX) return SECD_BYTEVEC_INVALID;
    if (bytevec_ensure_arena(heap) != 0) return SECD_BYTEVEC_INVALID;
    if ((uint32_t)heap->byte_arena_pos + len > heap->byte_arena_size) {
        return SECD_BYTEVEC_INVALID; /* Arena exhausted */
    }

    secd_bytevec_t *slot = &heap->bytevecs[heap->bytevec_count];
    slot->data = heap->byte_arena + heap->byte_arena_pos;
    slot->len = len;
    slot->writable = 1;
    memset((uint8_t*)slot->data, 0, len);
    heap->byte_arena_pos += len;
    return heap->bytevec_count++;
}

secd_bytevec_t* secd_bytevec_get(secd_heap_t *heap, uint16_t slot) {
    if (!heap || slot >= heap->bytevec_count) return NULL;
    return &heap->bytevecs[slot];
}

int secd_bytevec_read(secd_heap_t *heap, uint16_t slot, uint16_t index) {
    secd_bytevec_t *v = secd_bytevec_get(heap, slot);
    if (!v || index >= v->len) return -1;
    return (int)v->data[index];
}

int secd_bytevec_write(secd_heap_t *heap, uint16_t slot, uint16_t index, uint8_t byte) {
    secd_bytevec_t *v = secd_bytevec_get(heap, slot);
    if (!v || index >= v->len) return -1;
    if (!v->writable) return -1; /* ROM literal is read-only */
    ((uint8_t*)v->data)[index] = byte;
    return 0;
}
