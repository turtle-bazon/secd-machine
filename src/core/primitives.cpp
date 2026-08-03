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
#include "secd/primitives.h"
#include "secd/machine.h"
#include "hal/hal.h"
#include <stdlib.h>
#include <string.h>

/*
 * Primitive operations implementation.
 *
 * Primitives are C functions exposed to the SECD VM.
 * They receive arguments as a list and return a value.
 */

void secd_prim_init(secd_prim_registry_t *registry) {
    if (!registry) return;
    
    registry->count = 0;
    memset(registry->entries, 0, sizeof(registry->entries));
}

int secd_register_prim(secd_prim_registry_t *registry, const char *name, secd_prim_fn fn) {
    if (!registry || !name || !fn) return -1;
    
    if (registry->count >= SECD_PRIMITIVES_MAX) {
        return -1; /* Registry full */
    }
    
    secd_prim_entry_t *entry = &registry->entries[registry->count];
    entry->name = name;
    entry->fn = fn;
    entry->id = registry->count;
    
    registry->count++;
    
    return 0;
}

secd_prim_entry_t* secd_find_prim(secd_prim_registry_t *registry, const char *name) {
    if (!registry || !name) return NULL;
    
    for (uint8_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->entries[i].name, name) == 0) {
            return &registry->entries[i];
        }
    }
    
    return NULL;
}

secd_prim_entry_t* secd_find_prim_by_id(secd_prim_registry_t *registry, uint8_t id) {
    if (!registry || id >= registry->count) return NULL;
    
    return &registry->entries[id];
}

secd_value_t secd_call_prim(secd_prim_registry_t *registry, uint8_t id, secd_heap_t *heap, secd_value_t args) {
    if (!registry || id >= registry->count) {
        return SECD_NIL;
    }
    
    secd_prim_entry_t *entry = &registry->entries[id];
    if (!entry->fn) {
        return SECD_NIL;
    }
    
    return entry->fn(heap, args);
}

/*
 * Built-in primitive implementations
 */

/* Helper: get first argument */
static secd_value_t get_arg1(secd_heap_t *heap, secd_value_t args) {
    return secd_car(heap, args);
}

/* Helper: get second argument */
static secd_value_t get_arg2(secd_heap_t *heap, secd_value_t args) {
    return secd_car(heap, secd_cdr(heap, args));
}

/* List operations */
secd_value_t prim_car(secd_heap_t *heap, secd_value_t args) {
    secd_value_t pair = get_arg1(heap, args);
    return secd_car(heap, pair);
}

secd_value_t prim_cdr(secd_heap_t *heap, secd_value_t args) {
    secd_value_t pair = get_arg1(heap, args);
    return secd_cdr(heap, pair);
}

secd_value_t prim_cons(secd_heap_t *heap, secd_value_t args) {
    secd_value_t car = get_arg1(heap, args);
    secd_value_t cdr = get_arg2(heap, args);
    return secd_cons(heap, car, cdr);
}

secd_value_t prim_list(secd_heap_t *heap, secd_value_t args) {
    (void)heap; /* Unused parameter */
    /* list is just args itself */
    return args;
}

/* Arithmetic operations */
secd_value_t prim_add(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    return secd_make_fixnum(a + b);
}

secd_value_t prim_sub(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    return secd_make_fixnum(a - b);
}

secd_value_t prim_mul(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    return secd_make_fixnum(a * b);
}

secd_value_t prim_div(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    if (b == 0) {
        return SECD_NIL; /* Error: division by zero */
    }
    return secd_make_fixnum(a / b);
}

secd_value_t prim_mod(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    if (b == 0) {
        return SECD_NIL; /* Error: division by zero */
    }
    return secd_make_fixnum(a % b);
}

/* Comparison operations */
secd_value_t prim_eq(secd_heap_t *heap, secd_value_t args) {
    secd_value_t a = get_arg1(heap, args);
    secd_value_t b = get_arg2(heap, args);
    return secd_make_bool(a == b);
}

secd_value_t prim_lt(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    return secd_make_bool(a < b);
}

secd_value_t prim_gt(secd_heap_t *heap, secd_value_t args) {
    int16_t a = secd_fixnum_value(get_arg1(heap, args));
    int16_t b = secd_fixnum_value(get_arg2(heap, args));
    return secd_make_bool(a > b);
}

/* Type checking */
secd_value_t prim_null(secd_heap_t *heap, secd_value_t args) {
    secd_value_t val = get_arg1(heap, args);
    return secd_make_bool(secd_is_nil(val));
}

