#ifndef TIMER_H
#define TIMER_H
#include <stdint.h>

/* Initialise the Programmable Interval Timer (PIT) to interrupt at
 * the given frequency (Hz).  Only the hardware is programmed; you
 * still need to install an IRQ handler to receive timer ticks. */
void timer_init(uint32_t freq);

#endif /* TIMER_H */
