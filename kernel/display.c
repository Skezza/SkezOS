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

#define DISPLAY_FB_COMMAND_RATE_WINDOW 8U
#define DISPLAY_FB_COMMAND_SLOW_MIN_TICKS 24U
#define DISPLAY_FB_COMMAND_SLOW_MARGIN_TICKS 6U
#define DISPLAY_FB_COMMAND_SLOW_MULTIPLIER 2U

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
    DISPLAY_COMMAND_HEALTH_BOOT = 0,
    DISPLAY_COMMAND_HEALTH_OK = 1,
    DISPLAY_COMMAND_HEALTH_WARN = 2,
    DISPLAY_COMMAND_HEALTH_DEGRADED = 3,
} display_command_health_state_t;

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
    uint8_t rows[7];
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
    uint32_t command_last_ticks;
    uint32_t command_avg_ticks;
    uint32_t command_peak_ticks;
    uint32_t command_samples;
    uint8_t command_last_success;
    uint8_t command_last_slow;
    uint32_t command_last_budget_ticks;
    uint32_t command_slow_count;
    uint32_t command_slow_streak;
    uint32_t command_slow_streak_peak;
    uint32_t command_ok_count;
    uint32_t command_fail_count;
    uint32_t command_fail_streak;
    uint32_t command_fail_streak_peak;
    uint32_t command_finish_ticks[DISPLAY_FB_COMMAND_RATE_WINDOW];
    uint32_t command_finish_count;
    uint32_t command_finish_head;
    uint8_t command_recent_outcomes[DISPLAY_FB_COMMAND_RATE_WINDOW];
    uint32_t command_recent_count;
    uint32_t command_recent_head;
    uint8_t command_health_state;
    uint32_t command_health_state_changes;
    uint32_t command_health_state_since_ticks;
    uint32_t command_health_episode_start_ticks;
    uint32_t command_health_last_recovery_ticks;
    uint32_t command_health_avg_recovery_ticks;
    uint32_t command_health_peak_recovery_ticks;
    uint32_t command_health_recovery_count;
    uint32_t command_health_warn_dwell_ticks;
    uint32_t command_health_degr_dwell_ticks;
    uint8_t prompt_hint;
    uint32_t prompt_hint_until_ticks;
    char prompt_hint_tag;
    uint8_t transition_cause;
    uint8_t ready;
};

