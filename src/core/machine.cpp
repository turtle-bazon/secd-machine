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
#include "secd/machine.h"
#include "secd/bytecode.h"
#include "secd/primitives.h"
#include <string.h>

/*
 * SECD Machine implementation.
 *
 * Implements the core execution loop and stack/environment operations.
 */

/* Global primitive registry */
secd_prim_registry_t prim_registry;

void secd_machine_init_primitives(void) {
    secd_prim_init(&prim_registry);
    secd_register_builtins(&prim_registry);
}

int secd_machine_init(secd_machine_t *machine, secd_heap_t *heap) {
    if (!machine || !heap) {
        return -1;
    }
    
    machine->heap = heap;
    machine->S = SECD_NIL;
    machine->E = SECD_NIL;
    machine->C = SECD_NIL;
    machine->D = SECD_NIL;
    machine->dump_stack = SECD_NIL;
    machine->dump_size = 0;
    machine->error = SECD_ERROR_NONE;
    machine->running = false;
    machine->steps = 0;
    machine->max_steps = 0;
    machine->pool_loaded = false;
    
    /* Initialize primitives */
    secd_machine_init_primitives();
    
    return 0;
}

void secd_machine_free(secd_machine_t *machine) {
    if (!machine) return;
    
    machine->S = SECD_NIL;
    machine->E = SECD_NIL;
    machine->C = SECD_NIL;
    machine->D = SECD_NIL;
    machine->dump_stack = SECD_NIL;
    machine->dump_size = 0;
}

void secd_machine_reset(secd_machine_t *machine) {
    if (!machine) return;
    
    machine->S = SECD_NIL;
    machine->E = SECD_NIL;
    machine->C = SECD_NIL;
    machine->D = SECD_NIL;
    machine->dump_stack = SECD_NIL;
    machine->dump_size = 0;
    machine->error = SECD_ERROR_NONE;
    machine->running = false;
}

/*
 * Stack operations
 */

int secd_push(secd_machine_t *machine, secd_value_t value) {
    if (!machine) return -1;
    
    secd_value_t new_pair = secd_cons(machine->heap, value, machine->S);
    if (secd_is_nil(new_pair)) {
        secd_set_error(machine, SECD_ERROR_STACK_OVERFLOW);
        return -1;
    }
    
    machine->S = new_pair;
    return 0;
}

secd_value_t secd_pop(secd_machine_t *machine) {
    if (!machine || secd_is_nil(machine->S)) {
        if (machine) {
            secd_set_error(machine, SECD_ERROR_STACK_UNDERFLOW);
        }
        return SECD_NIL;
    }
    
    secd_value_t top = secd_car(machine->heap, machine->S);
    machine->S = secd_cdr(machine->heap, machine->S);
    
    return top;
}

secd_value_t secd_peek(secd_machine_t *machine) {
    if (!machine || secd_is_nil(machine->S)) {
        return SECD_NIL;
    }
    
    return secd_car(machine->heap, machine->S);
}

/*
 * Environment operations
 */

secd_value_t secd_make_env(secd_machine_t *machine, secd_value_t parent) {
    /* Environment is a pair (bindings . parent) */
    return secd_cons(machine->heap, SECD_NIL, parent);
}

secd_value_t secd_env_lookup(secd_machine_t *machine, secd_value_t env, secd_value_t symbol) {
    secd_value_t current = env;
    
    while (!secd_is_nil(current)) {
        secd_object_t *env_obj = secd_heap_get(machine->heap, secd_get_index(current));
        if (!env_obj) break;
        
        secd_value_t bindings = env_obj->car;
        
        /* Search bindings list */
        secd_value_t binding = bindings;
        while (!secd_is_nil(binding)) {
            secd_object_t *binding_obj = secd_heap_get(machine->heap, secd_get_index(binding));
            if (!binding_obj) break;
            
            secd_value_t pair = binding_obj->car;
            if (secd_is_pair(pair)) {
                secd_object_t *pair_obj = secd_heap_get(machine->heap, secd_get_index(pair));
                if (pair_obj && secd_is_symbol(pair_obj->car) && 
                    secd_get_index(pair_obj->car) == secd_get_index(symbol)) {
                    return pair_obj->cdr; /* Found */
                }
            }
            
            binding = binding_obj->cdr;
        }
        
        /* Move to parent environment */
        current = env_obj->cdr;
    }
    
    return SECD_NIL; /* Not found */
}

int secd_env_define(secd_machine_t *machine, secd_value_t env, secd_value_t symbol, secd_value_t value) {
    if (secd_is_nil(env)) return -1;
    
    secd_object_t *env_obj = secd_heap_get(machine->heap, secd_get_index(env));
    if (!env_obj) return -1;
    
    /* Create binding pair */
    secd_value_t binding = secd_cons(machine->heap, symbol, value);
    if (secd_is_nil(binding)) return -1;
    
    /* Add to bindings list */
    secd_value_t new_bindings = secd_cons(machine->heap, binding, env_obj->car);
    if (secd_is_nil(new_bindings)) return -1;
    
    env_obj->car = new_bindings;
    
    return 0;
}

/*
 * Dump operations (separate dump stack)
 */

int secd_dump_push(secd_machine_t *machine, secd_value_t value) {
    if (!machine) return -1;
    
    secd_value_t new_pair = secd_cons(machine->heap, value, machine->dump_stack);
    if (secd_is_nil(new_pair)) {
        return -1; /* Heap full */
    }
    
    machine->dump_stack = new_pair;
    machine->dump_size++;
    return 0;
}

secd_value_t secd_dump_pop(secd_machine_t *machine) {
    if (!machine || secd_is_nil(machine->dump_stack)) {
        return SECD_NIL;
    }
    
    secd_value_t top = secd_car(machine->heap, machine->dump_stack);
    machine->dump_stack = secd_cdr(machine->heap, machine->dump_stack);
    machine->dump_size--;
    
    return top;
}

/*
 * Error handling
 */

void secd_set_error(secd_machine_t *machine, uint8_t error_code) {
    if (machine) {
        machine->error = error_code;
        machine->running = false;
    }
}

uint8_t secd_get_error(secd_machine_t *machine) {
    return machine ? machine->error : SECD_ERROR_NONE;
}

const char* secd_error_string(uint8_t error_code) {
    switch (error_code) {
        case SECD_ERROR_NONE:              return "No error";
        case SECD_ERROR_STACK_OVERFLOW:    return "Stack overflow";
        case SECD_ERROR_STACK_UNDERFLOW:   return "Stack underflow";
        case SECD_ERROR_HEAP_FULL:         return "Heap full";
        case SECD_ERROR_INVALID_HANDLE:    return "Invalid handle";
        case SECD_ERROR_TYPE_ERROR:        return "Type error";
        case SECD_ERROR_DIVISION_BY_ZERO:  return "Division by zero";
        case SECD_ERROR_UNDEFINED_SYMBOL:  return "Undefined symbol";
        case SECD_ERROR_PRIMITIVE_NOT_FOUND: return "Primitive not found";
        default:                           return "Unknown error";
    }
}
