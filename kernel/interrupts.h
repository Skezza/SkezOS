#ifndef INTERRUPTS_H
#define INTERRUPTS_H

/* Install the default interrupt handlers, set up the IDT and PIC and
 * enable interrupts.  This must be called after the IDT has been
 * created. */
void interrupts_install(void);

#endif /* INTERRUPTS_H */