static const struct display_glyph g_display_font[] = {
    { ' ', { 0, 0, 0, 0, 0, 0, 0 } },
    { '!', { 4, 4, 4, 4, 4, 0, 4 } },
    { '"', { 10, 10, 10, 0, 0, 0, 0 } },
    { '#', { 10, 10, 31, 10, 31, 10, 10 } },
    { '$', { 4, 15, 20, 14, 5, 30, 4 } },
    { '%', { 24, 25, 2, 4, 8, 19, 3 } },
    { '&', { 8, 20, 20, 8, 21, 18, 13 } },
    { '\'', { 6, 6, 4, 8, 0, 0, 0 } },
    { '(', { 2, 4, 8, 8, 8, 4, 2 } },
    { ')', { 8, 4, 2, 2, 2, 4, 8 } },
    { '*', { 4, 21, 14, 31, 14, 21, 4 } },
    { '+', { 0, 4, 4, 31, 4, 4, 0 } },
    { ',', { 0, 0, 0, 0, 6, 6, 4 } },
    { '-', { 0, 0, 0, 31, 0, 0, 0 } },
    { '.', { 0, 0, 0, 0, 0, 6, 6 } },
    { '/', { 0, 1, 2, 4, 8, 16, 0 } },
    { '0', { 14, 17, 19, 21, 25, 17, 14 } },
    { '1', { 4, 12, 4, 4, 4, 4, 14 } },
    { '2', { 14, 17, 1, 14, 16, 16, 31 } },
    { '3', { 31, 1, 2, 6, 1, 17, 14 } },
    { '4', { 2, 6, 10, 18, 31, 2, 2 } },
    { '5', { 31, 16, 30, 1, 1, 17, 14 } },
    { '6', { 7, 8, 16, 30, 17, 17, 14 } },
    { '7', { 31, 1, 1, 2, 4, 8, 16 } },
    { '8', { 14, 17, 17, 14, 17, 17, 14 } },
    { '9', { 14, 17, 17, 15, 1, 2, 28 } },
    { ':', { 0, 0, 4, 0, 4, 0, 0 } },
    { ';', { 0, 0, 4, 0, 4, 4, 8 } },
    { '<', { 1, 2, 4, 8, 4, 2, 1 } },
    { '=', { 0, 0, 31, 0, 31, 0, 0 } },
    { '>', { 8, 4, 2, 1, 2, 4, 8 } },
    { '?', { 14, 17, 1, 6, 4, 0, 4 } },
    { '@', { 14, 17, 21, 23, 22, 16, 15 } },
    { 'A', { 4, 10, 17, 17, 31, 17, 17 } },
    { 'B', { 30, 17, 17, 30, 17, 17, 30 } },
    { 'C', { 14, 17, 16, 16, 16, 17, 14 } },
    { 'D', { 30, 17, 17, 17, 17, 17, 30 } },
    { 'E', { 31, 16, 16, 30, 16, 16, 31 } },
    { 'F', { 31, 16, 16, 30, 16, 16, 16 } },
    { 'G', { 15, 17, 16, 16, 19, 17, 15 } },
    { 'H', { 17, 17, 17, 31, 17, 17, 17 } },
    { 'I', { 14, 4, 4, 4, 4, 4, 14 } },
    { 'J', { 7, 2, 2, 2, 2, 18, 12 } },
    { 'K', { 17, 18, 20, 24, 20, 18, 17 } },
    { 'L', { 16, 16, 16, 16, 16, 16, 31 } },
    { 'M', { 17, 27, 21, 21, 21, 17, 17 } },
    { 'N', { 17, 17, 25, 21, 19, 17, 17 } },
    { 'O', { 14, 17, 17, 17, 17, 17, 14 } },
    { 'P', { 30, 17, 17, 30, 16, 16, 16 } },
    { 'Q', { 14, 17, 17, 17, 21, 18, 13 } },
    { 'R', { 30, 17, 17, 30, 20, 18, 17 } },
    { 'S', { 14, 17, 16, 14, 1, 17, 14 } },
    { 'T', { 31, 21, 4, 4, 4, 4, 4 } },
    { 'U', { 17, 17, 17, 17, 17, 17, 14 } },
    { 'V', { 17, 17, 17, 17, 17, 10, 4 } },
    { 'W', { 17, 17, 17, 21, 21, 21, 10 } },
    { 'X', { 17, 17, 10, 4, 10, 17, 17 } },
    { 'Y', { 17, 17, 10, 4, 4, 4, 4 } },
    { 'Z', { 31, 1, 2, 14, 8, 16, 31 } },
    { '[', { 15, 8, 8, 8, 8, 8, 15 } },
    { '\\', { 0, 16, 8, 4, 2, 1, 0 } },
    { ']', { 15, 1, 1, 1, 1, 1, 15 } },
    { '^', { 4, 10, 17, 0, 0, 0, 0 } },
    { '_', { 0, 0, 0, 0, 0, 0, 31 } },
    { '`', { 12, 12, 4, 2, 0, 0, 0 } },
    { 'a', { 0, 0, 12, 2, 14, 18, 15 } },
    { 'b', { 16, 16, 22, 25, 17, 25, 22 } },
    { 'c', { 0, 0, 14, 17, 16, 17, 14 } },
    { 'd', { 1, 1, 13, 19, 17, 19, 13 } },
    { 'e', { 0, 0, 14, 17, 31, 16, 14 } },
    { 'f', { 2, 5, 4, 14, 4, 4, 4 } },
    { 'g', { 0, 0, 14, 19, 19, 13, 1 } },
    { 'h', { 16, 16, 22, 25, 17, 17, 17 } },
    { 'i', { 4, 0, 12, 4, 4, 4, 14 } },
    { 'j', { 2, 0, 2, 2, 2, 18, 12 } },
    { 'k', { 16, 16, 18, 20, 24, 20, 18 } },
    { 'l', { 12, 4, 4, 4, 4, 4, 14 } },
    { 'm', { 0, 0, 26, 21, 21, 21, 21 } },
    { 'n', { 0, 0, 22, 25, 17, 17, 17 } },
    { 'o', { 0, 0, 14, 17, 17, 17, 14 } },
    { 'p', { 0, 0, 22, 25, 25, 22, 16 } },
    { 'q', { 0, 0, 13, 19, 19, 13, 1 } },
    { 'r', { 0, 0, 22, 25, 16, 16, 16 } },
    { 's', { 0, 0, 15, 16, 14, 1, 30 } },
    { 't', { 4, 4, 31, 4, 4, 5, 2 } },
    { 'u', { 0, 0, 17, 17, 17, 19, 13 } },
    { 'v', { 0, 0, 17, 17, 17, 10, 4 } },
    { 'w', { 0, 0, 17, 17, 21, 21, 10 } },
    { 'x', { 0, 0, 17, 10, 4, 10, 17 } },
    { 'y', { 0, 0, 17, 17, 15, 1, 17 } },
    { 'z', { 0, 0, 31, 2, 4, 8, 31 } },
    { '{', { 2, 4, 4, 8, 4, 4, 2 } },
    { '|', { 4, 4, 4, 0, 4, 4, 4 } },
    { '}', { 8, 4, 4, 2, 4, 4, 8 } },
    { '~', { 8, 21, 2, 0, 0, 0, 0 } },
};

static struct display_framebuffer_state g_display_fb;
static display_mode_t g_display_mode = DISPLAY_MODE_VGA;

#define DISPLAY_FRAMEBUFFER_PAGE_FLAGS \
    (PAGING_PAGE_FLAG_WRITABLE | PAGING_PAGE_FLAG_WRITE_THROUGH | PAGING_PAGE_FLAG_CACHE_DISABLE)
