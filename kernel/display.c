#include "display.h"

#include <stdint.h>

#include "klog.h"
#include "memory_layout.h"
#include "memmap.h"
#include "paging.h"
#include "pmm.h"
#include "sched.h"
#include "timer.h"
#include "vga.h"

typedef enum {
    DISPLAY_MODE_VGA = 0,
    DISPLAY_MODE_FRAMEBUFFER = 1,
} display_mode_t;

typedef enum {
    DISPLAY_LINE_STYLE_NORMAL = 0,
    DISPLAY_LINE_STYLE_PROMPT = 1,
    DISPLAY_LINE_STYLE_COMMAND = 2,
    DISPLAY_LINE_STYLE_USER = 3,
    DISPLAY_LINE_STYLE_TASK = 4,
} display_line_style_t;

typedef enum {
    DISPLAY_TIMELINE_EVENT_OK = 0,
    DISPLAY_TIMELINE_EVENT_FAIL = 1,
    DISPLAY_TIMELINE_EVENT_RUNNING = 2,
} display_timeline_event_t;

typedef enum {
    DISPLAY_PROMPT_HINT_INPUT = 0,
    DISPLAY_PROMPT_HINT_RUNNING = 1,
    DISPLAY_PROMPT_HINT_OK = 2,
    DISPLAY_PROMPT_HINT_FAIL = 3,
} display_prompt_hint_t;

typedef enum {
    DISPLAY_TRANSITION_CAUSE_NONE = 0,
    DISPLAY_TRANSITION_CAUSE_PROMPT = 1,
    DISPLAY_TRANSITION_CAUSE_WAIT = 2,
    DISPLAY_TRANSITION_CAUSE_LAUNCH_FAIL = 3,
    DISPLAY_TRANSITION_CAUSE_SHELL_EXIT = 4,
    DISPLAY_TRANSITION_CAUSE_ROLLOVER = 5,
    DISPLAY_TRANSITION_CAUSE_HOLD_EXPIRE = 6,
} display_transition_cause_t;

struct display_timeline_entry {
    uint16_t duration_ticks;
    uint8_t event;
    char tag;
};

struct display_glyph {
    char ch;
    uint8_t rows[5];
};

struct display_framebuffer_state {
    volatile uint8_t *base;
    uint32_t span_bytes;
    uint32_t bytes_per_pixel;
    uint32_t text_cols;
    uint32_t text_rows;
    uint32_t scroll_rows;
    uint32_t cursor_col;
    uint32_t cursor_row;
    uint32_t content_left_px;
    uint32_t content_width_px;
    uint32_t content_top_px;
    uint32_t content_bottom_px;
    struct boot_framebuffer_info info;
    char line_text[128];
    uint32_t line_len;
    display_line_style_t line_style;
    struct display_timeline_entry timeline[24];
    uint32_t timeline_count;
    uint32_t timeline_head;
    uint8_t command_active;
    uint32_t command_start_ticks;
    char command_tag;
    uint8_t prompt_hint;
    uint32_t prompt_hint_until_ticks;
    char prompt_hint_tag;
    uint8_t transition_cause;
    uint8_t ready;
};

static const struct display_glyph g_display_font[] = {
    { ' ', { 0, 0, 0, 0, 0 } },
    { '!', { 2, 2, 2, 0, 2 } },
    { '"', { 5, 5, 0, 0, 0 } },
    { '#', { 5, 7, 5, 7, 5 } },
    { '$', { 2, 7, 6, 3, 7 } },
    { '%', { 5, 1, 2, 4, 5 } },
    { '&', { 2, 5, 2, 5, 3 } },
    { '\'', { 2, 2, 0, 0, 0 } },
    { '(', { 1, 2, 2, 2, 1 } },
    { ')', { 4, 2, 2, 2, 4 } },
    { '*', { 0, 5, 2, 5, 0 } },
    { '+', { 0, 2, 7, 2, 0 } },
    { ',', { 0, 0, 0, 2, 4 } },
    { '-', { 0, 0, 7, 0, 0 } },
    { '.', { 0, 0, 0, 0, 2 } },
    { '/', { 1, 1, 2, 4, 4 } },
    { '0', { 7, 5, 5, 5, 7 } },
    { '1', { 2, 6, 2, 2, 7 } },
    { '2', { 7, 1, 7, 4, 7 } },
    { '3', { 7, 1, 7, 1, 7 } },
    { '4', { 5, 5, 7, 1, 1 } },
    { '5', { 7, 4, 7, 1, 7 } },
    { '6', { 7, 4, 7, 5, 7 } },
    { '7', { 7, 1, 1, 1, 1 } },
    { '8', { 7, 5, 7, 5, 7 } },
    { '9', { 7, 5, 7, 1, 7 } },
    { ':', { 0, 2, 0, 2, 0 } },
    { ';', { 0, 2, 0, 2, 4 } },
    { '<', { 1, 2, 4, 2, 1 } },
    { '=', { 0, 7, 0, 7, 0 } },
    { '>', { 4, 2, 1, 2, 4 } },
    { '?', { 6, 1, 2, 0, 2 } },
    { '@', { 2, 5, 7, 4, 3 } },
    { 'A', { 2, 5, 7, 5, 5 } },
    { 'B', { 6, 5, 6, 5, 6 } },
    { 'C', { 3, 4, 4, 4, 3 } },
    { 'D', { 6, 5, 5, 5, 6 } },
    { 'E', { 7, 4, 6, 4, 7 } },
    { 'F', { 7, 4, 6, 4, 4 } },
    { 'G', { 3, 4, 5, 5, 3 } },
    { 'H', { 5, 5, 7, 5, 5 } },
    { 'I', { 7, 2, 2, 2, 7 } },
    { 'J', { 1, 1, 1, 5, 2 } },
    { 'K', { 5, 5, 6, 5, 5 } },
    { 'L', { 4, 4, 4, 4, 7 } },
    { 'M', { 5, 7, 7, 5, 5 } },
    { 'N', { 5, 7, 7, 7, 5 } },
    { 'O', { 2, 5, 5, 5, 2 } },
    { 'P', { 6, 5, 6, 4, 4 } },
    { 'Q', { 2, 5, 5, 7, 3 } },
    { 'R', { 6, 5, 6, 5, 5 } },
    { 'S', { 3, 4, 2, 1, 6 } },
    { 'T', { 7, 2, 2, 2, 2 } },
    { 'U', { 5, 5, 5, 5, 7 } },
    { 'V', { 5, 5, 5, 5, 2 } },
    { 'W', { 5, 5, 7, 7, 5 } },
    { 'X', { 5, 5, 2, 5, 5 } },
    { 'Y', { 5, 5, 2, 2, 2 } },
    { 'Z', { 7, 1, 2, 4, 7 } },
    { '[', { 3, 2, 2, 2, 3 } },
    { '\\', { 4, 4, 2, 1, 1 } },
    { ']', { 6, 2, 2, 2, 6 } },
    { '^', { 2, 5, 0, 0, 0 } },
    { '_', { 0, 0, 0, 0, 7 } },
    { '`', { 4, 2, 0, 0, 0 } },
    { 'a', { 0, 3, 5, 5, 3 } },
    { 'b', { 4, 6, 5, 5, 6 } },
    { 'c', { 0, 3, 4, 4, 3 } },
    { 'd', { 1, 3, 5, 5, 3 } },
    { 'e', { 0, 3, 7, 4, 3 } },
    { 'f', { 1, 2, 7, 2, 2 } },
    { 'g', { 0, 3, 5, 3, 1 } },
    { 'h', { 4, 6, 5, 5, 5 } },
    { 'i', { 2, 0, 6, 2, 7 } },
    { 'j', { 1, 0, 1, 5, 2 } },
    { 'k', { 4, 5, 6, 5, 5 } },
    { 'l', { 6, 2, 2, 2, 7 } },
    { 'm', { 0, 6, 7, 5, 5 } },
    { 'n', { 0, 6, 5, 5, 5 } },
    { 'o', { 0, 2, 5, 5, 2 } },
    { 'p', { 0, 6, 5, 6, 4 } },
    { 'q', { 0, 3, 5, 3, 1 } },
    { 'r', { 0, 6, 5, 4, 4 } },
    { 's', { 0, 3, 6, 1, 6 } },
    { 't', { 2, 7, 2, 2, 1 } },
    { 'u', { 0, 5, 5, 5, 3 } },
    { 'v', { 0, 5, 5, 5, 2 } },
    { 'w', { 0, 5, 5, 7, 5 } },
    { 'x', { 0, 5, 2, 5, 5 } },
    { 'y', { 0, 5, 5, 3, 1 } },
    { 'z', { 0, 7, 1, 2, 7 } },
    { '{', { 1, 2, 6, 2, 1 } },
    { '|', { 2, 2, 2, 2, 2 } },
    { '}', { 4, 2, 3, 2, 4 } },
    { '~', { 0, 3, 6, 0, 0 } },
};

static struct display_framebuffer_state g_display_fb;
static display_mode_t g_display_mode = DISPLAY_MODE_VGA;

#define DISPLAY_FRAMEBUFFER_PAGE_FLAGS \
    (PAGING_PAGE_FLAG_WRITABLE | PAGING_PAGE_FLAG_WRITE_THROUGH | PAGING_PAGE_FLAG_CACHE_DISABLE)
