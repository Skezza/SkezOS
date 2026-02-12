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
    // ASCII logo for SkezOS
    const char *logo =
        " ad88888ba   88                                 ,ad8888ba,     ad88888ba  \n"
        "d8\"     \"8b  88                                d8\'    \`8b   d8\"     \"8b \n"
        "Y8,          88                               d8\'        \`8b  Y8,         \n"
        "`Y8aaaaa,    88   ,d8   ,adPPYba,  888888888  88          88  `Y8aaaaa,   \n"
        "  \`\"\"\"\"\"8b,  88 ,a8\"   a8P_____88       a8P\"  88          88    \`\"\"\"\"\"8b, \n"
        "        \`8b  8888[     8PP\"\"\"\"\"    ,d8P'    Y8,        ,8P          \`8b \n"
        "Y8a     a8P  88\`\"Yba,  \"8b,   ,aa  ,d8\"        Y8a.    .a8P   Y8a     a8P \n"
        " \"Y88888P\"   88   \`Y8a  \`\"Ybbd8\'\"  888888888    \`\"Y8888Y\'\"     \"Y88888P\"\n";
    serial_writestr(logo);
    vga_puts(logo);
    serial_writestr("SkezOS booting...\n");
    vga_puts("SkezOS booting...\n");

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
