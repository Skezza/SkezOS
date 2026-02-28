#include "vga.h"
#include "utils.h"

/* The VGA text buffer lives at physical address 0xB8000.  Each cell
 * consists of two bytes: the ASCII character and the colour
 * attribute.  A simple row/column cursor is maintained.  */
static volatile uint16_t *const VGA = (uint16_t *)0xB8000;
static const uint8_t VGA_ROWS = 25U;
static const uint8_t VGA_COLS = 80U;
static uint8_t row = 0;
static uint8_t col = 0;
static uint8_t colour = 0x0F; /* white on black */

static void vga_clear_row(uint8_t target_row) {
    for (uint8_t c = 0; c < VGA_COLS; c++) {
        VGA[target_row * VGA_COLS + c] = ((uint16_t)colour << 8) | ' ';
    }
}

static void vga_scroll_if_needed(void) {
    if (row < VGA_ROWS) {
        return;
    }

    for (uint8_t r = 1; r < VGA_ROWS; r++) {
        for (uint8_t c = 0; c < VGA_COLS; c++) {
            VGA[(r - 1U) * VGA_COLS + c] = VGA[r * VGA_COLS + c];
        }
    }
    vga_clear_row((uint8_t)(VGA_ROWS - 1U));
    row = (uint8_t)(VGA_ROWS - 1U);
}

void vga_clear(void) {
    for (uint8_t r = 0; r < VGA_ROWS; r++) {
        vga_clear_row(r);
    }
    row = 0;
    col = 0;
}

uint32_t vga_console_enter_critical(void) {
    uint32_t flags;

    __asm__ __volatile__(
        "pushf\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory");
    return flags;
}

void vga_console_leave_critical(uint32_t saved_flags) {
    if ((saved_flags & (1U << 9)) != 0U) {
        __asm__ __volatile__("sti" ::: "memory");
    } else {
        __asm__ __volatile__("" ::: "memory");
    }
}

void vga_putc(char c) {
    if (c == '\r') {
        col = 0;
        return;
    }
    if (c == '\n') {
        row++;
        col = 0;
        vga_scroll_if_needed();
        return;
    }
    VGA[row * VGA_COLS + col] = ((uint16_t)colour << 8) | (uint8_t)c;
    col++;
    if (col >= VGA_COLS) {
        col = 0;
        row++;
        vga_scroll_if_needed();
    }
}

void vga_puts(const char *str) {
    while (*str) {
        vga_putc(*str++);
    }
}
