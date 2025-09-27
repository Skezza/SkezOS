#include "timer.h"
#include "utils.h"
#include "serial.h"
#include "irq.h"
#include <stdint.h>

volatile uint64_t timer_ticks = 0;

static void timer_handler(void *ctx) {
    (void)ctx;
    timer_ticks++;
    if (timer_ticks % 100 == 0) {
        serial_writestr("tick\n");
    }
}

void timer_init(uint32_t freq) {
    if (freq == 0) return;
    /* Register timer IRQ handler and unmask IRQ0 */
    irq_register(0, timer_handler, NULL);
    irq_mask(0, 0);
    /* Program PIT channel 0 */
    uint32_t divisor = 1193180 / freq;
    outb(0x43, 0x36);      /* Command byte: channel 0, lo/hi, square wave */
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    serial_writestr("timer: init\n");
}