#define DISPLAY_FB_CHAR_W 14U
#define DISPLAY_FB_CHAR_H 17U
#define DISPLAY_FB_FONT_SRC_W 5U
#define DISPLAY_FB_FONT_SRC_H 7U
#define DISPLAY_FB_GLYPH_W 5U
#define DISPLAY_FB_GLYPH_H 7U
#define DISPLAY_FB_GLYPH_SCALE_X 2U
#define DISPLAY_FB_GLYPH_SCALE_Y 2U
#define DISPLAY_FB_GLYPH_X_PAD 2U
#define DISPLAY_FB_GLYPH_Y_PAD 1U
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
static void display_append_compact_u32(char *dst, uint32_t *len, uint32_t cap, uint32_t value);
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
static const char *display_command_health_label(display_command_health_state_t state);
static void display_framebuffer_build_footer_legend(char *legend_text, uint32_t cap);
static void display_framebuffer_build_footer_latency(char *latency_text, uint32_t cap);
static uint32_t display_command_latency_budget_ticks(uint32_t avg_ticks, uint32_t samples);
static const char *display_command_latency_status_label(void);
static uint32_t display_command_rate_per_min(void);
static void display_command_recent_push(int success);
static uint32_t display_command_recent_success_pct(uint32_t *out_samples);
static void display_command_health_account_dwell(uint32_t now_ticks,
                                                 display_command_health_state_t state);
static display_command_health_state_t display_classify_command_health(uint32_t success_pct,
                                                                      uint32_t recent_success_pct,
                                                                      uint32_t fail_streak,
                                                                      uint32_t recent_samples,
                                                                      uint32_t samples);
static void display_update_command_health_state(uint32_t success_pct,
                                                uint32_t recent_success_pct,
                                                uint32_t recent_samples,
                                                uint32_t rate_per_min,
                                                char tag);
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

