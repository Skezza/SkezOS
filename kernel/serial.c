#include "serial.h"
#include "utils.h"

#define COM1 0x3F8
#define SERIAL_RX_BUF_SIZE 64

static unsigned char serial_rx_buf[SERIAL_RX_BUF_SIZE];
static unsigned int serial_rx_head = 0;
static unsigned int serial_rx_tail = 0;

static int serial_rx_buffer_get(void) {
    if (serial_rx_head == serial_rx_tail) {
        return -1;
    }
    int c = serial_rx_buf[serial_rx_tail];
    serial_rx_tail = (serial_rx_tail + 1) % SERIAL_RX_BUF_SIZE;
    return c;
}

static void serial_rx_buffer_put(unsigned char c) {
    unsigned int next = (serial_rx_head + 1) % SERIAL_RX_BUF_SIZE;
    if (next == serial_rx_tail) {
        return;
    }
    serial_rx_buf[serial_rx_head] = c;
    serial_rx_head = next;
}

static void serial_drain_rx_to_buffer(void) {
    while (inb(COM1 + 5) & 0x01) {
        serial_rx_buffer_put(inb(COM1));
    }
}

/* Initialise the 16550A UART at 38400 baud, 8N1.  We leave
 * interrupts disabled until the IDT has been set up. */
void serial_init(void) {
    /* Preserve any bytes that may have arrived before the kernel
     * finishes booting. */
    serial_drain_rx_to_buffer();

    outb(COM1 + 1, 0x00);    /* disable all interrupts */
    outb(COM1 + 3, 0x80);    /* enable DLAB */
    outb(COM1 + 0, 0x03);    /* divisor low byte: 3 -> 38400 baud */
    outb(COM1 + 1, 0x00);    /* divisor high byte */
    outb(COM1 + 3, 0x03);    /* 8 bits, no parity, one stop bit */
    /* Enable FIFO with 14-byte threshold, but do not clear RX/TX FIFOs here.
     * This helps preserve bytes that may already be queued by the emulator. */
    outb(COM1 + 2, 0xC1);
    outb(COM1 + 4, 0x0B);    /* IRQs enabled, RTS/DSR set */

    serial_drain_rx_to_buffer();
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

static int serial_is_data_available(void) {
    return inb(COM1 + 5) & 0x01;
}

int serial_readchar(void) {
    int buffered = serial_rx_buffer_get();
    if (buffered != -1) {
        return buffered;
    }
    if (!serial_is_data_available())
        return -1;
    return inb(COM1);
}
