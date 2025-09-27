/*
 * Linker script for the tiny kernel.
 *
 * We load the kernel at the 1MiB mark to leave the lower physical
 * addresses free for BIOS tables, the video buffer and real‑mode data.
 */
ENTRY(_start)

SECTIONS
{
  /* The kernel is loaded at 1MiB in physical memory.  GRUB
   * automatically relocates the image for us.  */
  . = 1M;

  /* Text (code) segment.  The multiboot header must be kept at the
   * beginning of the text section so that GRUB can find it.  */
  .text : ALIGN(4K) {
    KEEP(*(.multiboot2_header))
    *(.text*)
  }

  /* Read‑only data */
  .rodata : ALIGN(4K) {
    *(.rodata*)
  }

  /* Data */
  .data : ALIGN(4K) {
    *(.data*)
  }

  /* Uninitialised data; includes the stack reserved in loader.S */
  .bss : ALIGN(4K) {
    *(.bss*)
    *(COMMON)
  }
}
