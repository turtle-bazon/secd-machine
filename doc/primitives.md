# C Primitive Interface

## Overview

The SECD VM exposes C functions for hardware access. These primitives are the minimal interface between the VM and hardware. secd-lisp wraps them in a nicer API.

## Primitive Registration

```c
// primitive.h
#ifndef SECD_PRIMITIVE_H
#define SECD_PRIMITIVE_H

#include <stdint.h>

// Primitive function signature
// Takes list of arguments, returns result
typedef value_t (*prim_fn)(value_t args);

// Register a primitive function
void secd_register_prim(const char* name, prim_fn fn);

// Call a primitive by ID
value_t secd_call_prim(uint8_t func_id, value_t args);

// Initialize all primitives for a target
void secd_init_primitives(void);

#endif // SECD_PRIMITIVE_H
```

## Primitive Calling Convention

Primitives receive arguments as a list and return a single value:

```lisp
;; In secd-lisp:
(gpio-write 25 1)

;; Bytecode:
LDC 25        ; pin
LDC 1         ; value
PRIM gpio-write  ; call primitive
```

```c
// C implementation
value_t prim_gpio_write(value_t args) {
    value_t pin = car(args);      // First argument
    value_t value = car(cdr(args)); // Second argument
    
    hal_gpio_write(pin, value);
    return NIL;  // Return nil
}
```

## Standard Primitives

### List Operations (built-in)

These are implemented in the VM, not as C primitives:

```lisp
(car pair)      ; Get first element
(cdr pair)      ; Get rest element
(cons a b)      ; Create pair
(eq? a b)       ; Equality test
```

### Arithmetic (built-in or primitive)

Option 1: Built-in (simpler, faster)
```
ADD, SUB, MUL, DIV, EQ, LT, GT, LE, GE
```

Option 2: Primitives (more flexible)
```c
value_t prim_add(value_t args) {
    value_t a = car(args);
    value_t b = car(cdr(args));
    return make_integer(integer_value(a) + integer_value(b));
}
```

### I/O Primitives (target-specific)

```c
// GPIO
value_t prim_gpio_init(value_t args);    ; (gpio-init pin mode)
value_t prim_gpio_write(value_t args);   ; (gpio-write pin value)
value_t prim_gpio_read(value_t args);    ; (gpio-read pin)

// UART
value_t prim_uart_init(value_t args);    ; (uart-init baud)
value_t prim_uart_write(value_t args);   ; (uart-write byte)
value_t prim_uart_read(value_t args);    ; (uart-read)

// Timing
value_t prim_sleep(value_t args);        ; (sleep ms)

// ADC
value_t prim_adc_read(value_t args);     ; (adc-read channel)
```

## Target Description

Each target defines available primitives:

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
              (sleep . prim-sleep)
              (adc-read . prim-adc-read))
 :memory (:heap 200000 :stack 16000 :symbols 256))
```

## secd-lisp Wrapper

secd-lisp wraps primitives in a nicer API:

```lisp
;; secd-lisp library: gpio.lisp

(defun digital-write (pin value)
  (prim-gpio-write pin value))

(defun digital-read (pin)
  (prim-gpio-read pin))

(defun pin-mode (pin mode)
  (prim-gpio-init pin mode))

;; Higher-level functions
(defun blink (pin times delay)
  (dotimes (i times)
    (digital-write pin 1)
    (sleep delay)
    (digital-write pin 0)
    (sleep delay)))
```

## Adding New Primitives

### 1. Implement in C

```c
// my_primitive.c
#include "secd/primitives.h"

value_t prim_my_function(value_t args) {
    // Extract arguments
    value_t arg1 = car(args);
    value_t arg2 = car(cdr(args));
    
    // Do something
    int result = do_something(arg1, arg2);
    
    // Return result
    return make_integer(result);
}
```

### 2. Register in target init

```c
// rp2040.c
void rp2040_init_primitives(void) {
    secd_register_prim("my-function", prim_my_function);
    // ... other primitives
}
```

### 3. Add to target description

```lisp
:primitives ((my-function . prim-my-function))
```

### 4. Use in secd-lisp

```lisp
(defun do-work ()
  (my-function 1 2))
```

## Error Handling

Primitives can signal errors:

```c
value_t prim_divide(value_t args) {
    value_t a = car(args);
    value_t b = car(cdr(args));
    
    if (integer_value(b) == 0) {
        secd_error(ERROR_DIVISION_BY_ZERO);
        return NIL;
    }
    
    return make_integer(integer_value(a) / integer_value(b));
}
```

Error codes:
```c
enum secd_error {
    ERROR_NONE = 0,
    ERROR_STACK_OVERFLOW,
    ERROR_STACK_UNDERFLOW,
    ERROR_HEAP_FULL,
    ERROR_INVALID_HANDLE,
    ERROR_TYPE_ERROR,
    ERROR_DIVISION_BY_ZERO,
    ERROR_UNDEFINED_SYMBOL,
    ERROR_PRIMITIVE_NOT_FOUND
};
```

## Testing Primitives

Test primitives on host before MCU:

```c
// test_primitives.c
void test_gpio_write(void) {
    // Mock HAL for host testing
    hal_mock_init();
    
    value_t args = cons(make_integer(25), cons(make_integer(1), NIL));
    value_t result = prim_gpio_write(args);
    
    assert(result == NIL);
    assert(hal_mock_gpio_written(25) == 1);
}
```
