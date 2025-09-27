#ifndef PIC_H
#define PIC_H

/* Remap the programmable interrupt controller (PIC) so that IRQ
 * vectors do not overlap with the CPU exception vectors (0–31).
 * offset1 is the vector offset for the master PIC, offset2 for the
 * slave.  0x20 and 0x28 are typical. */
void pic_remap(int offset1, int offset2);

#endif /* PIC_H */
