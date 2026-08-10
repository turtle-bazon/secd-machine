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
#include "secd/bytecode.h"
#include "secd/machine.h"
#include "secd/primitives.h"
#include "hal/hal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * Bytecode loader and executor.
 *
 * Loads bytecode from memory and executes it.
 */

/* Global primitive registry (extern) */
extern secd_prim_registry_t prim_registry;

int secd_inst_length(uint8_t opcode) {
    switch (opcode) {
        /* 1-byte instructions */
        case OP_STOP:
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:
        case OP_NEG:
        case OP_EQ:
        case OP_LT:
        case OP_GT:
        case OP_LE:
        case OP_GE:
        case OP_NOT:
        case OP_CAR:
        case OP_CDR:
        case OP_CONS:
        case OP_VREF:
        case OP_VSTOR:
        case OP_MKV:
        case OP_LEN:
        case OP_JOIN:
        case OP_APP:
        case OP_RTN:
        case OP_PRN:
        case OP_DUMP:
        case OP_GC:
        case OP_NOP:
        case OP_POP:
            return 1;
        
        /* 2-byte instructions */
        case OP_LDM:
        case OP_LDC:
        case OP_LDF:
        case OP_LD:
        case OP_ST:
        case OP_ARGS:
        case OP_PRIM:
        case OP_BRK:
        case OP_ERROR:
            return 2;
        
        /* 3-byte instructions */
        case OP_LDV:
        case OP_SEL:
        case OP_LOOP:
        case OP_CALL:
        case OP_JMP:
            return 3;
        
        default:
            return 1;
    }
}

uint16_t secd_read_u16(const uint8_t *data) {
    return ((uint16_t)data[0] << 8) | data[1];
}

int16_t secd_read_i16(const uint8_t *data) {
    return (int16_t)secd_read_u16(data);
}

/*
 * Load the ROM byte-vector pool appended after OP_STOP (see the pool
 * footer layout in bytecode.h).  Registers each literal as descriptor slot
 * N, matching the slot index the compiler baked into the preceding OP_LDV
 * instructions.  Should only run once per bytecode buffer.
 */
static int secd_load_pool(secd_machine_t *machine, const uint8_t *bytecode, size_t length) {
    if (!machine || !bytecode || length < 4) return -1;
    if (machine->pool_loaded) return 0;

    /* Trailer: [u16 pool_size][u16 magic] */
    if (secd_read_u16(&bytecode[length - 2]) != SECD_POOL_MAGIC) {
        return 0; /* No pool present; nothing to do */
    }
    uint16_t pool_size = secd_read_u16(&bytecode[length - 4]);
    if ((size_t)pool_size + 4 > length) return -1; /* Corrupt footer */

    size_t pool_start = length - 4 - pool_size;
    uint16_t count = secd_read_u16(&bytecode[pool_start]);
    size_t dir = pool_start + 2;                 /* count * (off, len) entries */
    size_t data = pool_start + 2 + 4 * (size_t)count;

    for (uint16_t i = 0; i < count; i++) {
        if (data + 4 * (size_t)i >= length) return -1;
        uint16_t off = secd_read_u16(&bytecode[dir + 4 * (size_t)i]);
        uint16_t len = secd_read_u16(&bytecode[dir + 4 * (size_t)i + 2]);
        if ((size_t)off + len > pool_size) return -1; /* Out of pool bounds */
        if (secd_bytevec_register(machine->heap, &bytecode[data + off], len)
            == SECD_BYTEVEC_INVALID) {
            return -1; /* Descriptor table full */
        }
    }
    machine->pool_loaded = true;
    return 0;
}