static void display_reset_command_health(void) {
    g_display_fb.command_last_slow = 0U;
    g_display_fb.command_last_budget_ticks = 0U;
    g_display_fb.command_slow_count = 0U;
    g_display_fb.command_slow_streak = 0U;
    g_display_fb.command_slow_streak_peak = 0U;
    g_display_fb.command_ok_count = 0U;
    g_display_fb.command_fail_count = 0U;
    g_display_fb.command_fail_streak = 0U;
    g_display_fb.command_fail_streak_peak = 0U;
    g_display_fb.command_finish_count = 0U;
    g_display_fb.command_finish_head = 0U;
    for (uint32_t i = 0U; i < DISPLAY_FB_COMMAND_RATE_WINDOW; i++) {
        g_display_fb.command_finish_ticks[i] = 0U;
        g_display_fb.command_recent_outcomes[i] = 0U;
    }
    g_display_fb.command_recent_count = 0U;
    g_display_fb.command_recent_head = 0U;
    g_display_fb.command_health_state = DISPLAY_COMMAND_HEALTH_BOOT;
    g_display_fb.command_health_state_changes = 0U;
    g_display_fb.command_health_state_since_ticks = display_ticks32();
    g_display_fb.command_health_episode_start_ticks = 0U;
    g_display_fb.command_health_last_recovery_ticks = 0U;
    g_display_fb.command_health_avg_recovery_ticks = 0U;
    g_display_fb.command_health_peak_recovery_ticks = 0U;
    g_display_fb.command_health_recovery_count = 0U;
    g_display_fb.command_health_warn_dwell_ticks = 0U;
    g_display_fb.command_health_degr_dwell_ticks = 0U;
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

    if ((display_prompt_hint_t)g_display_fb.prompt_hint == DISPLAY_PROMPT_HINT_INPUT) {
        display_command_health_state_t health_state =
            (display_command_health_state_t)g_display_fb.command_health_state;
        if (health_state == DISPLAY_COMMAND_HEALTH_DEGRADED) {
            prompt_fg = display_framebuffer_pack_rgb(255U, 226U, 221U);
            prompt_well_bg = display_framebuffer_pack_rgb(34U, 10U, 13U);
            prompt_accent = display_framebuffer_pack_rgb(228U, 96U, 86U);
            status_label[0] = 'D';
            status_label[1] = 'E';
            status_label[2] = 'G';
            status_label[3] = 'R';
            status_label[4] = '\0';
        } else if (health_state == DISPLAY_COMMAND_HEALTH_WARN) {
            prompt_fg = display_framebuffer_pack_rgb(224U, 233U, 246U);
            prompt_well_bg = display_framebuffer_pack_rgb(34U, 10U, 13U);
            prompt_accent = display_framebuffer_pack_rgb(248U, 188U, 96U);
            if (g_display_fb.command_fail_streak != 0U) {
                uint32_t label_len = 4U;
                uint32_t fail_streak = g_display_fb.command_fail_streak;

                if (fail_streak > 999U) {
                    fail_streak = 999U;
                }
                status_label[0] = 'C';
                status_label[1] = 'H';
                status_label[2] = 'K';
                status_label[3] = ' ';
                status_label[4] = '\0';
                display_append_u32(status_label, &label_len, sizeof(status_label), fail_streak);
            } else {
                status_label[0] = 'W';
                status_label[1] = 'A';
                status_label[2] = 'R';
                status_label[3] = 'N';
                status_label[4] = '\0';
            }
        }
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

    hash = display_hash_u32(hash, 0x46425335U); /* "FBS5" */
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

static const char *display_command_health_label(display_command_health_state_t state) {
    switch (state) {
    case DISPLAY_COMMAND_HEALTH_OK:
        return "OK";
    case DISPLAY_COMMAND_HEALTH_WARN:
        return "WARN";
    case DISPLAY_COMMAND_HEALTH_DEGRADED:
        return "DEGR";
    case DISPLAY_COMMAND_HEALTH_BOOT:
    default:
        return "BOOT";
    }
}

static display_command_health_state_t display_classify_command_health(uint32_t success_pct,
                                                                      uint32_t recent_success_pct,
                                                                      uint32_t fail_streak,
                                                                      uint32_t recent_samples,
                                                                      uint32_t samples) {
    uint32_t baseline_pct = success_pct;

    if (recent_samples >= 3U) {
        baseline_pct = recent_success_pct;
    }
    if (samples < 3U) {
        if (fail_streak >= 2U) {
            return DISPLAY_COMMAND_HEALTH_DEGRADED;
        }
        if (fail_streak >= 1U || baseline_pct < 75U) {
            return DISPLAY_COMMAND_HEALTH_WARN;
        }
        return DISPLAY_COMMAND_HEALTH_OK;
    }
    if (fail_streak >= 2U || baseline_pct < 50U) {
        return DISPLAY_COMMAND_HEALTH_DEGRADED;
    }
    if (fail_streak >= 1U || baseline_pct < 80U) {
        return DISPLAY_COMMAND_HEALTH_WARN;
    }
    return DISPLAY_COMMAND_HEALTH_OK;
}

static void display_update_command_health_state(uint32_t success_pct,
                                                uint32_t recent_success_pct,
                                                uint32_t recent_samples,
                                                uint32_t rate_per_min,
                                                char tag) {
    display_command_health_state_t prev_state =
        (display_command_health_state_t)g_display_fb.command_health_state;
    display_command_health_state_t next_state =
        display_classify_command_health(success_pct,
                                        recent_success_pct,
                                        g_display_fb.command_fail_streak,
                                        recent_samples,
                                        g_display_fb.command_samples);
    uint32_t now_ticks;
    uint32_t recovery_ticks = 0U;
    uint32_t prev_recovery_avg;
    uint32_t prev_recovery_count;

    if (next_state == prev_state) {
        return;
    }
    now_ticks = display_ticks32();
    display_command_health_account_dwell(now_ticks, prev_state);

    if (next_state == DISPLAY_COMMAND_HEALTH_OK) {
        if ((prev_state == DISPLAY_COMMAND_HEALTH_WARN ||
             prev_state == DISPLAY_COMMAND_HEALTH_DEGRADED) &&
            g_display_fb.command_health_episode_start_ticks != 0U) {
            recovery_ticks = now_ticks - g_display_fb.command_health_episode_start_ticks;
            if (recovery_ticks == 0U) {
                recovery_ticks = 1U;
            }
            prev_recovery_avg = g_display_fb.command_health_avg_recovery_ticks;
            prev_recovery_count = g_display_fb.command_health_recovery_count;
            g_display_fb.command_health_last_recovery_ticks = recovery_ticks;
            if (recovery_ticks > g_display_fb.command_health_peak_recovery_ticks) {
                g_display_fb.command_health_peak_recovery_ticks = recovery_ticks;
            }
            if (prev_recovery_count == 0U) {
                g_display_fb.command_health_avg_recovery_ticks = recovery_ticks;
            } else {
                g_display_fb.command_health_avg_recovery_ticks =
                    ((prev_recovery_avg * 3U) + recovery_ticks) / 4U;
            }
            if (g_display_fb.command_health_recovery_count != 0xFFFFFFFFU) {
                g_display_fb.command_health_recovery_count++;
            }
            KLOGI("display: cmd_health_recovery from=%s ticks=%u avg=%u peak=%u count=%u warn_dwell=%u degr_dwell=%u",
                  display_command_health_label(prev_state),
                  recovery_ticks,
                  g_display_fb.command_health_avg_recovery_ticks,
                  g_display_fb.command_health_peak_recovery_ticks,
                  g_display_fb.command_health_recovery_count,
                  g_display_fb.command_health_warn_dwell_ticks,
                  g_display_fb.command_health_degr_dwell_ticks);
        }
        g_display_fb.command_health_episode_start_ticks = 0U;
    } else if (prev_state == DISPLAY_COMMAND_HEALTH_OK ||
               prev_state == DISPLAY_COMMAND_HEALTH_BOOT) {
        g_display_fb.command_health_episode_start_ticks = now_ticks;
    }

    g_display_fb.command_health_state = (uint8_t)next_state;
    if (g_display_fb.command_health_state_changes != 0xFFFFFFFFU) {
        g_display_fb.command_health_state_changes++;
    }
    g_display_fb.command_health_state_since_ticks = now_ticks;

    KLOGI("display: cmd_health_state from=%s to=%s tag=%c streak=%u success_pct=%u recent_pct=%u recent_samples=%u rate_per_min=%u samples=%u rec_last=%u",
          display_command_health_label(prev_state),
          display_command_health_label(next_state),
          tag,
          g_display_fb.command_fail_streak,
          success_pct,
          recent_success_pct,
          recent_samples,
          rate_per_min,
          g_display_fb.command_samples,
          g_display_fb.command_health_last_recovery_ticks);
}

static void display_framebuffer_build_footer_legend(char *legend_text, uint32_t cap) {
    uint32_t len = 0U;
    const char *cause =
        display_transition_cause_label((display_transition_cause_t)g_display_fb.transition_cause);
    const char *health =
        display_command_health_label((display_command_health_state_t)g_display_fb.command_health_state);
    const char *latency_status = display_command_latency_status_label();
    uint32_t recent_samples = 0U;
    uint32_t recent_pct = display_command_recent_success_pct(&recent_samples);

    if (cap == 0U) {
        return;
    }

    legend_text[0] = '\0';
    display_append_text(legend_text, &len, cap, "HUD I/R/O/E C:");
    display_append_text(legend_text, &len, cap, cause);
    display_append_text(legend_text, &len, cap, " H:");
    display_append_text(legend_text, &len, cap, health);
    display_append_text(legend_text, &len, cap, " R");
    if (recent_samples == 0U) {
        display_append_text(legend_text, &len, cap, "-");
    } else {
        display_append_u32(legend_text, &len, cap, recent_pct);
        display_append_text(legend_text, &len, cap, "%");
    }
    display_append_text(legend_text, &len, cap, " X");
    if (g_display_fb.command_health_recovery_count == 0U) {
        display_append_text(legend_text, &len, cap, "-");
    } else {
        display_append_compact_u32(legend_text,
                                   &len,
                                   cap,
                                   g_display_fb.command_health_last_recovery_ticks);
    }
    display_append_text(legend_text, &len, cap, " S:");
    display_append_text(legend_text, &len, cap, latency_status);
}

static void display_append_compact_u32(char *dst, uint32_t *len, uint32_t cap, uint32_t value) {
    if (value < 10000U) {
        display_append_u32(dst, len, cap, value);
        return;
    }

    display_append_u32(dst, len, cap, value / 1000U);
    display_append_text(dst, len, cap, "K");
}

static uint32_t display_command_latency_budget_ticks(uint32_t avg_ticks, uint32_t samples) {
    uint32_t hz = timer_frequency_hz();
    uint32_t min_ticks = DISPLAY_FB_COMMAND_SLOW_MIN_TICKS;
    uint32_t adaptive_ticks = 0U;

    if (hz != 0U) {
        uint32_t quarter_second_ticks = hz / 4U;
        if (quarter_second_ticks > min_ticks) {
            min_ticks = quarter_second_ticks;
        }
    }

    if (samples >= 3U && avg_ticks != 0U) {
        if (avg_ticks >
            (0xFFFFFFFFU - DISPLAY_FB_COMMAND_SLOW_MARGIN_TICKS) /
                DISPLAY_FB_COMMAND_SLOW_MULTIPLIER) {
            adaptive_ticks = 0xFFFFFFFFU;
        } else {
            adaptive_ticks = (avg_ticks * DISPLAY_FB_COMMAND_SLOW_MULTIPLIER) +
                             DISPLAY_FB_COMMAND_SLOW_MARGIN_TICKS;
        }
    }

    if (adaptive_ticks < min_ticks) {
        adaptive_ticks = min_ticks;
    }
    return adaptive_ticks;
}

static const char *display_command_latency_status_label(void) {
    uint32_t budget_ticks =
        display_command_latency_budget_ticks(g_display_fb.command_avg_ticks,
                                             g_display_fb.command_samples);

    if (g_display_fb.command_active != 0U) {
        uint32_t running_ticks = display_ticks32() - g_display_fb.command_start_ticks;
        if (running_ticks >= budget_ticks) {
            return "RUN!";
        }
        return "RUN";
    }
    if (g_display_fb.command_samples == 0U) {
        return "-";
    }
    return g_display_fb.command_last_slow != 0U ? "SLOW" : "OK";
}

static uint32_t display_command_rate_per_min(void) {
    uint32_t hz = timer_frequency_hz();
    uint32_t count = g_display_fb.command_finish_count;
    uint32_t newest_idx;
    uint32_t oldest_idx;
    uint32_t newest_ticks;
    uint32_t oldest_ticks;
    uint32_t delta_ticks;
    uint32_t per_min_base;
    uint32_t numerator;

    if (hz == 0U || count < 2U) {
        return 0U;
    }
    newest_idx =
        (g_display_fb.command_finish_head + DISPLAY_FB_COMMAND_RATE_WINDOW - 1U) %
        DISPLAY_FB_COMMAND_RATE_WINDOW;
    oldest_idx =
        (g_display_fb.command_finish_head + DISPLAY_FB_COMMAND_RATE_WINDOW - count) %
        DISPLAY_FB_COMMAND_RATE_WINDOW;
    newest_ticks = g_display_fb.command_finish_ticks[newest_idx];
    oldest_ticks = g_display_fb.command_finish_ticks[oldest_idx];
    delta_ticks = newest_ticks - oldest_ticks;
    if (delta_ticks == 0U) {
        return 0U;
    }
    if (hz > (0xFFFFFFFFU / 60U)) {
        return 0xFFFFFFFFU;
    }
    per_min_base = hz * 60U;
    if ((count - 1U) > (0xFFFFFFFFU / per_min_base)) {
        numerator = 0xFFFFFFFFU;
    } else {
        numerator = (count - 1U) * per_min_base;
    }
    return (numerator + (delta_ticks / 2U)) / delta_ticks;
}

static void display_command_recent_push(int success) {
    g_display_fb.command_recent_outcomes[g_display_fb.command_recent_head] = success != 0 ? 1U : 0U;
    g_display_fb.command_recent_head =
        (g_display_fb.command_recent_head + 1U) % DISPLAY_FB_COMMAND_RATE_WINDOW;
    if (g_display_fb.command_recent_count < DISPLAY_FB_COMMAND_RATE_WINDOW) {
        g_display_fb.command_recent_count++;
    }
}

static uint32_t display_command_recent_success_pct(uint32_t *out_samples) {
    uint32_t count = g_display_fb.command_recent_count;
    uint32_t ok = 0U;

    if (out_samples) {
        *out_samples = count;
    }
    if (count == 0U) {
        return 0U;
    }

    for (uint32_t i = 0U; i < count; i++) {
        uint32_t idx =
            (g_display_fb.command_recent_head + DISPLAY_FB_COMMAND_RATE_WINDOW - count + i) %
            DISPLAY_FB_COMMAND_RATE_WINDOW;
        if (g_display_fb.command_recent_outcomes[idx] != 0U) {
            ok++;
        }
    }
    return (ok * 100U) / count;
}

static void display_command_health_account_dwell(uint32_t now_ticks,
                                                 display_command_health_state_t state) {
    uint32_t since = g_display_fb.command_health_state_since_ticks;
    uint32_t delta;

    if (since == 0U) {
        return;
    }
    delta = now_ticks - since;
    if (delta == 0U) {
        return;
    }
    if (state == DISPLAY_COMMAND_HEALTH_WARN) {
        if (g_display_fb.command_health_warn_dwell_ticks > 0xFFFFFFFFU - delta) {
            g_display_fb.command_health_warn_dwell_ticks = 0xFFFFFFFFU;
        } else {
            g_display_fb.command_health_warn_dwell_ticks += delta;
        }
    } else if (state == DISPLAY_COMMAND_HEALTH_DEGRADED) {
        if (g_display_fb.command_health_degr_dwell_ticks > 0xFFFFFFFFU - delta) {
            g_display_fb.command_health_degr_dwell_ticks = 0xFFFFFFFFU;
        } else {
            g_display_fb.command_health_degr_dwell_ticks += delta;
        }
    }
}

static void display_framebuffer_build_footer_latency(char *latency_text, uint32_t cap) {
    uint32_t len = 0U;
    uint32_t budget_ticks = g_display_fb.command_last_budget_ticks;

    if (budget_ticks == 0U || g_display_fb.command_active != 0U) {
        budget_ticks = display_command_latency_budget_ticks(g_display_fb.command_avg_ticks,
                                                            g_display_fb.command_samples);
    }

    if (cap == 0U) {
        return;
    }
    latency_text[0] = '\0';

    display_append_text(latency_text, &len, cap, "T ");
    if (g_display_fb.command_samples == 0U) {
        display_append_text(latency_text, &len, cap, "L- A- P- B- O-");
    } else {
        display_append_text(latency_text, &len, cap, "L");
        display_append_compact_u32(latency_text, &len, cap, g_display_fb.command_last_ticks);
        display_append_text(latency_text, &len, cap, " A");
        display_append_compact_u32(latency_text, &len, cap, g_display_fb.command_avg_ticks);
        display_append_text(latency_text, &len, cap, " P");
        display_append_compact_u32(latency_text, &len, cap, g_display_fb.command_peak_ticks);
        display_append_text(latency_text, &len, cap, " B");
        display_append_compact_u32(latency_text, &len, cap, budget_ticks);
        display_append_text(latency_text, &len, cap, " O");
        display_append_text(latency_text,
                            &len,
                            cap,
                            g_display_fb.command_last_success != 0U ? "OK" : "ER");
    }

    if (g_display_fb.command_active != 0U) {
        uint32_t running_ticks = display_ticks32() - g_display_fb.command_start_ticks;
        display_append_text(latency_text, &len, cap, " R");
        display_append_compact_u32(latency_text, &len, cap, running_ticks);
    }
    display_append_text(latency_text, &len, cap, " X");
    if (g_display_fb.command_health_recovery_count == 0U) {
        display_append_text(latency_text, &len, cap, "-");
    } else {
        display_append_compact_u32(latency_text,
                                   &len,
                                   cap,
                                   g_display_fb.command_health_last_recovery_ticks);
    }
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
    uint32_t budget_ticks;
    uint32_t prev_avg_ticks;
    uint32_t sample_count;
    uint32_t success_pct = 0U;
    uint32_t recent_success_pct = 0U;
    uint32_t recent_samples = 0U;
    uint32_t rate_per_min;
    uint32_t total_commands;
    char tag;
    int latency_slow;

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

    prev_avg_ticks = g_display_fb.command_avg_ticks;
    sample_count = g_display_fb.command_samples;
    budget_ticks = display_command_latency_budget_ticks(prev_avg_ticks, sample_count);
    latency_slow = duration_ticks >= budget_ticks;
    g_display_fb.command_last_ticks = duration_ticks;
    g_display_fb.command_last_success = success != 0U ? 1U : 0U;
    g_display_fb.command_last_slow = latency_slow != 0 ? 1U : 0U;
    g_display_fb.command_last_budget_ticks = budget_ticks;
    if (duration_ticks > g_display_fb.command_peak_ticks) {
        g_display_fb.command_peak_ticks = duration_ticks;
    }
    if (sample_count == 0U) {
        g_display_fb.command_avg_ticks = duration_ticks;
    } else {
        g_display_fb.command_avg_ticks = ((prev_avg_ticks * 3U) + duration_ticks) / 4U;
    }
    if (g_display_fb.command_samples != 0xFFFFFFFFU) {
        g_display_fb.command_samples++;
    }
    if (success != 0) {
        if (g_display_fb.command_ok_count != 0xFFFFFFFFU) {
            g_display_fb.command_ok_count++;
        }
        g_display_fb.command_fail_streak = 0U;
    } else {
        if (g_display_fb.command_fail_count != 0xFFFFFFFFU) {
            g_display_fb.command_fail_count++;
        }
        if (g_display_fb.command_fail_streak != 0xFFFFFFFFU) {
            g_display_fb.command_fail_streak++;
        }
        if (g_display_fb.command_fail_streak > g_display_fb.command_fail_streak_peak) {
            g_display_fb.command_fail_streak_peak = g_display_fb.command_fail_streak;
        }
    }
    if (latency_slow != 0) {
        if (g_display_fb.command_slow_count != 0xFFFFFFFFU) {
            g_display_fb.command_slow_count++;
        }
        if (g_display_fb.command_slow_streak != 0xFFFFFFFFU) {
            g_display_fb.command_slow_streak++;
        }
        if (g_display_fb.command_slow_streak > g_display_fb.command_slow_streak_peak) {
            g_display_fb.command_slow_streak_peak = g_display_fb.command_slow_streak;
        }
    } else {
        g_display_fb.command_slow_streak = 0U;
    }
    g_display_fb.command_finish_ticks[g_display_fb.command_finish_head] = now_ticks;
    g_display_fb.command_finish_head =
        (g_display_fb.command_finish_head + 1U) % DISPLAY_FB_COMMAND_RATE_WINDOW;
    if (g_display_fb.command_finish_count < DISPLAY_FB_COMMAND_RATE_WINDOW) {
        g_display_fb.command_finish_count++;
    }
    display_command_recent_push(success);
    recent_success_pct = display_command_recent_success_pct(&recent_samples);
    rate_per_min = display_command_rate_per_min();
    total_commands = g_display_fb.command_ok_count + g_display_fb.command_fail_count;
    if (total_commands != 0U) {
        if (total_commands < 100U) {
            success_pct = (g_display_fb.command_ok_count * 100U) / total_commands;
        } else {
            success_pct = g_display_fb.command_ok_count / (total_commands / 100U);
            if (success_pct > 100U) {
                success_pct = 100U;
            }
        }
    }
    display_update_command_health_state(success_pct,
                                        recent_success_pct,
                                        recent_samples,
                                        rate_per_min,
                                        tag);

    KLOGI("display: cmd_latency tag=%c ticks=%u avg=%u peak=%u samples=%u outcome=%s cause=%s",
          tag,
          duration_ticks,
          g_display_fb.command_avg_ticks,
          g_display_fb.command_peak_ticks,
          g_display_fb.command_samples,
          success != 0 ? "ok" : "fail",
          display_transition_cause_label(cause));
    KLOGI("display: cmd_latency_budget tag=%c ticks=%u budget=%u slow=%u slow_count=%u slow_streak=%u peak_slow_streak=%u",
          tag,
          duration_ticks,
          budget_ticks,
          latency_slow != 0 ? 1U : 0U,
          g_display_fb.command_slow_count,
          g_display_fb.command_slow_streak,
          g_display_fb.command_slow_streak_peak);
    KLOGI("display: cmd_health tag=%c ok=%u fail=%u streak=%u peak_streak=%u success_pct=%u rate_per_min=%u",
          tag,
          g_display_fb.command_ok_count,
          g_display_fb.command_fail_count,
          g_display_fb.command_fail_streak,
          g_display_fb.command_fail_streak_peak,
          success_pct,
          rate_per_min);
    KLOGI("display: cmd_health_window tag=%c recent_pct=%u recent_samples=%u",
          tag,
          recent_success_pct,
          recent_samples);

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
    if (row >= DISPLAY_FB_FONT_SRC_H || col >= DISPLAY_FB_FONT_SRC_W) {
        return 0;
    }

    return (rows[row] & (1U << (DISPLAY_FB_FONT_SRC_W - 1U - col))) != 0U;
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
    uint32_t rate_per_min = display_command_rate_per_min();
    uint32_t budget_ticks = g_display_fb.command_last_budget_ticks;
    const char *health_label =
        display_command_health_label((display_command_health_state_t)g_display_fb.command_health_state);
    uint32_t recent_samples = 0U;
    uint32_t recent_pct = display_command_recent_success_pct(&recent_samples);

    if (cap == 0U) {
        return;
    }
    metrics_text[0] = '\0';

    if (budget_ticks == 0U || g_display_fb.command_active != 0U) {
        budget_ticks = display_command_latency_budget_ticks(g_display_fb.command_avg_ticks,
                                                            g_display_fb.command_samples);
    }

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
    display_append_text(metrics_text, &len, cap, "  CMD ");
    if (g_display_fb.command_samples == 0U) {
        display_append_text(metrics_text, &len, cap, "O-/E- S- Q- R- L- B- X- H-");
    } else {
        display_append_text(metrics_text, &len, cap, "O");
        display_append_compact_u32(metrics_text, &len, cap, g_display_fb.command_ok_count);
        display_append_text(metrics_text, &len, cap, "/E");
        display_append_compact_u32(metrics_text, &len, cap, g_display_fb.command_fail_count);
        display_append_text(metrics_text, &len, cap, " S");
        display_append_compact_u32(metrics_text, &len, cap, g_display_fb.command_fail_streak);
        display_append_text(metrics_text, &len, cap, " Q");
        display_append_compact_u32(metrics_text, &len, cap, rate_per_min);
        display_append_text(metrics_text, &len, cap, " R");
        if (recent_samples == 0U) {
            display_append_text(metrics_text, &len, cap, "-");
        } else {
            display_append_u32(metrics_text, &len, cap, recent_pct);
            display_append_text(metrics_text, &len, cap, "%");
        }
        display_append_text(metrics_text, &len, cap, " L");
        display_append_compact_u32(metrics_text, &len, cap, g_display_fb.command_slow_count);
        display_append_text(metrics_text, &len, cap, " B");
        display_append_compact_u32(metrics_text, &len, cap, budget_ticks);
        display_append_text(metrics_text, &len, cap, " X");
        if (g_display_fb.command_health_recovery_count == 0U) {
            display_append_text(metrics_text, &len, cap, "-");
        } else {
            display_append_compact_u32(metrics_text,
                                       &len,
                                       cap,
                                       g_display_fb.command_health_last_recovery_ticks);
        }
        display_append_text(metrics_text, &len, cap, " H");
        display_append_text(metrics_text, &len, cap, health_label);
    }
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
    char legend_text[80];
    char latency_text[40];

    display_framebuffer_fill_rect_packed(
        0U,
        footer_top,
        g_display_fb.info.width,
        DISPLAY_FB_CHAR_H,
        footer_bg);
    display_framebuffer_build_footer_legend(legend_text, sizeof(legend_text));
    display_framebuffer_build_footer_latency(latency_text, sizeof(latency_text));
    if (latency_text[0] != '\0') {
        uint32_t legend_len = 0U;
        while (legend_text[legend_len] != '\0' && legend_len + 1U < sizeof(legend_text)) {
            legend_len++;
        }
        if (legend_len + 2U < sizeof(legend_text)) {
            legend_text[legend_len++] = ' ';
            legend_text[legend_len] = '\0';
            display_append_text(legend_text, &legend_len, (uint32_t)sizeof(legend_text), latency_text);
        }
    }
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
    g_display_fb.command_last_ticks = 0U;
    g_display_fb.command_avg_ticks = 0U;
    g_display_fb.command_peak_ticks = 0U;
    g_display_fb.command_samples = 0U;
    g_display_fb.command_last_success = 0U;
    display_reset_command_health();
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
    g_display_fb.command_last_ticks = 0U;
    g_display_fb.command_avg_ticks = 0U;
    g_display_fb.command_peak_ticks = 0U;
    g_display_fb.command_samples = 0U;
    g_display_fb.command_last_success = 0U;
    display_reset_command_health();
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
    KLOGI("display: gui_state_hash=%x profile=fb-shell-v5", gui_hash);
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
