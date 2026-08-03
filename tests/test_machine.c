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
#include "secd/heap.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

/* Simple test runner */
#define TEST(name) static void name(void)
#define RUN_TEST(name) do { printf("Running " #name "..."); name(); printf(" OK\n"); } while(0)

/* Test machine initialization */
TEST(test_machine_init) {
    secd_heap_t heap;
    secd_heap_init(&heap, 1024);
    
    secd_machine_t machine;
    int result = secd_machine_init(&machine, &heap);
    assert(result == 0);
    assert(machine.S == SECD_NIL);
    assert(machine.E == SECD_NIL);
    assert(machine.C == SECD_NIL);
    assert(machine.D == SECD_NIL);
    assert(machine.error == SECD_ERROR_NONE);
    assert(machine.running == false);
    
    secd_machine_free(&machine);
    secd_heap_free(&heap);
}

/* Test stack push/pop */
TEST(test_stack_operations) {
    secd_heap_t heap;
    secd_heap_init(&heap, 1024);
    
    secd_machine_t machine;
    secd_machine_init(&machine, &heap);
    
    /* Push values */
    secd_value_t a = secd_make_fixnum(42);
    secd_value_t b = secd_make_fixnum(100);
    
    assert(secd_push(&machine, a) == 0);
    assert(secd_push(&machine, b) == 0);
    
    /* Pop values */
    assert(secd_pop(&machine) == b);
    assert(secd_pop(&machine) == a);
    
    /* Pop from empty stack */
    assert(secd_pop(&machine) == SECD_NIL);
    assert(machine.error == SECD_ERROR_STACK_UNDERFLOW);
    
    secd_machine_free(&machine);
    secd_heap_free(&heap);
}

/* Test environment */
TEST(test_environment) {
    secd_heap_t heap;
    secd_heap_init(&heap, 1024);
    
    secd_machine_t machine;
    secd_machine_init(&machine, &heap);
    
    /* Create environment */
    secd_value_t env = secd_make_env(&machine, SECD_NIL);
    assert(secd_is_pair(env));
    
    /* Define variables */
    secd_value_t sym_x = secd_make_symbol(1);
    secd_value_t val_x = secd_make_fixnum(42);
    
    assert(secd_env_define(&machine, env, sym_x, val_x) == 0);
    
    /* Lookup */
    secd_value_t result = secd_env_lookup(&machine, env, sym_x);
    assert(result == val_x);
    
    secd_machine_free(&machine);
    secd_heap_free(&heap);
}

/* Test error handling */
TEST(test_error_handling) {
    secd_machine_t machine;
    machine.error = SECD_ERROR_NONE;
    
    secd_set_error(&machine, SECD_ERROR_HEAP_FULL);
    assert(machine.error == SECD_ERROR_HEAP_FULL);
    assert(machine.running == false);
    
    assert(strcmp(secd_error_string(SECD_ERROR_STACK_OVERFLOW), "Stack overflow") == 0);
}

int test_machine(void) {
    printf("Running machine tests...\n");
    
    RUN_TEST(test_machine_init);
    RUN_TEST(test_stack_operations);
    RUN_TEST(test_environment);
    RUN_TEST(test_error_handling);
    
    printf("All machine tests passed!\n");
    return 0;
}
