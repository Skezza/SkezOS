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

static void handle_input_char(char c) {
    serial_writechar(c);
    vga_putc(c);
    serial_writestr("\nReceived: ");
    serial_writechar(c);
    serial_writechar('\n');
}

void kmain(uint32_t magic, uint32_t mb2_addr) {
    // Initialize serial and VGA outputs
    serial_init();
    vga_clear();
    serial_writestr("TinyOS booting...\n");
    vga_puts("TinyOS booting...\n");

    // Parse memory map and initialize memory management
    memmap_parse(magic, mb2_addr);

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

    // Prompt for user input on the serial console
    serial_writestr("Ready> ");
    vga_puts("Ready> ");

    // Loop forever, consuming keyboard and serial input.
    // Serial input is injected into the keyboard path for one unified queue.
    for (;;) {
        int ch = kbd_getchar();
        if (ch == -1) {
            int serial_ch = serial_readchar();
            if (serial_ch != -1) {
                kbd_feed_ascii((char)serial_ch);
                ch = kbd_getchar();
                if (ch == -1) {
                    ch = serial_ch;
                }
            }
        }
        if (ch != -1) {
            handle_input_char((char)ch);
        }
        __asm__ __volatile__("hlt");
    }
}
