# SECD Machine Specification

**Status:** Normative reference for implementers (VM authors, compiler backends,
emulators, tooling).
**Version:** 1 — tracks firmware `0.0.1.x`; this document is derived from the
reference implementation in `src/core/` and `include/secd/`, which remains the
final arbiter for anything underspecified here.

The SECD Machine is a small stack-based virtual machine for microcontrollers.
A Lisp dialect (`secd-lisp`) compiles to SECD bytecode; the bytecode executes
in-place from flash next to a tiny firmware that supplies HAL primitives
(GPIO, UART, I²C, USB HID, arbitrary-precision integers, …).

---

## 1. Container formats

### 1.1 Bytecode file (`.secd`) — 14-byte header

    offset size  field
    0      4     magic  'S' 'E' 'C' 'D'
    4      1     version-major   (currently 0)
    5      1     version-minor   (currently 1)
    6      2     reserved        (0)
    8      2     code-size       big-endian u16
    10     2     const-size      big-endian u16 (unused, 0)
    12     2     symbols-size    big-endian u16 (unused, 0)

The executable unit is `code-size + const-size` bytes immediately after the
header. All instruction operands inside are **big-endian**.

### 1.2 Flashable image

Two layouts exist; both are *firmware image followed (or followed at a fixed
slot) by the `.secd` unit*:

* **Concatenated** (ESP32, STM32): `image.bin = firmware.bin ++ .secd`.
  The runtime locates bytecode by scanning forward from its own image end
  (`__flash_binary_end`, window 4 KiB) for the `'S','E','C','D'` magic.
* **Fixed slot**: board metadata may declare
  `flash_layout.bytecode_addr`; tooling splices the unit there.

### 1.3 Device package (`.machine`)

A ZIP containing the platform firmware plus `metadata.json` describing the
target (schemas in §9). The compiler reads only `metadata.json`; flash tools
consume the rest.

---

## 2. Value representation

Every value is a **16-bit handle**: `tag[15:12] | payload[11:0]`.

| Tag    | Type      | Meaning                                                     |
|--------|-----------|-------------------------------------------------------------|
| 0x0    | NIL       | The empty list. Handle `0x0000`.                             |
| 0x1    | FIXNUM    | Immediate signed 12-bit integer, two's complement in payload (**range −2048 … 2047**, sign-extended on read). |
| 0x2    | PAIR      | Index of a heap object `{car, cdr}`.                         |
| 0x3    | SYMBOL    | Index into the interned symbol table (compiler-side names; runtime treats opaquely). |
| 0x4    | BOOL      | Payload distinguishes true/false; canonical handles: TRUE `0x8000`… see note, FALSE. |
| 0x5    | CLOSURE   | Heap pair `(code-address . environment)`.                    |
| 0x6    | BIGNUM    | Heap object `{car = sign (raw 0/1), cdr = byte-vector descriptor slot}` whose byte-vector holds the magnitude little-endian base-256, normalized (canonical zero = single `0x00`). Arbitrary precision, limited by arena size. |
| 0x7    | FREE      | Free-list marker; never observable.                          |
| 0xA    | BYTEVEC   | Payload = index into the byte-vector descriptor table (**not** a heap index). |

Booleans are distinct handles (`SECD_TRUE`/`SECD_FALSE`); they are not
fixnums. Only `NIL` and `FALSE` are falsy; every other value is truthy.

## 3. Memory model

* **Heap**: up to 4095 objects (payload limit), each `{car:u16, cdr:u16}`
  plus a separate type byte array. Allocation is first-free linear scan;
  freed slots are reused.
* **Byte-vector table**: `SECD_BYTEVEC_MAX` (≥256 recommended) descriptors
  `{data*, len, writable}`. ROM slots point into the loaded bytecode buffer
  (the pool, §5); RAM slots point into a bump arena. The arena is compacted
  during GC; **descriptor indices are stable**.
* **GC**: mark–sweep with stable indices. Roots: machine S/E/G/dump. A
  BIGNUM keeps its referenced descriptor slot alive; reachable RAM
  byte-vectors are packed to the arena front. Handles held anywhere else
  (e.g. native code) must be re-read after collection.

## 4. Machine state

| Register | Contents                                                        |
|----------|------------------------------------------------------------------|
| `S`      | Value stack (bounded; overflow ⇒ error 1).                       |
| `E`      | Current environment: flat list, parameter *i* at position *i* (innermost first). |
| `G`      | Globals: auto-growing list of cells (see STG).                   |
| `D`      | Dump: control stack of `{return-address, saved-E}` frames (pushed by CALL/APP; also usable by branch joins). |
| `ip`     | Byte offset into the code unit.                                  |
| `error`  | One of §8; nonzero halts.                                        |
| `steps`, `max_steps` | Instruction counters (0 = unlimited).                |

