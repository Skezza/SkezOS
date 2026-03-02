#include "timer.h"
#include "klog.h"
#include "sched.h"
#include "utils.h"
#include "irq.h"
#include <stdint.h>

volatile uint64_t timer_ticks = 0;
static uint32_t g_timer_frequency_hz = 0;

static uint32_t timer_enter_critical(void) {
    uint32_t flags;

    __asm__ __volatile__(
        "pushf\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory");
    return flags;
}

static void timer_leave_critical(uint32_t saved_flags) {
    if ((saved_flags & (1U << 9)) != 0U) {
        __asm__ __volatile__("sti" ::: "memory");
    } else {
        __asm__ __volatile__("" ::: "memory");
    }
}

static void timer_handler(void *ctx) {
    (void)ctx;
    timer_ticks++;
    sched_on_timer_tick_irq();
}

uint64_t timer_ticks_snapshot(void) {
    uint32_t saved_flags = timer_enter_critical();
    uint64_t ticks = timer_ticks;

    timer_leave_critical(saved_flags);
    return ticks;
}

uint32_t timer_frequency_hz(void) {
    return g_timer_frequency_hz;
}

void timer_init(uint32_t freq) {
    if (freq == 0) {
        KLOGW("timer_init called with freq=0");
        return;
    }
    /* Register timer IRQ handler and unmask IRQ0 */
    irq_register(0, timer_handler, NULL);
    irq_mask(0, 0);
    /* Program PIT channel 0 */
    uint32_t divisor = 1193180 / freq;
    outb(0x43, 0x36);      /* Command byte: channel 0, lo/hi, square wave */
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    g_timer_frequency_hz = freq;
    KLOGI("timer: init (%u Hz)", freq);
}