/* Execute bytecode */
int secd_execute(secd_machine_t *machine, const uint8_t *bytecode, size_t length) {
    if (!machine || !bytecode || length == 0) {
        return -1;
    }
    
    /* Register ROM byte-vector literals (once per buffer) */
    if (secd_load_pool(machine, bytecode, length) != 0) {
        secd_set_error(machine, SECD_ERROR_INVALID_HANDLE);
        return -1;
    }

    machine->running = true;
    machine->error = SECD_ERROR_NONE;
    
    /* Simple instruction pointer */
    size_t ip = 0;
    
    while (machine->running && ip < length && machine->error == SECD_ERROR_NONE) {
        if (machine->max_steps != 0 && machine->steps >= machine->max_steps) {
            break;
        }
        machine->steps++;

        if (machine->heap->stats.free < SECD_GC_THRESHOLD) {
            secd_gc_run(machine);
        }

        uint8_t opcode = bytecode[ip];
        switch (opcode) {
            case OP_JMP: {
                /* Unconditional jump */
                ip = secd_read_u16(&bytecode[ip + 1]);
                break;
            }

            case OP_STOP:
                machine->running = false;
                break;
            
            case OP_LDM: {
                /* Load from memory address (heap) */
                uint16_t addr = secd_read_u16(&bytecode[ip + 1]);
                secd_object_t *obj = secd_heap_get(machine->heap, addr);
                secd_value_t val = obj ? obj->car : SECD_NIL;
                if (secd_push(machine, val) != 0) {
                    return -1;
                }
                ip += 3;
                break;
            }
            
            case OP_LDC: {
                /* Load constant */
                int16_t value = secd_read_i16(&bytecode[ip + 1]);
                if (secd_push(machine, secd_make_fixnum(value)) != 0) {
                    return -1;
                }
                ip += 3;
                break;
            }
            
            case OP_LDF: {
                /* Load function (closure) - creates a closure with current environment */
                uint16_t addr = secd_read_u16(&bytecode[ip + 1]);
                /* Closure is a pair: (bytecode-address . environment) */
                secd_value_t closure = secd_cons(machine->heap, 
                    secd_make_fixnum(addr), 
                    machine->E);
                if (secd_push(machine, closure) != 0) {
                    return -1;
                }
                ip += 3;
                break;
            }
            
            case OP_LDE: {
                /* Load environment */
                if (secd_push(machine, machine->E) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_ARGS: {
                /* Bind `count` arguments popped from the S stack into the
                   environment. E is a flat list of bound values, innermost
                   first; new args are consed onto the front so parameter 0
                   is the head. The S-stack top holds the LAST argument
                   (the callee pushes args last-first), hence the reversal
                   via cons. */
                uint8_t count = bytecode[ip + 1];
                for (uint8_t i = 0; i < count; i++) {
                    if (secd_is_nil(machine->S)) {
                        secd_set_error(machine, SECD_ERROR_STACK_UNDERFLOW);
                        return -1;
                    }
                    machine->E = secd_cons(machine->heap, secd_pop(machine), machine->E);
                }
                ip += 2;
                break;
            }
            
            case OP_LD: {
                /* Load variable at E[index] (innermost-first). */
                uint16_t index = secd_read_u16(&bytecode[ip + 1]);
                secd_value_t e = machine->E;
                uint16_t i = 0;
                while (secd_is_pair(e) && i < index) {
                    e = secd_cdr(machine->heap, e);
                    i++;
                }
                secd_value_t val = secd_is_pair(e) ? secd_car(machine->heap, e) : SECD_NIL;
                if (secd_push(machine, val) != 0) {
                    return -1;
                }
                ip += 3;
                break;
            }
            
            case OP_ST: {
                /* Store into variable at index `index`; leaves the value on
                 * the stack (setf yields the stored value). */
                uint16_t index = secd_read_u16(&bytecode[ip + 1]);
                secd_value_t val = secd_pop(machine);
                secd_value_t e = machine->E;
                uint16_t i = 0;
                while (secd_is_pair(e) && i < index) {
                    e = secd_cdr(machine->heap, e);
                    i++;
                }
                if (secd_is_pair(e)) {
                    secd_set_car(machine->heap, e, val);
                }
                if (secd_push(machine, val) != 0) {
                    return -1;
                }
                ip += 3;
                break;
            }
            
            case OP_ADD: {
                secd_value_t b = secd_pop(machine);
                secd_value_t a = secd_pop(machine);
                secd_value_t result = prim_add(machine->heap, secd_cons(machine->heap, a, secd_cons(machine->heap, b, SECD_NIL)));
                if (secd_push(machine, result) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_SUB: {
                secd_value_t b = secd_pop(machine);
                secd_value_t a = secd_pop(machine);
                secd_value_t result = prim_sub(machine->heap, secd_cons(machine->heap, a, secd_cons(machine->heap, b, SECD_NIL)));
                if (secd_push(machine, result) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_MUL: {
                secd_value_t b = secd_pop(machine);
                secd_value_t a = secd_pop(machine);
                secd_value_t result = prim_mul(machine->heap, secd_cons(machine->heap, a, secd_cons(machine->heap, b, SECD_NIL)));
                if (secd_push(machine, result) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_DIV: {
                secd_value_t b = secd_pop(machine);
                secd_value_t a = secd_pop(machine);
                secd_value_t result = prim_div(machine->heap, secd_cons(machine->heap, a, secd_cons(machine->heap, b, SECD_NIL)));
                if (secd_push(machine, result) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_EQ: {
                secd_value_t b = secd_pop(machine);
                secd_value_t a = secd_pop(machine);
                secd_value_t result = prim_eq(machine->heap, secd_cons(machine->heap, a, secd_cons(machine->heap, b, SECD_NIL)));
                if (secd_push(machine, result) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_LT: {
                secd_value_t b = secd_pop(machine);
                secd_value_t a = secd_pop(machine);
                secd_value_t result = prim_lt(machine->heap, secd_cons(machine->heap, a, secd_cons(machine->heap, b, SECD_NIL)));
                if (secd_push(machine, result) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_GT: {
                secd_value_t b = secd_pop(machine);
                secd_value_t a = secd_pop(machine);
                secd_value_t result = prim_gt(machine->heap, secd_cons(machine->heap, a, secd_cons(machine->heap, b, SECD_NIL)));
                if (secd_push(machine, result) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_LE: {
                /* Less than or equal */
                secd_value_t b = secd_pop(machine);
                secd_value_t a = secd_pop(machine);
                int16_t av = secd_fixnum_value(a);
                int16_t bv = secd_fixnum_value(b);
                if (secd_push(machine, secd_make_bool(av <= bv)) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_GE: {
                /* Greater than or equal */
                secd_value_t b = secd_pop(machine);
                secd_value_t a = secd_pop(machine);
                int16_t av = secd_fixnum_value(a);
                int16_t bv = secd_fixnum_value(b);
                if (secd_push(machine, secd_make_bool(av >= bv)) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_NOT: {
                /* Logical not */
                secd_value_t a = secd_pop(machine);
                if (secd_push(machine, secd_make_bool(!secd_bool_value(a))) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_MOD: {
                /* Modulo */
                secd_value_t b = secd_pop(machine);
                secd_value_t a = secd_pop(machine);
                secd_value_t result = prim_mod(machine->heap, secd_cons(machine->heap, a, secd_cons(machine->heap, b, SECD_NIL)));
                if (secd_push(machine, result) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_NEG: {
                /* Negate */
                secd_value_t a = secd_pop(machine);
                int16_t av = secd_fixnum_value(a);
                if (secd_push(machine, secd_make_fixnum(-av)) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_CAR: {
                secd_value_t pair = secd_pop(machine);
                secd_value_t result = secd_car(machine->heap, pair);
                if (secd_push(machine, result) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_CDR: {
                secd_value_t pair = secd_pop(machine);
                secd_value_t result = secd_cdr(machine->heap, pair);
                if (secd_push(machine, result) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_CONS: {
                secd_value_t cdr = secd_pop(machine);
                secd_value_t car = secd_pop(machine);
                secd_value_t result = secd_cons(machine->heap, car, cdr);
                if (secd_push(machine, result) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_LDV: {
                /* Load ROM byte-vector: descriptor slot is the 16-bit operand */
                uint16_t slot = secd_read_u16(&bytecode[ip + 1]);
                if (secd_push(machine, secd_make_bytevec(slot)) != 0) {
                    return -1;
                }
                ip += 3;
                break;
            }

            case OP_VREF: {
                /* vref: pop idx, pop vec, push byte (fixnum 0..255) */
                secd_value_t idx = secd_pop(machine);
                secd_value_t vec = secd_pop(machine);
                if (!secd_is_bytevec(vec) || !secd_is_fixnum(idx)) {
                    secd_set_error(machine, SECD_ERROR_TYPE_ERROR);
                    return -1;
                }
                int b = secd_bytevec_read(machine->heap, secd_get_index(vec),
                                          secd_get_index(idx));
                if (b < 0) {
                    secd_set_error(machine, SECD_ERROR_INVALID_HANDLE);
                    return -1;
                }
                if (secd_push(machine, secd_make_fixnum((int16_t)b)) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }

            case OP_VSTOR: {
                /* vstore: pop val, pop idx, pop vec; write; push val back
                 * (setf yields the stored value, like OP_ST). */
                secd_value_t val = secd_pop(machine);
                secd_value_t idx = secd_pop(machine);
                secd_value_t vec = secd_pop(machine);
                if (!secd_is_bytevec(vec) || !secd_is_fixnum(idx)
                    || !secd_is_fixnum(val)) {
                    secd_set_error(machine, SECD_ERROR_TYPE_ERROR);
                    return -1;
                }
                if (secd_bytevec_write(machine->heap, secd_get_index(vec),
                                       secd_get_index(idx),
                                       (uint8_t)secd_get_index(val)) != 0) {
                    secd_set_error(machine, SECD_ERROR_INVALID_HANDLE);
                    return -1;
                }
                if (secd_push(machine, val) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }

            case OP_MKV: {
                /* make-vector: pop n, push zeroed writable byte-vector */
                secd_value_t n = secd_pop(machine);
                if (!secd_is_fixnum(n)) {
                    secd_set_error(machine, SECD_ERROR_TYPE_ERROR);
                    return -1;
                }
                uint16_t len = secd_get_index(n);
                uint16_t slot = secd_bytevec_alloc(machine->heap, len);
                if (slot == SECD_BYTEVEC_INVALID) {
                    secd_set_error(machine, SECD_ERROR_HEAP_FULL);
                    return -1;
                }
                if (secd_push(machine, secd_make_bytevec(slot)) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }

            case OP_LEN: {
                /* length: byte-vector -> its len; pair -> cell count; else 0 */
                secd_value_t val = secd_pop(machine);
                uint16_t len = 0;
                if (secd_is_bytevec(val)) {
                    secd_bytevec_t *v = secd_bytevec_get(machine->heap,
                                                         secd_get_index(val));
                    len = v ? v->len : 0;
                } else {
                    secd_value_t p = val;
                    while (secd_is_pair(p)) {
                        len++;
                        p = secd_cdr(machine->heap, p);
                    }
                }
                if (secd_push(machine, secd_make_fixnum((int16_t)len)) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_SEL: {
                /* Conditional branch.
                   Layout: SEL <else_addr:u16> [then body inline].
                   Cond TRUE  -> fall through (ip+3) into the inline then body.
                   Cond FALSE -> jump to <else_addr>. */
                secd_value_t condition = secd_pop(machine);
                uint16_t else_addr = secd_read_u16(&bytecode[ip + 1]);
                if (secd_bool_value(condition)) {
                    ip += 3;
                } else {
                    ip = else_addr;
                }
                break;
            }
            
            case OP_JOIN: {
                /* Return from conditional */
                secd_value_t return_addr = secd_dump_pop(machine);
                ip = secd_get_index(return_addr);
                break;
            }

            case OP_LOOP: {
                /* Unconditional jump back to loop start */
                uint16_t target = secd_read_u16(&bytecode[ip + 1]);
                ip = target;
                break;
            }

            case OP_POP: {
                /* Discard top of stack */
                (void)secd_pop(machine);
                ip += 1;
                break;
            }
            
            case OP_APP: {
                /* Apply function */
                secd_value_t args = secd_pop(machine);  /* Arguments list */
                secd_value_t func = secd_pop(machine);  /* Function/closure */
                
                if (secd_is_pair(func)) {
                    /* Closure is a pair: (bytecode-address . environment) */
                    secd_value_t code_addr = secd_car(machine->heap, func);
                    secd_value_t closure_env = secd_cdr(machine->heap, func);
                    
                    /* Save current state to dump */
                    secd_dump_push(machine, machine->E);
                    secd_dump_push(machine, secd_make_fixnum(ip + 1)); /* Return address */
                    
                    /* Set up new environment with arguments */
                    machine->E = closure_env;
                    
                    /* Push arguments to stack; stop at a non-pair tail
                 * (compiler emits LDC 0 as the list terminator) */
                    secd_value_t arg_list = args;
                    while (secd_is_pair(arg_list)) {
                        secd_push(machine, secd_car(machine->heap, arg_list));
                        arg_list = secd_cdr(machine->heap, arg_list);
                    }
                    
                    /* Jump to closure bytecode */
                    ip = secd_fixnum_value(code_addr);
                } else if (secd_is_fixnum(func)) {
                    /* Primitive call */
                    uint8_t prim_id = (uint8_t)secd_fixnum_value(func);
                    secd_value_t result = secd_call_prim(&prim_registry, prim_id, machine->heap, args);
                    if (secd_push(machine, result) != 0) {
                        return -1;
                    }
                    ip += 1;
                }
                break;
            }

            case OP_CALL: {
                /* Direct call to a known global function (no closure allocation) */
                uint16_t addr = secd_read_u16(&bytecode[ip + 1]);
                secd_value_t args = secd_pop(machine);  /* Arguments list */
                
                /* Save current state to dump */
                secd_dump_push(machine, machine->E);
                secd_dump_push(machine, secd_make_fixnum(ip + 3)); /* Return address */
                
                /* Push argument values for the callee; stop at a non-pair
                 * tail (compiler emits LDC 0 as the list terminator) */
                secd_value_t arg_list = args;
                while (secd_is_pair(arg_list)) {
                    secd_push(machine, secd_car(machine->heap, arg_list));
                    arg_list = secd_cdr(machine->heap, arg_list);
                }
                
                /* Jump to function body */
                ip = addr;
                break;
            }
            
            case OP_RTN: {
                /* Return from function */
                secd_value_t return_addr = secd_dump_pop(machine);
                machine->E = secd_dump_pop(machine);
                ip = secd_get_index(return_addr);
                break;
            }
            
            case OP_PRIM: {
                /* Call primitive */
                uint8_t prim_id = bytecode[ip + 1];
                secd_value_t args = secd_pop(machine);
                secd_value_t result = secd_call_prim(&prim_registry, prim_id, machine->heap, args);
                if (secd_push(machine, result) != 0) {
                    return -1;
                }
                ip += 2;
                break;
            }
            
            case OP_PRN: {
                /* Print (debug) */
                secd_value_t val = secd_pop(machine);
                if (secd_is_fixnum(val)) {
                    hal_print_int(secd_fixnum_value(val));
                } else if (secd_is_nil(val)) {
                    hal_print("nil");
                } else if (val == SECD_TRUE) {
                    hal_print("t");
                } else if (val == SECD_FALSE) {
                    hal_print("nil");
                } else {
                    hal_print("<object>");
                }
                hal_print("\n");
                ip += 1;
                break;
            }
            
            case OP_GC: {
                /* Trigger garbage collection */
                uint16_t freed = secd_gc_run(machine);
                if (secd_push(machine, secd_make_fixnum(freed)) != 0) {
                    return -1;
                }
                ip += 1;
                break;
            }
            
            case OP_NOP:
                ip += 1;
                break;
            
            default:
                /* Unknown opcode */
                secd_set_error(machine, SECD_ERROR_INVALID_HANDLE);
                return -1;
        }
        
        /* Check for errors */
        if (machine->error != SECD_ERROR_NONE) {
            return -1;
        }
    }
    
    return machine->error == SECD_ERROR_NONE ? 0 : -1;
}

int secd_step(secd_machine_t *machine, const uint8_t *bytecode, size_t length) {
    if (!machine || !bytecode || length == 0) {
        return -1;
    }
    
    machine->running = true;
    
    /* Execute single instruction */
    return secd_execute(machine, bytecode, length);
}