## 5. ROM byte-vector pool

Appended after the code unit, terminated by a trailer:

    [u16 count]
    [count × (u16 offset, u16 length)]     ; offsets relative to data start
    [data bytes…]
    [u16 total-pool-size]                  ; count area + directory + data
    [u16 magic = 0xB1C5]

Detection is by trailer magic at end-of-buffer. Slot *i* of the table is
registered read-only for entry *i*; `LDV i` pushes handle `(BYTEVEC, i)`.

## 6. Instruction set

Operand widths: none = 1 byte total; `u8` = 2; `u16` = 3; `u24` = 4.
All multi-byte operands big-endian. `push`/`pop` refer to `S`.

### 6.1 Moves & constants

| Op    | Mnemonic | Operand | Semantics |
|-------|----------|---------|-----------|
| 0x00  | STOP     | –       | Halt cleanly. |
| 0x01  | LDM      | u16     | Push `heap[addr].car` (direct object peek). |
| 0x02  | LDC      | u16     | Push `FIXNUM(sign-extend16(operand))`. Values outside −2048…2047 wrap via low-12 masking — compilers must use LDCW instead. |
| 0x03  | LDF      | u16     | Push closure `(operand . E)`.                              |
| 0x04  | LDE      | –       | Push `E`.                                                   |
| 0x05  | LDCW     | u24     | Box operand as fresh non-negative BIGNUM; push it.          |
| 0x33  | LDV      | u16     | Push `(BYTEVEC, operand)` — pool slot.                      |

### 6.2 Environment & globals

| Op   | Mnemonic | Operand | Semantics |
|------|----------|---------|-----------|
| 0x40 | LD       | u16     | Push `E[index]` (walk cdr ×index). Missing ⇒ NIL.           |
| 0x41 | ST       | u16     | Pop val; overwrite `E[index]` cell in place; push val back. |
| 0x43 | LDG      | u16     | As LD on the globals list.                                   |
| 0x44 | STG      | u16     | Pop val; grow `G` with NIL cells up to index if needed; set cell; push val. |

### 6.3 Function application

| Op   | Mnemonic | Operand | Semantics |
|------|----------|---------|-----------|
| 0x42 | ARGS     | u8 n    | Pop n values onto front of `E` one-by-one (so the value popped last becomes param 0). Compiler emits args last-first before ARGS. |
| 0x60 | APP      | –       | Pop `args`, pop `func`. PAIR ⇒ closure call: dump-push `E`, dump-push ret=`ip+1`; `E ← closure.cdr`; unroll args onto `S` (first arg deepest); `ip ← closure.car`. FIXNUM ⇒ primitive call by id (§7) with result pushed; `ip += 1`. |
| 0x62 | CALL     | u16 addr| Direct call: pop args; dump-push `E`, ret=`ip+3`; unroll args onto `S`; `ip ← addr`. |
| 0x61 | RTN      | –       | `ret ← D.pop; E ← D.pop; ip ← ret`. Result stays on `S`.     |

Argument-list convention: the caller builds a proper list terminated by
**`LDC 0` (NIL)**; the callee receives values such that the *last* listed
argument ends deepest on `S`, matching ARGS/LD indexing.

### 6.4 Branches

| Op   | Mnemonic | Operand | Semantics |
|------|----------|---------|-----------|
| 0x50 | SEL      | u16 else| Pop cond. Truthy ⇒ `ip += 3` (then-branch is **inline**, immediately following); falsy ⇒ `ip ← else`. |
| 0x51 | JOIN     | –       | `ip ← D.pop` (branch merge). Not emitted by the current compiler (it uses JMP over the dead arm); supported for completeness. |
| 0x52 | LOOP     | u16 tgt | Unconditional jump (loop back-edge).                           |
| 0x54 | JMP      | u16 tgt | Unconditional jump.                                            |
| 0x53 | BRK      | u8      | Debug break hook (no-op in release).                            |

Program prologue produced by the compiler: `JMP entry` at offset 0 (patched
post-codegen), function bodies, then the *entry region*: global-init stores,
then `LDF <entry>`/`LDC nil` + `APP`-style invocation of the entry function,
then implicit STOP.

### 6.5 Lists & vectors

| Op   | Mnemonic | Operand | Semantics |
|------|----------|---------|-----------|
| 0x30/0x31 | CAR/CDR | – | Push car/cdr of popped pair (NIL-safe). |
| 0x32 | CONS     | –       | Pop cdr, pop car, push cons.                                |
| 0x34 | VREF     | –       | Pop idx, pop vec ⇒ push byte (fixnum). Type/bounds errors halt. |
| 0x35 | VSTOR    | –       | Pop val, idx, vec; write byte; push val back.               |
| 0x36 | MKV      | u8? –   | Pop n (fixnum); push fresh zeroed RAM byte-vector of n bytes. |
| 0x37 | LEN      | –       | Push length: bytevec ⇒ bytes; pair ⇒ cell count; else 0.    |

