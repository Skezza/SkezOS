#include "keyboard.h"
#include "utils.h"
#include "irq.h"
#include "serial.h"
#include "vga.h"
#include <stdint.h>
#include <stdbool.h>

/* Keyboard buffer */
#define KBD_BUF_SIZE 128
static char kbd_buffer[KBD_BUF_SIZE];
static uint8_t kbd_head = 0;
static uint8_t kbd_tail = 0;

/* US keyboard layout for set 1 scancodes */
static const char kbd_us[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0,'*',0,' ',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char kbd_us_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,
    '|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static bool shift_pressed = false;

static void keyboard_handler(void *ctx) {
    (void)ctx;
    uint8_t scancode = inb(0x60);
    if (scancode & 0x80) {
        /* Key release */
        uint8_t code = scancode & 0x7F;
        if (code == 0x2A || code == 0x36) {
            shift_pressed = false;
        }
        return;
    } else {
        /* Key press */
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = true;
            return;
        }
        char ch = shift_pressed ? kbd_us_shift[scancode] : kbd_us[scancode];
        if (ch != 0) {
            uint8_t next = (kbd_head + 1) % KBD_BUF_SIZE;
            if (next != kbd_tail) {
                kbd_buffer[kbd_head] = ch;
                kbd_head = next;
            }
        }
    }
}

/* Retrieve a character from the keyboard buffer.
 * Returns -1 if no character is available. */
int kbd_getchar(void) {
    if (kbd_head == kbd_tail) {
        return -1;
    }
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return (int)(unsigned char)c;
}

void keyboard_init(void) {
    /* Register keyboard IRQ handler (IRQ1) and unmask it */
    irq_register(1, keyboard_handler, NULL);
    irq_mask(1, 0);
}
