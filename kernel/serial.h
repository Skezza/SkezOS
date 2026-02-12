#ifndef SERIAL_H
#define SERIAL_H

/* Initialise the first serial port (COM1). */
void serial_init(void);

/* Write a single character to the serial port.  Blocks until the
 * transmit FIFO is empty. */
void serial_writechar(char c);

/* Write a NUL‑terminated string to the serial port.  Newline
 * characters are converted to carriage return/newline as most
 * terminals expect a CR before LF. */
void serial_writestr(const char *s);

/* Read a character from the serial port if available, or -1 otherwise. */
int serial_readchar(void);

#endif /* SERIAL_H */
