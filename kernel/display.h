#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

/* Initialize the active kernel display surface.  The current backend is
 * VGA text mode; future framebuffer support should plug in here without
 * forcing callers to care about the backend choice.
 */
void display_init(void);

/* Finish display initialization after the memory map, paging, and heap
 * are ready.  This is where non-VGA backends can claim mapped resources.
 */
void display_late_init(void);

/* Enter/leave the active console surface critical section. */
uint32_t display_console_enter_critical(void);
void display_console_leave_critical(uint32_t saved_flags);

/* Write to the active display surface. */
void display_putc(char c);
void display_puts(const char *str);

/* Returns non-zero if a framebuffer window is present and mapped, even
 * if the active console output still targets VGA.
 */
int display_framebuffer_ready(void);

#endif /* DISPLAY_H */
