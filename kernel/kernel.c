#include <stdint.h>
#include "serial.h"
#include "vga.h"
#include "panic.h"
#include "interrupts.h"
#include "irq.h"
#include "memmap.h"
#include "pmm.h"
#include "paging.h"
#include "kmalloc.h"
#include "timer.h"
#include "keyboard.h"

void kmain(uint32_t magic, uint32_t mb2_addr) {
    // Initialize serial and VGA outputs
    serial_init();
    vga_clear();
    serial_writestr("TinyOS booting...\n");
    vga_puts("TinyOS booting...\n");

    // Parse memory map and initialize memory management
    memmap_parse(magic, mb2_addr);
    pmm_init();

    // Setup paging and enable it
    paging_init();
    paging_enable();

    // Initialize a simple kernel heap (4 MiB starting at 0xC0100000)
    kmalloc_init((void *)0xC0100000, 4 * 1024 * 1024);

    // Install default interrupt handlers and remap the PIC
    interrupts_install();

    // Disable interrupts while we set up IRQ stubs and hardware IRQ handlers
    __asm__ __volatile__("cli");

    // Initialize IRQ stubs in the IDT and mask all hardware IRQs
    irq_init();

    // Set up timer (100Hz) and keyboard
    timer_init(100);
    keyboard_init();

    // Now enable CPU interrupts
    __asm__ __volatile__("sti");

    serial_writestr("kernel initialised\n");
    vga_puts("kernel initialised\n");

    // Loop forever, echoing typed characters
    for (;;) {
        int ch = kbd_getchar();
        if (ch != -1) {
            char c = (char)ch;
            serial_writechar(c);
            vga_putc(c);
        }
        // Halt CPU until next interrupt to save power
        __asm__ __volatile__("hlt");
    }
}
