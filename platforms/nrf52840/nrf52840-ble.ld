/* nRF52840 BLE linker script (SoftDevice build).
 * Copyright (C) 2026  License: GPL3
 *
 * Same as nrf52840.ld except RAM origin is shifted up by 16 KiB: the S140
 * SoftDevice reserves 0x20000000 .. 0x20003FFF for its own RAM. The
 * application RAM region must live at (or above) the base we hand to
 * sd_ble_enable() (0x20004000).
 *
 * Flash origin is still supplied at link time via --defsym=__app_base__.
 */

PROVIDE(__app_base__ = 0x00000000);

MEMORY
{
  FLASH (rx) : ORIGIN = __app_base__, LENGTH = 0x100000 - __app_base__
  RAM (rwx)  : ORIGIN = 0x20004000, LENGTH = 0x3C000
}

__ram_start__ = ORIGIN(RAM);
__ram_end__   = ORIGIN(RAM) + LENGTH(RAM);
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

  /* RAM that survives warm reset (neither copied from flash nor zeroed):
   * used to carry the SoftDevice fault record across the SD's auto-reset. */
  .noinit (NOLOAD) :
  {
    . = ALIGN(4);
    __noinit_start = .;
    *(.noinit .noinit.*)
    . = ALIGN(4);
    __noinit_end = .;
  } > RAM

  __heap_start = __noinit_end;
  __heap_end = __ram_end__;

  /DISCARD/ :
  {
    *(.comment)
    *(.ARM.attributes)
  }
}
