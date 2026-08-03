# Project Separation

## Overview

The SECD project is split into three components:

1. **secd-machine** - VM that runs on MCUs (C)
2. **secd-lisp** - Compiler that runs on developer PC (Common Lisp)
3. **secd-emulator** - Emulator for testing (Common Lisp)

## Repository Structure

```
/home/turtle/scm-controlled/common-lisp/
├── secd-machine/     (this repository)
│   ├── src/           # VM implementation (C)
│   ├── include/       # VM headers
│   ├── hal/           # Hardware abstraction
│   ├── doc/           # VM documentation
│   └── tests/         # VM tests
│
├── secd-lisp/        (separate repository)
│   ├── src/           # Compiler implementation (Common Lisp)
│   ├── core/          # Core secd-lisp files
│   ├── drivers/       # Hardware drivers (secd-lisp)
│   ├── lib/           # Precompiled libraries (.secd-lib)
│   ├── targets/       # Target descriptions (.secd-target)
│   ├── doc/           # Compiler documentation
│   └── examples/      # Example programs
│
└── secd-emulator/    (separate repository)
    ├── src/           # Emulator implementation (Common Lisp)
    └── doc/           # Emulator documentation
```

## Format Ownership

| Format | Owner | Purpose |
|--------|-------|---------|
| Bytecode | secd-machine | VM executes this |
| .secd-lib | secd-lisp | Precompiled libraries |
| .secd-target | secd-lisp | Platform descriptions |

## Interface Between Components

### Bytecode Format (owned by secd-machine)

The bytecode format is the contract between compiler and VM.

Compiler outputs:
- Bytecode instructions
- Constant pool
- Symbol table

VM reads:
- Bytecode instructions
- Calls C primitives for hardware

### C Primitive Signatures (defined in secd-machine)

Primitives are defined in secd-machine but used by secd-lisp.

```c
// Defined in secd-machine
value_t prim_gpio_write(value_t args);

// Used in secd-lisp
(gpio-write 25 1)  ; Calls prim_gpio_write
```

### Target Descriptions (owned by secd-lisp)

Target descriptions live in secd-lisp:

```lisp
;; secd-lisp/targets/rp2040.secd-target
(:platform :rp2040
 :features (:has-gpio :has-uart)
 :primitives ((gpio-write . prim-gpio-write))
 :memory (:heap 200000 :stack 16000))
```

secd-lisp reads these to know:
- What features are available
- What primitives can be called
- Memory constraints

### Precompiled Libraries (owned by secd-lisp)

```lisp
;; secd-lisp/lib/ws2812.secd-lib
(:name "ws2812"
 :requires (:has-gpio)
 :functions ((ws2812-init . <bytecode>)
             (ws2812-set-color . <bytecode>)))
```

## Workflow

### Development

```
1. Write secd-lisp code
2. secd-lisp compiles to bytecode
3. secd-vm executes bytecode on MCU
```

### Build Process

```bash
# On developer PC
secd-lisp --build project.secd -o firmware.secd

# Flash to MCU
secd-flash --target rp2040 firmware.secd
```

### Library Development

```bash
# On developer PC
secd-lisp --compile libs/ws2812.lisp -o libs/ws2812.secd-lib

# Distribute .secd-lib file
# No source code needed
```

## Versioning

All projects should be versioned together:

```
secd-machine v1.0.0
secd-lisp v1.0.0
secd-emulator v1.0.0
Bytecode format v1.0
```

When bytecode format changes, secd-machine and secd-lisp must be updated.

## Shared Documentation

Documentation lives in each repository:

- **secd-machine/doc/**: VM architecture, bytecode format, HAL
- **secd-lisp/doc/**: Language spec, compiler internals, libraries, targets
- **secd-emulator/doc/**: Emulator usage, debugging

Bytecode format docs should be identical in secd-machine and secd-lisp.
