#ifndef KEYBOARD_H
#define KEYBOARD_H

/* Initialise the keyboard driver.  In this minimal implementation
 * nothing is required; a real driver would install an IRQ handler
 * and decode scan codes into ASCII. */
void keyboard_init(void);

#endif /* KEYBOARD_H */
