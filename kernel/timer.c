#include "timer.h"
#include "utils.h"
#include "serial.h"

/* We don't yet have IRQ dispatch, so timer callbacks are unused.  The
 * init routine simply programs the PIT channel 0 for the requested
 * frequency. */
void timer_init(uint32_t freq) {
    if (freq == 0) return;
    uint32_t divisor = 1193180 / freq;
    outb(0x43, 0x36);             /* Command byte: channel 0, lo/hi, square wave */
    outb(0x40, divisor & 0xFF);   /* Low byte of divisor */
    outb(0x40, (divisor >> 8) & 0xFF); /* High byte */
    serial_writestr("timer: set\n");
}
