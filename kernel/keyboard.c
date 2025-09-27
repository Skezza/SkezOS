#include "keyboard.h"
#include "utils.h"
#include "serial.h"

/* Placeholder keyboard driver.  In a full kernel this would
 * register an IRQ handler (IRQ1) with the PIC and translate scan
 * codes into ASCII characters.  For now we simply read scan codes
 * and dump them to the serial port. */
static void keyboard_callback(void) {
    uint8_t scancode = inb(0x60);
    serikl_writestr("key ");
    char hex[3];
    const char *digits = "0123456789ABCDEF";
    hex[0] = digits[(scancode >> 4) & 0xF];
    hex[1] = digits[scancode & 0xF];
    hex[2] = '\0';
    serial_writestr(hex);
    serial_writestr("\n");
}

void keyboard_init(void) {
    /* Nothing to do until IRQ routing is implemented */
    (void)keyboard_callback;
}
