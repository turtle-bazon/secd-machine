# SECD Machine for Microcontrollers - Development Plan

## Project Overview

A minimal SECD (Stack, Environment, Control, Dump) abstract machine implemented in C, designed for resource-constrained microcontrollers (starting with RP2040, then ESP32).

## Project Ecosystem

### secd-machine (this repository)
- SECD VM that executes on MCUs
- Written in C
- Minimal footprint
- Hardware abstraction layer (HAL)
- C primitives for hardware access

### secd-lisp (separate repository)
- secd-lisp compiler (runs on developer PC)
- Cross-compiles secd-lisp → bytecode
- Library manager (secd-lib-manager)
- Tree-shaking and optimization

### secd-emulator (separate repository, Common Lisp)
- Emulates SECD VM execution with MCU constraints
- Tests code without real hardware
- Simulates target features and memory limits
- Details to be discussed later

### Shared Interface
- Bytecode format specification (owned by secd-machine)
- C primitive signatures (defined in secd-machine)

## Architecture Decisions

### Target Platform
- **Primary**: RP2040 (Raspberry Pi Pico) - ARM Cortex-M0+, 133MHz, 264KB SRAM, 2MB Flash
- **Secondary**: ESP32 family (Xtensa, WiFi capable)

### Implementation Language
- **C** - Minimal runtime overhead, maximum portability, direct hardware access

### Memory Model
- **Value Representation**: 16-bit indirect handles
  - 16-bit handles (max 65536 objects)
  - Values stored in a single heap array
  - Handles are indices into the heap
- **Garbage Collection**: Mark-sweep (in-place, no compaction needed)
- **Memory Management**: Start with dynamic allocation, plan for modern GC later

### Symbol Table
- Sorted array for binary search
- Symbols stored as string literals in bytecode
- Compact representation for embedded use

### Hardware Access
- C primitives exposed to VM
- secd-lisp wraps them in nice API
- Target description defines available primitives

### Bytecode Format
- Programs compile to SECD bytecode
- Bytecode uploaded to MCU via serial/USB or compiled into firmware
- Format designed for compact representation
- Precompiled libraries (.secd-lib) linked at build time

## Project Structure

```
secd-machine/
├── CMakeLists.txt              # Build system
├── cmake/                      # Cross-compilation toolchains
│   ├── rp2040.cmake
│   └── esp32.cmake
├── include/
│   ├── secd/
│   │   ├── types.h             # Value types and handles
│   │   ├── heap.h              # Heap management
│   │   ├── gc.h                # Garbage collector
│   │   ├── symbols.h           # Symbol table
│   │   ├── machine.h           # SECD machine state
│   │   ├── bytecode.h          # Bytecode format
│   │   └── primitives.h        # C primitive interface
│   └── hal/                    # Hardware abstraction layer
│       ├── hal.h               # HAL interface
│       ├── rp2040.h            # RP2040 specific
│       └── esp32.h             # ESP32 specific
├── src/
│   ├── core/
│   │   ├── heap.c              # Heap implementation
│   │   ├── gc.c                # Mark-sweep GC
│   │   ├── symbols.c           # Symbol table
│   │   ├── machine.c           # SECD machine
│   │   ├── bytecode.c          # Bytecode loader/executor
│   │   └── primitives.c        # Primitive registration
│   └── hal/
│       ├── rp2040.c            # RP2040 HAL
│       └── esp32.c             # ESP32 HAL
├── targets/                    # Target platform descriptions
│   ├── rp2040.secd-target
│   └── esp32.secd-target
├── tests/
│   ├── test_heap.c
│   ├── test_gc.c
│   └── test_machine.c
├── doc/
│   ├── architecture.md         # VM architecture
│   ├── bytecode.md             # Bytecode format spec
│   ├── primitives.md           # C primitive interface
│   └── hal.md                  # Hardware abstraction layer
└── CMakePresets.json           # Build presets for targets
```

## Implementation Phases

### Phase 1: Core SECD Machine
1. Define value types and handle representation
2. Implement single heap with allocation
3. Implement mark-sweep garbage collector
4. Implement symbol table (sorted array)
5. Implement SECD machine state (S, E, C, D registers)
6. Implement minimal instruction set

### Phase 2: Primitive Interface
1. Define C primitive interface
2. Implement primitive registration
3. Create target description format
4. Implement target loading

### Phase 3: RP2040 Port
1. Create HAL interface
2. Implement RP2040 HAL (memory, I/O, timing)
3. Create CMake toolchain for RP2040
4. Create target description
5. Test on actual hardware

### Phase 4: Extensions
1. Add ESP32 support
2. Implement firmware update mechanism
3. Add debugging/profiling tools
4. Optimize for size and speed

## Memory Layout (RP2040)

```
SRAM: 264KB total
├── System stack: ~8KB
├── SECD machine: ~1KB (registers, state)
├── Heap: ~200KB (objects, pairs, symbols)
├── Bytecode: ~50KB (compiled programs)
└── Free: ~5KB (safety margin)
```

## Target Description Format

```lisp
;; rp2040.secd-target
(:platform :rp2040
 :features (:has-gpio :has-uart :has-spi :has-i2c :has-adc :has-pwm)
 :primitives ((gpio-init . prim-gpio-init)
              (gpio-write . prim-gpio-write)
              (gpio-read . prim-gpio-read)
              (uart-init . prim-uart-init)
              (uart-write . prim-uart-write)
              (uart-read . prim-uart-read)
              (sleep . prim-sleep))
 :memory (:heap 200000 :stack 16000 :symbols 256))
```

## secd-emulator (Future)

A Common Lisp project that emulates SECD VM execution with MCU constraints.

**Purpose:**
- Fast iteration without reflashing MCU
- Test code before deploying to hardware
- Simulate target platform constraints
- Debug and profile programs

**Why important:**
- Editing → Compiling → Flashing MCU is slow (minutes per cycle)
- Emulator runs in seconds on PC
- Catch bugs before hardware deployment

**Open Questions (to discuss later):**
- Output format (execution trace, memory usage, errors?)
- I/O simulation (GPIO, UART output?)
- Debugging features (step-through, breakpoints?)
- Profiling (instruction count, GC pauses?)

## Next Steps

1. Set up project structure and build system
2. Implement value representation
3. Implement heap and GC
4. Implement SECD machine core
5. Create target description format
6. Port to RP2040