#define DISPLAY_FB_CHAR_W 14U
#define DISPLAY_FB_CHAR_H 17U
#define DISPLAY_FB_FONT_SRC_W 3U
#define DISPLAY_FB_FONT_SRC_H 5U
#define DISPLAY_FB_GLYPH_W 4U
#define DISPLAY_FB_GLYPH_H 8U
#define DISPLAY_FB_GLYPH_SCALE_X 2U
#define DISPLAY_FB_GLYPH_SCALE_Y 2U
#define DISPLAY_FB_GLYPH_X_PAD 3U
#define DISPLAY_FB_GLYPH_Y_PAD 0U
#define DISPLAY_FB_HEADER_ROWS 1U
#define DISPLAY_FB_FOOTER_ROWS 1U
#define DISPLAY_FB_PANEL_MARGIN_X 16U
#define DISPLAY_FB_PANEL_MARGIN_Y 4U
#define DISPLAY_FB_PANEL_BORDER 1U
#define DISPLAY_FB_LINE_GUTTER_WIDTH 4U
#define DISPLAY_FB_LINE_GUTTER_GAP 4U
#define DISPLAY_FB_LINE_GUTTER_TOTAL \
    (DISPLAY_FB_LINE_GUTTER_WIDTH + DISPLAY_FB_LINE_GUTTER_GAP)
#define DISPLAY_FB_PROMPT_TEXT_OFFSET_COLS 7U
#define DISPLAY_FB_PROMPT_STATUS_RESERVE_COLS 0U
#define DISPLAY_FB_PROMPT_HINT_HOLD_TICKS 120U
#define DISPLAY_FB_TIMELINE_CAP 24U
#define DISPLAY_FB_TIMELINE_RAIL_INSET 4U
#define DISPLAY_FB_FOOTER_RAIL_LEFT_COLS 33U

static uint32_t display_framebuffer_pack_rgb(uint8_t r, uint8_t g, uint8_t b);
static void display_framebuffer_fill_rect_packed(uint32_t x, uint32_t y,
                                                 uint32_t width, uint32_t height,
                                                 uint32_t pixel);
static void display_framebuffer_draw_glyph_mask_packed(uint32_t x, uint32_t y,
                                                       char c,
                                                       uint32_t pixel);
static void display_framebuffer_draw_glyph_packed(uint32_t x, uint32_t y,
                                                  char c,
                                                  uint32_t fg_pixel,
                                                  uint32_t bg_pixel);
static void display_framebuffer_draw_text_emphasized_packed(uint32_t x, uint32_t y,
                                                            const char *text,
                                                            uint32_t fg_pixel,
                                                            uint32_t bg_pixel,
                                                            uint32_t shadow_pixel);
static void display_framebuffer_draw_text_right_emphasized_packed(uint32_t right_x,
                                                                  uint32_t y,
                                                                  const char *text,
                                                                  uint32_t fg_pixel,
                                                                  uint32_t bg_pixel,
                                                                  uint32_t shadow_pixel);
static void display_framebuffer_draw_text_packed(uint32_t x, uint32_t y,
                                                 const char *text,
                                                 uint32_t fg_pixel,
                                                 uint32_t bg_pixel);
static void display_append_text(char *dst, uint32_t *len, uint32_t cap, const char *text);
static void display_append_u32(char *dst, uint32_t *len, uint32_t cap, uint32_t value);
static void display_append_mib_value(char *dst, uint32_t *len, uint32_t cap, uint32_t frame_count);
static void display_framebuffer_build_header_metrics(char *metrics_text, uint32_t cap);
static void display_framebuffer_draw_header_metrics(void);
static void display_framebuffer_draw_footer_hud(void);
static uint32_t display_framebuffer_prompt_visible_cols(void);
static int display_font_has_glyph(char c);
static void display_verify_font_coverage(void);
static uint32_t display_hash_u32(uint32_t hash, uint32_t value);
static uint32_t display_compute_gui_state_hash(void);
static const char *display_transition_cause_label(display_transition_cause_t cause);
static void display_framebuffer_build_footer_legend(char *legend_text, uint32_t cap);
static int display_line_starts_with(const char *text, uint32_t len, const char *prefix);
static int display_line_contains(const char *text, uint32_t len, const char *needle);
static int display_parse_wait_exit_code(const char *text, uint32_t len, int32_t *out_exit_code);
static int display_parse_prompt_command(const char *text, uint32_t len, char *out_tag);
static void display_timeline_push_event(display_timeline_event_t event,
                                        uint32_t duration_ticks,
                                        char tag);
static void display_timeline_finish_active(int success, display_transition_cause_t cause);
static void display_timeline_start_command(char tag);
static void display_framebuffer_on_line_complete(const char *text,
                                                 uint32_t len,
                                                 display_line_style_t style);

static void display_framebuffer_line_colors(display_line_style_t style,
                                            uint32_t *fg_pixel,
                                            uint32_t *bg_pixel) {
    if (style == DISPLAY_LINE_STYLE_PROMPT) {
        *fg_pixel = display_framebuffer_pack_rgb(244U, 248U, 255U);
        *bg_pixel = display_framebuffer_pack_rgb(6U, 14U, 30U);
        return;
    }
    if (style == DISPLAY_LINE_STYLE_COMMAND) {
        *fg_pixel = display_framebuffer_pack_rgb(146U, 192U, 220U);
        *bg_pixel = display_framebuffer_pack_rgb(0U, 0U, 0U);
        return;
    }
    if (style == DISPLAY_LINE_STYLE_USER) {
        *fg_pixel = display_framebuffer_pack_rgb(182U, 232U, 255U);
        *bg_pixel = display_framebuffer_pack_rgb(0U, 0U, 0U);
        return;
    }
    if (style == DISPLAY_LINE_STYLE_TASK) {
        *fg_pixel = display_framebuffer_pack_rgb(255U, 220U, 160U);
        *bg_pixel = display_framebuffer_pack_rgb(0U, 0U, 0U);
        return;
    }

    *fg_pixel = display_framebuffer_pack_rgb(248U, 250U, 252U);
    *bg_pixel = display_framebuffer_pack_rgb(0U, 0U, 0U);
}

static uint32_t display_framebuffer_line_marker_color(display_line_style_t style) {
    if (style == DISPLAY_LINE_STYLE_PROMPT) {
        return display_framebuffer_pack_rgb(86U, 178U, 244U);
    }
    if (style == DISPLAY_LINE_STYLE_COMMAND) {
        return display_framebuffer_pack_rgb(68U, 122U, 166U);
    }
    if (style == DISPLAY_LINE_STYLE_USER) {
        return display_framebuffer_pack_rgb(102U, 212U, 255U);
    }
    if (style == DISPLAY_LINE_STYLE_TASK) {
        return display_framebuffer_pack_rgb(248U, 188U, 96U);
    }
    return display_framebuffer_pack_rgb(62U, 76U, 96U);
}

static display_line_style_t display_framebuffer_classify_line(const char *text,
                                                              uint32_t len,
                                                              display_line_style_t current_style) {
    if (current_style != DISPLAY_LINE_STYLE_NORMAL) {
        return current_style;
    }
    if (len >= 3U && text[0] == 's' && text[1] == 'h' && text[2] == '>') {
        return DISPLAY_LINE_STYLE_PROMPT;
    }
    if (len >= 5U &&
        text[0] == 'u' && text[1] == 's' && text[2] == 'e' && text[3] == 'r' && text[4] == ':') {
        return DISPLAY_LINE_STYLE_USER;
    }
    if (len >= 4U && text[0] == 'e' && text[1] == 'l' && text[2] == 'f' && text[3] == '-') {
        return DISPLAY_LINE_STYLE_TASK;
    }
    return DISPLAY_LINE_STYLE_NORMAL;
}

static void display_framebuffer_reset_line_tracking(display_line_style_t next_style) {
    g_display_fb.line_len = 0U;
    g_display_fb.line_style = next_style;
}

static void display_framebuffer_draw_line_gutter(uint32_t row,
                                                 display_line_style_t style) {
    uint32_t gutter_left = g_display_fb.content_left_px - DISPLAY_FB_LINE_GUTTER_TOTAL;
    uint32_t cell_y = g_display_fb.content_top_px + (row * DISPLAY_FB_CHAR_H);
    uint32_t track_bg = display_framebuffer_pack_rgb(10U, 17U, 29U);
    uint32_t marker = display_framebuffer_line_marker_color(style);

    display_framebuffer_fill_rect_packed(
        gutter_left,
        cell_y,
        DISPLAY_FB_LINE_GUTTER_TOTAL,
        DISPLAY_FB_CHAR_H,
        track_bg);
    display_framebuffer_fill_rect_packed(
        gutter_left,
        cell_y,
        DISPLAY_FB_LINE_GUTTER_WIDTH,
        DISPLAY_FB_CHAR_H,
        marker);
}

static uint32_t display_framebuffer_prompt_visible_cols(void) {
    if (g_display_fb.text_cols <=
        DISPLAY_FB_PROMPT_TEXT_OFFSET_COLS + DISPLAY_FB_PROMPT_STATUS_RESERVE_COLS) {
        return 1U;
    }
    return g_display_fb.text_cols -
           DISPLAY_FB_PROMPT_TEXT_OFFSET_COLS -
           DISPLAY_FB_PROMPT_STATUS_RESERVE_COLS;
}

