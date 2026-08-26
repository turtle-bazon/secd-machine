/* nRF52840 linker script.
 * Copyright (C) 2026  License: GPL3
 *
 * The flash origin is NOT hardcoded here: it is supplied at link time via
 * --defsym=__app_base__=<addr> (read from the board metadata). A bare-metal
 * SWD image uses 0x00000000; an image for the Adafruit nRF52 bootloader +
 * SoftDevice S140 uses 0x00026000. The bytecode is appended by the secd-lisp
 * linker right after this image; the runtime scans for it from __flash_binary_end.
 *
 * Memory:
 *   FLASH : <__app_base__>, length 1MB - __app_base__
 *   RAM   : 0x20000000, length 256KB (stack from top, heap over _sbrk)
 */

PROVIDE(__app_base__ = 0x00000000);

MEMORY
{
  FLASH (rx) : ORIGIN = __app_base__, LENGTH = 0x100000 - __app_base__
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 256K
}

__ram_start__ = ORIGIN(RAM);
__ram_end__   = ORIGIN(RAM) + LENGTH(RAM);
/* Keep the initial SP clearly inside SRAM.  Some bootloader app-validity
 * checks reject SP == SRAM end (0x20040000); leave 1 KiB of headroom. */
__stack_top__ = __ram_end__ - 0x400;

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
