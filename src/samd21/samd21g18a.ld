/* SAMD21G18A linker script for the Seeed XIAO SAMD21.
 *
 * Copyright (C) 2026  License: GPL3
 *
 * The Adafruit/Seeed UF2 bootloader occupies the first 8KB of flash
 * (0x0000..0x1FFF) and jumps to the application at 0x2000. The bytecode
 * blob is glued right after the firmware in flash (see __flash_binary_end).
 *
 * Memory:
 *   FLASH : 0x00002000, length 0x3E000  (256KB - 8KB bootloader)
 *   RAM   : 0x20000000, length 0x8000   (32KB SRAM)
 */

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00002000, LENGTH = 0x3E000
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 0x8000
}

__ram_start__  = ORIGIN(RAM);
__ram_end__    = ORIGIN(RAM) + LENGTH(RAM);
__stack_top__  = __ram_end__;

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

  /* End of firmware image in flash: bytecode is appended here by the linker. */
  __flash_binary_end = .;

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata .rodata.* .constdata .constdata.*)
    . = ALIGN(4);
    *(.gnu.linkonce.r*)
  } > FLASH

  .init_array :
  {
    . = ALIGN(4);
    __init_array_start = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array))
    __init_array_end = .;
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
    . = ALIGN(4);
    *(.gnu.linkonce.d*)
    __data_end = .;
  } > RAM AT> FLASH

  __data_load = LOADADDR(.data);

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > RAM

  /* Heap: everything between end of .bss and top of SRAM. */
  __heap_start = __bss_end;
  __heap_end = __ram_end__;

  /DISCARD/ :
  {
    *(.comment)
    *(.ARM.attributes)
  }
}