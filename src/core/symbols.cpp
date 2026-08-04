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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "secd/symbols.h"
#include <stdlib.h>
#include <string.h>

/*
 * Symbol table implementation.
 *
 * Symbols are stored as length-prefixed strings.
 * The symbol table is a sorted array for binary search.
 * Each entry maps a string to a handle (for the symbol in the heap).
 */

int secd_symbols_init(secd_symbol_table_t *table, uint16_t capacity) {
    if (!table || capacity == 0) {
        return -1;
    }
    
    table->entries = (secd_symbol_entry_t*)calloc(capacity, sizeof(secd_symbol_entry_t));
    if (!table->entries) {
        return -1;
    }
    
    table->capacity = capacity;
    table->count = 0;
    
    return 0;
}

void secd_symbols_free(secd_symbol_table_t *table) {
    if (!table) return;
    
    if (table->entries) {
        for (uint16_t i = 0; i < table->count; i++) {
            if (table->entries[i].name) {
                free(table->entries[i].name);
            }
        }
        free(table->entries);
        table->entries = NULL;
    }
    
    table->capacity = 0;
    table->count = 0;
}

/* Binary search for symbol */
static int find_symbol(secd_symbol_table_t *table, const char *name) {
    int left = 0;
    int right = table->count - 1;
    
    while (left <= right) {
        int mid = (left + right) / 2;
        int cmp = strcmp(table->entries[mid].name, name);
        
        if (cmp == 0) {
            return mid; /* Found */
        } else if (cmp < 0) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    
    return -1; /* Not found */
}

/* Insert symbol at position (shifting entries) */
static int insert_at(secd_symbol_table_t *table, int pos, const char *name, secd_value_t value) {
    if (table->count >= table->capacity) {
        return -1; /* Table full */
    }
    
    /* Make space */
    for (int i = table->count; i > pos; i--) {
        table->entries[i] = table->entries[i - 1];
    }
    
    /* Copy name */
    char *copy = strdup(name);
    if (!copy) {
        return -1;
    }
    
    table->entries[pos].name = copy;
    table->entries[pos].value = value;
    table->count++;
    
    return 0;
}

int secd_symbols_insert(secd_symbol_table_t *table, const char *name, secd_value_t value) {
    if (!table || !name) {
        return -1;
    }
    
    /* Check if already exists */
    int idx = find_symbol(table, name);
    if (idx >= 0) {
        /* Update existing */
        table->entries[idx].value = value;
        return 0;
    }
    
    /* Find insertion point (keep sorted) */
    int pos = 0;
    while (pos < table->count && strcmp(table->entries[pos].name, name) < 0) {
        pos++;
    }
    
    return insert_at(table, pos, name, value);
}

secd_value_t secd_symbols_lookup(secd_symbol_table_t *table, const char *name) {
    if (!table || !name) {
        return SECD_NIL;
    }
    
    int idx = find_symbol(table, name);
    if (idx >= 0) {
        return table->entries[idx].value;
    }
    
    return SECD_NIL; /* Not found */
}

secd_value_t secd_symbols_intern(secd_symbol_table_t *table, const char *name, secd_heap_t *heap) {
    if (!table || !name) {
        return SECD_NIL;
    }
    
    /* Check if already interned */
    secd_value_t existing = secd_symbols_lookup(table, name);
    if (!secd_is_nil(existing)) {
        return existing;
    }
    
    /* Create new symbol in heap */
    uint16_t sym_index = secd_heap_alloc(heap, SECD_TYPE_SYMBOL);
    if (sym_index == 0) {
        return SECD_NIL; /* Heap full */
    }
    
    secd_value_t sym = secd_make_symbol(sym_index);
    
    /* Add to table */
    if (secd_symbols_insert(table, name, sym) != 0) {
        secd_heap_free_object(heap, sym_index);
        return SECD_NIL;
    }
    
    return sym;
}

const char* secd_symbols_name(secd_symbol_table_t *table, secd_value_t symbol) {
    if (!table || !secd_is_symbol(symbol)) {
        return NULL;
    }
    
    uint16_t idx = secd_get_index(symbol);
    
    /* Linear search by value */
    for (uint16_t i = 0; i < table->count; i++) {
        if (secd_get_index(table->entries[i].value) == idx) {
            return table->entries[i].name;
        }
    }
    
    return NULL; /* Not found */
}

uint16_t secd_symbols_count(secd_symbol_table_t *table) {
    return table ? table->count : 0;
}
