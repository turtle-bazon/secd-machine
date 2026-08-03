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
#include <stdio.h>
#include <assert.h>

/* Simple test runner */
#define TEST(name) static void name(void)
#define RUN_TEST(name) do { printf("Running " #name "..."); name(); printf(" OK\n"); } while(0)

/* Test heap initialization */
TEST(test_heap_init) {
    secd_heap_t heap;
    int result = secd_heap_init(&heap, 128);
    assert(result == 0);
    assert(heap.size == 128);
    assert(heap.stats.total == 128);
    assert(heap.stats.used == 0);
    assert(heap.stats.free == 128);
    secd_heap_free(&heap);
}

/* Test allocation */
TEST(test_heap_alloc) {
    secd_heap_t heap;
    secd_heap_init(&heap, 128);
    
    uint16_t idx1 = secd_heap_alloc(&heap, SECD_TYPE_FIXNUM);
    assert(idx1 == 1); /* First allocation */
    
    uint16_t idx2 = secd_heap_alloc(&heap, SECD_TYPE_PAIR);
    assert(idx2 == 2);
    
    assert(heap.stats.used == 2);
    assert(heap.stats.free == 126);
    
    secd_heap_free(&heap);
}

/* Test object access */
TEST(test_heap_get) {
    secd_heap_t heap;
    secd_heap_init(&heap, 128);
    
    uint16_t idx = secd_heap_alloc(&heap, SECD_TYPE_PAIR);
    secd_object_t *obj = secd_heap_get(&heap, idx);
    assert(obj != NULL);
    assert(obj->car == SECD_NIL);
    assert(obj->cdr == SECD_NIL);
    
    secd_heap_free(&heap);
}

/* Test cons */
TEST(test_cons) {
    secd_heap_t heap;
    secd_heap_init(&heap, 128);
    
    secd_value_t a = secd_make_fixnum(42);
    secd_value_t b = secd_make_fixnum(100);
    
    secd_value_t pair = secd_cons(&heap, a, b);
    assert(secd_is_pair(pair));
    
    assert(secd_car(&heap, pair) == a);
    assert(secd_cdr(&heap, pair) == b);
    
    secd_heap_free(&heap);
}

/* Test garbage collection */
TEST(test_gc) {
    secd_heap_t heap;
    secd_heap_init(&heap, 128);
    
    /* Allocate some objects */
    uint16_t idx1 = secd_heap_alloc(&heap, SECD_TYPE_FIXNUM);
    uint16_t idx2 = secd_heap_alloc(&heap, SECD_TYPE_PAIR);
    secd_heap_alloc(&heap, SECD_TYPE_SYMBOL); /* This will be freed */
    
    /* Mark only first two */
    secd_heap_mark(&heap, idx1);
    secd_heap_mark(&heap, idx2);
    
    /* Run GC */
    uint16_t freed = secd_heap_gc(&heap);
    assert(freed == 1); /* Only the third should be freed */
    
    assert(heap.stats.used == 2);
    assert(heap.stats.free == 126);
    
    secd_heap_free(&heap);
}

int test_heap(void) {
    printf("Running heap tests...\n");
    
    RUN_TEST(test_heap_init);
    RUN_TEST(test_heap_alloc);
    RUN_TEST(test_heap_get);
    RUN_TEST(test_cons);
    RUN_TEST(test_gc);
    
    printf("All heap tests passed!\n");
    return 0;
}
