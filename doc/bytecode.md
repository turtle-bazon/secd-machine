# SECD Bytecode Format

## Overview

The SECD bytecode is a compact, stack-based instruction set designed for resource-constrained microcontrollers. Each instruction is 1-3 bytes: 1 byte opcode + 0-2 bytes operand.

## Instruction Format

```
+--------+--------+--------+
| Opcode | Operand (optional) |
+--------+--------+--------+
  1 byte   0-2 bytes
```

## Opcodes

### Stack Operations

| Opcode | Name | Operand | Description |
|--------|------|---------|-------------|
| 0x00 | STOP | None | Halt execution |
| 0x01 | LDM | addr (16-bit) | Push value from memory address onto stack |
| 0x02 | LDC | value (16-bit) | Push constant value onto stack |

### Arithmetic Operations

| Opcode | Name | Operand | Description |
|--------|------|---------|-------------|
| 0x10 | ADD | None | Pop two values, push sum |
| 0x11 | SUB | None | Pop two values, push difference (second - first) |
| 0x12 | MUL | None | Pop two values, push product |
| 0x13 | DIV | None | Pop two values, push quotient (second / first) |
| 0x14 | MOD | None | Pop two values, push remainder |
| 0x15 | NEG | None | Pop one value, push negation |

### Comparison Operations

| Opcode | Name | Operand | Description |
|--------|------|---------|-------------|
| 0x20 | EQ | None | Pop two values, push #t if equal |
| 0x21 | LT | None | Pop two values, push #t if second < first |
| 0x22 | GT | None | Pop two values, push #t if second > first |
| 0x23 | LE | None | Pop two values, push #t if second <= first |
| 0x24 | GE | None | Pop two values, push #t if second >= first |
| 0x25 | NOT | None | Pop boolean, push negation |

### List Operations

| Opcode | Name | Operand | Description |
|--------|------|---------|-------------|
| 0x30 | CAR | None | Pop pair, push first element |
| 0x31 | CDR | None | Pop pair, push rest element |
| 0x32 | CONS | None | Pop two values, push new pair |
| 0x33 | LIST | count (8-bit) | Pop count values, build list |
| 0x34 | APPEND | None | Pop two lists, append |

### Environment Operations

| Opcode | Name | Operand | Description |
|--------|------|---------|-------------|
| 0x40 | LD | offset (16-bit) | Push value from environment at offset |
| 0x41 | ST | offset (16-bit) | Store top of stack into environment |
| 0x42 | ARGS | count (8-bit) | Pop count arguments, create new environment |
| 0x43 | LDG | offset (16-bit) | Push value from global frame at offset |
| 0x44 | STG | offset (16-bit) | Store top of stack into global frame |

### Control Flow

| Opcode | Name | Operand | Description |
|--------|------|---------|-------------|
| 0x50 | SEL | then (16-bit) | Conditional branch (pop boolean) |
| 0x51 | JOIN | None | Return from conditional |
| 0x52 | LOOP | addr (16-bit) | Set loop point |
| 0x53 | BRK | None | Break from loop |

### Function Operations

| Opcode | Name | Operand | Description |
|--------|------|---------|-------------|
| 0x60 | LDF | addr (16-bit) | Load closure (function) |
| 0x61 | APP | None | Apply function to arguments |
| 0x62 | RTN | None | Return from function |
| 0x63 | CALL | addr (16-bit) | Direct function call |

### I/O Operations

| Opcode | Name | Operand | Description |
|--------|------|---------|-------------|
| 0x70 | PRN | None | Print top of stack (debug) |
| 0x71 | PRS | None | Print string |
| 0x72 | RD | None | Read input |
| 0x73 | GPIO | pin (8-bit) | Read GPIO pin |
| 0x74 | GPIO! | pin (8-bit) | Write GPIO pin |

### Special Operations

| Opcode | Name | Operand | Description |
|--------|------|---------|-------------|
| 0xF0 | NOP | None | No operation |
| 0xF1 | GC | None | Trigger garbage collection |
| 0xF2 | DUMP | None | Dump machine state (debug) |
| 0xFF | ERROR | code (8-bit) | Signal error |

