#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/* Global tick count incremented by the PIT interrupt handler */
extern volatile uint64_t timer_ticks;

/* Return a stable 64-bit monotonic tick snapshot. */
uint64_t timer_ticks_snapshot(void);

/* Return the configured PIT frequency in Hz. */
uint32_t timer_frequency_hz(void);

/* Initialise the Programmable Interval Timer (PIT) to interrupt at the given frequency (Hz). */
void timer_init(uint32_t freq);

#endif /* TIMER_H */
