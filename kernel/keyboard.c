#include "keyboard.h"
#include "display.h"
#include "utils.h"
#include "irq.h"
#include "serial.h"
#include "syscall_abi.h"
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
static bool keyboard_verbose = false;
static bool extended_prefix = false;

static uint32_t kbd_modifiers(void) {
    return shift_pressed ? SYSCALL_GUI_MOD_SHIFT : 0U;
}

static uint32_t kbd_gui_keycode_for_extended(uint8_t scancode) {
    switch (scancode) {
    case 0x48:
        return SYSCALL_GUI_KEY_UP;
    case 0x50:
        return SYSCALL_GUI_KEY_DOWN;
    case 0x4B:
        return SYSCALL_GUI_KEY_LEFT;
    case 0x4D:
        return SYSCALL_GUI_KEY_RIGHT;
    default:
        return SYSCALL_GUI_KEY_NONE;
    }
}

static uint32_t kbd_gui_keycode_for_scancode(uint8_t scancode) {
    switch (scancode) {
    case 0x01:
        return SYSCALL_GUI_KEY_ESCAPE;
    case 0x0E:
        return SYSCALL_GUI_KEY_BACKSPACE;
    case 0x0F:
        return SYSCALL_GUI_KEY_TAB;
    case 0x1C:
        return SYSCALL_GUI_KEY_ENTER;
    case 0x39:
        return SYSCALL_GUI_KEY_SPACE;
    default:
        return SYSCALL_GUI_KEY_NONE;
    }
}

static int kbd_gui_route_key(uint32_t keycode, uint32_t ch, int pressed) {
    if (!display_gui_mode_active()) {
        return 0;
    }
    return display_gui_handle_key_event(keycode, ch, kbd_modifiers(), pressed);
}

static int kbd_try_handle_extended_press(uint8_t scancode) {
    uint32_t gui_keycode = kbd_gui_keycode_for_extended(scancode);

    if (gui_keycode != SYSCALL_GUI_KEY_NONE &&
        kbd_gui_route_key(gui_keycode, 0U, 1)) {
        return 1;
    }
    switch (scancode) {
    case 0x48:
        return display_handle_navigation_key(DISPLAY_NAV_KEY_UP);
    case 0x50:
        return display_handle_navigation_key(DISPLAY_NAV_KEY_DOWN);
    case 0x4B:
        return display_handle_navigation_key(DISPLAY_NAV_KEY_LEFT);
    case 0x4D:
        return display_handle_navigation_key(DISPLAY_NAV_KEY_RIGHT);
    default:
        return 0;
    }
}

static void kbd_buffer_put(char ch) {
    uint8_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next == kbd_tail) {
        return;
    }
    kbd_buffer[kbd_head] = ch;
    kbd_head = next;
}

