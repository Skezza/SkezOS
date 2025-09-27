#include "vga.h"
#include "utils.h"

/* The VGA text buffer lives at physical address 0xB8000.  Each cell
 * consists of two bytes: the ASCII character and the colour
 * attribute.  A simple row/column cursor is maintained.  */
static volatile uint16_t *const VGA = (uint16_t *)0xB8000;
static uint8_t row = 0;
static uint8_t col = 0;
static uint8_t colour = 0x0F; /* white on black */

void vga_clear(void) {
    for (int r = 0; r < 25; r++) {
        for (int c = 0; c < 80; c++) {
            VGA[r * 80 + c] = ((uint16_t)colour << 8) | ' ';
        }
    }
    row = 0;
    col = 0;
}

void vga_putc(char c) {
    if (c == '\n') {
        row++;
        col = 0;
        return;
    }
    VGA[row * 80 + col] = ((uint16_t)colour << 8) | (uint8_t)c;
    col++;
    if (col >= 80) {
        col = 0;
        row++;
    }
    if (row >= 25) {
        row = 0;
    }
}

void vga_puts(const char *str) {
    while (*str) {
        vga_putc(*str++);
    }
}