secd_value_t prim_pair(secd_heap_t *heap, secd_value_t args) {
    secd_value_t val = get_arg1(heap, args);
    return secd_make_bool(secd_is_pair(val));
}

secd_value_t prim_atom(secd_heap_t *heap, secd_value_t args) {
    secd_value_t val = get_arg1(heap, args);
    /* atom is true if not a pair and not nil */
    return secd_make_bool(!secd_is_pair(val) && !secd_is_nil(val));
}

/*
 * HAL primitives (RP2040 target).
 * Registration order MUST match the "primitives" table in
 * targets/rp2040-pico.json so ids line up.
 */

secd_value_t prim_gpio_init(secd_heap_t *heap, secd_value_t args) {
    uint8_t pin = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    uint8_t mode = (uint8_t)secd_fixnum_value(get_arg2(heap, args));
    return secd_make_fixnum(hal_gpio_init(pin, mode));
}

secd_value_t prim_gpio_write(secd_heap_t *heap, secd_value_t args) {
    uint8_t pin = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    uint8_t value = (uint8_t)secd_fixnum_value(get_arg2(heap, args));
    return secd_make_fixnum(hal_gpio_write(pin, value));
}

secd_value_t prim_gpio_read(secd_heap_t *heap, secd_value_t args) {
    uint8_t pin = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    return secd_make_fixnum(hal_gpio_read(pin));
}

secd_value_t prim_uart_init(secd_heap_t *heap, secd_value_t args) {
    uint32_t baud = (uint32_t)secd_fixnum_value(get_arg1(heap, args));
    hal_serial_init(baud);
    return SECD_NIL;
}

secd_value_t prim_uart_write(secd_heap_t *heap, secd_value_t args) {
    uint8_t byte = (uint8_t)secd_fixnum_value(get_arg1(heap, args));
    hal_serial_write(byte);
    return SECD_NIL;
}

secd_value_t prim_uart_read(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    (void)args;
    return secd_make_fixnum(hal_serial_read());
}

secd_value_t prim_sleep(secd_heap_t *heap, secd_value_t args) {
    uint32_t ms = (uint32_t)secd_fixnum_value(get_arg1(heap, args));
    hal_sleep(ms);
    return SECD_NIL;
}

secd_value_t prim_millis(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    (void)args;
    return secd_make_fixnum((int16_t)hal_millis());
}

secd_value_t prim_adc_read(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    (void)args;
    return secd_make_fixnum(0);
}

secd_value_t prim_pwm_write(secd_heap_t *heap, secd_value_t args) {
    (void)heap;
    (void)args;
    return secd_make_fixnum(0);
}

/* Register all built-in primitives */
void secd_register_builtins(secd_prim_registry_t *registry) {
    if (!registry) return;
    
    /* List operations */
    secd_register_prim(registry, "car", prim_car);
    secd_register_prim(registry, "cdr", prim_cdr);
    secd_register_prim(registry, "cons", prim_cons);
    secd_register_prim(registry, "list", prim_list);
    
    /* Arithmetic */
    secd_register_prim(registry, "+", prim_add);
    secd_register_prim(registry, "-", prim_sub);
    secd_register_prim(registry, "*", prim_mul);
    secd_register_prim(registry, "/", prim_div);
    secd_register_prim(registry, "%", prim_mod);
    
    /* Comparison */
    secd_register_prim(registry, "=", prim_eq);
    secd_register_prim(registry, "<", prim_lt);
    secd_register_prim(registry, ">", prim_gt);
    
    /* Type checking */
    secd_register_prim(registry, "null?", prim_null);
    secd_register_prim(registry, "pair?", prim_pair);
    secd_register_prim(registry, "atom?", prim_atom);
    
    /* HAL primitives (ids 14-23, order matches target metadata) */
    secd_register_prim(registry, "gpio-init", prim_gpio_init);
    secd_register_prim(registry, "gpio-write", prim_gpio_write);
    secd_register_prim(registry, "gpio-read", prim_gpio_read);
    secd_register_prim(registry, "uart-init", prim_uart_init);
    secd_register_prim(registry, "uart-write", prim_uart_write);
    secd_register_prim(registry, "uart-read", prim_uart_read);
    secd_register_prim(registry, "sleep", prim_sleep);
    secd_register_prim(registry, "millis", prim_millis);
    secd_register_prim(registry, "adc-read", prim_adc_read);
    secd_register_prim(registry, "pwm-write", prim_pwm_write);
}