### 6.6 Arithmetic / comparison (all pop `b` then `a`; fixnum domain)

ADD `a+b` · SUB `a−b` · MUL · DIV (÷0 ⇒ NIL) · MOD (÷0 ⇒ NIL) · NEG ·
EQ (structural handle equality) · LT · GT · LE · GE · NOT (logical).

Opcodes: ADD 0x10, SUB 0x11, MUL 0x12, DIV 0x13, MOD 0x14, NEG 0x15,
EQ 0x20, LT 0x21, GT 0x22, LE 0x23, GE 0x24, NOT 0x25.

### 6.7 Control & debug

PRIM 0x70 (u8 id) — call primitive, push result.
PRN 0x78 — print popped value (fixnum decimal / `t` / `nil` / `<object>`), push it back (value-preserving).
GC 0x7A — run collection, push freed-object count.
NOP 0x7B · POP 0x7C · DUMP 0x79 (debug state dump) · ERROR 0xFF (unconditional fault).

Unknown opcodes ⇒ error 4 and halt.

## 7. Primitives

Calling convention: `S` holds an **argument list** (proper NIL-terminated);
the primitive receives that list and returns exactly one value (which may be
NIL). Errors are signaled by returning NIL where documented, or by the VM
halting on type faults.

Identifier assignment is **positional**: ids are the registration order in
the firmware build, and each target's `targets/chips/<chip>.json`
`primitives` table is the normative id map for that device. Emulators must
build their dispatch from that table.

Universal (every device): the core ops above (`car`…`wave-play` block),
UTF-16 codecs `utf16-enc`/`utf16-dec` at ids **200/201**, and the
arbitrary-precision family registered at the HAL tail:

| Name              | Args            | Returns | Notes                                        |
|-------------------|-----------------|---------|----------------------------------------------|
| `%bn-add/sub/mul` | a b             | BIGNUM  | schoolbook over base-256 limbs               |
| `%bn-div/%bn-mod` | a b (b≠0)       | BIGNUM  | truncated division; Knuth D; ÷0 ⇒ NIL        |
| `%bn-cmp`         | a b             | FIXNUM  | −1 / 0 / 1                                   |
| `%bn-to-string`   | a               | STRING  | decimal ASCII, `-` prefix when negative      |
| `%bn-from-string` | s               | BIGNUM  | optional `-`/`+`, digits only, else NIL      |

Board HAL families (presence varies by chip; consult the device's table):
GPIO `%gpio-init/write/read`, UART `%uart-*` (baud accepts boxed wide ints),
sleep/timers `%sleep/%millis`, ADC/PWM/wave stubs, multi-bus I²C
(`%i2c-init sda scl khz → bus-index`, transfers take `bus` first), USB
composite (`%usb-init/hid-add/mouse-add/serial-add/start`, identity setters,
HID report senders, extra CDC ports), and the universal UTF-16 pair used by
the compiler's string literals.

## 8. Error codes

0 ok · 1 stack-overflow · 2 stack-underflow · 3 heap-full · 4 invalid-handle
· 5 type-error · 6 division-by-zero · 7 undefined-symbol · 8 primitive-not-found.
Any error stops execution; `secd_error_string` maps codes to text.

## 9. Device metadata schema (normative for emulators/tools)

`targets/chips/<chip>.json` (merged under board):

```json
{ "chip": "esp32s3", "description": str,
  "output": {"format": "bin"|"uf2", "family": str},
  "memory": {"flash_size": int, "sram_size": int},
  "constraints": {"max_fixnum": 2047, "min_fixnum": -2048,
                   "bignum_support": true},
  "capabilities": ["gpio","uart","i2c", ...],
  "primitives": {"%gpio-init": {"id": 15, "args": 2}, ...},
  "usb": {"device": bool, ...} }
```

`targets/boards/<board>.json` overrides/adds:

```json
{ "name": "blue-pill", "chip": "stm32f103",
  "memory": {"heap_size":int,"stack_size":int,"symbols_size":int},
  "flash_layout": {"firmware_addr","firmware_size","bytecode_addr","bytecode_size"},
  "board_pins": {"led_pin":45,"ws2812_pin":null,...},
  "features": ["gpio","uart","i2c","sleep","millis"] }
```

Emulators should present devices from these files and gate available
primitives by the merged `primitives` table.

---
*Reference implementation:* `include/secd/{types,heap,machine,bytecode,primitives}.h`,
`src/core/{bytecode,primitives,heap,gc,machine,symbols}.cpp`. Companion docs:
`doc/architecture.md`, `doc/bytecode.md`, `doc/primitives.md`, `doc/hal.md`.
