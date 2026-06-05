#ifndef GUILIB_H
#define GUILIB_H

#include <stdint.h>

#include "userlib.h"

#define GUI_FONT_W 6U
#define GUI_FONT_H 8U

struct gui_window {
    int32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t *pixels;
};

static inline uint32_t gui_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline void gui_memset32(uint32_t *dst, uint32_t value, uint32_t count) {
    for (uint32_t i = 0U; i < count; i++) {
        dst[i] = value;
    }
}

static inline char gui_upper_char(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - 'a' + 'A');
    }
    return ch;
}

static inline int gui_str_eq(const char *lhs, const char *rhs) {
    uint32_t idx = 0U;

    if (!lhs || !rhs) {
        return 0;
    }
    while (lhs[idx] != '\0' || rhs[idx] != '\0') {
        if (gui_upper_char(lhs[idx]) != gui_upper_char(rhs[idx])) {
            return 0;
        }
        idx++;
    }
    return 1;
}

static inline const uint8_t *gui_glyph_rows(char ch) {
    static const uint8_t kUnknown[7] = { 14, 17, 2, 4, 4, 0, 4 };
    static const uint8_t kSpace[7] = { 0, 0, 0, 0, 0, 0, 0 };
    static const uint8_t kDash[7] = { 0, 0, 0, 31, 0, 0, 0 };
    static const uint8_t kDot[7] = { 0, 0, 0, 0, 0, 12, 12 };
    static const uint8_t kColon[7] = { 0, 12, 12, 0, 12, 12, 0 };
    static const uint8_t kSlash[7] = { 1, 2, 4, 8, 16, 0, 0 };
    static const uint8_t kLBracket[7] = { 14, 8, 8, 8, 8, 8, 14 };
    static const uint8_t kRBracket[7] = { 14, 2, 2, 2, 2, 2, 14 };
    static const uint8_t kX[7] = { 17, 10, 4, 4, 4, 10, 17 };
    static const uint8_t kDigits[10][7] = {
        { 14, 17, 19, 21, 25, 17, 14 },
        { 4, 12, 4, 4, 4, 4, 14 },
        { 14, 17, 1, 2, 4, 8, 31 },
        { 30, 1, 1, 14, 1, 1, 30 },
        { 2, 6, 10, 18, 31, 2, 2 },
        { 31, 16, 16, 30, 1, 1, 30 },
        { 14, 16, 16, 30, 17, 17, 14 },
        { 31, 1, 2, 4, 8, 8, 8 },
        { 14, 17, 17, 14, 17, 17, 14 },
        { 14, 17, 17, 15, 1, 1, 14 },
    };
    static const uint8_t kLetters[26][7] = {
        { 14, 17, 17, 31, 17, 17, 17 },
        { 30, 17, 17, 30, 17, 17, 30 },
        { 14, 17, 16, 16, 16, 17, 14 },
        { 30, 17, 17, 17, 17, 17, 30 },
        { 31, 16, 16, 30, 16, 16, 31 },
        { 31, 16, 16, 30, 16, 16, 16 },
        { 14, 17, 16, 23, 17, 17, 14 },
        { 17, 17, 17, 31, 17, 17, 17 },
        { 14, 4, 4, 4, 4, 4, 14 },
        { 1, 1, 1, 1, 17, 17, 14 },
        { 17, 18, 20, 24, 20, 18, 17 },
        { 16, 16, 16, 16, 16, 16, 31 },
        { 17, 27, 21, 21, 17, 17, 17 },
        { 17, 25, 21, 19, 17, 17, 17 },
        { 14, 17, 17, 17, 17, 17, 14 },
        { 30, 17, 17, 30, 16, 16, 16 },
        { 14, 17, 17, 17, 21, 18, 13 },
        { 30, 17, 17, 30, 20, 18, 17 },
        { 15, 16, 16, 14, 1, 1, 30 },
        { 31, 4, 4, 4, 4, 4, 4 },
        { 17, 17, 17, 17, 17, 17, 14 },
        { 17, 17, 17, 17, 17, 10, 4 },
        { 17, 17, 17, 21, 21, 27, 17 },
        { 17, 17, 10, 4, 10, 17, 17 },
        { 17, 17, 10, 4, 4, 4, 4 },
        { 31, 1, 2, 4, 8, 16, 31 },
    };

    ch = gui_upper_char(ch);
    if (ch >= '0' && ch <= '9') {
        return kDigits[ch - '0'];
    }
    if (ch >= 'A' && ch <= 'Z') {
        return kLetters[ch - 'A'];
    }
    switch (ch) {
    case ' ':
        return kSpace;
    case '-':
        return kDash;
    case '.':
        return kDot;
    case ':':
        return kColon;
    case '/':
        return kSlash;
    case '[':
        return kLBracket;
    case ']':
        return kRBracket;
    case 'X':
        return kX;
    default:
        return kUnknown;
    }
}