static void display_framebuffer_clear_scroll_row(uint32_t row) {
    display_framebuffer_draw_line_gutter(row, DISPLAY_LINE_STYLE_NORMAL);
    display_framebuffer_fill_rect_packed(
        g_display_fb.content_left_px,
        g_display_fb.content_top_px + (row * DISPLAY_FB_CHAR_H),
        g_display_fb.content_width_px,
        DISPLAY_FB_CHAR_H,
        display_framebuffer_pack_rgb(0U, 0U, 0U));
}

static uint32_t display_ticks32(void) {
    return (uint32_t)(timer_ticks_snapshot() & 0xFFFFFFFFULL);
}

static void display_prompt_hint_set(display_prompt_hint_t hint, char tag, uint32_t hold_ticks) {
    g_display_fb.prompt_hint = (uint8_t)hint;
    g_display_fb.prompt_hint_tag = tag;
    if (hold_ticks == 0U) {
        g_display_fb.prompt_hint_until_ticks = 0U;
        return;
    }
    g_display_fb.prompt_hint_until_ticks = display_ticks32() + hold_ticks;
}

static void display_prompt_hint_refresh(void) {
    uint32_t now_ticks;

    if (g_display_fb.command_active != 0U) {
        return;
    }
    if (g_display_fb.prompt_hint != DISPLAY_PROMPT_HINT_OK &&
        g_display_fb.prompt_hint != DISPLAY_PROMPT_HINT_FAIL) {
        return;
    }
    if (g_display_fb.prompt_hint_until_ticks == 0U) {
        display_prompt_hint_set(DISPLAY_PROMPT_HINT_INPUT, '?', 0U);
        g_display_fb.transition_cause = DISPLAY_TRANSITION_CAUSE_HOLD_EXPIRE;
        return;
    }
    now_ticks = display_ticks32();
    if ((int32_t)(now_ticks - g_display_fb.prompt_hint_until_ticks) >= 0) {
        display_prompt_hint_set(DISPLAY_PROMPT_HINT_INPUT, '?', 0U);
        g_display_fb.transition_cause = DISPLAY_TRANSITION_CAUSE_HOLD_EXPIRE;
    }
}

static void display_framebuffer_draw_prompt_strip_idle(void) {
    uint32_t prompt_top = g_display_fb.content_top_px + (g_display_fb.scroll_rows * DISPLAY_FB_CHAR_H);
    uint32_t prompt_fg = display_framebuffer_pack_rgb(248U, 242U, 214U);
    uint32_t prompt_bg = display_framebuffer_pack_rgb(12U, 22U, 42U);
    uint32_t prompt_well_bg = display_framebuffer_pack_rgb(6U, 14U, 30U);
    uint32_t prompt_accent = display_framebuffer_pack_rgb(86U, 178U, 244U);
    char status_label[8] = { 'I', 'N', 'P', 'U', 'T', '\0', '\0', '\0' };
    char tag = g_display_fb.prompt_hint_tag;
    uint32_t prompt_text_left =
        g_display_fb.content_left_px + (DISPLAY_FB_PROMPT_TEXT_OFFSET_COLS * DISPLAY_FB_CHAR_W);
    uint32_t prompt_text_width = display_framebuffer_prompt_visible_cols() * DISPLAY_FB_CHAR_W;
    uint32_t separator_x = prompt_text_left - (DISPLAY_FB_CHAR_W / 2U);

    display_prompt_hint_refresh();
    if (tag >= 'a' && tag <= 'z') {
        tag = (char)(tag - ('a' - 'A'));
    }
    switch ((display_prompt_hint_t)g_display_fb.prompt_hint) {
    case DISPLAY_PROMPT_HINT_RUNNING:
        prompt_fg = display_framebuffer_pack_rgb(232U, 243U, 255U);
        prompt_well_bg = display_framebuffer_pack_rgb(8U, 18U, 36U);
        prompt_accent = display_framebuffer_pack_rgb(64U, 146U, 220U);
        status_label[0] = 'R';
        status_label[1] = 'U';
        status_label[2] = 'N';
        status_label[3] = '\0';
        if ((tag >= 'A' && tag <= 'Z') || (tag >= '0' && tag <= '9')) {
            status_label[3] = ' ';
            status_label[4] = tag;
            status_label[5] = '\0';
        }
        break;
    case DISPLAY_PROMPT_HINT_OK:
        prompt_fg = display_framebuffer_pack_rgb(228U, 252U, 239U);
        prompt_well_bg = display_framebuffer_pack_rgb(8U, 24U, 21U);
        prompt_accent = display_framebuffer_pack_rgb(74U, 204U, 148U);
        status_label[0] = 'O';
        status_label[1] = 'K';
        status_label[2] = '\0';
        if ((tag >= 'A' && tag <= 'Z') || (tag >= '0' && tag <= '9')) {
            status_label[2] = ' ';
            status_label[3] = tag;
            status_label[4] = '\0';
        }
        break;
    case DISPLAY_PROMPT_HINT_FAIL:
        prompt_fg = display_framebuffer_pack_rgb(255U, 226U, 221U);
        prompt_well_bg = display_framebuffer_pack_rgb(34U, 10U, 13U);
        prompt_accent = display_framebuffer_pack_rgb(228U, 96U, 86U);
        status_label[0] = 'E';
        status_label[1] = 'R';
        status_label[2] = 'R';
        status_label[3] = '\0';
        if ((tag >= 'A' && tag <= 'Z') || (tag >= '0' && tag <= '9')) {
            status_label[3] = ' ';
            status_label[4] = tag;
            status_label[5] = '\0';
        }
        break;
    case DISPLAY_PROMPT_HINT_INPUT:
    default:
        break;
    }

    display_framebuffer_draw_header_metrics();

    display_framebuffer_draw_line_gutter(g_display_fb.scroll_rows, DISPLAY_LINE_STYLE_PROMPT);
    display_framebuffer_fill_rect_packed(
        g_display_fb.content_left_px,
        prompt_top,
        g_display_fb.content_width_px,
        DISPLAY_FB_CHAR_H,
        prompt_bg);
    display_framebuffer_fill_rect_packed(
        g_display_fb.content_left_px,
        prompt_top,
        g_display_fb.content_width_px,
        1U,
        prompt_accent);
    display_framebuffer_fill_rect_packed(
        prompt_text_left,
        prompt_top,
        prompt_text_width,
        DISPLAY_FB_CHAR_H,
        prompt_well_bg);
    display_framebuffer_fill_rect_packed(
        separator_x,
        prompt_top + 2U,
        2U,
        DISPLAY_FB_CHAR_H - 4U,
        prompt_accent);
    display_framebuffer_draw_text_packed(
        g_display_fb.content_left_px + DISPLAY_FB_CHAR_W,
        prompt_top + 2U,
        status_label,
        prompt_fg,
        prompt_bg);
}

static void display_framebuffer_redraw_current_line(void) {
    uint32_t fg_pixel;
    uint32_t bg_pixel;
    uint32_t cell_y;
    uint32_t cell_x = g_display_fb.content_left_px;
    uint32_t render_row = g_display_fb.cursor_row;
    uint32_t render_len = g_display_fb.line_len;
    uint32_t start_idx = 0U;
    uint32_t clipped_prompt = 0U;

    display_framebuffer_line_colors(g_display_fb.line_style, &fg_pixel, &bg_pixel);
    if (g_display_fb.line_style == DISPLAY_LINE_STYLE_PROMPT) {
        uint32_t visible_cols;

        if (g_display_fb.cursor_row < g_display_fb.scroll_rows) {
            display_framebuffer_clear_scroll_row(g_display_fb.cursor_row);
        }
        render_row = g_display_fb.scroll_rows;
        cell_x = g_display_fb.content_left_px +
                 (DISPLAY_FB_PROMPT_TEXT_OFFSET_COLS * DISPLAY_FB_CHAR_W);
        visible_cols = display_framebuffer_prompt_visible_cols();
        render_len = visible_cols;
        if (g_display_fb.line_len > render_len) {
            clipped_prompt = 1U;
            if (visible_cols > 1U) {
                render_len = visible_cols - 1U;
                start_idx = g_display_fb.line_len - render_len;
            } else {
                render_len = 0U;
                start_idx = g_display_fb.line_len;
            }
        } else {
            render_len = g_display_fb.line_len;
        }
        display_framebuffer_draw_prompt_strip_idle();
    } else {
        if (render_len > g_display_fb.text_cols) {
            render_len = g_display_fb.text_cols;
        }
    }
    cell_y = g_display_fb.content_top_px + (render_row * DISPLAY_FB_CHAR_H);
    display_framebuffer_draw_line_gutter(render_row, g_display_fb.line_style);
    if (g_display_fb.line_style == DISPLAY_LINE_STYLE_PROMPT) {
        display_framebuffer_fill_rect_packed(
            cell_x,
            cell_y,
            display_framebuffer_prompt_visible_cols() * DISPLAY_FB_CHAR_W,
            DISPLAY_FB_CHAR_H,
            bg_pixel);
    } else {
        display_framebuffer_fill_rect_packed(
            g_display_fb.content_left_px,
            cell_y,
            g_display_fb.content_width_px,
            DISPLAY_FB_CHAR_H,
            bg_pixel);
    }

    if (clipped_prompt != 0U) {
        display_framebuffer_draw_glyph_packed(cell_x, cell_y, '<', fg_pixel, bg_pixel);
        cell_x += DISPLAY_FB_CHAR_W;
    }
    for (uint32_t i = 0; i < render_len; i++) {
        display_framebuffer_draw_glyph_packed(
            cell_x, cell_y, g_display_fb.line_text[start_idx + i], fg_pixel, bg_pixel);
        cell_x += DISPLAY_FB_CHAR_W;
    }
}

