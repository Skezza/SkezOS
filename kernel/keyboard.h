#ifndef KEYBOARD_H
#define KEYBOARD_H

/* Initialise the keyboard driver and register the IRQ handler. */
void keyboard_init(void);

/* Retrieve a character from the keyboard buffer.
 * Returns -1 if no character is available. */
int kbd_getchar(void);

#endif /* KEYBOARD_H */