## Bytecode Example

### Scheme Code
```scheme
(define (fib n)
  (if (< n 2)
      n
      (+ (fib (- n 1)) (fib (- n 2)))))

(fib 10)
```

### Compiled Bytecode

```hex
; Function fib (address 0x0000)
; Parameter: n (offset 0)
; Body:
0x50 0x00 0x10    ; SEL 0x0010 (jump to else)
; Then branch:
0x41 0x00         ; LD 0 (push n)
0x51              ; JOIN
; Else branch (0x0010):
0x41 0x00         ; LD 0 (push n)
0x02 0x00 0x01    ; LDC 1
0x11              ; SUB (n - 1)
0x60 0x00 0x00    ; LDF 0x0000 (fib)
0x61              ; APP (fib (- n 1))
0x41 0x00         ; LD 0 (push n)
0x02 0x00 0x02    ; LDC 2
0x11              ; SUB (n - 2)
0x60 0x00 0x00    ; LDF 0x0000 (fib)
0x61              ; APP (fib (- n 2))
0x10              ; ADD
0x51              ; JOIN

; Main program (0x0020):
0x60 0x00 0x00    ; LDF 0x0000 (fib)
0x02 0x00 0x0A    ; LDC 10
0x61              ; APP (fib 10)
0x00              ; STOP
```

## Memory Layout

### Bytecode Segment

```
+------------------+
| Code[0]          | <- Entry point
+------------------+
| Code[1]          |
+------------------+
| ...              |
+------------------+
| Code[N]          |
+------------------+
| Constants Pool   |
+------------------+
| Symbol Table     |
+------------------+
```

### Constant Pool

Constants are stored after the code segment:
```c
struct constant_pool {
    uint16_t count;        // Number of constants
    uint16_t offsets[];    // Offset to each constant
    uint8_t data[];        // Constant data
};
```

### Symbol Table

Symbols are stored as length-prefixed strings:
```c
struct symbol_entry {
    uint8_t length;        // Symbol length
    char name[];           // Symbol name (not null-terminated)
};
```

## Compilation Process

### Scheme -> AST -> Bytecode

1. **Parsing**: Scheme source -> AST
2. **Analysis**: Variable binding, scope analysis
3. **Code Generation**: AST -> SECD bytecode
4. **Optimization**: Constant folding, dead code elimination

### Example Compilation

Input:
```scheme
(+ 1 2)
```

AST:
```c
{
    type: APPLICATION,
    operator: { type: SYMBOL, name: "+" },
    operands: [
        { type: INTEGER, value: 1 },
        { type: INTEGER, value: 2 }
    ]
}
```

Bytecode:
```hex
0x02 0x00 0x01    ; LDC 1
0x02 0x00 0x02    ; LDC 2
0x60 0x00 0x00    ; LDF (address of +)
0x61              ; APP
```

## Debugging

### DUMP Instruction

The DUMP instruction outputs machine state:
```
S: [1 2 3]
E: ((x . 1) (y . 2))
C: [0x0010 0x0011 0x0012]
D: []
```

### Error Codes

| Code | Name | Description |
|------|------|-------------|
| 0x01 | STACK_OVERFLOW | Stack exceeded maximum size |
| 0x02 | STACK_UNDERFLOW | Pop from empty stack |
| 0x03 | HEAP_FULL | No space for new object |
| 0x04 | INVALID_HANDLE | Reference to freed object |
| 0x05 | TYPE_ERROR | Invalid type for operation |
| 0x06 | DIVISION_BY_ZERO | Division by zero |
| 0x07 | UNDEFINED_SYMBOL | Reference to unknown symbol |
| 0x08 | STACK_UNDERFLOW | Not enough arguments |

## Performance Considerations

### Instruction Frequency
- Most common: LDC, ADD, SUB, CAR, CDR, CONS
- Optimize for these cases

### Code Size
- Average: 2-3 bytes per instruction
- Compact representation important for flash-limited MCUs

### Execution Speed
- Simple fetch-decode-execute loop
- No pipelining needed for most MCUs
- Consider threaded code for speed