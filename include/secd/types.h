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
#ifndef SECD_TYPES_H
#define SECD_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Value representation using 16-bit indirect handles.
 *
 * A value_t is a 16-bit handle that references an object in the heap.
 * The handle consists of:
 *   - Type bits (4 bits): identifies the kind of value
 *   - Index bits (12 bits): index into the heap/object table
 *
 * Special values (not in heap):
 *   - NIL: empty list
 *   - Boolean T/F
 */

/* Handle type tags (upper 4 bits) */
#define SECD_TYPE_NIL      0x0
#define SECD_TYPE_FIXNUM   0x1
#define SECD_TYPE_PAIR     0x2
#define SECD_TYPE_SYMBOL   0x3
#define SECD_TYPE_BOOL     0x4
#define SECD_TYPE_CLOSURE  0x5
#define SECD_TYPE_BIGNUM   0x6
#define SECD_TYPE_FREE     0x7

/* Type mask and shift */
#define SECD_TYPE_MASK     0xF000
#define SECD_INDEX_MASK    0x0FFF
#define SECD_TYPE_SHIFT    12

/* Special values */
#define SECD_NIL           ((secd_value_t)0)
#define SECD_TRUE          ((secd_value_t)0x8000)
#define SECD_FALSE         ((secd_value_t)0x9000)

/* Value type */
typedef uint16_t secd_value_t;

/* Object in heap - 4 bytes */
typedef struct {
    secd_value_t car;   /* First value / data */
    secd_value_t cdr;   /* Second value / next */
} secd_object_t;

/* Type info for each object - stored separately for compactness */
typedef uint8_t secd_type_t;

/*
 * Value operations
 */

/* Create a handle from type and index */
static inline secd_value_t secd_make_handle(secd_type_t type, uint16_t index) {
    return ((secd_value_t)type << SECD_TYPE_SHIFT) | (index & SECD_INDEX_MASK);
}

/* Extract type from handle */
static inline secd_type_t secd_get_type(secd_value_t val) {
    if (val == SECD_NIL) return SECD_TYPE_NIL;
    if (val == SECD_TRUE || val == SECD_FALSE) return SECD_TYPE_BOOL;
    return (val & SECD_TYPE_MASK) >> SECD_TYPE_SHIFT;
}

/* Extract index from handle */
static inline uint16_t secd_get_index(secd_value_t val) {
    return val & SECD_INDEX_MASK;
}

/* Check if value is nil */
static inline bool secd_is_nil(secd_value_t val) {
    return val == SECD_NIL;
}

/* Check if value is boolean */
static inline bool secd_is_bool(secd_value_t val) {
    return val == SECD_TRUE || val == SECD_FALSE;
}

/* Check if value is fixnum */
static inline bool secd_is_fixnum(secd_value_t val) {
    return secd_get_type(val) == SECD_TYPE_FIXNUM;
}

/* Check if value is pair */
static inline bool secd_is_pair(secd_value_t val) {
    return secd_get_type(val) == SECD_TYPE_PAIR;
}

/* Check if value is symbol */
static inline bool secd_is_symbol(secd_value_t val) {
    return secd_get_type(val) == SECD_TYPE_SYMBOL;
}

/* Check if value is closure */
static inline bool secd_is_closure(secd_value_t val) {
    return secd_get_type(val) == SECD_TYPE_CLOSURE;
}

/* Check if value is bignum */
static inline bool secd_is_bignum(secd_value_t val) {
    return secd_get_type(val) == SECD_TYPE_BIGNUM;
}

/*
 * Fixnum operations
 *
 * Fixnums are immediate values: the 16-bit handle is composed of a 4-bit type
 * tag plus a 12-bit index, and a fixnum's number payload occupies those 12
 * index bits.  Therefore the representable range is signed 12-bit:
 * -2048 to 2047.  Values are stored as the low 12 bits (two's complement) and
 * sign-extended on read.
 */
#define SECD_FIXNUM_MIN (-2048)
#define SECD_FIXNUM_MAX 2047

static inline secd_value_t secd_make_fixnum(int16_t val) {
    return secd_make_handle(SECD_TYPE_FIXNUM, ((uint16_t)val & 0x0FFF));
}

static inline int16_t secd_fixnum_value(secd_value_t val) {
    /* Sign-extend the 12-bit payload (bit 11 is the sign bit). */
    uint16_t i = (uint16_t)(val & SECD_INDEX_MASK);
    return (int16_t)((i ^ 0x0800) - 0x0800);
}

/*
 * Boolean operations
 */

static inline secd_value_t secd_make_bool(bool val) {
    return val ? SECD_TRUE : SECD_FALSE;
}

static inline bool secd_bool_value(secd_value_t val) {
    return val == SECD_TRUE;
}

/*
 * Pair operations (handle creation, actual storage in heap)
 */

static inline secd_value_t secd_make_pair(uint16_t index) {
    return secd_make_handle(SECD_TYPE_PAIR, index);
}

/*
 * Symbol operations (handle creation, actual storage in heap)
 */

static inline secd_value_t secd_make_symbol(uint16_t index) {
    return secd_make_handle(SECD_TYPE_SYMBOL, index);
}

/*
 * Closure operations (handle creation, actual storage in heap)
 */

static inline secd_value_t secd_make_closure(uint16_t index) {
    return secd_make_handle(SECD_TYPE_CLOSURE, index);
}

#endif /* SECD_TYPES_H */

#ifdef __cplusplus
}
#endif
