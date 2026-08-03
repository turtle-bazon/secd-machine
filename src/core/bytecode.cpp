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

/* Execute bytecode */
int secd_execute(secd_machine_t *machine, const uint8_t *bytecode, size_t length) {
    if (!machine || !bytecode || length == 0) {
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
            
            case OP_SEL: {
                /* Conditional branch */
                secd_value_t condition = secd_pop(machine);
                uint16_t then_addr = secd_read_u16(&bytecode[ip + 1]);
                
                /* Save return address for JOIN */
                secd_dump_push(machine, secd_make_handle(0, ip + 3));
                
                if (secd_bool_value(condition)) {
                    ip = then_addr;
                } else {
                    /* else_addr follows then_addr */
                    ip = secd_read_u16(&bytecode[then_addr]);
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
