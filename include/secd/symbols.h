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
#ifndef SECD_SYMBOLS_H
#define SECD_SYMBOLS_H

#include "secd/types.h"
#include "secd/heap.h"
#include <stdint.h>

/*
 * Symbol table implementation.
 *
 * Symbols are stored as length-prefixed strings.
 * The symbol table is a sorted array for binary search.
 * Each entry maps a string to a handle (for the symbol in the heap).
 */

/* Symbol table entry */
typedef struct {
    char *name;             /* Symbol name (dynamically allocated) */
    secd_value_t value;     /* Handle to symbol in heap */
} secd_symbol_entry_t;

/* Symbol table */
typedef struct {
    secd_symbol_entry_t *entries;
    uint16_t capacity;
    uint16_t count;
} secd_symbol_table_t;

/* Initialize symbol table */
int secd_symbols_init(secd_symbol_table_t *table, uint16_t capacity);

/* Free symbol table */
void secd_symbols_free(secd_symbol_table_t *table);

/* Insert symbol */
int secd_symbols_insert(secd_symbol_table_t *table, const char *name, secd_value_t value);

/* Lookup symbol */
secd_value_t secd_symbols_lookup(secd_symbol_table_t *table, const char *name);

/* Intern symbol (create if not exists) */
secd_value_t secd_symbols_intern(secd_symbol_table_t *table, const char *name, secd_heap_t *heap);

/* Get symbol name */
const char* secd_symbols_name(secd_symbol_table_t *table, secd_value_t symbol);

/* Get symbol count */
uint16_t secd_symbols_count(secd_symbol_table_t *table);

#endif /* SECD_SYMBOLS_H */

#ifdef __cplusplus
}
#endif