static inline void gui_fill_rect(struct gui_window *window,
                                 uint32_t x,
                                 uint32_t y,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t color) {
    if (!window || !window->pixels || x >= window->width || y >= window->height) {
        return;
    }
    if (x + width > window->width) {
        width = window->width - x;
    }
    if (y + height > window->height) {
        height = window->height - y;
    }
    for (uint32_t row = 0U; row < height; row++) {
        gui_memset32(window->pixels + ((y + row) * window->stride) + x, color, width);
    }
}

static inline void gui_stroke_rect(struct gui_window *window,
                                   uint32_t x,
                                   uint32_t y,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t color) {
    if (width == 0U || height == 0U) {
        return;
    }
    gui_fill_rect(window, x, y, width, 1U, color);
    gui_fill_rect(window, x, y + height - 1U, width, 1U, color);
    gui_fill_rect(window, x, y, 1U, height, color);
    gui_fill_rect(window, x + width - 1U, y, 1U, height, color);
}

static inline void gui_draw_char(struct gui_window *window,
                                 uint32_t x,
                                 uint32_t y,
                                 char ch,
                                 uint32_t fg,
                                 uint32_t bg) {
    const uint8_t *rows = gui_glyph_rows(ch);

    gui_fill_rect(window, x, y, GUI_FONT_W - 1U, GUI_FONT_H - 1U, bg);
    for (uint32_t row = 0U; row < 7U; row++) {
        for (uint32_t col = 0U; col < 5U; col++) {
            if ((rows[row] & (uint8_t)(1U << (4U - col))) == 0U) {
                continue;
            }
            gui_fill_rect(window, x + col, y + row, 1U, 1U, fg);
        }
    }
}

static inline void gui_draw_text(struct gui_window *window,
                                 uint32_t x,
                                 uint32_t y,
                                 const char *text,
                                 uint32_t fg,
                                 uint32_t bg) {
    uint32_t cursor = x;

    if (!text) {
        return;
    }
    while (*text != '\0') {
        gui_draw_char(window, cursor, y, *text, fg, bg);
        cursor += GUI_FONT_W;
        text++;
    }
}

static inline int32_t gui_window_create(struct gui_window *window,
                                        const char *title,
                                        uint32_t width,
                                        uint32_t height,
                                        uint32_t *pixels) {
    struct syscall_gui_create_req req;
    int32_t rc;

    if (!window || !title || !pixels) {
        return -1;
    }
    req.width = width;
    req.height = height;
    req.title_ptr = (uint32_t)(uintptr_t)title;
    req.title_len = user_strlen(title);
    req.flags = 0U;

    rc = user_gui_create(&req);
    if (rc < 0) {
        return rc;
    }
    window->id = rc;
    window->width = width;
    window->height = height;
    window->stride = width;
    window->pixels = pixels;
    return rc;
}

static inline int32_t gui_present_rect(struct gui_window *window,
                                       uint32_t x,
                                       uint32_t y,
                                       uint32_t width,
                                       uint32_t height) {
    struct syscall_gui_flush_req req;

    if (!window || !window->pixels) {
        return -1;
    }
    req.window_id = window->id;
    req.pixels_ptr = (uint32_t)(uintptr_t)window->pixels;
    req.stride = window->stride;
    req.rect.x = (int32_t)x;
    req.rect.y = (int32_t)y;
    req.rect.w = width;
    req.rect.h = height;
    return user_gui_flush(&req);
}

static inline int32_t gui_present_full(struct gui_window *window) {
    return gui_present_rect(window, 0U, 0U, window->width, window->height);
}

#endif /* GUILIB_H */