static void keyboard_handler(void *ctx) {
    (void)ctx;
    uint8_t scancode = inb(0x60);
    if (keyboard_verbose) {
        serial_writestr("IRQ1 scancode: ");
        serial_writechar(((scancode >> 4) & 0xF) < 10 ? '0' + ((scancode >> 4) & 0xF) : 'A' + ((scancode >> 4) & 0xF) - 10);
        serial_writechar((scancode & 0xF) < 10 ? '0' + (scancode & 0xF) : 'A' + (scancode & 0xF) - 10);
        serial_writestr("\n");
    }
    if (scancode == 0xE0) {
        extended_prefix = true;
        return;
    }
    if (extended_prefix) {
        uint8_t code = scancode & 0x7F;

        if ((scancode & 0x80U) == 0U) {
            (void)kbd_try_handle_extended_press(code);
        } else {
            uint32_t gui_keycode = kbd_gui_keycode_for_extended(code);
            if (gui_keycode != SYSCALL_GUI_KEY_NONE) {
                (void)kbd_gui_route_key(gui_keycode, 0U, 0);
            }
        }
        extended_prefix = false;
        return;
    }
    if (scancode & 0x80) {
        /* Key release */
        uint8_t code = scancode & 0x7F;
        if (code == 0x2A || code == 0x36) {
            shift_pressed = false;
        }
        {
            uint32_t gui_keycode = kbd_gui_keycode_for_scancode(code);
            if (gui_keycode != SYSCALL_GUI_KEY_NONE) {
                (void)kbd_gui_route_key(gui_keycode, 0U, 0);
            }
        }
        return;
    } else {
        /* Key press */
        uint32_t gui_keycode = kbd_gui_keycode_for_scancode(scancode);

        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = true;
            return;
        }
        char ch = shift_pressed ? kbd_us_shift[scancode] : kbd_us[scancode];
        if (display_gui_mode_active()) {
            if (kbd_gui_route_key(gui_keycode, (uint32_t)(uint8_t)ch, 1)) {
                return;
            }
        }
        if (ch != 0) {
            kbd_buffer_put(ch);
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

static bool ascii_to_scancode(char c, uint8_t *code, bool *shift) {
    *shift = false;
    if (c >= 'a' && c <= 'z') {
        static const uint8_t table[26] = {
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23,
            0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19,
            0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D,
            0x15, 0x2C
        };
        *code = table[c - 'a'];
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        uint8_t lower;
        if (!ascii_to_scancode((char)(c - 'A' + 'a'), &lower, shift))
            return false;
        *code = lower;
        *shift = true;
        return true;
    }
    switch (c) {
    case '1': *code = 0x02; return true;
    case '2': *code = 0x03; return true;
    case '3': *code = 0x04; return true;
    case '4': *code = 0x05; return true;
    case '5': *code = 0x06; return true;
    case '6': *code = 0x07; return true;
    case '7': *code = 0x08; return true;
    case '8': *code = 0x09; return true;
    case '9': *code = 0x0A; return true;
    case '0': *code = 0x0B; return true;
    case '\r': *code = 0x1C; return true;
    case '\n': *code = 0x1C; return true;
    case '\b': *code = 0x0E; return true;
    case '\t': *code = 0x0F; return true;
    case ' ': *code = 0x39; return true;
    case '-': *code = 0x0C; return true;
    case '=': *code = 0x0D; return true;
    case '[': *code = 0x1A; return true;
    case ']': *code = 0x1B; return true;
    case ';': *code = 0x27; return true;
    case '\'': *code = 0x28; return true;
    case '`': *code = 0x29; return true;
    case '\\': *code = 0x2B; return true;
    case ',': *code = 0x33; return true;
    case '.': *code = 0x34; return true;
    case '/': *code = 0x35; return true;
    default:
        return false;
    }
}

static void kbd_send_scancode(uint8_t scancode) {
    kbd_feed_scancode(scancode);
    kbd_feed_scancode(scancode | 0x80);
}

void kbd_feed_ascii(char c) {
    uint8_t code;
    bool need_shift;
    if (c == '\r') {
        c = '\n';
    }
    if (!ascii_to_scancode(c, &code, &need_shift)) {
        if (c == '\n' || c == '\b' || c == '\t' || (c >= 32 && c <= 126)) {
            kbd_buffer_put(c);
        }
        return;
    }
    if (need_shift) {
        kbd_feed_scancode(0x2A);
    }
    kbd_send_scancode(code);
    if (need_shift) {
        kbd_feed_scancode(0xAA);
    }
}

void kbd_feed_scancode(uint8_t scancode) {
    if (scancode == 0xE0) {
        extended_prefix = true;
        return;
    }
    if (extended_prefix) {
        uint8_t code = scancode & 0x7FU;

        if ((scancode & 0x80U) == 0U) {
            (void)kbd_try_handle_extended_press(code);
        } else {
            uint32_t gui_keycode = kbd_gui_keycode_for_extended(code);
            if (gui_keycode != SYSCALL_GUI_KEY_NONE) {
                (void)kbd_gui_route_key(gui_keycode, 0U, 0);
            }
        }
        extended_prefix = false;
        return;
    }
    if (scancode & 0x80) {
        uint8_t code = scancode & 0x7F;
        if (code == 0x2A || code == 0x36) {
            shift_pressed = false;
        }
        {
            uint32_t gui_keycode = kbd_gui_keycode_for_scancode(code);
            if (gui_keycode != SYSCALL_GUI_KEY_NONE) {
                (void)kbd_gui_route_key(gui_keycode, 0U, 0);
            }
        }
        return;
    }
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
        return;
    }
    {
        uint32_t gui_keycode = kbd_gui_keycode_for_scancode(scancode);
        char ch = shift_pressed ? kbd_us_shift[scancode] : kbd_us[scancode];
        if (display_gui_mode_active() &&
            kbd_gui_route_key(gui_keycode, (uint32_t)(uint8_t)ch, 1)) {
            return;
        }
    }
    char ch = shift_pressed ? kbd_us_shift[scancode] : kbd_us[scancode];
    if (ch == 0) {
        return;
    }
    kbd_buffer_put(ch);
}

void keyboard_init(void) {
    /* Register keyboard IRQ handler (IRQ1) and unmask it */
    irq_register(1, keyboard_handler, NULL);
    irq_mask(1, 0);
}

void keyboard_set_verbose(bool enabled) {
    keyboard_verbose = enabled;
}

bool keyboard_is_verbose(void) {
    return keyboard_verbose;
}
