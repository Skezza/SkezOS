#include <stdint.h>
#include "vga.h"
#include "serial.h"
#include "panic.h"
#include "interrupts.h"
#include "memmap.h"
#include "pmm.h"
#include "paging.h"
#include "kmalloc.h"
#include "timer.h"
#include "keyboard.h"

/* Entry point into the kernel from assembly.  The multiboot magic
 * value and pointer to the multiboot information structure are
 * passed in as arguments by the loader. */
void kmain(uint32_t mb_magic, uint32_t mb_info) {
    /* Bring up basic I/O devices */
    serial_init();
    vga_clear();
    serial_writestr("serial online\n");
    vga_puts("tinyos kernel starting...\n");

    /* Parse the memory map and initialise the physical memory manager */
    memmap_parse(mb_magic, mb_info);

    /* Initialise paging structures and enable paging */
    paging_init();
    paging_enable();

    /* Initialise a simple heap between 16MiB and 20MiB.  In a real
     * kernel you would allocate this region using the PMM and map it
     * into the virtual address space. */
    kmalloc_init((void *)0x01000000, 4 * 1024 * 1024);
    serial_writestr("kmalloc: ready\n");

    /* Install interrupt handlers and enable hardware interrupts */
    interrupts_install();

    /* Set up timer (100Hz) and keyboard.  Without IRQ routing the
     * handlers are never invoked, but the hardware is programmed. */
    timer_init(100);
    keyboard_init();
    serial_writestr("kernel initialised\n");
    vga_puts("kernel initialised\n");

    /* Test the page fault handler by accessing an unmapped address.
     * This should trigger our page fault handler which will panic. */
    volatile uint32_t *p = (uint32_t *)0xDEADBEEF;
    (void)*p;

    /* Loop forever, halting the CPU when idle. */
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
