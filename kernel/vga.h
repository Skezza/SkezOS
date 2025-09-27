#ifndef VGA_H
#define VGA_H

#include <stdint.h>

/* Clear the VGA text buffer and reset the cursor. */
void vga_clear(void);

/* Write a single character to the VGA text buffer at the current
 * cursor position.  Handles newlines by advancing to the next line.
 */
void vga_putc(char c);

/* Write a NUL‑terminated string to the VGA text buffer. */
void vga_puts(const char *str);

#endif /* VGA_H */