static const uint8_t *display_lookup_glyph(char c) {
    for (uint32_t i = 0; i < sizeof(g_display_font) / sizeof(g_display_font[0]); i++) {
        if (g_display_font[i].ch == c) {
            return g_display_font[i].rows;
        }
    }
    for (uint32_t i = 0; i < sizeof(g_display_font) / sizeof(g_display_font[0]); i++) {
        if (g_display_font[i].ch == '?') {
            return g_display_font[i].rows;
        }
    }
    return g_display_font[0].rows;
}

static int display_font_has_glyph(char c) {
    for (uint32_t i = 0; i < sizeof(g_display_font) / sizeof(g_display_font[0]); i++) {
        if (g_display_font[i].ch == c) {
            return 1;
        }
    }
    return 0;
}

static void display_verify_font_coverage(void) {
    uint32_t missing = 0U;
    char first_missing = '?';

    for (char c = ' '; c <= '~'; c++) {
        if (!display_font_has_glyph(c)) {
            if (missing == 0U) {
                first_missing = c;
            }
            missing++;
        }
    }

    if (missing == 0U) {
        KLOGI("display: bootstrap font coverage printable_ascii=95/95");
    } else {
        KLOGW("display: bootstrap font coverage missing=%u first='%c'", missing, first_missing);
    }
}

static uint32_t display_hash_u32(uint32_t hash, uint32_t value) {
    for (uint32_t i = 0U; i < 4U; i++) {
        hash ^= (value >> (i * 8U)) & 0xFFU;
        hash *= 16777619U;
    }
    return hash;
}

