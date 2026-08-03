# SECD Machine Architecture

## Overview

The SECD machine is an abstract machine for evaluating functional programs. It consists of four registers:

- **S** (Stack): Holds intermediate results and arguments
- **E** (Environment): Maps variables to values
- **C** (Control): Contains the program to execute (bytecode instructions)
- **D** (Dump): Stores saved state for function calls and conditionals

**This is the VM component only.** The secd-lisp compiler runs on developer PC and outputs bytecode for this VM.

## Machine State

```c
typedef struct {
    value_t S;    // Stack pointer (top of stack)
    value_t E;    // Current environment
    value_t C;    // Control register (current instruction pointer)
    value_t D;    // Dump (saved state)
} secd_machine_t;
```

## Execution Model

1. Fetch instruction from C register
2. Execute instruction (may modify S, E, C, D)
3. Advance C to next instruction
4. Repeat until STOP instruction

## Instruction Categories

### Stack Operations
- `LDM addr` - Push value from memory address onto stack
- `LDC value` - Push constant value onto stack

### Arithmetic Operations
- `ADD` - Pop two values, push sum
- `SUB` - Pop two values, push difference
- `MUL` - Pop two values, push product
- `DIV` - Pop two values, push quotient

### Comparison Operations
- `EQ` - Pop two values, push boolean (equal?)
- `LT` - Pop two values, push boolean (less than?)

### List Operations
- `CAR` - Pop pair, push first element
- `CDR` - Pop pair, push rest element
- `CONS` - Pop two values, push new pair

### Control Flow
- `SEL then_addr else_addr` - Conditional branch
- `JOIN` - Return from conditional
- `RTN` - Return from function call
- `APP` - Apply function to arguments

### Primitive Calls
- `PRIM func_id` - Call C primitive function

### Special
- `STOP` - Halt execution

## Memory Model

### Value Representation

All values are represented as 16-bit indirect handles:

```
+------------------+------------------+
| Type (4 bits)    | Index (12 bits)  |
+------------------+------------------+
```

Types:
- `0x0` - Nil (empty list)
- `0x1` - Integer (fixnum)
- `0x2` - Pair (cons cell)
- `0x3` - Symbol
- `0x4` - Boolean
- `0x5` - Closure (function)
- `0x6` - Bytecode pointer
- `0x7` - Free (available for allocation)

### Heap Layout

```
+------------------+
| Object 0         |
+------------------+
| Object 1         |
+------------------+
| ...              |
+------------------+
| Object N         |
+------------------+
```

Each object is 4 bytes:
- 2 bytes for car/first value
- 2 bytes for cdr/second value
- 1 byte for type/tag (stored separately in type table)

### Pair Representation

A pair (cons cell) is stored as:
```
car (2 bytes) | cdr (2 bytes)
```

Example:
```lisp
(cons 1 2)  ; Creates pair with car=1, cdr=2
```

## Garbage Collection

### Mark-Sweep Algorithm

1. **Mark Phase**: Traverse from roots (S, E, D registers), marking all reachable objects
2. **Sweep Phase**: Scan heap, freeing unmarked objects

### Roots
- Stack contents
- Environment chain
- Dump contents
- Symbol table entries

### Collection Trigger
- When heap allocation fails
- Programmatically triggered
- After N allocations (configurable)

## Function Application

### CALL/RETURN Protocol

1. **CALL**:
   - Push current E onto D (save environment)
   - Push current C onto D (save return address)
   - Set E to closure's environment
   - Set C to closure's bytecode

2. **RETURN**:
   - Pop C from D (restore return address)
   - Pop E from D (restore environment)
   - Continue execution

### Example

```lisp
(defun add (a b) (+ a b))
(add 1 2)
```

Compiled bytecode:
```
LDC add        ; Load function address
LDC 1          ; Load first argument
LDC 2          ; Load second argument
APP            ; Apply function
STOP           ; Halt
```

## Conditional Execution

### SEL/JOIN Protocol

1. **SEL**:
   - Pop condition from stack
   - If true, set C to then_addr
   - If false, set C to else_addr
   - Push current C onto D (for JOIN)

2. **JOIN**:
   - Pop C from D (restore after conditional)
   - Continue execution

### Example

```lisp
(if (> x 0) x (- x))
```

Compiled bytecode:
```
LDC x
LDC 0
GT
SEL then_addr else_addr
then_addr:
  LDC x
  JOIN
else_addr:
  LDC x
  NEG
  JOIN
```

## Environment Structure

Environments are association lists:
```lisp
((var1 . val1) (var2 . val2) ...)
```

### Lookup
1. Search environment list for variable
2. Return associated value
3. If not found, search parent environment (closure chain)

### Extending
```lisp
(let ((x 1) (y 2))
  body)
```

Creates new environment: `((x . 1) (y . 2) . parent_env)`

## C Primitive Interface

The VM exposes C functions for hardware access:

```c
// Primitive function signature
typedef value_t (*prim_fn)(value_t args);

// Register a primitive
void secd_register_prim(const char* name, prim_fn fn);

// Call a primitive from bytecode
value_t secd_call_prim(uint8_t func_id, value_t args);
```

### Target Description

Each target platform has a description file:

```lisp
(:platform :rp2040
 :features (:has-gpio :has-uart :has-spi)
 :primitives ((gpio-init . prim-gpio-init)
              (gpio-write . prim-gpio-write)
              (gpio-read . prim-gpio-read))
 :memory (:heap 200000 :stack 16000))
```

## Limitations

### Current Constraints
- Maximum 65536 objects in heap (16-bit handles)
- Maximum 256 symbols in symbol table
- Fixed-size stack (configurable)
- No tail-call optimization (yet)
- No continuations (yet)

### Future Extensions
- Tail-call optimization
- First-class continuations (call/cc)
- Generational garbage collection
- Hardware-accelerated operations
