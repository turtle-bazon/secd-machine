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
#ifndef SECD_BYTECODE_H
#define SECD_BYTECODE_H

#include <stdint.h>
#include <stddef.h>

/*
 * SECD Bytecode Format.
 *
 * Instructions are 1-3 bytes:
 *   - 1 byte: opcode (no operand)
 *   - 2 bytes: opcode + 8-bit operand
 *   - 3 bytes: opcode + 16-bit operand (big-endian)
 *
 * Program structure:
 *   [Code 0] [Code 1] ... [Code N]
 *   [Constants Pool]
 *   [Symbol Table]
 */

/* Opcodes */
typedef enum {
    /* Stack operations */
    OP_STOP  = 0x00,  /* Halt execution */
    OP_LDM   = 0x01,  /* Load: push value from memory address */
    OP_LDC   = 0x02,  /* Load constant: push value */
    OP_LDF   = 0x03,  /* Load function: push closure */
    OP_LDE   = 0x04,  /* Load environment: push current env */
    
    /* Arithmetic operations */
    OP_ADD   = 0x10,  /* Add */
    OP_SUB   = 0x11,  /* Subtract */
    OP_MUL   = 0x12,  /* Multiply */
    OP_DIV   = 0x13,  /* Divide */
    OP_MOD   = 0x14,  /* Modulo */
    OP_NEG   = 0x15,  /* Negate */
    
    /* Comparison operations */
    OP_EQ    = 0x20,  /* Equal */
    OP_LT    = 0x21,  /* Less than */
    OP_GT    = 0x22,  /* Greater than */
    OP_LE    = 0x23,  /* Less or equal */
    OP_GE    = 0x24,  /* Greater or equal */
    OP_NOT   = 0x25,  /* Logical not */
    
    /* List operations */
    OP_CAR   = 0x30,  /* First element */
    OP_CDR   = 0x31,  /* Rest element */
    OP_CONS  = 0x32,  /* Construct pair */
    
    /* Byte-vector operations */
    OP_LDV   = 0x33,  /* Load byte-vector: push handle for ROM pool slot (u16) */
    OP_VREF  = 0x34,  /* Byte-vector ref: pop idx, pop vec, push byte 0..255 */
    OP_VSTOR = 0x35,  /* Byte-vector store: pop val, pop idx, pop vec; push val */
    OP_MKV   = 0x36,  /* Make-vector: pop n, push writable byte-vector of n zeros */
    OP_LEN   = 0x37,  /* Length: pop value; push len (byte-vec) or cell count (list) */
    
    /* Environment operations */
    OP_LD    = 0x40,  /* Load from environment */
    OP_ST    = 0x41,  /* Store to environment */
    OP_ARGS  = 0x42,  /* Pop arguments into env */

    /* Global environment operations */
    OP_LDG   = 0x43,  /* Load global: push value from global frame */
    OP_STG   = 0x44,  /* Store global: set global frame cell */
    
    /* Control flow */
    OP_SEL   = 0x50,  /* Conditional branch */
    OP_JOIN  = 0x51,  /* Return from conditional */
    OP_LOOP  = 0x52,  /* Loop point */
    OP_BRK   = 0x53,  /* Break from loop */
    OP_JMP   = 0x54,  /* Unconditional jump */
    
    /* Function operations */
    OP_APP   = 0x60,  /* Apply function */
    OP_RTN   = 0x61,  /* Return from function */
    OP_CALL  = 0x62,  /* Direct call */
    
    /* Primitive operations */
    OP_PRIM  = 0x70,  /* Call C primitive */
    
    /* Debug operations */
    OP_PRN   = 0x78,  /* Print (debug) */
    OP_DUMP  = 0x79,  /* Dump state (debug) */
    OP_GC    = 0x7A,  /* Trigger GC */
    OP_NOP   = 0x7B,  /* No operation */
    OP_POP   = 0x7C,  /* Discard top of stack */
    
    /* Error */
    OP_ERROR = 0xFF   /* Signal error */
} secd_opcode_t;

/* Bytecode header (at start of bytecode file) */
typedef struct {
    uint8_t magic[4];       /* "SECD" */
    uint16_t version;       /* Format version */
    uint16_t code_size;     /* Size of code section */
    uint16_t constants_size; /* Size of constants pool */
    uint16_t symbols_size;  /* Size of symbol table */
    uint16_t entry_point;   /* Start address */
} secd_bytecode_header_t;

/* Bytecode file magic */
#define SECD_MAGIC "SECD"
#define SECD_VERSION 1

/* ROM byte-vector pool footer.
 *
 * The compiler appends the byte-vector literals after OP_STOP as a data pool
 * so the loader can point descriptors directly into flash.  Layout (bytes at
 * the very end of the buffer):
 *
 *   [pool data: u16 count, then count * (u16 offset, u16 len),
 *    then the byte blobs]        offset relative to the first data byte
 *   [u16 pool_size]              total bytes of the pool section above
 *   [u16 magic]
 *
 * pool_start = buffer_length - 4 - pool_size, and the first data byte is at
 * pool_start + 2 + 4*count.  A pool must be readable from a plain pointer,
 * so on the ESP32 side it stays in the raw flash bytecode array.
 */
#define SECD_POOL_MAGIC 0xB1C5

/* Get instruction length (including operands) */
int secd_inst_length(uint8_t opcode);

/* Read 16-bit value from bytecode (big-endian) */
uint16_t secd_read_u16(const uint8_t *data);

/* Read signed 16-bit value from bytecode (big-endian) */
int16_t secd_read_i16(const uint8_t *data);

#endif /* SECD_BYTECODE_H */

#ifdef __cplusplus
}
#endif
