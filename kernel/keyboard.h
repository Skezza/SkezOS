#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/* Initialise the keyboard driver and register the IRQ handler. */
void keyboard_init(void);

/* Retrieve a character from the keyboard buffer.
 * Returns -1 if no character is available. */
int kbd_getchar(void);

/* Feed a PS/2 scancode into the keyboard state machine (for testing). */
void kbd_feed_scancode(uint8_t scancode);

/* Feed an ASCII character by generating the corresponding scancode. */
void kbd_feed_ascii(char c);

#endif /* KEYBOARD_H */
