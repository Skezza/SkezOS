#ifndef VGA_H
#define VGA_H

#include <stdint.h>

/* Clear the VGA text buffer, redraw the fixed chrome rows, and reset the
 * cursor to the first content row.
 */
void vga_clear(void);

/* Enter/leave a short single-core console critical section. The return value
 * captures the prior interrupt-enabled state and must be passed back to the
 * matching leave call.
 */
uint32_t vga_console_enter_critical(void);
void vga_console_leave_critical(uint32_t saved_flags);

/* Write a single character to the VGA text buffer at the current content
 * cursor position.  Handles newlines by advancing to the next line.
 */
void vga_putc(char c);

/* Write a NUL‑terminated string to the VGA text buffer. */
void vga_puts(const char *str);

#endif /* VGA_H */
