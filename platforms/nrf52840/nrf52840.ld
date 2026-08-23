/* nRF52840 linker script for SuperMini / XIAO BLE.
 * Copyright (C) 2026  License: GPL3
 *
 * Plain bare-metal image: whole .bin flashes at 0x00000000 (SWD or
 * Adafruit nRF52 bootloader). Bytecode appended by secd-lisp linker.
 *
 * Memory:
 *   FLASH : 0x00000000, length 1MB
 *   RAM   : 0x20000000, length 256KB (stack from top, heap over _sbrk)
 */

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 1024K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 256K
}

__ram_start__ = ORIGIN(RAM);
__ram_end__   = ORIGIN(RAM) + LENGTH(RAM);
__stack_top__ = __ram_end__;

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text .text.*)
    *(.gnu.linkonce.t*)
    . = ALIGN(4);
  } > FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata .rodata.* .constdata .constdata.*)
    *(.gnu.linkonce.r*)
    . = ALIGN(4);
  } > FLASH

  .ARM.exidx :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } > FLASH

  .data :
  {
    . = ALIGN(4);
    __data_start = .;
    *(.data .data.*)
    *(.gnu.linkonce.d*)
    __data_end = .;
  } > RAM AT > FLASH

  __data_load = LOADADDR(.data);

  __flash_binary_end = LOADADDR(.data) + SIZEOF(.data);

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > RAM

  __heap_start = __bss_end;
  __heap_end = __ram_end__;

  /DISCARD/ :
  {
    *(.comment)
    *(.ARM.attributes)
  }
}
