/* STM32F401RC linker script for the Black Pill (WeAct v1204).
 *
 * Copyright (C) 2026  License: GPL3
 *
 * Plain bare-metal image: the whole .bin flashes at 0x08000000 (SWD or the
 * ROM serial bootloader). The secd-lisp linker appends bytecode right after
 * the firmware image; __flash_binary_end marks the true end of the flashed
 * sections so the runtime can scan for it.
 *
 * Memory:
 *   FLASH : 0x08000000, length 256KB
 *   RAM   : 0x20000000, length 64KB (stack from top, heap over _sbrk)
 */

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 256K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 64K
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

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata .rodata.* .constdata .constdata.*)
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
    *(.gnu.linkonce.d*)
    __data_end = .;
  } > RAM AT> FLASH

  __data_load = LOADADDR(.data);

  /* End of the flashed image: firmware + data load region. Bytecode is
   * appended here by the secd-lisp linker. */
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

  /* Heap: everything between end of .bss and top of SRAM (the stack grows
   * down from the top; combined use must stay within the part's SRAM). */
  __heap_start = __bss_end;
  __heap_end = __ram_end__;

  /DISCARD/ :
  {
    *(.comment)
    *(.ARM.attributes)
  }
}
