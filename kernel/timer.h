#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/* Global tick count incremented by the PIT interrupt handler */
extern volatile uint64_t timer_ticks;

/* Initialise the Programmable Interval Timer (PIT) to interrupt at the given frequency (Hz). */
void timer_init(uint32_t freq);

#endif /* TIMER_H */
