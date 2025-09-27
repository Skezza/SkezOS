#include "serial.h"
#include "utils.h"

#define COM1 0x3F8

/* Initialise the 16550A UART at 38400 baud, 8N1.  We leave
 * interrupts disabled until the IDT has been set up. */
void serial_init(void) {
    outb(COM1 + 1, 0x00);    /* disable all interrupts */
    outb(COM1 + 3, 0x80);    /* enable DLAB */
    outb(COM1 + 0, 0x03);    /* divisor low byte: 3 -> 38400 baud */
    outb(COM1 + 1, 0x00);    /* divisor high byte */
    outb(COM1 + 3, 0x03);    /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7);    /* enable FIFO, clear with 14‑byte threshold */
    outb(COM1 + 4, 0x0B);    /* IRQs enabled, RTS/DSR set */
}

static int serial_is_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

void serial_writechar(char c) {
    while (!serial_is_transmit_empty())
        ;
    outb(COM1, c);
}

void serial_writestr(const char *s) {
    while (*s) {
        if (*s == '\n')
            serial_writechar('\r');
        serial_writechar(*s++);
    }
}
