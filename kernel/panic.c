#include "panic.h"
#include "serial.h"
#include "vga.h"

/* Print a panic message and halt the CPU. */
void panic(const char *msg) {
    serial_writestr("PANIC: ");
    serial_writestr(msg);
    serial_writestr("\n");
    vga_puts("PANIC: ");
    vga_puts(msg);
    vga_puts("\n");
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
