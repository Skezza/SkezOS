#ifndef PANIC_H
#define PANIC_H

/* Display a panic message on both the serial console and the VGA
 * display, then halt the processor.  This function does not return. */
void panic(const char *msg);

#endif /* PANIC_H */