static uint32_t display_compute_gui_state_hash(void) {
    uint32_t hash = 2166136261U;

    hash = display_hash_u32(hash, 0x46425334U); /* "FBS4" */
    hash = display_hash_u32(hash, g_display_fb.info.width);
    hash = display_hash_u32(hash, g_display_fb.info.height);
    hash = display_hash_u32(hash, g_display_fb.info.pitch);
    hash = display_hash_u32(hash, (uint32_t)g_display_fb.info.bpp);
    hash = display_hash_u32(hash, g_display_fb.bytes_per_pixel);
    hash = display_hash_u32(hash, g_display_fb.text_cols);
    hash = display_hash_u32(hash, g_display_fb.text_rows);
    hash = display_hash_u32(hash, g_display_fb.scroll_rows);
    hash = display_hash_u32(hash, g_display_fb.content_left_px);
    hash = display_hash_u32(hash, g_display_fb.content_width_px);
    hash = display_hash_u32(hash, g_display_fb.content_top_px);
    hash = display_hash_u32(hash, g_display_fb.content_bottom_px);
    hash = display_hash_u32(hash, DISPLAY_FB_CHAR_W);
    hash = display_hash_u32(hash, DISPLAY_FB_CHAR_H);
    hash = display_hash_u32(hash, DISPLAY_FB_PANEL_MARGIN_X);
    hash = display_hash_u32(hash, DISPLAY_FB_PANEL_MARGIN_Y);
    hash = display_hash_u32(hash, DISPLAY_FB_PANEL_BORDER);
    hash = display_hash_u32(hash, DISPLAY_FB_LINE_GUTTER_TOTAL);
    hash = display_hash_u32(hash, DISPLAY_FB_HEADER_ROWS);
    hash = display_hash_u32(hash, DISPLAY_FB_FOOTER_ROWS);
    hash = display_hash_u32(hash, DISPLAY_FB_PROMPT_HINT_HOLD_TICKS);
    hash = display_hash_u32(hash, DISPLAY_FB_TIMELINE_CAP);
    hash = display_hash_u32(hash, DISPLAY_FB_TIMELINE_RAIL_INSET);
    hash = display_hash_u32(hash, DISPLAY_FB_FOOTER_RAIL_LEFT_COLS);

    /* Include style palette IDs so visual tune-ups are regression-gated. */
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(13U, 24U, 42U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(224U, 233U, 246U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(8U, 15U, 29U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(74U, 102U, 136U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(74U, 204U, 148U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(228U, 96U, 86U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(64U, 146U, 220U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(86U, 178U, 244U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(68U, 122U, 166U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(102U, 212U, 255U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(248U, 188U, 96U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(62U, 76U, 96U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(7U, 11U, 19U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(18U, 40U, 86U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(248U, 182U, 86U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(44U, 60U, 84U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(8U, 18U, 36U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(8U, 24U, 21U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(34U, 10U, 13U));

    return hash;
}

static const char *display_transition_cause_label(display_transition_cause_t cause) {
    switch (cause) {
    case DISPLAY_TRANSITION_CAUSE_PROMPT:
        return "PROM";
    case DISPLAY_TRANSITION_CAUSE_WAIT:
        return "WAIT";
    case DISPLAY_TRANSITION_CAUSE_LAUNCH_FAIL:
        return "FAIL";
    case DISPLAY_TRANSITION_CAUSE_SHELL_EXIT:
        return "EXIT";
    case DISPLAY_TRANSITION_CAUSE_ROLLOVER:
        return "ROLL";
    case DISPLAY_TRANSITION_CAUSE_HOLD_EXPIRE:
        return "HOLD";
    case DISPLAY_TRANSITION_CAUSE_NONE:
    default:
        return "BOOT";
    }
}

static void display_framebuffer_build_footer_legend(char *legend_text, uint32_t cap) {
    uint32_t len = 0U;
    const char *cause =
        display_transition_cause_label((display_transition_cause_t)g_display_fb.transition_cause);

    if (cap == 0U) {
        return;
    }

    legend_text[0] = '\0';
    display_append_text(legend_text, &len, cap, "HUD I:IN R:RUN O:OK E:ERR C:");
    display_append_text(legend_text, &len, cap, cause);
}

static uint32_t display_string_length(const char *text) {
    uint32_t len = 0U;

    while (text[len] != '\0') {
        len++;
    }
    return len;
}

static int display_line_starts_with(const char *text, uint32_t len, const char *prefix) {
    uint32_t i = 0U;

    while (prefix[i] != '\0') {
        if (i >= len || text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int display_line_contains(const char *text, uint32_t len, const char *needle) {
    uint32_t nlen = display_string_length(needle);

    if (nlen == 0U || len < nlen) {
        return 0;
    }
    for (uint32_t i = 0U; i + nlen <= len; i++) {
        uint32_t j = 0U;
        while (j < nlen && text[i + j] == needle[j]) {
            j++;
        }
        if (j == nlen) {
            return 1;
        }
    }
    return 0;
}

static int display_parse_wait_exit_code(const char *text, uint32_t len, int32_t *out_exit_code) {
    uint32_t i = 0U;

    if (!display_line_contains(text, len, "[INFO] sys_waitpid:")) {
        return 0;
    }
    while (i + 5U <= len) {
        if (text[i] == 'e' && text[i + 1U] == 'x' && text[i + 2U] == 'i' &&
            text[i + 3U] == 't' && text[i + 4U] == '=') {
            uint32_t pos = i + 5U;
            int sign = 1;
            int32_t value = 0;
            int saw_digit = 0;

            if (pos < len && text[pos] == '-') {
                sign = -1;
                pos++;
            }
            while (pos < len && text[pos] >= '0' && text[pos] <= '9') {
                value = (value * 10) + (int32_t)(text[pos] - '0');
                saw_digit = 1;
                pos++;
            }
            if (!saw_digit) {
                return 0;
            }
            if (out_exit_code != 0) {
                *out_exit_code = value * sign;
            }
            return 1;
        }
        i++;
    }
    return 0;
}

static int display_parse_prompt_command(const char *text, uint32_t len, char *out_tag) {
    uint32_t cmd_start = len;
    uint32_t token_end;
    char tag = '?';

    for (uint32_t i = 0U; i + 1U < len; i++) {
        if ((text[i] == '$' || text[i] == '#') && text[i + 1U] == ' ') {
            cmd_start = i + 2U;
        }
    }
    if (cmd_start >= len) {
        return 0;
    }
    while (cmd_start < len && text[cmd_start] == ' ') {
        cmd_start++;
    }
    if (cmd_start >= len) {
        return 0;
    }

    token_end = cmd_start;
    while (token_end < len && text[token_end] != ' ') {
        token_end++;
    }
    for (uint32_t i = cmd_start; i < token_end; i++) {
        char c = text[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            tag = c;
            break;
        }
    }
    if (tag >= 'a' && tag <= 'z') {
        tag = (char)(tag - ('a' - 'A'));
    }
    if (out_tag != 0) {
        *out_tag = tag;
    }
    return 1;
}

static void display_timeline_push_event(display_timeline_event_t event,
                                        uint32_t duration_ticks,
                                        char tag) {
    struct display_timeline_entry *entry = &g_display_fb.timeline[g_display_fb.timeline_head];

    if (duration_ticks > 0xFFFFU) {
        duration_ticks = 0xFFFFU;
    }
    entry->event = (uint8_t)event;
    entry->duration_ticks = (uint16_t)duration_ticks;
    entry->tag = tag;

    g_display_fb.timeline_head = (g_display_fb.timeline_head + 1U) % DISPLAY_FB_TIMELINE_CAP;
    if (g_display_fb.timeline_count < DISPLAY_FB_TIMELINE_CAP) {
        g_display_fb.timeline_count++;
    }
}

static void display_timeline_finish_active(int success, display_transition_cause_t cause) {
    uint32_t now_ticks;
    uint32_t duration_ticks;
    char tag;

    if (g_display_fb.command_active == 0U) {
        return;
    }
    tag = g_display_fb.command_tag;
    now_ticks = (uint32_t)(timer_ticks_snapshot() & 0xFFFFFFFFULL);
    duration_ticks = now_ticks - g_display_fb.command_start_ticks;
    if (duration_ticks == 0U) {
        duration_ticks = 1U;
    }
    display_timeline_push_event(success ? DISPLAY_TIMELINE_EVENT_OK : DISPLAY_TIMELINE_EVENT_FAIL,
                                duration_ticks,
                                tag);
    g_display_fb.command_active = 0U;
    g_display_fb.command_tag = '?';
    g_display_fb.transition_cause = (uint8_t)cause;
    display_prompt_hint_set(success ? DISPLAY_PROMPT_HINT_OK : DISPLAY_PROMPT_HINT_FAIL,
                            tag,
                            DISPLAY_FB_PROMPT_HINT_HOLD_TICKS);
}

static void display_timeline_start_command(char tag) {
    if (g_display_fb.command_active != 0U) {
        display_timeline_finish_active(1, DISPLAY_TRANSITION_CAUSE_ROLLOVER);
    }
    g_display_fb.command_active = 1U;
    g_display_fb.command_start_ticks = (uint32_t)(timer_ticks_snapshot() & 0xFFFFFFFFULL);
    g_display_fb.command_tag = tag;
    display_prompt_hint_set(DISPLAY_PROMPT_HINT_RUNNING, tag, 0U);
    g_display_fb.transition_cause = DISPLAY_TRANSITION_CAUSE_PROMPT;
}

static void display_framebuffer_on_line_complete(const char *text,
                                                 uint32_t len,
                                                 display_line_style_t style) {
    int32_t exit_code = 0;
    char tag = '?';
    int footer_dirty = 0;
    int prompt_dirty = 0;

    if (len == 0U || text == 0) {
        return;
    }
    if (style == DISPLAY_LINE_STYLE_PROMPT) {
        if (display_parse_prompt_command(text, len, &tag)) {
            display_timeline_start_command(tag);
            footer_dirty = 1;
            prompt_dirty = 1;
        }
    } else if (g_display_fb.command_active != 0U) {
        if (display_line_contains(text, len, "run: launch failed") ||
            display_line_contains(text, len, "run: redirect open failed") ||
            display_line_contains(text, len, "spawn failed")) {
            display_timeline_finish_active(0, DISPLAY_TRANSITION_CAUSE_LAUNCH_FAIL);
            footer_dirty = 1;
            prompt_dirty = 1;
        } else if (display_parse_wait_exit_code(text, len, &exit_code)) {
            display_timeline_finish_active(exit_code == 0, DISPLAY_TRANSITION_CAUSE_WAIT);
            footer_dirty = 1;
            prompt_dirty = 1;
        } else if (display_line_starts_with(text, len, "sh: exit")) {
            display_timeline_finish_active(1, DISPLAY_TRANSITION_CAUSE_SHELL_EXIT);
            footer_dirty = 1;
            prompt_dirty = 1;
        }
    }

    if (prompt_dirty != 0 && g_display_fb.ready != 0U) {
        display_framebuffer_draw_prompt_strip_idle();
    }
    if (footer_dirty != 0 && g_display_fb.ready != 0U) {
        display_framebuffer_draw_footer_hud();
    }
}

static void display_append_text(char *dst, uint32_t *len, uint32_t cap, const char *text) {
    while (*text != '\0' && *len + 1U < cap) {
        dst[*len] = *text;
        (*len)++;
        text++;
    }
    if (cap != 0U) {
        dst[*len < cap ? *len : (cap - 1U)] = '\0';
    }
}

static void display_append_u32(char *dst, uint32_t *len, uint32_t cap, uint32_t value) {
    char digits[10];
    uint32_t count = 0U;

    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);

    while (count > 0U && *len + 1U < cap) {
        dst[*len] = digits[--count];
        (*len)++;
    }
    if (cap != 0U) {
        dst[*len < cap ? *len : (cap - 1U)] = '\0';
    }
}

static void display_append_mib_value(char *dst, uint32_t *len, uint32_t cap, uint32_t frame_count) {
    uint32_t mib = frame_count / 256U;

    if (mib == 0U && frame_count != 0U) {
        mib = 1U;
    }
    display_append_u32(dst, len, cap, mib);
    display_append_text(dst, len, cap, "M");
}

static uint32_t display_scale_channel(uint8_t value, uint8_t mask_size) {
    if (mask_size == 0U) {
        return 0U;
    }
    if (mask_size >= 8U) {
        return (uint32_t)value << (mask_size - 8U);
    }
    return ((uint32_t)value * ((1U << mask_size) - 1U) + 127U) / 255U;
}

static int display_framebuffer_bootstrap_font_pixel_on(const uint8_t *rows,
                                                       uint32_t row,
                                                       uint32_t col) {
    uint32_t src_row =
        (((row * 2U) + 1U) * DISPLAY_FB_FONT_SRC_H) / (DISPLAY_FB_GLYPH_H * 2U);
    uint32_t src_col =
        (((col * 2U) + 1U) * DISPLAY_FB_FONT_SRC_W) / (DISPLAY_FB_GLYPH_W * 2U);

    if (src_row >= DISPLAY_FB_FONT_SRC_H) {
        src_row = DISPLAY_FB_FONT_SRC_H - 1U;
    }
    if (src_col >= DISPLAY_FB_FONT_SRC_W) {
        src_col = DISPLAY_FB_FONT_SRC_W - 1U;
    }

    return (rows[src_row] & (1U << (DISPLAY_FB_FONT_SRC_W - 1U - src_col))) != 0U;
}

static uint32_t display_framebuffer_pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t pixel = 0U;

    pixel |= display_scale_channel(r, g_display_fb.info.red_mask_size)
             << g_display_fb.info.red_field_position;
    pixel |= display_scale_channel(g, g_display_fb.info.green_mask_size)
             << g_display_fb.info.green_field_position;
    pixel |= display_scale_channel(b, g_display_fb.info.blue_mask_size)
             << g_display_fb.info.blue_field_position;
    return pixel;
}

static void display_framebuffer_write_pixel(uint32_t x, uint32_t y, uint32_t pixel) {
    volatile uint8_t *dst;

    if (x >= g_display_fb.info.width || y >= g_display_fb.info.height) {
        return;
    }

    dst = g_display_fb.base +
          (y * g_display_fb.info.pitch) +
          (x * g_display_fb.bytes_per_pixel);
    for (uint32_t i = 0; i < g_display_fb.bytes_per_pixel; i++) {
        dst[i] = (uint8_t)(pixel >> (i * 8U));
    }
}

static void display_framebuffer_fill_rect_packed(uint32_t x, uint32_t y,
                                                 uint32_t width, uint32_t height,
                                                 uint32_t pixel) {
    if (x >= g_display_fb.info.width || y >= g_display_fb.info.height) {
        return;
    }
    if (x + width > g_display_fb.info.width) {
        width = g_display_fb.info.width - x;
    }
    if (y + height > g_display_fb.info.height) {
        height = g_display_fb.info.height - y;
    }

    for (uint32_t py = 0; py < height; py++) {
        for (uint32_t px = 0; px < width; px++) {
            display_framebuffer_write_pixel(x + px, y + py, pixel);
        }
    }
}

static void display_framebuffer_draw_glyph_mask_packed(uint32_t x, uint32_t y,
                                                       char c,
                                                       uint32_t pixel) {
    const uint8_t *rows = display_lookup_glyph(c);

    for (uint32_t row = 0; row < DISPLAY_FB_GLYPH_H; row++) {
        for (uint32_t col = 0; col < DISPLAY_FB_GLYPH_W; col++) {
            if (!display_framebuffer_bootstrap_font_pixel_on(rows, row, col)) {
                continue;
            }
            display_framebuffer_fill_rect_packed(
                x + (col * DISPLAY_FB_GLYPH_SCALE_X),
                y + (row * DISPLAY_FB_GLYPH_SCALE_Y),
                DISPLAY_FB_GLYPH_SCALE_X,
                DISPLAY_FB_GLYPH_SCALE_Y,
                pixel);
        }
    }
}

static void display_framebuffer_draw_glyph_packed(uint32_t x, uint32_t y,
                                                  char c,
                                                  uint32_t fg_pixel,
                                                  uint32_t bg_pixel) {
    display_framebuffer_fill_rect_packed(x, y, DISPLAY_FB_CHAR_W, DISPLAY_FB_CHAR_H, bg_pixel);
    display_framebuffer_draw_glyph_mask_packed(
        x + DISPLAY_FB_GLYPH_X_PAD,
        y + DISPLAY_FB_GLYPH_Y_PAD,
        c,
        fg_pixel);
}

static void display_framebuffer_draw_text_emphasized_packed(uint32_t x, uint32_t y,
                                                            const char *text,
                                                            uint32_t fg_pixel,
                                                            uint32_t bg_pixel,
                                                            uint32_t shadow_pixel) {
    uint32_t cursor_x = x;

    while (*text != '\0') {
        display_framebuffer_fill_rect_packed(cursor_x, y, DISPLAY_FB_CHAR_W, DISPLAY_FB_CHAR_H, bg_pixel);
        display_framebuffer_draw_glyph_mask_packed(
            cursor_x + DISPLAY_FB_GLYPH_X_PAD + 1U,
            y + DISPLAY_FB_GLYPH_Y_PAD + 1U,
            *text,
            shadow_pixel);
        display_framebuffer_draw_glyph_mask_packed(
            cursor_x + DISPLAY_FB_GLYPH_X_PAD,
            y + DISPLAY_FB_GLYPH_Y_PAD,
            *text,
            fg_pixel);
        cursor_x += DISPLAY_FB_CHAR_W;
        text++;
    }
}

static void display_framebuffer_draw_text_right_emphasized_packed(uint32_t right_x,
                                                                  uint32_t y,
                                                                  const char *text,
                                                                  uint32_t fg_pixel,
                                                                  uint32_t bg_pixel,
                                                                  uint32_t shadow_pixel) {
    uint32_t text_width = display_string_length(text) * DISPLAY_FB_CHAR_W;
    uint32_t draw_x = 0U;

    if (right_x > text_width) {
        draw_x = right_x - text_width;
    }
    display_framebuffer_draw_text_emphasized_packed(
        draw_x,
        y,
        text,
        fg_pixel,
        bg_pixel,
        shadow_pixel);
}

static void display_framebuffer_draw_text_packed(uint32_t x, uint32_t y,
                                                 const char *text,
                                                 uint32_t fg_pixel,
                                                 uint32_t bg_pixel) {
    uint32_t cursor_x = x;

    while (*text != '\0') {
        display_framebuffer_draw_glyph_packed(cursor_x, y, *text, fg_pixel, bg_pixel);
        cursor_x += DISPLAY_FB_CHAR_W;
        text++;
    }
}


static void display_framebuffer_build_header_metrics(char *metrics_text, uint32_t cap) {
    struct pmm_stats pmm_stats;
    uint32_t len = 0U;
    uint32_t hz = timer_frequency_hz();
    uint32_t seconds = 0U;

    if (cap == 0U) {
        return;
    }
    metrics_text[0] = '\0';

    pmm_get_stats(&pmm_stats);
    if (hz != 0U) {
        seconds = (uint32_t)(timer_ticks_snapshot() & 0xFFFFFFFFULL) / hz;
    }

    display_append_text(metrics_text, &len, cap, "BUILD ");
    display_append_text(metrics_text, &len, cap, __DATE__);
    display_append_text(metrics_text, &len, cap, "  ");
    display_append_text(metrics_text, &len, cap, "UP ");
    display_append_u32(metrics_text, &len, cap, seconds);
    display_append_text(metrics_text, &len, cap, "s  RAM ");
    display_append_mib_value(metrics_text, &len, cap, pmm_stats.free_frames);
    display_append_text(metrics_text, &len, cap, "/");
    display_append_mib_value(metrics_text, &len, cap, pmm_stats.total_frames);
}

static void display_framebuffer_draw_header_metrics(void) {
    uint32_t metrics_bg = display_framebuffer_pack_rgb(11U, 22U, 40U);
    uint32_t metrics_fg = display_framebuffer_pack_rgb(234U, 240U, 250U);
    uint32_t metrics_shadow = display_framebuffer_pack_rgb(0U, 8U, 20U);
    uint32_t metrics_y = 2U;
    uint32_t metrics_left = 20U * DISPLAY_FB_CHAR_W;
    uint32_t metrics_right = g_display_fb.info.width - DISPLAY_FB_CHAR_W;
    char metrics_text[64];

    if (metrics_left >= metrics_right) {
        return;
    }

    display_framebuffer_build_header_metrics(metrics_text, sizeof(metrics_text));
    display_framebuffer_fill_rect_packed(
        metrics_left,
        metrics_y,
        metrics_right - metrics_left,
        DISPLAY_FB_CHAR_H - 2U,
        metrics_bg);
    display_framebuffer_draw_text_right_emphasized_packed(
        metrics_right,
        metrics_y,
        metrics_text,
        metrics_fg,
        metrics_bg,
        metrics_shadow);
}

static void display_framebuffer_draw_footer_hud(void) {
#if DISPLAY_FB_FOOTER_ROWS > 0
    uint32_t footer_top = g_display_fb.info.height - (DISPLAY_FB_FOOTER_ROWS * DISPLAY_FB_CHAR_H);
    uint32_t footer_bg = display_framebuffer_pack_rgb(13U, 24U, 42U);
    uint32_t footer_fg = display_framebuffer_pack_rgb(224U, 233U, 246U);
    uint32_t rail_bg = display_framebuffer_pack_rgb(8U, 15U, 29U);
    uint32_t rail_border = display_framebuffer_pack_rgb(74U, 102U, 136U);
    uint32_t rail_left = DISPLAY_FB_FOOTER_RAIL_LEFT_COLS * DISPLAY_FB_CHAR_W;
    uint32_t rail_right = g_display_fb.info.width - DISPLAY_FB_CHAR_W;
    uint32_t rail_top = footer_top + 3U;
    uint32_t rail_h = DISPLAY_FB_CHAR_H - 6U;
    uint32_t cap_w;
    uint32_t visible_finished;
    uint32_t slots_taken;
    uint32_t active_slots = g_display_fb.command_active != 0U ? 1U : 0U;
    char legend_text[40];

    display_framebuffer_fill_rect_packed(
        0U,
        footer_top,
        g_display_fb.info.width,
        DISPLAY_FB_CHAR_H,
        footer_bg);
    display_framebuffer_build_footer_legend(legend_text, sizeof(legend_text));
    display_framebuffer_draw_text_packed(
        DISPLAY_FB_CHAR_W,
        footer_top + 1U,
        legend_text,
        footer_fg,
        footer_bg);

    if (rail_right <= rail_left + (DISPLAY_FB_TIMELINE_CAP * 3U)) {
        return;
    }

    display_framebuffer_fill_rect_packed(
        rail_left,
        rail_top,
        rail_right - rail_left,
        rail_h,
        rail_border);
    display_framebuffer_fill_rect_packed(
        rail_left + 1U,
        rail_top + 1U,
        (rail_right - rail_left) - 2U,
        rail_h - 2U,
        rail_bg);

    cap_w = ((rail_right - rail_left) - (DISPLAY_FB_TIMELINE_RAIL_INSET * 2U)) / DISPLAY_FB_TIMELINE_CAP;
    if (cap_w < 3U) {
        return;
    }

    visible_finished = g_display_fb.timeline_count;
    if (visible_finished > DISPLAY_FB_TIMELINE_CAP - active_slots) {
        visible_finished = DISPLAY_FB_TIMELINE_CAP - active_slots;
    }
    slots_taken = visible_finished + active_slots;

    for (uint32_t i = 0U; i < visible_finished; i++) {
        uint32_t oldest =
            (g_display_fb.timeline_head + DISPLAY_FB_TIMELINE_CAP - g_display_fb.timeline_count) %
            DISPLAY_FB_TIMELINE_CAP;
        uint32_t logical = g_display_fb.timeline_count - visible_finished + i;
        uint32_t entry_idx = (oldest + logical) % DISPLAY_FB_TIMELINE_CAP;
        uint32_t slot = (DISPLAY_FB_TIMELINE_CAP - slots_taken) + i;
        uint32_t cap_x = rail_left + DISPLAY_FB_TIMELINE_RAIL_INSET + (slot * cap_w);
        uint32_t cap_height = 3U + (g_display_fb.timeline[entry_idx].duration_ticks / 4U);
        uint32_t cap_max_h = rail_h - 4U;
        uint32_t cap_y;
        uint32_t cap_color;

        if (cap_height > cap_max_h) {
            cap_height = cap_max_h;
        }
        cap_y = (rail_top + rail_h - 2U) - cap_height;
        if (g_display_fb.timeline[entry_idx].event == DISPLAY_TIMELINE_EVENT_FAIL) {
            cap_color = display_framebuffer_pack_rgb(228U, 96U, 86U);
        } else {
            cap_color = display_framebuffer_pack_rgb(74U, 204U, 148U);
        }
        if (cap_w > 2U) {
            display_framebuffer_fill_rect_packed(
                cap_x,
                cap_y,
                cap_w - 1U,
                cap_height,
                cap_color);
        }
    }

    if (g_display_fb.command_active != 0U) {
        uint32_t slot = DISPLAY_FB_TIMELINE_CAP - 1U;
        uint32_t cap_x = rail_left + DISPLAY_FB_TIMELINE_RAIL_INSET + (slot * cap_w);
        uint32_t cap_color = display_framebuffer_pack_rgb(64U, 146U, 220U);

        if (cap_w > 2U) {
            display_framebuffer_fill_rect_packed(
                cap_x,
                rail_top + 2U,
                cap_w - 1U,
                rail_h - 4U,
                cap_color);
        }
    }
#endif
}

static void display_framebuffer_scroll_if_needed(void) {
    uint32_t line_bytes;
    uint32_t scroll_bottom_px;

    if (g_display_fb.cursor_row < g_display_fb.scroll_rows) {
        return;
    }

    scroll_bottom_px = g_display_fb.content_top_px + (g_display_fb.scroll_rows * DISPLAY_FB_CHAR_H);
    line_bytes = (g_display_fb.content_width_px + DISPLAY_FB_LINE_GUTTER_TOTAL) *
                 g_display_fb.bytes_per_pixel;
    for (uint32_t y = g_display_fb.content_top_px;
         y + DISPLAY_FB_CHAR_H < scroll_bottom_px;
         y++) {
        volatile uint8_t *dst = g_display_fb.base + (y * g_display_fb.info.pitch) +
                                ((g_display_fb.content_left_px - DISPLAY_FB_LINE_GUTTER_TOTAL) *
                                 g_display_fb.bytes_per_pixel);
        volatile uint8_t *src = g_display_fb.base + ((y + DISPLAY_FB_CHAR_H) * g_display_fb.info.pitch) +
                                ((g_display_fb.content_left_px - DISPLAY_FB_LINE_GUTTER_TOTAL) *
                                 g_display_fb.bytes_per_pixel);

        for (uint32_t i = 0; i < line_bytes; i++) {
            dst[i] = src[i];
        }
    }

    display_framebuffer_fill_rect_packed(
        g_display_fb.content_left_px - DISPLAY_FB_LINE_GUTTER_TOTAL,
        scroll_bottom_px - DISPLAY_FB_CHAR_H,
        DISPLAY_FB_LINE_GUTTER_TOTAL,
        DISPLAY_FB_CHAR_H,
        display_framebuffer_pack_rgb(8U, 14U, 24U));
    display_framebuffer_fill_rect_packed(
        g_display_fb.content_left_px,
        scroll_bottom_px - DISPLAY_FB_CHAR_H,
        g_display_fb.content_width_px,
        DISPLAY_FB_CHAR_H,
        display_framebuffer_pack_rgb(0U, 0U, 0U));
    display_framebuffer_draw_line_gutter(g_display_fb.scroll_rows - 1U, DISPLAY_LINE_STYLE_NORMAL);
    g_display_fb.cursor_row = g_display_fb.scroll_rows - 1U;
}

static void display_framebuffer_putc(char c) {
    display_line_style_t next_style;

    if (c == '\r') {
        g_display_fb.cursor_col = 0U;
        display_framebuffer_reset_line_tracking(DISPLAY_LINE_STYLE_NORMAL);
        return;
    }
    if (c == '\b') {
        if (g_display_fb.line_style == DISPLAY_LINE_STYLE_PROMPT) {
            uint32_t visible_cols = display_framebuffer_prompt_visible_cols();

            if (g_display_fb.line_len == 0U) {
                return;
            }
            g_display_fb.line_len--;
            g_display_fb.line_style = display_framebuffer_classify_line(
                g_display_fb.line_text,
                g_display_fb.line_len,
                DISPLAY_LINE_STYLE_NORMAL);
            if (g_display_fb.line_len < visible_cols) {
                g_display_fb.cursor_col = g_display_fb.line_len;
            } else {
                g_display_fb.cursor_col = visible_cols - 1U;
            }
            display_framebuffer_redraw_current_line();
            return;
        }
        if (g_display_fb.cursor_col > 0U) {
            g_display_fb.cursor_col--;
        } else if (g_display_fb.cursor_row > 0U) {
            g_display_fb.cursor_row--;
            g_display_fb.cursor_col = g_display_fb.text_cols - 1U;
        }
        if (g_display_fb.line_len == 0U) {
            return;
        }
        g_display_fb.line_len--;
        g_display_fb.line_style = display_framebuffer_classify_line(
            g_display_fb.line_text,
            g_display_fb.line_len,
            DISPLAY_LINE_STYLE_NORMAL);
        display_framebuffer_redraw_current_line();
        return;
    }
    if (c == '\n') {
        display_line_style_t completed_style = g_display_fb.line_style;

        display_framebuffer_on_line_complete(
            g_display_fb.line_text,
            g_display_fb.line_len,
            completed_style);
        if (completed_style == DISPLAY_LINE_STYLE_PROMPT) {
            g_display_fb.line_style = DISPLAY_LINE_STYLE_COMMAND;
            display_framebuffer_redraw_current_line();
        }
        g_display_fb.cursor_row++;
        g_display_fb.cursor_col = 0U;
        display_framebuffer_scroll_if_needed();
        display_framebuffer_reset_line_tracking(DISPLAY_LINE_STYLE_NORMAL);
        if (completed_style == DISPLAY_LINE_STYLE_PROMPT) {
            display_framebuffer_draw_prompt_strip_idle();
        }
        return;
    }
    if ((uint8_t)c < 0x20U) {
        return;
    }

    if (g_display_fb.line_len < sizeof(g_display_fb.line_text)) {
        g_display_fb.line_text[g_display_fb.line_len++] = c;
    }
    next_style = display_framebuffer_classify_line(
        g_display_fb.line_text,
        g_display_fb.line_len,
        g_display_fb.line_style);
    if (next_style != g_display_fb.line_style) {
        g_display_fb.line_style = next_style;
        display_framebuffer_redraw_current_line();
    } else {
        if (g_display_fb.line_style == DISPLAY_LINE_STYLE_PROMPT) {
            display_framebuffer_redraw_current_line();
        } else {
        uint32_t cell_x;
        uint32_t cell_y;
        uint32_t fg_pixel;
        uint32_t bg_pixel;
        uint32_t render_row = g_display_fb.cursor_row;

        display_framebuffer_line_colors(g_display_fb.line_style, &fg_pixel, &bg_pixel);
        if (g_display_fb.line_style == DISPLAY_LINE_STYLE_PROMPT) {
            if (g_display_fb.cursor_row < g_display_fb.scroll_rows) {
                display_framebuffer_clear_scroll_row(g_display_fb.cursor_row);
            }
            render_row = g_display_fb.scroll_rows;
            cell_x = g_display_fb.content_left_px +
                     (DISPLAY_FB_PROMPT_TEXT_OFFSET_COLS * DISPLAY_FB_CHAR_W) +
                     (g_display_fb.cursor_col * DISPLAY_FB_CHAR_W);
        } else {
            cell_x = g_display_fb.content_left_px + (g_display_fb.cursor_col * DISPLAY_FB_CHAR_W);
        }
        display_framebuffer_draw_line_gutter(render_row, g_display_fb.line_style);
        cell_y = g_display_fb.content_top_px + (render_row * DISPLAY_FB_CHAR_H);
        display_framebuffer_draw_glyph_packed(cell_x, cell_y, c, fg_pixel, bg_pixel);
        }
    }

    if (g_display_fb.line_style == DISPLAY_LINE_STYLE_PROMPT) {
        uint32_t visible_cols = display_framebuffer_prompt_visible_cols();

        if (g_display_fb.line_len < visible_cols) {
            g_display_fb.cursor_col = g_display_fb.line_len;
        } else {
            g_display_fb.cursor_col = visible_cols - 1U;
        }
        return;
    }

    g_display_fb.cursor_col++;
    if (g_display_fb.cursor_col >= g_display_fb.text_cols) {
        g_display_fb.cursor_col = 0U;
        g_display_fb.cursor_row++;
        display_framebuffer_scroll_if_needed();
        display_framebuffer_reset_line_tracking(g_display_fb.line_style);
    }
}

static void display_framebuffer_draw_shell_frame(void) {
    uint32_t back_bg = display_framebuffer_pack_rgb(7U, 11U, 19U);
    uint32_t title_bg = display_framebuffer_pack_rgb(18U, 40U, 86U);
    uint32_t title_fg = display_framebuffer_pack_rgb(252U, 254U, 255U);
    uint32_t title_shadow = display_framebuffer_pack_rgb(4U, 16U, 38U);
    uint32_t accent = display_framebuffer_pack_rgb(248U, 182U, 86U);
    uint32_t badge_fg = display_framebuffer_pack_rgb(10U, 20U, 36U);
    uint32_t badge_x = 8U * DISPLAY_FB_CHAR_W;
    uint32_t badge_y = 2U;
    uint32_t badge_w = (7U * DISPLAY_FB_CHAR_W) - 4U;
    uint32_t badge_h = DISPLAY_FB_CHAR_H - 4U;
    uint32_t panel_border = display_framebuffer_pack_rgb(44U, 60U, 84U);
    uint32_t panel_bg = display_framebuffer_pack_rgb(0U, 0U, 0U);
    uint32_t panel_left =
        g_display_fb.content_left_px - DISPLAY_FB_PANEL_BORDER - DISPLAY_FB_LINE_GUTTER_TOTAL;
    uint32_t panel_top = g_display_fb.content_top_px - DISPLAY_FB_PANEL_BORDER;
    uint32_t panel_width =
        g_display_fb.content_width_px + (DISPLAY_FB_PANEL_BORDER * 2U) + DISPLAY_FB_LINE_GUTTER_TOTAL;
    uint32_t panel_height =
        (g_display_fb.content_bottom_px - g_display_fb.content_top_px) + (DISPLAY_FB_PANEL_BORDER * 2U);

    display_framebuffer_fill_rect_packed(0U, 0U, g_display_fb.info.width, g_display_fb.info.height, back_bg);
    display_framebuffer_fill_rect_packed(
        0U, 0U, g_display_fb.info.width, DISPLAY_FB_CHAR_H, title_bg);
    display_framebuffer_fill_rect_packed(0U, DISPLAY_FB_CHAR_H - 2U, g_display_fb.info.width, 2U, accent);
    display_framebuffer_fill_rect_packed(panel_left, panel_top, panel_width, panel_height, panel_border);
    display_framebuffer_fill_rect_packed(
        g_display_fb.content_left_px - DISPLAY_FB_LINE_GUTTER_TOTAL,
        g_display_fb.content_top_px,
        DISPLAY_FB_LINE_GUTTER_TOTAL,
        g_display_fb.content_bottom_px - g_display_fb.content_top_px,
        display_framebuffer_pack_rgb(10U, 17U, 29U));
    display_framebuffer_fill_rect_packed(
        g_display_fb.content_left_px,
        g_display_fb.content_top_px,
        g_display_fb.content_width_px,
        g_display_fb.content_bottom_px - g_display_fb.content_top_px,
        panel_bg);
    display_framebuffer_fill_rect_packed(
        g_display_fb.content_left_px - DISPLAY_FB_LINE_GUTTER_TOTAL,
        g_display_fb.content_top_px,
        DISPLAY_FB_LINE_GUTTER_WIDTH,
        g_display_fb.content_bottom_px - g_display_fb.content_top_px,
        display_framebuffer_pack_rgb(68U, 84U, 106U));
    display_framebuffer_draw_prompt_strip_idle();
    display_framebuffer_draw_text_emphasized_packed(
        DISPLAY_FB_CHAR_W,
        2U,
        "SKEZOS",
        title_fg,
        title_bg,
        title_shadow);
    display_framebuffer_fill_rect_packed(
        badge_x,
        badge_y,
        badge_w,
        badge_h,
        accent);
    display_framebuffer_draw_text_packed(
        badge_x + 6U,
        badge_y + 1U,
        "KERNEL",
        badge_fg,
        accent);
    display_framebuffer_draw_header_metrics();
    display_framebuffer_draw_footer_hud();
}

void display_init(void) {
    g_display_mode = DISPLAY_MODE_VGA;
    g_display_fb.ready = 0U;
    g_display_fb.timeline_count = 0U;
    g_display_fb.timeline_head = 0U;
    g_display_fb.command_active = 0U;
    g_display_fb.command_start_ticks = 0U;
    g_display_fb.command_tag = '?';
    g_display_fb.prompt_hint = DISPLAY_PROMPT_HINT_INPUT;
    g_display_fb.prompt_hint_until_ticks = 0U;
    g_display_fb.prompt_hint_tag = '?';
    g_display_fb.transition_cause = DISPLAY_TRANSITION_CAUSE_NONE;
    display_verify_font_coverage();
    vga_clear();
}

void display_late_init(void) {
    struct boot_framebuffer_info info;
    uint32_t phys_base;
    uint32_t phys_offset;
    uint32_t map_length;
    uint64_t span_bytes_u64;
    uint32_t gui_hash;
    int rc;

    g_display_fb.ready = 0U;
    if (memmap_get_framebuffer_info(&info) != 0) {
        return;
    }
    if (info.type == 2U) {
        /* Multiboot type 2 is text mode and already covered by the VGA
         * backend.  Leave it alone so the visual shell stays on the
         * existing path until a real pixel surface is available.
         */
        return;
    }
    if (info.type != 1U) {
        KLOGW("display: unsupported framebuffer type=%u", (uint32_t)info.type);
        return;
    }
    if (info.red_mask_size == 0U || info.green_mask_size == 0U || info.blue_mask_size == 0U) {
        KLOGW("display: missing direct-rgb mask metadata");
        return;
    }
    if ((info.address >> 32) != 0U) {
        KLOGW("display: framebuffer above 4GiB is unsupported");
        return;
    }
    if (info.bpp == 0U || info.bpp > 32U) {
        KLOGW("display: unsupported framebuffer bpp=%u", (uint32_t)info.bpp);
        return;
    }

    span_bytes_u64 = (uint64_t)info.pitch * (uint64_t)info.height;
    if (span_bytes_u64 == 0U || span_bytes_u64 > KERNEL_FRAMEBUFFER_WINDOW_SIZE_BYTES) {
        KLOGW("display: framebuffer span unsupported bytes=%u",
              (uint32_t)span_bytes_u64);
        return;
    }

    phys_base = (uint32_t)info.address & ~(PAGE_SIZE_BYTES - 1U);
    phys_offset = (uint32_t)info.address - phys_base;
    if (span_bytes_u64 > 0xFFFFFFFFU - phys_offset) {
        KLOGW("display: framebuffer mapping length overflow");
        return;
    }
    map_length = (uint32_t)span_bytes_u64 + phys_offset;

    rc = paging_map_kernel_region(KERNEL_FRAMEBUFFER_WINDOW_BASE,
                                  phys_base,
                                  map_length,
                                  DISPLAY_FRAMEBUFFER_PAGE_FLAGS);
    if (rc != 0) {
        KLOGW("display: framebuffer map failed rc=%d", rc);
        return;
    }

    g_display_fb.base = (volatile uint8_t *)(uintptr_t)(KERNEL_FRAMEBUFFER_WINDOW_BASE + phys_offset);
    g_display_fb.span_bytes = (uint32_t)span_bytes_u64;
    g_display_fb.bytes_per_pixel = (info.bpp + 7U) / 8U;
    g_display_fb.info = info;
    if (info.width <= (DISPLAY_FB_PANEL_MARGIN_X * 2U) + DISPLAY_FB_CHAR_W ||
        info.height <=
            (DISPLAY_FB_HEADER_ROWS * DISPLAY_FB_CHAR_H) +
                (DISPLAY_FB_FOOTER_ROWS * DISPLAY_FB_CHAR_H) +
                (DISPLAY_FB_PANEL_MARGIN_Y * 2U) +
                (DISPLAY_FB_PANEL_BORDER * 2U) +
                DISPLAY_FB_CHAR_H) {
        KLOGW("display: framebuffer geometry too small %ux%u", info.width, info.height);
        return;
    }
    g_display_fb.text_cols = (info.width - (DISPLAY_FB_PANEL_MARGIN_X * 2U)) / DISPLAY_FB_CHAR_W;
    if (g_display_fb.text_cols == 0U) {
        KLOGW("display: framebuffer text cols unavailable");
        return;
    }
    g_display_fb.content_width_px = g_display_fb.text_cols * DISPLAY_FB_CHAR_W;
    g_display_fb.content_left_px = (info.width - g_display_fb.content_width_px) / 2U;
    g_display_fb.content_top_px =
        (DISPLAY_FB_HEADER_ROWS * DISPLAY_FB_CHAR_H) + DISPLAY_FB_PANEL_MARGIN_Y + DISPLAY_FB_PANEL_BORDER;
    g_display_fb.content_bottom_px =
        info.height - (DISPLAY_FB_FOOTER_ROWS * DISPLAY_FB_CHAR_H) - DISPLAY_FB_PANEL_MARGIN_Y -
        DISPLAY_FB_PANEL_BORDER;
    if (g_display_fb.content_bottom_px <= g_display_fb.content_top_px + DISPLAY_FB_CHAR_H) {
        KLOGW("display: framebuffer content window unavailable");
        return;
    }
    g_display_fb.text_rows =
        (g_display_fb.content_bottom_px - g_display_fb.content_top_px) / DISPLAY_FB_CHAR_H;
    if (g_display_fb.text_rows < 2U) {
        KLOGW("display: framebuffer text rows unavailable");
        return;
    }
    g_display_fb.scroll_rows = g_display_fb.text_rows - 1U;
    g_display_fb.content_bottom_px =
        g_display_fb.content_top_px + (g_display_fb.text_rows * DISPLAY_FB_CHAR_H);
    g_display_fb.cursor_col = 0U;
    g_display_fb.cursor_row = 0U;
    g_display_fb.timeline_count = 0U;
    g_display_fb.timeline_head = 0U;
    g_display_fb.command_active = 0U;
    g_display_fb.command_start_ticks = 0U;
    g_display_fb.command_tag = '?';
    g_display_fb.prompt_hint = DISPLAY_PROMPT_HINT_INPUT;
    g_display_fb.prompt_hint_until_ticks = 0U;
    g_display_fb.prompt_hint_tag = '?';
    g_display_fb.transition_cause = DISPLAY_TRANSITION_CAUSE_NONE;
    g_display_fb.ready = 1U;

    display_framebuffer_draw_shell_frame();
    g_display_mode = DISPLAY_MODE_FRAMEBUFFER;
    gui_hash = display_compute_gui_state_hash();

    KLOGI("display: framebuffer console active phys=%x virt=%x bytes=%u %ux%u pitch=%u bpp=%u",
          (uint32_t)info.address,
          (uint32_t)(uintptr_t)g_display_fb.base,
          g_display_fb.span_bytes,
          info.width,
          info.height,
          info.pitch,
          (uint32_t)info.bpp);
    KLOGI("display: gui_state_hash=%x profile=fb-shell-v4", gui_hash);
}

uint32_t display_console_enter_critical(void) {
    return vga_console_enter_critical();
}

void display_console_leave_critical(uint32_t saved_flags) {
    vga_console_leave_critical(saved_flags);
}

void display_putc(char c) {
    if (g_display_mode == DISPLAY_MODE_FRAMEBUFFER && g_display_fb.ready != 0U) {
        display_framebuffer_putc(c);
        return;
    }
    vga_putc(c);
}

void display_puts(const char *str) {
    while (*str != '\0') {
        display_putc(*str++);
    }
}

int display_framebuffer_ready(void) {
    return g_display_fb.ready != 0U;
}
