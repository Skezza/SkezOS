#include "display.h"

#include <stdint.h>

#include "kerrno.h"
#include "klog.h"
#include "kmalloc.h"
#include "memory_layout.h"
#include "memmap.h"
#include "paging.h"
#include "pmm.h"
#include "sched.h"
#include "syscall_abi.h"
#include "timer.h"
#include "uaccess.h"
#include "utils.h"
#include "vfs.h"
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
    DISPLAY_SHELL_THEME_PLAIN = 0,
    DISPLAY_SHELL_THEME_ANSI = 1,
} display_shell_theme_t;

typedef enum {
    DISPLAY_TRANSITION_CAUSE_NONE = 0,
    DISPLAY_TRANSITION_CAUSE_PROMPT = 1,
    DISPLAY_TRANSITION_CAUSE_WAIT = 2,
    DISPLAY_TRANSITION_CAUSE_LAUNCH_FAIL = 3,
    DISPLAY_TRANSITION_CAUSE_SHELL_EXIT = 4,
    DISPLAY_TRANSITION_CAUSE_ROLLOVER = 5,
    DISPLAY_TRANSITION_CAUSE_HOLD_EXPIRE = 6,
} display_transition_cause_t;

typedef enum {
    DISPLAY_NAV_REGION_DOCK = 0,
    DISPLAY_NAV_REGION_PANEL = 1,
    DISPLAY_NAV_REGION_SIDEBAR = 2,
} display_nav_region_t;

typedef enum {
    DISPLAY_NAV_VIEW_TERM = 0,
    DISPLAY_NAV_VIEW_TASK = 1,
    DISPLAY_NAV_VIEW_FS = 2,
    DISPLAY_NAV_VIEW_LOG = 3,
    DISPLAY_NAV_VIEW_COUNT = 4,
} display_nav_view_t;

typedef enum {
    DISPLAY_SIDEBAR_CARD_WORKSPACE = 0,
    DISPLAY_SIDEBAR_CARD_HUD = 1,
    DISPLAY_SIDEBAR_CARD_HEALTH = 2,
    DISPLAY_SIDEBAR_CARD_TASKS = 3,
    DISPLAY_SIDEBAR_CARD_COUNT = 4,
} display_sidebar_card_t;

struct display_timeline_entry {
    uint16_t duration_ticks;
    uint8_t event;
    char tag;
};

struct display_glyph {
    char ch;
    uint8_t rows[7];
};

struct display_term_cell {
    char ch;
    uint8_t style;
};

struct display_term_row {
    struct display_term_cell cells[128];
};

struct display_framebuffer_state {
    volatile uint8_t *base;
    uint32_t span_bytes;
    uint32_t bytes_per_pixel;
    uint32_t dock_left_px;
    uint32_t dock_top_px;
    uint32_t dock_width_px;
    uint32_t dock_height_px;
    uint32_t sidebar_left_px;
    uint32_t sidebar_top_px;
    uint32_t sidebar_width_px;
    uint32_t sidebar_height_px;
    uint32_t panel_left_px;
    uint32_t panel_top_px;
    uint32_t panel_width_px;
    uint32_t panel_height_px;
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
    uint8_t shell_theme;
    uint8_t shell_hud_enabled;
    uint8_t shell_bootshow_enabled;
    uint32_t shell_showcase_until_ticks;
    uint32_t shell_hud_jobs;
    uint32_t shell_hud_latency_ticks;
    char shell_hud_last_tag[16];
    char shell_hud_state[16];
    char shell_cwd[SYSCALL_CWD_MAX];
    uint8_t transition_cause;
    uint8_t nav_region;
    uint8_t nav_view;
    uint8_t nav_sidebar_card;
    struct display_term_row term_rows[64];
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

#define DISPLAY_GUI_MAX_WINDOWS 4U
#define DISPLAY_GUI_EVENT_QUEUE_CAP 32U
#define DISPLAY_GUI_TITLE_MAX 31U
#define DISPLAY_GUI_MAX_WIDTH 640U
#define DISPLAY_GUI_MAX_HEIGHT 360U
#define DISPLAY_GUI_FRAME_BORDER 1U
#define DISPLAY_GUI_TITLE_H 24U
#define DISPLAY_GUI_CLOSE_W 18U
#define DISPLAY_GUI_CLOSE_H 14U
#define DISPLAY_GUI_CLIENT_INSET 0U
#define DISPLAY_GUI_DESKTOP_TOP 28U
#define DISPLAY_GUI_DESKTOP_PAD 20U
#define DISPLAY_GUI_CURSOR_W 8U
#define DISPLAY_GUI_CURSOR_H 12U
#define DISPLAY_GUI_DIRTY_MAX 12U

struct display_gui_dirty_rect {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
};

struct display_gui_window {
    int used;
    int window_id;
    int owner_pid;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    char title[DISPLAY_GUI_TITLE_MAX + 1U];
    uint32_t *surface;
    struct syscall_gui_event events[DISPLAY_GUI_EVENT_QUEUE_CAP];
    uint32_t event_head;
    uint32_t event_count;
};

struct display_gui_state {
    int active;
    int focused_index;
    int drag_index;
    int next_window_id;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t buttons;
    uint32_t drag_grab_x;
    uint32_t drag_grab_y;
    uint32_t drag_window_x;
    uint32_t drag_window_y;
    uint32_t z_order[DISPLAY_GUI_MAX_WINDOWS];
    uint32_t z_count;
    int clip_active;
    struct display_gui_dirty_rect clip;
    struct display_gui_dirty_rect dirty[DISPLAY_GUI_DIRTY_MAX];
    uint32_t dirty_count;
    struct display_gui_window windows[DISPLAY_GUI_MAX_WINDOWS];
};

static struct display_gui_state g_display_gui;

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
#define DISPLAY_FB_STAGE_MARGIN_X 14U
#define DISPLAY_FB_STAGE_MARGIN_Y 10U
#define DISPLAY_FB_STAGE_GAP 12U
#define DISPLAY_FB_DOCK_WIDTH 72U
#define DISPLAY_FB_SIDEBAR_WIDTH 210U
#define DISPLAY_FB_WINDOW_TITLE_H 17U
#define DISPLAY_FB_WINDOW_INSET_X 10U
#define DISPLAY_FB_WINDOW_INSET_Y 8U
#define DISPLAY_FB_SIDEBAR_CARD_GAP 10U
#define DISPLAY_FB_SIDEBAR_CARD_H 76U
#define DISPLAY_FB_PROMPT_TEXT_OFFSET_COLS 7U
#define DISPLAY_FB_PROMPT_STATUS_RESERVE_COLS 0U
#define DISPLAY_FB_PROMPT_HINT_HOLD_TICKS 120U
#define DISPLAY_FB_TIMELINE_CAP 24U
#define DISPLAY_FB_TIMELINE_RAIL_INSET 4U
#define DISPLAY_FB_FOOTER_RAIL_LEFT_COLS 33U
#define DISPLAY_FB_TERM_MODEL_ROWS_MAX 64U
#define DISPLAY_FB_TERM_MODEL_COLS_MAX 128U

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
static void display_framebuffer_draw_chip_packed(uint32_t x,
                                                 uint32_t y,
                                                 const char *text,
                                                 uint32_t fg_pixel,
                                                 uint32_t bg_pixel);
static uint32_t display_framebuffer_draw_chip_right_packed(uint32_t right_x,
                                                           uint32_t y,
                                                           const char *text,
                                                           uint32_t fg_pixel,
                                                           uint32_t bg_pixel);
static void display_framebuffer_draw_desktop_chrome(void);
static void display_framebuffer_redraw_chrome(void);
static void display_framebuffer_draw_sidebar(void);
static void display_framebuffer_draw_active_panel_body(void);
static void display_append_text(char *dst, uint32_t *len, uint32_t cap, const char *text);
static void display_append_u32(char *dst, uint32_t *len, uint32_t cap, uint32_t value);
static void display_append_compact_u32(char *dst, uint32_t *len, uint32_t cap, uint32_t value);
static void display_append_mib_value(char *dst, uint32_t *len, uint32_t cap, uint32_t frame_count);
static uint32_t display_string_length(const char *text);
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
static const char *display_nav_region_label(display_nav_region_t region);
static const char *display_nav_view_label(display_nav_view_t view);
static const char *display_sidebar_card_label(display_sidebar_card_t card);
static const char *display_nav_window_label(display_nav_view_t view);
static uint32_t display_nav_view_accent(int showcase, int hot_theme, display_nav_view_t view);
static void display_framebuffer_term_model_clear_all(void);
static void display_framebuffer_term_model_clear_row(uint32_t row);
static void display_framebuffer_term_model_shift_up(void);
static void display_framebuffer_term_model_set_cell(uint32_t row,
                                                    uint32_t col,
                                                    char ch,
                                                    display_line_style_t style);
static void display_framebuffer_term_model_capture_prompt_cwd(const char *text, uint32_t len);
static void display_framebuffer_term_model_sync_current_line(void);
static void display_framebuffer_repaint_term_surface(void);
static void display_framebuffer_draw_task_panel(void);
static void display_framebuffer_draw_fs_panel(void);
static void display_framebuffer_draw_log_panel(void);
static void display_framebuffer_build_footer_legend(char *legend_text, uint32_t cap);
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
static int display_find_text(const char *text, uint32_t len, const char *needle, uint32_t *out_idx);
static void display_copy_token(char *dst, uint32_t cap, const char *src, uint32_t len);
static int display_parse_u32_token(const char *text, uint32_t len, uint32_t *out_value);
static int display_parse_shell_hud_line(const char *text, uint32_t len);
static int display_observe_shell_gui_line(const char *text, uint32_t len);
static int display_shell_showcase_active(void);
static const char *display_task_state_short(uint32_t state);
static int display_handle_navigation_internal(display_nav_key_t key);
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

static void display_framebuffer_term_model_clear_row(uint32_t row) {
    uint32_t col;

    if (row >= DISPLAY_FB_TERM_MODEL_ROWS_MAX) {
        return;
    }
    for (col = 0U; col < DISPLAY_FB_TERM_MODEL_COLS_MAX; col++) {
        g_display_fb.term_rows[row].cells[col].ch = ' ';
        g_display_fb.term_rows[row].cells[col].style = DISPLAY_LINE_STYLE_NORMAL;
    }
}

static void display_framebuffer_term_model_clear_all(void) {
    for (uint32_t row = 0U; row < DISPLAY_FB_TERM_MODEL_ROWS_MAX; row++) {
        display_framebuffer_term_model_clear_row(row);
    }
}

static void display_framebuffer_term_model_shift_up(void) {
    uint32_t row;
    uint32_t col;

    if (g_display_fb.scroll_rows == 0U || g_display_fb.scroll_rows > DISPLAY_FB_TERM_MODEL_ROWS_MAX) {
        return;
    }
    for (row = 0U; row + 1U < g_display_fb.scroll_rows; row++) {
        for (col = 0U; col < DISPLAY_FB_TERM_MODEL_COLS_MAX; col++) {
            g_display_fb.term_rows[row].cells[col] = g_display_fb.term_rows[row + 1U].cells[col];
        }
    }
    display_framebuffer_term_model_clear_row(g_display_fb.scroll_rows - 1U);
}

static void display_framebuffer_term_model_set_cell(uint32_t row,
                                                    uint32_t col,
                                                    char ch,
                                                    display_line_style_t style) {
    if (row >= DISPLAY_FB_TERM_MODEL_ROWS_MAX || col >= DISPLAY_FB_TERM_MODEL_COLS_MAX) {
        return;
    }
    if ((uint8_t)ch < 0x20U) {
        ch = ' ';
    }
    g_display_fb.term_rows[row].cells[col].ch = ch;
    g_display_fb.term_rows[row].cells[col].style = (uint8_t)style;
}

static void display_framebuffer_term_model_capture_prompt_cwd(const char *text, uint32_t len) {
    uint32_t start = 4U;
    uint32_t end = 0U;
    uint32_t copy_len = 0U;

    if (len < 7U || !display_line_starts_with(text, len, "sh> ")) {
        return;
    }
    for (uint32_t i = start; i + 2U < len; i++) {
        if (text[i] == ' ' && (text[i + 1U] == '$' || text[i + 1U] == '#') && text[i + 2U] == ' ') {
            end = i;
            break;
        }
    }
    if (end <= start) {
        return;
    }
    copy_len = end - start;
    if (copy_len >= sizeof(g_display_fb.shell_cwd)) {
        copy_len = sizeof(g_display_fb.shell_cwd) - 1U;
    }
    for (uint32_t i = 0U; i < copy_len; i++) {
        g_display_fb.shell_cwd[i] = text[start + i];
    }
    g_display_fb.shell_cwd[copy_len] = '\0';
}

static void display_framebuffer_term_model_sync_current_line(void) {
    uint32_t render_len = g_display_fb.line_len;
    uint32_t start_idx = 0U;

    if (g_display_fb.text_cols > DISPLAY_FB_TERM_MODEL_COLS_MAX ||
        g_display_fb.text_rows > DISPLAY_FB_TERM_MODEL_ROWS_MAX) {
        return;
    }

    if (g_display_fb.line_style == DISPLAY_LINE_STYLE_PROMPT) {
        uint32_t visible_cols = display_framebuffer_prompt_visible_cols();
        uint32_t row = g_display_fb.scroll_rows;
        uint32_t col = 0U;

        if (g_display_fb.cursor_row < g_display_fb.scroll_rows) {
            display_framebuffer_term_model_clear_row(g_display_fb.cursor_row);
        }
        if (row >= DISPLAY_FB_TERM_MODEL_ROWS_MAX) {
            return;
        }
        display_framebuffer_term_model_clear_row(row);
        display_framebuffer_term_model_capture_prompt_cwd(g_display_fb.line_text, g_display_fb.line_len);
        if (g_display_fb.line_len > visible_cols) {
            if (visible_cols > 1U) {
                render_len = visible_cols - 1U;
                start_idx = g_display_fb.line_len - render_len;
                display_framebuffer_term_model_set_cell(row, 0U, '<', DISPLAY_LINE_STYLE_PROMPT);
                col = 1U;
            } else {
                render_len = 0U;
                start_idx = g_display_fb.line_len;
            }
        }
        for (uint32_t i = 0U; i < render_len && col < DISPLAY_FB_TERM_MODEL_COLS_MAX; i++, col++) {
            display_framebuffer_term_model_set_cell(row,
                                                    col,
                                                    g_display_fb.line_text[start_idx + i],
                                                    DISPLAY_LINE_STYLE_PROMPT);
        }
        return;
    }

    if (g_display_fb.cursor_row >= DISPLAY_FB_TERM_MODEL_ROWS_MAX) {
        return;
    }
    display_framebuffer_term_model_clear_row(g_display_fb.cursor_row);
    if (g_display_fb.scroll_rows < DISPLAY_FB_TERM_MODEL_ROWS_MAX) {
        display_framebuffer_term_model_clear_row(g_display_fb.scroll_rows);
    }
    if (render_len > g_display_fb.text_cols) {
        render_len = g_display_fb.text_cols;
    }
    for (uint32_t i = 0U; i < render_len && i < DISPLAY_FB_TERM_MODEL_COLS_MAX; i++) {
        display_framebuffer_term_model_set_cell(g_display_fb.cursor_row,
                                                i,
                                                g_display_fb.line_text[i],
                                                g_display_fb.line_style);
    }
}

static void display_framebuffer_clear_scroll_row(uint32_t row) {
    display_framebuffer_term_model_clear_row(row);
    if ((display_nav_view_t)g_display_fb.nav_view != DISPLAY_NAV_VIEW_TERM) {
        return;
    }
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
    int hot_theme = g_display_fb.shell_theme == DISPLAY_SHELL_THEME_ANSI;
    int showcase = display_shell_showcase_active();
    uint32_t prompt_top = g_display_fb.content_top_px + (g_display_fb.scroll_rows * DISPLAY_FB_CHAR_H);
    uint32_t prompt_fg = hot_theme ? display_framebuffer_pack_rgb(228U, 247U, 255U)
                                   : display_framebuffer_pack_rgb(248U, 242U, 214U);
    uint32_t prompt_bg =
        showcase ? display_framebuffer_pack_rgb(24U, 14U, 34U)
                 : (hot_theme ? display_framebuffer_pack_rgb(8U, 24U, 36U)
                              : display_framebuffer_pack_rgb(12U, 22U, 42U));
    uint32_t prompt_well_bg =
        showcase ? display_framebuffer_pack_rgb(18U, 10U, 28U)
                 : (hot_theme ? display_framebuffer_pack_rgb(6U, 16U, 30U)
                              : display_framebuffer_pack_rgb(6U, 14U, 30U));
    uint32_t prompt_accent =
        showcase ? display_framebuffer_pack_rgb(255U, 122U, 96U)
                 : (hot_theme ? display_framebuffer_pack_rgb(102U, 226U, 255U)
                              : display_framebuffer_pack_rgb(86U, 178U, 244U));
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

    display_framebuffer_term_model_sync_current_line();
    if ((display_nav_view_t)g_display_fb.nav_view != DISPLAY_NAV_VIEW_TERM) {
        return;
    }

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

    hash = display_hash_u32(hash, 0x46425336U); /* "FBS6" */
    hash = display_hash_u32(hash, g_display_fb.info.width);
    hash = display_hash_u32(hash, g_display_fb.info.height);
    hash = display_hash_u32(hash, g_display_fb.info.pitch);
    hash = display_hash_u32(hash, (uint32_t)g_display_fb.info.bpp);
    hash = display_hash_u32(hash, g_display_fb.bytes_per_pixel);
    hash = display_hash_u32(hash, g_display_fb.dock_left_px);
    hash = display_hash_u32(hash, g_display_fb.dock_top_px);
    hash = display_hash_u32(hash, g_display_fb.dock_width_px);
    hash = display_hash_u32(hash, g_display_fb.dock_height_px);
    hash = display_hash_u32(hash, g_display_fb.sidebar_left_px);
    hash = display_hash_u32(hash, g_display_fb.sidebar_top_px);
    hash = display_hash_u32(hash, g_display_fb.sidebar_width_px);
    hash = display_hash_u32(hash, g_display_fb.sidebar_height_px);
    hash = display_hash_u32(hash, g_display_fb.panel_left_px);
    hash = display_hash_u32(hash, g_display_fb.panel_top_px);
    hash = display_hash_u32(hash, g_display_fb.panel_width_px);
    hash = display_hash_u32(hash, g_display_fb.panel_height_px);
    hash = display_hash_u32(hash, g_display_fb.text_cols);
    hash = display_hash_u32(hash, g_display_fb.text_rows);
    hash = display_hash_u32(hash, g_display_fb.scroll_rows);
    hash = display_hash_u32(hash, g_display_fb.content_left_px);
    hash = display_hash_u32(hash, g_display_fb.content_width_px);
    hash = display_hash_u32(hash, g_display_fb.content_top_px);
    hash = display_hash_u32(hash, g_display_fb.content_bottom_px);
    hash = display_hash_u32(hash, DISPLAY_FB_CHAR_W);
    hash = display_hash_u32(hash, DISPLAY_FB_CHAR_H);
    hash = display_hash_u32(hash, DISPLAY_FB_PANEL_BORDER);
    hash = display_hash_u32(hash, DISPLAY_FB_LINE_GUTTER_TOTAL);
    hash = display_hash_u32(hash, DISPLAY_FB_HEADER_ROWS);
    hash = display_hash_u32(hash, DISPLAY_FB_FOOTER_ROWS);
    hash = display_hash_u32(hash, DISPLAY_FB_STAGE_MARGIN_X);
    hash = display_hash_u32(hash, DISPLAY_FB_STAGE_MARGIN_Y);
    hash = display_hash_u32(hash, DISPLAY_FB_STAGE_GAP);
    hash = display_hash_u32(hash, DISPLAY_FB_DOCK_WIDTH);
    hash = display_hash_u32(hash, DISPLAY_FB_SIDEBAR_WIDTH);
    hash = display_hash_u32(hash, DISPLAY_FB_WINDOW_TITLE_H);
    hash = display_hash_u32(hash, DISPLAY_FB_WINDOW_INSET_X);
    hash = display_hash_u32(hash, DISPLAY_FB_WINDOW_INSET_Y);
    hash = display_hash_u32(hash, DISPLAY_FB_SIDEBAR_CARD_GAP);
    hash = display_hash_u32(hash, DISPLAY_FB_SIDEBAR_CARD_H);
    hash = display_hash_u32(hash, DISPLAY_FB_PROMPT_HINT_HOLD_TICKS);
    hash = display_hash_u32(hash, DISPLAY_FB_TIMELINE_CAP);
    hash = display_hash_u32(hash, DISPLAY_FB_TIMELINE_RAIL_INSET);
    hash = display_hash_u32(hash, DISPLAY_FB_FOOTER_RAIL_LEFT_COLS);

    /* Include style palette IDs so visual tune-ups are regression-gated. */
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(6U, 10U, 18U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(16U, 26U, 40U));
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
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(10U, 17U, 29U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(24U, 50U, 104U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(248U, 182U, 86U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(44U, 60U, 84U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(11U, 17U, 28U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(12U, 22U, 38U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(154U, 180U, 212U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(8U, 18U, 36U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(8U, 24U, 21U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(34U, 10U, 13U));
    hash = display_hash_u32(hash, display_framebuffer_pack_rgb(42U, 24U, 10U));

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
    const char *health =
        display_command_health_label((display_command_health_state_t)g_display_fb.command_health_state);

    if (cap == 0U) {
        return;
    }

    legend_text[0] = '\0';
    display_append_text(legend_text, &len, cap, "F ");
    display_append_text(legend_text,
                        &len,
                        cap,
                        display_nav_region_label((display_nav_region_t)g_display_fb.nav_region));
    display_append_text(legend_text, &len, cap, "  V ");
    display_append_text(legend_text,
                        &len,
                        cap,
                        display_nav_view_label((display_nav_view_t)g_display_fb.nav_view));
    display_append_text(legend_text, &len, cap, "  C ");
    display_append_text(legend_text,
                        &len,
                        cap,
                        display_sidebar_card_label((display_sidebar_card_t)g_display_fb.nav_sidebar_card));
    display_append_text(legend_text, &len, cap, "  H ");
    display_append_text(legend_text, &len, cap, health);
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

static uint32_t display_string_length(const char *text) {
    uint32_t len = 0U;

    while (text[len] != '\0') {
        len++;
    }
    return len;
}

static const char *display_nav_region_label(display_nav_region_t region) {
    switch (region) {
    case DISPLAY_NAV_REGION_DOCK:
        return "NAV";
    case DISPLAY_NAV_REGION_SIDEBAR:
        return "SIDE";
    case DISPLAY_NAV_REGION_PANEL:
    default:
        return "MAIN";
    }
}

static const char *display_nav_view_label(display_nav_view_t view) {
    switch (view) {
    case DISPLAY_NAV_VIEW_TASK:
        return "TASK";
    case DISPLAY_NAV_VIEW_FS:
        return "FS";
    case DISPLAY_NAV_VIEW_LOG:
        return "LOG";
    case DISPLAY_NAV_VIEW_TERM:
    default:
        return "TERM";
    }
}

static const char *display_sidebar_card_label(display_sidebar_card_t card) {
    switch (card) {
    case DISPLAY_SIDEBAR_CARD_HUD:
        return "HUD";
    case DISPLAY_SIDEBAR_CARD_HEALTH:
        return "HEALTH";
    case DISPLAY_SIDEBAR_CARD_TASKS:
        return "TASKS";
    case DISPLAY_SIDEBAR_CARD_WORKSPACE:
    default:
        return "WORK";
    }
}

static const char *display_nav_window_label(display_nav_view_t view) {
    switch (view) {
    case DISPLAY_NAV_VIEW_TASK:
        return "TASKS";
    case DISPLAY_NAV_VIEW_FS:
        return "FILES";
    case DISPLAY_NAV_VIEW_LOG:
        return "LOGS";
    case DISPLAY_NAV_VIEW_TERM:
    default:
        return "TERMINAL";
    }
}

static uint32_t display_nav_view_accent(int showcase, int hot_theme, display_nav_view_t view) {
    switch (view) {
    case DISPLAY_NAV_VIEW_TASK:
        return showcase ? display_framebuffer_pack_rgb(255U, 122U, 96U)
                        : display_framebuffer_pack_rgb(102U, 226U, 255U);
    case DISPLAY_NAV_VIEW_FS:
        return display_framebuffer_pack_rgb(248U, 188U, 96U);
    case DISPLAY_NAV_VIEW_LOG:
        return display_framebuffer_pack_rgb(228U, 96U, 86U);
    case DISPLAY_NAV_VIEW_TERM:
    default:
        return showcase ? display_framebuffer_pack_rgb(196U, 104U, 220U)
                        : (hot_theme ? display_framebuffer_pack_rgb(86U, 178U, 244U)
                                     : display_framebuffer_pack_rgb(64U, 146U, 220U));
    }
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

static int display_find_text(const char *text, uint32_t len, const char *needle, uint32_t *out_idx) {
    uint32_t nlen = display_string_length(needle);

    if (out_idx) {
        *out_idx = 0U;
    }
    if (nlen == 0U || len < nlen) {
        return 0;
    }
    for (uint32_t i = 0U; i + nlen <= len; i++) {
        uint32_t j = 0U;
        while (j < nlen && text[i + j] == needle[j]) {
            j++;
        }
        if (j == nlen) {
            if (out_idx) {
                *out_idx = i;
            }
            return 1;
        }
    }
    return 0;
}

static void display_copy_token(char *dst, uint32_t cap, const char *src, uint32_t len) {
    uint32_t out = 0U;

    if (!dst || cap == 0U) {
        return;
    }
    while (out + 1U < cap && out < len && src[out] != ' ') {
        dst[out] = src[out];
        out++;
    }
    dst[out] = '\0';
}

static int display_parse_u32_token(const char *text, uint32_t len, uint32_t *out_value) {
    uint32_t value = 0U;
    uint32_t i = 0U;

    if (out_value) {
        *out_value = 0U;
    }
    if (!text || len == 0U) {
        return 0;
    }
    while (i < len && text[i] >= '0' && text[i] <= '9') {
        uint32_t digit = (uint32_t)(text[i] - '0');

        if (value > 429496729U || (value == 429496729U && digit > 5U)) {
            value = 0xFFFFFFFFU;
        } else if (value != 0xFFFFFFFFU) {
            value = (value * 10U) + digit;
        }
        i++;
    }
    if (i == 0U) {
        return 0;
    }
    if (out_value) {
        *out_value = value;
    }
    return 1;
}

static int display_shell_showcase_active(void) {
    if (g_display_fb.shell_bootshow_enabled != 0U) {
        return 1;
    }
    if (g_display_fb.shell_showcase_until_ticks == 0U) {
        return 0;
    }
    if ((int32_t)(display_ticks32() - g_display_fb.shell_showcase_until_ticks) >= 0) {
        g_display_fb.shell_showcase_until_ticks = 0U;
        return 0;
    }
    return 1;
}

static const char *display_task_state_short(uint32_t state) {
    switch (state) {
    case SYSCALL_TASK_STATE_RUNNING:
        return "RUN";
    case SYSCALL_TASK_STATE_RUNNABLE:
        return "RDY";
    case SYSCALL_TASK_STATE_SLEEPING:
        return "SLP";
    case SYSCALL_TASK_STATE_WAIT_CHILD:
        return "WAI";
    case SYSCALL_TASK_STATE_ZOMBIE:
        return "EXT";
    default:
        return "---";
    }
}

static int display_handle_navigation_internal(display_nav_key_t key) {
    uint8_t prev_region = g_display_fb.nav_region;
    uint8_t prev_view = g_display_fb.nav_view;
    uint8_t prev_sidebar_card = g_display_fb.nav_sidebar_card;

    switch ((display_nav_region_t)g_display_fb.nav_region) {
    case DISPLAY_NAV_REGION_DOCK:
        if (key == DISPLAY_NAV_KEY_RIGHT) {
            g_display_fb.nav_region = DISPLAY_NAV_REGION_PANEL;
        } else if (key == DISPLAY_NAV_KEY_UP) {
            if (g_display_fb.nav_view == 0U) {
                g_display_fb.nav_view = DISPLAY_NAV_VIEW_COUNT - 1U;
            } else {
                g_display_fb.nav_view--;
            }
        } else if (key == DISPLAY_NAV_KEY_DOWN) {
            g_display_fb.nav_view = (g_display_fb.nav_view + 1U) % DISPLAY_NAV_VIEW_COUNT;
        }
        break;
    case DISPLAY_NAV_REGION_SIDEBAR:
        if (key == DISPLAY_NAV_KEY_LEFT) {
            g_display_fb.nav_region = DISPLAY_NAV_REGION_PANEL;
        } else if (key == DISPLAY_NAV_KEY_UP) {
            if (g_display_fb.nav_sidebar_card == 0U) {
                g_display_fb.nav_sidebar_card = DISPLAY_SIDEBAR_CARD_COUNT - 1U;
            } else {
                g_display_fb.nav_sidebar_card--;
            }
        } else if (key == DISPLAY_NAV_KEY_DOWN) {
            g_display_fb.nav_sidebar_card =
                (g_display_fb.nav_sidebar_card + 1U) % DISPLAY_SIDEBAR_CARD_COUNT;
        }
        break;
    case DISPLAY_NAV_REGION_PANEL:
    default:
        if (key == DISPLAY_NAV_KEY_LEFT) {
            g_display_fb.nav_region = DISPLAY_NAV_REGION_DOCK;
        } else if (key == DISPLAY_NAV_KEY_RIGHT) {
            g_display_fb.nav_region = DISPLAY_NAV_REGION_SIDEBAR;
        } else if (key == DISPLAY_NAV_KEY_UP) {
            if (g_display_fb.nav_view == 0U) {
                g_display_fb.nav_view = DISPLAY_NAV_VIEW_COUNT - 1U;
            } else {
                g_display_fb.nav_view--;
            }
        } else if (key == DISPLAY_NAV_KEY_DOWN) {
            g_display_fb.nav_view = (g_display_fb.nav_view + 1U) % DISPLAY_NAV_VIEW_COUNT;
        }
        break;
    }

    return g_display_fb.nav_region != prev_region ||
           g_display_fb.nav_view != prev_view ||
           g_display_fb.nav_sidebar_card != prev_sidebar_card;
}

static int display_parse_shell_hud_line(const char *text, uint32_t len) {
    uint32_t idx;
    uint32_t token_len;
    uint32_t jobs = 0U;
    uint32_t latency = 0U;

    if (!display_line_starts_with(text, len, "hud: jobs=")) {
        return 0;
    }
    if (!display_parse_u32_token(text + 10U, len - 10U, &jobs)) {
        return 0;
    }
    g_display_fb.shell_hud_jobs = jobs;

    if (display_find_text(text, len, "last=", &idx)) {
        idx += 5U;
        token_len = 0U;
        while (idx + token_len < len && text[idx + token_len] != ' ') {
            token_len++;
        }
        display_copy_token(g_display_fb.shell_hud_last_tag,
                           sizeof(g_display_fb.shell_hud_last_tag),
                           text + idx,
                           token_len);
    }
    if (display_find_text(text, len, "latency=", &idx)) {
        idx += 8U;
        (void)display_parse_u32_token(text + idx, len - idx, &latency);
        g_display_fb.shell_hud_latency_ticks = latency;
    }
    if (display_find_text(text, len, "state=", &idx)) {
        idx += 6U;
        token_len = 0U;
        while (idx + token_len < len && text[idx + token_len] != ' ') {
            token_len++;
        }
        display_copy_token(g_display_fb.shell_hud_state,
                           sizeof(g_display_fb.shell_hud_state),
                           text + idx,
                           token_len);
    }
    return 1;
}

static int display_observe_shell_gui_line(const char *text, uint32_t len) {
    if (display_line_starts_with(text, len, "set: theme=ansi")) {
        g_display_fb.shell_theme = DISPLAY_SHELL_THEME_ANSI;
        return 1;
    }
    if (display_line_starts_with(text, len, "set: theme=plain")) {
        g_display_fb.shell_theme = DISPLAY_SHELL_THEME_PLAIN;
        return 1;
    }
    if (display_line_starts_with(text, len, "set: hud=on")) {
        g_display_fb.shell_hud_enabled = 1U;
        return 1;
    }
    if (display_line_starts_with(text, len, "set: hud=off")) {
        g_display_fb.shell_hud_enabled = 0U;
        return 1;
    }
    if (display_line_starts_with(text, len, "bootshow: on")) {
        g_display_fb.shell_bootshow_enabled = 1U;
        g_display_fb.shell_hud_enabled = 1U;
        g_display_fb.shell_theme = DISPLAY_SHELL_THEME_ANSI;
        g_display_fb.shell_showcase_until_ticks = display_ticks32() + 600U;
        return 1;
    }
    if (display_line_starts_with(text, len, "bootshow: off")) {
        g_display_fb.shell_bootshow_enabled = 0U;
        g_display_fb.shell_showcase_until_ticks = 0U;
        return 1;
    }
    if (display_line_starts_with(text, len, "bootshow: showcase")) {
        g_display_fb.shell_showcase_until_ticks = display_ticks32() + 600U;
        return 1;
    }
    return display_parse_shell_hud_line(text, len);
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
    int chrome_dirty = 0;
    int footer_dirty = 0;
    int prompt_dirty = 0;

    if (len == 0U || text == 0) {
        return;
    }
    if (style == DISPLAY_LINE_STYLE_PROMPT) {
        if (display_parse_prompt_command(text, len, &tag)) {
            display_timeline_start_command(tag);
            chrome_dirty = 1;
            footer_dirty = 1;
            prompt_dirty = 1;
        }
    } else if (g_display_fb.command_active != 0U) {
        if (display_line_contains(text, len, "run: launch failed") ||
            display_line_contains(text, len, "run: redirect open failed") ||
            display_line_contains(text, len, "spawn failed")) {
            display_timeline_finish_active(0, DISPLAY_TRANSITION_CAUSE_LAUNCH_FAIL);
            chrome_dirty = 1;
            footer_dirty = 1;
            prompt_dirty = 1;
        } else if (display_parse_wait_exit_code(text, len, &exit_code)) {
            display_timeline_finish_active(exit_code == 0, DISPLAY_TRANSITION_CAUSE_WAIT);
            chrome_dirty = 1;
            footer_dirty = 1;
            prompt_dirty = 1;
        } else if (display_line_starts_with(text, len, "sh: exit")) {
            display_timeline_finish_active(1, DISPLAY_TRANSITION_CAUSE_SHELL_EXIT);
            chrome_dirty = 1;
            footer_dirty = 1;
            prompt_dirty = 1;
        }
    }
    if (style != DISPLAY_LINE_STYLE_PROMPT && display_observe_shell_gui_line(text, len)) {
        chrome_dirty = 1;
    }

    if (chrome_dirty != 0 && g_display_fb.ready != 0U) {
        display_framebuffer_redraw_chrome();
        return;
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

static uint32_t display_min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static uint32_t display_max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

static int display_gui_clip_rect(uint32_t *x,
                                 uint32_t *y,
                                 uint32_t *width,
                                 uint32_t *height) {
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;

    if (!x || !y || !width || !height || *width == 0U || *height == 0U) {
        return 0;
    }
    if (*x >= g_display_fb.info.width || *y >= g_display_fb.info.height) {
        return 0;
    }

    x0 = *x;
    y0 = *y;
    x1 = (*width > 0xFFFFFFFFU - x0) ? g_display_fb.info.width : x0 + *width;
    y1 = (*height > 0xFFFFFFFFU - y0) ? g_display_fb.info.height : y0 + *height;
    x1 = display_min_u32(x1, g_display_fb.info.width);
    y1 = display_min_u32(y1, g_display_fb.info.height);

    if (g_display_gui.active && g_display_gui.clip_active) {
        uint32_t clip_x1 = g_display_gui.clip.x + g_display_gui.clip.w;
        uint32_t clip_y1 = g_display_gui.clip.y + g_display_gui.clip.h;

        x0 = display_max_u32(x0, g_display_gui.clip.x);
        y0 = display_max_u32(y0, g_display_gui.clip.y);
        x1 = display_min_u32(x1, clip_x1);
        y1 = display_min_u32(y1, clip_y1);
    }

    if (x0 >= x1 || y0 >= y1) {
        return 0;
    }

    *x = x0;
    *y = y0;
    *width = x1 - x0;
    *height = y1 - y0;
    return 1;
}

static void display_framebuffer_fill_rect_packed(uint32_t x, uint32_t y,
                                                 uint32_t width, uint32_t height,
                                                 uint32_t pixel) {
    uint8_t pixel_bytes[4];

    if (!display_gui_clip_rect(&x, &y, &width, &height)) {
        return;
    }

    pixel_bytes[0] = (uint8_t)(pixel & 0xFFU);
    pixel_bytes[1] = (uint8_t)((pixel >> 8U) & 0xFFU);
    pixel_bytes[2] = (uint8_t)((pixel >> 16U) & 0xFFU);
    pixel_bytes[3] = (uint8_t)((pixel >> 24U) & 0xFFU);

    for (uint32_t py = 0; py < height; py++) {
        volatile uint8_t *row = g_display_fb.base +
                                ((y + py) * g_display_fb.info.pitch) +
                                (x * g_display_fb.bytes_per_pixel);

        if (g_display_fb.bytes_per_pixel == 4U) {
            volatile uint32_t *row32 = (volatile uint32_t *)(void *)row;

            for (uint32_t px = 0; px < width; px++) {
                row32[px] = pixel;
            }
            continue;
        }
        if (g_display_fb.bytes_per_pixel == 2U) {
            volatile uint16_t *row16 = (volatile uint16_t *)(void *)row;
            uint16_t pixel16 = (uint16_t)(pixel & 0xFFFFU);

            for (uint32_t px = 0; px < width; px++) {
                row16[px] = pixel16;
            }
            continue;
        }
        if (g_display_fb.bytes_per_pixel == 1U) {
            for (uint32_t px = 0; px < width; px++) {
                row[px] = pixel_bytes[0];
            }
            continue;
        }

        for (uint32_t px = 0; px < width; px++) {
            volatile uint8_t *dst = row + (px * g_display_fb.bytes_per_pixel);

            for (uint32_t i = 0; i < g_display_fb.bytes_per_pixel; i++) {
                dst[i] = pixel_bytes[i];
            }
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

static void display_framebuffer_draw_chip_packed(uint32_t x,
                                                 uint32_t y,
                                                 const char *text,
                                                 uint32_t fg_pixel,
                                                 uint32_t bg_pixel) {
    uint32_t chip_w;
    uint32_t text_w;

    if (!text || text[0] == '\0') {
        return;
    }

    text_w = display_string_length(text) * DISPLAY_FB_CHAR_W;
    chip_w = text_w + 14U;
    if (chip_w < (2U * DISPLAY_FB_CHAR_W)) {
        chip_w = 2U * DISPLAY_FB_CHAR_W;
    }

    display_framebuffer_fill_rect_packed(x, y, chip_w, DISPLAY_FB_CHAR_H - 4U, bg_pixel);
    display_framebuffer_draw_text_packed(x + 7U, y + 1U, text, fg_pixel, bg_pixel);
}

static uint32_t display_framebuffer_draw_chip_right_packed(uint32_t right_x,
                                                           uint32_t y,
                                                           const char *text,
                                                           uint32_t fg_pixel,
                                                           uint32_t bg_pixel) {
    uint32_t chip_w;
    uint32_t draw_x;

    if (!text || text[0] == '\0') {
        return 0U;
    }

    chip_w = (display_string_length(text) * DISPLAY_FB_CHAR_W) + 14U;
    if (chip_w < (2U * DISPLAY_FB_CHAR_W)) {
        chip_w = 2U * DISPLAY_FB_CHAR_W;
    }
    if (right_x < chip_w) {
        return 0U;
    }

    draw_x = right_x - chip_w;
    display_framebuffer_draw_chip_packed(draw_x, y, text, fg_pixel, bg_pixel);
    return chip_w;
}

static void display_framebuffer_draw_term_model_row(uint32_t row, int prompt_row) {
    uint32_t fg_pixel;
    uint32_t bg_pixel;
    uint32_t cell_y = g_display_fb.content_top_px + (row * DISPLAY_FB_CHAR_H);
    display_line_style_t style = DISPLAY_LINE_STYLE_NORMAL;

    if (row >= g_display_fb.text_rows || row >= DISPLAY_FB_TERM_MODEL_ROWS_MAX) {
        return;
    }

    if (prompt_row != 0) {
        uint32_t prompt_left =
            g_display_fb.content_left_px + (DISPLAY_FB_PROMPT_TEXT_OFFSET_COLS * DISPLAY_FB_CHAR_W);
        uint32_t prompt_width = display_framebuffer_prompt_visible_cols() * DISPLAY_FB_CHAR_W;

        display_framebuffer_line_colors(DISPLAY_LINE_STYLE_PROMPT, &fg_pixel, &bg_pixel);
        display_framebuffer_draw_prompt_strip_idle();
        display_framebuffer_draw_line_gutter(row, DISPLAY_LINE_STYLE_PROMPT);
        display_framebuffer_fill_rect_packed(prompt_left,
                                             cell_y,
                                             prompt_width,
                                             DISPLAY_FB_CHAR_H,
                                             bg_pixel);
        for (uint32_t col = 0U; col < display_framebuffer_prompt_visible_cols(); col++) {
            char ch = g_display_fb.term_rows[row].cells[col].ch;

            if (ch == ' ' || ch == '\0') {
                continue;
            }
            display_framebuffer_draw_glyph_packed(prompt_left + (col * DISPLAY_FB_CHAR_W),
                                                  cell_y,
                                                  ch,
                                                  fg_pixel,
                                                  bg_pixel);
        }
        return;
    }

    for (uint32_t col = 0U; col < g_display_fb.text_cols; col++) {
        char ch = g_display_fb.term_rows[row].cells[col].ch;

        if (ch != ' ' && ch != '\0') {
            style = (display_line_style_t)g_display_fb.term_rows[row].cells[col].style;
            break;
        }
    }
    display_framebuffer_line_colors(style, &fg_pixel, &bg_pixel);
    display_framebuffer_draw_line_gutter(row, style);
    display_framebuffer_fill_rect_packed(g_display_fb.content_left_px,
                                         cell_y,
                                         g_display_fb.content_width_px,
                                         DISPLAY_FB_CHAR_H,
                                         bg_pixel);
    for (uint32_t col = 0U; col < g_display_fb.text_cols; col++) {
        char ch = g_display_fb.term_rows[row].cells[col].ch;

        if (ch == ' ' || ch == '\0') {
            continue;
        }
        display_framebuffer_draw_glyph_packed(g_display_fb.content_left_px + (col * DISPLAY_FB_CHAR_W),
                                              cell_y,
                                              ch,
                                              fg_pixel,
                                              bg_pixel);
    }
}

static void display_framebuffer_repaint_term_surface(void) {
    for (uint32_t row = 0U; row < g_display_fb.scroll_rows; row++) {
        display_framebuffer_draw_term_model_row(row, 0);
    }
    display_framebuffer_draw_term_model_row(g_display_fb.scroll_rows, 1);
}

static void display_framebuffer_draw_panel_line(uint32_t row,
                                                const char *text,
                                                uint32_t fg_pixel,
                                                uint32_t bg_pixel,
                                                display_line_style_t marker_style) {
    uint32_t y;

    if (row >= g_display_fb.scroll_rows) {
        return;
    }
    y = g_display_fb.content_top_px + (row * DISPLAY_FB_CHAR_H);
    display_framebuffer_draw_line_gutter(row, marker_style);
    display_framebuffer_fill_rect_packed(g_display_fb.content_left_px,
                                         y,
                                         g_display_fb.content_width_px,
                                         DISPLAY_FB_CHAR_H,
                                         bg_pixel);
    display_framebuffer_draw_text_packed(g_display_fb.content_left_px + DISPLAY_FB_CHAR_W,
                                         y,
                                         text,
                                         fg_pixel,
                                         bg_pixel);
}

static void display_framebuffer_draw_task_panel(void) {
    struct syscall_task_snapshot_entry tasks[16];
    uint32_t task_count = 0U;
    uint32_t bg_pixel = display_framebuffer_pack_rgb(4U, 8U, 16U);
    uint32_t title_bg = display_framebuffer_pack_rgb(18U, 34U, 56U);
    uint32_t title_fg = display_framebuffer_pack_rgb(240U, 246U, 255U);
    uint32_t body_fg = display_framebuffer_pack_rgb(212U, 228U, 246U);
    uint32_t muted_fg = display_framebuffer_pack_rgb(154U, 180U, 212U);
    char line[80];
    uint32_t len;

    for (uint32_t row = 0U; row < g_display_fb.scroll_rows; row++) {
        display_framebuffer_draw_panel_line(row, "", body_fg, bg_pixel, DISPLAY_LINE_STYLE_TASK);
    }
    display_framebuffer_draw_panel_line(0U,
                                        "TASK BOARD PID MODE STATE NAME",
                                        title_fg,
                                        title_bg,
                                        DISPLAY_LINE_STYLE_TASK);
    if (!sched_is_started()) {
        display_framebuffer_draw_panel_line(2U,
                                            "scheduler offline: boot handoff not complete",
                                            body_fg,
                                            bg_pixel,
                                            DISPLAY_LINE_STYLE_TASK);
        return;
    }
    if (sched_collect_task_snapshot(tasks, 16U, &task_count) != 0) {
        display_framebuffer_draw_panel_line(2U,
                                            "task snapshot unavailable",
                                            body_fg,
                                            bg_pixel,
                                            DISPLAY_LINE_STYLE_TASK);
        return;
    }
    if (task_count == 0U) {
        display_framebuffer_draw_panel_line(2U,
                                            "no runnable work visible",
                                            body_fg,
                                            bg_pixel,
                                            DISPLAY_LINE_STYLE_TASK);
        return;
    }
    for (uint32_t i = 0U; i < task_count && (i + 2U) < g_display_fb.scroll_rows; i++) {
        len = 0U;
        line[0] = '\0';
        display_append_text(line, &len, sizeof(line), "P");
        display_append_u32(line, &len, sizeof(line), (uint32_t)tasks[i].pid);
        display_append_text(line,
                            &len,
                            sizeof(line),
                            (tasks[i].flags & SYSCALL_TASK_FLAG_USER) != 0U ? " USR " : " KRN ");
        display_append_text(line,
                            &len,
                            sizeof(line),
                            display_task_state_short(tasks[i].state));
        display_append_text(line, &len, sizeof(line), " ");
        display_append_text(line,
                            &len,
                            sizeof(line),
                            tasks[i].name[0] != '\0' ? tasks[i].name : "-");
        display_framebuffer_draw_panel_line(i + 2U,
                                            line,
                                            i == 0U ? body_fg : muted_fg,
                                            bg_pixel,
                                            DISPLAY_LINE_STYLE_TASK);
    }
}

static void display_framebuffer_draw_fs_panel(void) {
    struct vfs_dir_entry entries[16];
    uint32_t count = 0U;
    uint32_t bg_pixel = display_framebuffer_pack_rgb(5U, 8U, 14U);
    uint32_t title_bg = display_framebuffer_pack_rgb(52U, 34U, 10U);
    uint32_t title_fg = display_framebuffer_pack_rgb(255U, 242U, 214U);
    uint32_t body_fg = display_framebuffer_pack_rgb(244U, 232U, 198U);
    uint32_t muted_fg = display_framebuffer_pack_rgb(220U, 196U, 150U);
    char line[80];
    uint32_t len;
    const char *cwd = g_display_fb.shell_cwd[0] != '\0' ? g_display_fb.shell_cwd : "/";
    int rc;

    for (uint32_t row = 0U; row < g_display_fb.scroll_rows; row++) {
        display_framebuffer_draw_panel_line(row, "", body_fg, bg_pixel, DISPLAY_LINE_STYLE_TASK);
    }

    len = 0U;
    line[0] = '\0';
    display_append_text(line, &len, sizeof(line), "FILESYSTEM ");
    display_append_text(line, &len, sizeof(line), cwd);
    display_framebuffer_draw_panel_line(0U, line, title_fg, title_bg, DISPLAY_LINE_STYLE_TASK);

    rc = vfs_list_dir(cwd, entries, 16U, &count);
    if (rc != 0) {
        len = 0U;
        line[0] = '\0';
        display_append_text(line, &len, sizeof(line), "list failed rc=");
        if (rc < 0) {
            display_append_text(line, &len, sizeof(line), "-");
            display_append_u32(line, &len, sizeof(line), (uint32_t)(-rc));
        } else {
            display_append_u32(line, &len, sizeof(line), (uint32_t)rc);
        }
        display_framebuffer_draw_panel_line(2U, line, body_fg, bg_pixel, DISPLAY_LINE_STYLE_TASK);
        return;
    }
    if (count == 0U) {
        display_framebuffer_draw_panel_line(2U,
                                            "directory empty",
                                            body_fg,
                                            bg_pixel,
                                            DISPLAY_LINE_STYLE_TASK);
        return;
    }
    for (uint32_t i = 0U; i < count && (i + 2U) < g_display_fb.scroll_rows; i++) {
        len = 0U;
        line[0] = '\0';
        if (entries[i].type == VFS_NODE_DIR) {
            display_append_text(line, &len, sizeof(line), "DIR ");
        } else if (entries[i].type == VFS_NODE_CHARDEV) {
            display_append_text(line, &len, sizeof(line), "CHR ");
        } else {
            display_append_text(line, &len, sizeof(line), "FIL ");
        }
        display_append_text(line, &len, sizeof(line), entries[i].name);
        if (entries[i].type == VFS_NODE_DIR) {
            display_append_text(line, &len, sizeof(line), "/");
        }
        display_framebuffer_draw_panel_line(i + 2U,
                                            line,
                                            i == 0U ? body_fg : muted_fg,
                                            bg_pixel,
                                            DISPLAY_LINE_STYLE_TASK);
    }
}

static void display_framebuffer_draw_log_panel(void) {
    uint32_t bg_pixel = display_framebuffer_pack_rgb(10U, 6U, 12U);
    uint32_t title_bg = display_framebuffer_pack_rgb(54U, 18U, 26U);
    uint32_t title_fg = display_framebuffer_pack_rgb(255U, 236U, 238U);
    uint32_t body_fg = display_framebuffer_pack_rgb(244U, 224U, 228U);
    uint32_t muted_fg = display_framebuffer_pack_rgb(214U, 174U, 186U);
    uint32_t recent_samples = 0U;
    uint32_t recent_pct = display_command_recent_success_pct(&recent_samples);
    uint32_t rate_per_min = display_command_rate_per_min();
    uint32_t budget_ticks =
        display_command_latency_budget_ticks(g_display_fb.command_avg_ticks, g_display_fb.command_samples);
    char line[80];
    uint32_t len;

    for (uint32_t row = 0U; row < g_display_fb.scroll_rows; row++) {
        display_framebuffer_draw_panel_line(row, "", body_fg, bg_pixel, DISPLAY_LINE_STYLE_TASK);
    }
    display_framebuffer_draw_panel_line(0U,
                                        "EVENT LOG HEALTH RATE LATENCY TIMELINE",
                                        title_fg,
                                        title_bg,
                                        DISPLAY_LINE_STYLE_TASK);

    len = 0U;
    line[0] = '\0';
    display_append_text(line, &len, sizeof(line), "STATE ");
    display_append_text(line,
                        &len,
                        sizeof(line),
                        display_command_health_label(
                            (display_command_health_state_t)g_display_fb.command_health_state));
    display_append_text(line, &len, sizeof(line), " CAUSE ");
    display_append_text(line,
                        &len,
                        sizeof(line),
                        display_transition_cause_label(
                            (display_transition_cause_t)g_display_fb.transition_cause));
    display_framebuffer_draw_panel_line(2U, line, body_fg, bg_pixel, DISPLAY_LINE_STYLE_TASK);

    len = 0U;
    line[0] = '\0';
    display_append_text(line, &len, sizeof(line), "REC ");
    if (recent_samples == 0U) {
        display_append_text(line, &len, sizeof(line), "-");
    } else {
        display_append_u32(line, &len, sizeof(line), recent_pct);
        display_append_text(line, &len, sizeof(line), "%");
    }
    display_append_text(line, &len, sizeof(line), " Q ");
    display_append_compact_u32(line, &len, sizeof(line), rate_per_min);
    display_append_text(line, &len, sizeof(line), " LAST ");
    display_append_compact_u32(line, &len, sizeof(line), g_display_fb.command_last_ticks);
    display_append_text(line, &len, sizeof(line), " B ");
    display_append_compact_u32(line, &len, sizeof(line), budget_ticks);
    display_framebuffer_draw_panel_line(3U, line, muted_fg, bg_pixel, DISPLAY_LINE_STYLE_TASK);

    for (uint32_t i = 0U; i < g_display_fb.timeline_count && (i + 5U) < g_display_fb.scroll_rows; i++) {
        uint32_t oldest =
            (g_display_fb.timeline_head + DISPLAY_FB_TIMELINE_CAP - g_display_fb.timeline_count) %
            DISPLAY_FB_TIMELINE_CAP;
        uint32_t entry_idx = (oldest + i) % DISPLAY_FB_TIMELINE_CAP;
        const char *event_label = "OK";

        if (g_display_fb.timeline[entry_idx].event == DISPLAY_TIMELINE_EVENT_FAIL) {
            event_label = "ER";
        } else if (g_display_fb.timeline[entry_idx].event == DISPLAY_TIMELINE_EVENT_RUNNING) {
            event_label = "RUN";
        }
        len = 0U;
        line[0] = '\0';
        display_append_text(line, &len, sizeof(line), event_label);
        display_append_text(line, &len, sizeof(line), " ");
        if (g_display_fb.timeline[entry_idx].tag != '\0') {
            char tag_text[2];

            tag_text[0] = g_display_fb.timeline[entry_idx].tag;
            tag_text[1] = '\0';
            display_append_text(line, &len, sizeof(line), tag_text);
        } else {
            display_append_text(line, &len, sizeof(line), "-");
        }
        display_append_text(line, &len, sizeof(line), " T");
        display_append_compact_u32(line,
                                   &len,
                                   sizeof(line),
                                   g_display_fb.timeline[entry_idx].duration_ticks);
        display_framebuffer_draw_panel_line(i + 5U,
                                            line,
                                            i == g_display_fb.timeline_count - 1U ? body_fg : muted_fg,
                                            bg_pixel,
                                            DISPLAY_LINE_STYLE_TASK);
    }
}

static void display_framebuffer_draw_active_panel_body(void) {
    if ((display_nav_view_t)g_display_fb.nav_view == DISPLAY_NAV_VIEW_TERM) {
        display_framebuffer_repaint_term_surface();
        return;
    }

    switch ((display_nav_view_t)g_display_fb.nav_view) {
    case DISPLAY_NAV_VIEW_TASK:
        display_framebuffer_draw_task_panel();
        break;
    case DISPLAY_NAV_VIEW_FS:
        display_framebuffer_draw_fs_panel();
        break;
    case DISPLAY_NAV_VIEW_LOG:
        display_framebuffer_draw_log_panel();
        break;
    case DISPLAY_NAV_VIEW_TERM:
    default:
        display_framebuffer_repaint_term_surface();
        break;
    }
}


static void display_framebuffer_build_header_metrics(char *metrics_text, uint32_t cap) {
    struct pmm_stats pmm_stats;
    uint32_t len = 0U;
    uint32_t hz = timer_frequency_hz();
    uint32_t seconds = 0U;
    const char *health_label =
        display_command_health_label((display_command_health_state_t)g_display_fb.command_health_state);

    if (cap == 0U) {
        return;
    }
    metrics_text[0] = '\0';

    pmm_get_stats(&pmm_stats);
    if (hz != 0U) {
        seconds = (uint32_t)(timer_ticks_snapshot() & 0xFFFFFFFFULL) / hz;
    }

    display_append_text(metrics_text, &len, cap, "UP ");
    display_append_u32(metrics_text, &len, cap, seconds);
    display_append_text(metrics_text, &len, cap, "s MEM ");
    display_append_mib_value(metrics_text, &len, cap, pmm_stats.free_frames);
    display_append_text(metrics_text, &len, cap, "/");
    display_append_mib_value(metrics_text, &len, cap, pmm_stats.total_frames);
    display_append_text(metrics_text, &len, cap, " CMD ");
    if (g_display_fb.command_samples == 0U) {
        display_append_text(metrics_text, &len, cap, health_label);
    } else {
        display_append_text(metrics_text, &len, cap, "OK");
        display_append_compact_u32(metrics_text, &len, cap, g_display_fb.command_ok_count);
        display_append_text(metrics_text, &len, cap, " ER");
        display_append_compact_u32(metrics_text, &len, cap, g_display_fb.command_fail_count);
        display_append_text(metrics_text, &len, cap, " H");
        display_append_text(metrics_text, &len, cap, health_label);
    }
}

static void display_framebuffer_draw_header_metrics(void) {
    int hot_theme = g_display_fb.shell_theme == DISPLAY_SHELL_THEME_ANSI;
    int showcase = display_shell_showcase_active();
    uint32_t metrics_bg =
        showcase ? display_framebuffer_pack_rgb(48U, 18U, 72U)
                 : (hot_theme ? display_framebuffer_pack_rgb(10U, 40U, 58U)
                              : display_framebuffer_pack_rgb(16U, 26U, 40U));
    uint32_t metrics_fg = display_framebuffer_pack_rgb(234U, 240U, 250U);
    uint32_t metrics_shadow =
        showcase ? display_framebuffer_pack_rgb(20U, 4U, 32U)
                 : (hot_theme ? display_framebuffer_pack_rgb(0U, 14U, 26U)
                              : display_framebuffer_pack_rgb(0U, 8U, 20U));
    uint32_t metrics_y = 2U;
    uint32_t metrics_left = 32U * DISPLAY_FB_CHAR_W;
    uint32_t metrics_right = g_display_fb.info.width - DISPLAY_FB_CHAR_W;
    char metrics_text[42];

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

static void display_framebuffer_draw_sidebar(void) {
    int hot_theme = g_display_fb.shell_theme == DISPLAY_SHELL_THEME_ANSI;
    int showcase = display_shell_showcase_active();
    int sidebar_region_active = g_display_fb.nav_region == DISPLAY_NAV_REGION_SIDEBAR;
    display_sidebar_card_t selected_card = (display_sidebar_card_t)g_display_fb.nav_sidebar_card;
    uint32_t panel_border = hot_theme ? display_framebuffer_pack_rgb(52U, 96U, 126U)
                                      : display_framebuffer_pack_rgb(44U, 60U, 84U);
    uint32_t panel_bg = hot_theme ? display_framebuffer_pack_rgb(8U, 15U, 26U)
                                  : display_framebuffer_pack_rgb(11U, 17U, 28U);
    uint32_t panel_header_bg = showcase ? display_framebuffer_pack_rgb(40U, 18U, 68U)
                                        : (hot_theme ? display_framebuffer_pack_rgb(10U, 40U, 58U)
                                                     : display_framebuffer_pack_rgb(17U, 30U, 52U));
    uint32_t panel_header_fg = display_framebuffer_pack_rgb(236U, 242U, 250U);
    uint32_t card_bg = hot_theme ? display_framebuffer_pack_rgb(7U, 14U, 25U)
                                 : display_framebuffer_pack_rgb(8U, 13U, 24U);
    uint32_t card_title_bg = showcase ? display_framebuffer_pack_rgb(24U, 32U, 76U)
                                      : (hot_theme ? display_framebuffer_pack_rgb(8U, 28U, 44U)
                                                   : display_framebuffer_pack_rgb(12U, 22U, 38U));
    uint32_t card_fg = display_framebuffer_pack_rgb(224U, 233U, 246U);
    uint32_t card_muted_fg = hot_theme ? display_framebuffer_pack_rgb(170U, 214U, 232U)
                                       : display_framebuffer_pack_rgb(154U, 180U, 212U);
    uint32_t accent = showcase ? display_framebuffer_pack_rgb(255U, 122U, 96U)
                               : (hot_theme ? display_framebuffer_pack_rgb(102U, 226U, 255U)
                                            : display_framebuffer_pack_rgb(248U, 182U, 86U));
    uint32_t ok_bg = display_framebuffer_pack_rgb(8U, 24U, 21U);
    uint32_t ok_fg = display_framebuffer_pack_rgb(228U, 252U, 239U);
    uint32_t warn_bg = display_framebuffer_pack_rgb(42U, 24U, 10U);
    uint32_t warn_fg = display_framebuffer_pack_rgb(255U, 232U, 194U);
    uint32_t fail_bg = display_framebuffer_pack_rgb(34U, 10U, 13U);
    uint32_t fail_fg = display_framebuffer_pack_rgb(255U, 226U, 221U);
    uint32_t rail_bg = hot_theme ? display_framebuffer_pack_rgb(7U, 26U, 40U)
                                 : display_framebuffer_pack_rgb(10U, 17U, 29U);
    uint32_t selected_card_bg =
        showcase ? display_framebuffer_pack_rgb(24U, 18U, 38U)
                 : (hot_theme ? display_framebuffer_pack_rgb(10U, 28U, 40U)
                              : display_framebuffer_pack_rgb(14U, 24U, 38U));
    uint32_t selected_title_bg =
        showcase ? display_framebuffer_pack_rgb(78U, 30U, 102U)
                 : (hot_theme ? display_framebuffer_pack_rgb(18U, 58U, 82U)
                              : display_framebuffer_pack_rgb(28U, 60U, 120U));
    uint32_t header_x = g_display_fb.sidebar_left_px + 10U;
    uint32_t outer_top = g_display_fb.sidebar_top_px;
    uint32_t title_right = g_display_fb.sidebar_left_px + g_display_fb.sidebar_width_px - 10U;
    uint32_t cards_top = outer_top + DISPLAY_FB_CHAR_H + 8U;
    uint32_t card_left = g_display_fb.sidebar_left_px + 8U;
    uint32_t card_width = g_display_fb.sidebar_width_px - 16U;
    uint32_t capsule_width = 5U * DISPLAY_FB_CHAR_W;
    uint32_t capsule_bg = ok_bg;
    uint32_t capsule_fg = ok_fg;
    uint32_t card_y;
    uint32_t budget_ticks = g_display_fb.command_last_budget_ticks;
    uint32_t recent_samples = 0U;
    uint32_t recent_pct = display_command_recent_success_pct(&recent_samples);
    uint32_t rate_per_min = display_command_rate_per_min();
    struct syscall_task_snapshot_entry tasks[16];
    uint32_t task_count = 0U;
    uint32_t user_count = 0U;
    uint32_t kernel_count = 0U;
    uint32_t running_count = 0U;
    const char *focus_name = "-";
    const char *focus_state = "---";
    const char *health_label =
        display_command_health_label((display_command_health_state_t)g_display_fb.command_health_state);
    const char *latency_label = display_command_latency_status_label();
    const char *cause_label =
        display_transition_cause_label((display_transition_cause_t)g_display_fb.transition_cause);
    const char *hud_state =
        g_display_fb.shell_hud_state[0] != '\0' ? g_display_fb.shell_hud_state : "-";
    const char *hud_last_tag =
        g_display_fb.shell_hud_last_tag[0] != '\0' ? g_display_fb.shell_hud_last_tag : "-";
    char line1[24];
    char line2[24];
    char line3[24];
    uint32_t len;

    if (g_display_fb.sidebar_width_px < 24U || g_display_fb.sidebar_height_px < 24U) {
        return;
    }
    if ((display_command_health_state_t)g_display_fb.command_health_state == DISPLAY_COMMAND_HEALTH_WARN) {
        capsule_bg = warn_bg;
        capsule_fg = warn_fg;
    } else if ((display_command_health_state_t)g_display_fb.command_health_state ==
               DISPLAY_COMMAND_HEALTH_DEGRADED) {
        capsule_bg = fail_bg;
        capsule_fg = fail_fg;
    }
    if (g_display_fb.command_active != 0U) {
        capsule_bg = display_framebuffer_pack_rgb(8U, 18U, 36U);
        capsule_fg = display_framebuffer_pack_rgb(232U, 243U, 255U);
    }
    if (budget_ticks == 0U || g_display_fb.command_active != 0U) {
        budget_ticks = display_command_latency_budget_ticks(g_display_fb.command_avg_ticks,
                                                            g_display_fb.command_samples);
    }
    if (sched_is_started() && sched_collect_task_snapshot(tasks, 16U, &task_count) == 0) {
        for (uint32_t i = 0U; i < task_count; i++) {
            if ((tasks[i].flags & SYSCALL_TASK_FLAG_USER) != 0U) {
                user_count++;
            } else {
                kernel_count++;
            }
            if (tasks[i].state == SYSCALL_TASK_STATE_RUNNING) {
                running_count++;
                if (focus_name[0] == '-') {
                    focus_name = tasks[i].name[0] != '\0' ? tasks[i].name : "?";
                    focus_state = display_task_state_short(tasks[i].state);
                }
            } else if (focus_name[0] == '-') {
                focus_name = tasks[i].name[0] != '\0' ? tasks[i].name : "?";
                focus_state = display_task_state_short(tasks[i].state);
            }
        }
    } else if (!sched_is_started()) {
        focus_name = "boot";
        focus_state = "INIT";
    }

    display_framebuffer_fill_rect_packed(
        g_display_fb.sidebar_left_px,
        g_display_fb.sidebar_top_px,
        g_display_fb.sidebar_width_px,
        g_display_fb.sidebar_height_px,
        sidebar_region_active ? accent : panel_border);
    display_framebuffer_fill_rect_packed(
        g_display_fb.sidebar_left_px + 1U,
        g_display_fb.sidebar_top_px + 1U,
        g_display_fb.sidebar_width_px - 2U,
        g_display_fb.sidebar_height_px - 2U,
        panel_bg);
    display_framebuffer_fill_rect_packed(
        g_display_fb.sidebar_left_px + 1U,
        g_display_fb.sidebar_top_px + 1U,
        g_display_fb.sidebar_width_px - 2U,
        DISPLAY_FB_CHAR_H,
        panel_header_bg);
    display_framebuffer_draw_text_packed(
        header_x,
        outer_top + 2U,
        "SYSTEM",
        panel_header_fg,
        panel_header_bg);
    {
        uint32_t capsule_x =
            title_right > capsule_width ? title_right - capsule_width : g_display_fb.sidebar_left_px + 8U;
        display_framebuffer_fill_rect_packed(
            capsule_x,
            outer_top + 2U,
            capsule_width,
            DISPLAY_FB_CHAR_H - 4U,
            capsule_bg);
        display_framebuffer_draw_text_packed(
            capsule_x + 7U,
            outer_top + 3U,
            showcase ? "SHOW" : (g_display_fb.command_active != 0U ? "RUN" : health_label),
            capsule_fg,
            capsule_bg);
    }

    card_y = cards_top;
    {
        int card_selected = selected_card == DISPLAY_SIDEBAR_CARD_WORKSPACE;
        uint32_t card_border = card_selected ? accent : panel_border;
        uint32_t body_bg = card_selected ? selected_card_bg : card_bg;
        uint32_t title_bg = card_selected ? selected_title_bg : card_title_bg;
        uint32_t stripe = accent;

        display_framebuffer_fill_rect_packed(card_left, card_y, card_width, DISPLAY_FB_SIDEBAR_CARD_H, card_border);
        display_framebuffer_fill_rect_packed(card_left + 1U, card_y + 1U, card_width - 2U,
                                             DISPLAY_FB_SIDEBAR_CARD_H - 2U, body_bg);
        display_framebuffer_fill_rect_packed(card_left + 1U, card_y + 1U, card_width - 2U,
                                             DISPLAY_FB_CHAR_H, title_bg);
        display_framebuffer_fill_rect_packed(card_left + 1U, card_y + 1U, 4U,
                                             DISPLAY_FB_SIDEBAR_CARD_H - 2U, stripe);
        if (card_selected) {
            display_framebuffer_fill_rect_packed(card_left + 1U, card_y + DISPLAY_FB_SIDEBAR_CARD_H - 5U,
                                                 card_width - 2U, 4U, accent);
        }
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + 2U, "SESSION", card_fg, title_bg);
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + DISPLAY_FB_CHAR_H + 7U,
                                             "CONSOLE SHELL", card_fg, body_bg);
        len = 0U;
        line1[0] = '\0';
        display_append_text(line1, &len, sizeof(line1), "THEME ");
        display_append_text(line1, &len, sizeof(line1), hot_theme ? "ANSI" : "PLAIN");
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + (DISPLAY_FB_CHAR_H * 2U) + 4U,
                                             line1, card_muted_fg, body_bg);
        len = 0U;
        line2[0] = '\0';
        display_append_text(line2, &len, sizeof(line2), "SHOW ");
        display_append_text(line2, &len, sizeof(line2),
                            showcase ? "LIVE" :
                            (g_display_fb.shell_bootshow_enabled != 0U ? "ARMD" : "IDLE"));
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + (DISPLAY_FB_CHAR_H * 3U) + 1U,
                                             line2, card_muted_fg, body_bg);
    }

    card_y += DISPLAY_FB_SIDEBAR_CARD_H + DISPLAY_FB_SIDEBAR_CARD_GAP;
    {
        int card_selected = selected_card == DISPLAY_SIDEBAR_CARD_HUD;
        uint32_t card_border = card_selected ? accent : panel_border;
        uint32_t body_bg = card_selected ? selected_card_bg : card_bg;
        uint32_t title_bg = card_selected ? selected_title_bg : card_title_bg;
        uint32_t stripe = card_selected ? accent : capsule_bg;

        display_framebuffer_fill_rect_packed(card_left, card_y, card_width, DISPLAY_FB_SIDEBAR_CARD_H, card_border);
        display_framebuffer_fill_rect_packed(card_left + 1U, card_y + 1U, card_width - 2U,
                                             DISPLAY_FB_SIDEBAR_CARD_H - 2U, body_bg);
        display_framebuffer_fill_rect_packed(card_left + 1U, card_y + 1U, card_width - 2U,
                                             DISPLAY_FB_CHAR_H, title_bg);
        display_framebuffer_fill_rect_packed(card_left + 1U, card_y + 1U, 4U,
                                             DISPLAY_FB_SIDEBAR_CARD_H - 2U, stripe);
        if (card_selected) {
            display_framebuffer_fill_rect_packed(card_left + 1U, card_y + DISPLAY_FB_SIDEBAR_CARD_H - 5U,
                                                 card_width - 2U, 4U, accent);
        }
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + 2U, "HUD", card_fg, title_bg);
        len = 0U;
        line1[0] = '\0';
        display_append_text(line1, &len, sizeof(line1), "HUD ");
        display_append_text(line1, &len, sizeof(line1), g_display_fb.shell_hud_enabled != 0U ? "ON" : "OFF");
        len = 0U;
        line2[0] = '\0';
        display_append_text(line2, &len, sizeof(line2), "J ");
        display_append_compact_u32(line2, &len, sizeof(line2), g_display_fb.shell_hud_jobs);
        display_append_text(line2, &len, sizeof(line2), " T ");
        display_append_text(line2, &len, sizeof(line2), hud_last_tag);
        len = 0U;
        line3[0] = '\0';
        display_append_text(line3, &len, sizeof(line3), "LAT ");
        display_append_compact_u32(line3, &len, sizeof(line3), g_display_fb.shell_hud_latency_ticks);
        display_append_text(line3, &len, sizeof(line3), "T ");
        display_append_text(line3, &len, sizeof(line3), hud_state);
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + DISPLAY_FB_CHAR_H + 7U,
                                             line1, card_fg, body_bg);
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + (DISPLAY_FB_CHAR_H * 2U) + 4U,
                                             line2, card_muted_fg, body_bg);
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + (DISPLAY_FB_CHAR_H * 3U) + 1U,
                                             line3, card_muted_fg, body_bg);
    }

    card_y += DISPLAY_FB_SIDEBAR_CARD_H + DISPLAY_FB_SIDEBAR_CARD_GAP;
    {
        int card_selected = selected_card == DISPLAY_SIDEBAR_CARD_HEALTH;
        uint32_t stripe =
            latency_label[0] == 'S' ? fail_bg : (latency_label[0] == 'R' ? rail_bg : ok_bg);
        uint32_t card_border = card_selected ? accent : panel_border;
        uint32_t body_bg = card_selected ? selected_card_bg : card_bg;
        uint32_t title_bg = card_selected ? selected_title_bg : card_title_bg;

        if (card_selected) {
            stripe = accent;
        }
        display_framebuffer_fill_rect_packed(card_left, card_y, card_width, DISPLAY_FB_SIDEBAR_CARD_H, card_border);
        display_framebuffer_fill_rect_packed(card_left + 1U, card_y + 1U, card_width - 2U,
                                             DISPLAY_FB_SIDEBAR_CARD_H - 2U, body_bg);
        display_framebuffer_fill_rect_packed(card_left + 1U, card_y + 1U, card_width - 2U,
                                             DISPLAY_FB_CHAR_H, title_bg);
        display_framebuffer_fill_rect_packed(card_left + 1U, card_y + 1U, 4U,
                                             DISPLAY_FB_SIDEBAR_CARD_H - 2U, stripe);
        if (card_selected) {
            display_framebuffer_fill_rect_packed(card_left + 1U, card_y + DISPLAY_FB_SIDEBAR_CARD_H - 5U,
                                                 card_width - 2U, 4U, accent);
        }
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + 2U, "HEALTH", card_fg, title_bg);
        len = 0U;
        line1[0] = '\0';
        display_append_text(line1, &len, sizeof(line1), "STATE ");
        display_append_text(line1, &len, sizeof(line1), health_label);
        len = 0U;
        line2[0] = '\0';
        display_append_text(line2, &len, sizeof(line2), "REC ");
        if (recent_samples == 0U) {
            display_append_text(line2, &len, sizeof(line2), "-");
        } else {
            display_append_u32(line2, &len, sizeof(line2), recent_pct);
            display_append_text(line2, &len, sizeof(line2), "%");
        }
        display_append_text(line2, &len, sizeof(line2), " Q ");
        display_append_compact_u32(line2, &len, sizeof(line2), rate_per_min);
        len = 0U;
        line3[0] = '\0';
        display_append_text(line3, &len, sizeof(line3), "B ");
        display_append_compact_u32(line3, &len, sizeof(line3), budget_ticks);
        display_append_text(line3, &len, sizeof(line3), " C ");
        display_append_text(line3, &len, sizeof(line3), cause_label);
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + DISPLAY_FB_CHAR_H + 7U,
                                             line1, card_fg, body_bg);
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + (DISPLAY_FB_CHAR_H * 2U) + 4U,
                                             line2, card_muted_fg, body_bg);
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + (DISPLAY_FB_CHAR_H * 3U) + 1U,
                                             line3, card_muted_fg, body_bg);
    }

    card_y += DISPLAY_FB_SIDEBAR_CARD_H + DISPLAY_FB_SIDEBAR_CARD_GAP;
    {
        int card_selected = selected_card == DISPLAY_SIDEBAR_CARD_TASKS;
        uint32_t card_border = card_selected ? accent : panel_border;
        uint32_t body_bg = card_selected ? selected_card_bg : card_bg;
        uint32_t title_bg = card_selected ? selected_title_bg : card_title_bg;
        uint32_t stripe = card_selected ? accent : rail_bg;

        display_framebuffer_fill_rect_packed(card_left, card_y, card_width, DISPLAY_FB_SIDEBAR_CARD_H, card_border);
        display_framebuffer_fill_rect_packed(card_left + 1U, card_y + 1U, card_width - 2U,
                                             DISPLAY_FB_SIDEBAR_CARD_H - 2U, body_bg);
        display_framebuffer_fill_rect_packed(card_left + 1U, card_y + 1U, card_width - 2U,
                                             DISPLAY_FB_CHAR_H, title_bg);
        display_framebuffer_fill_rect_packed(card_left + 1U, card_y + 1U, 4U,
                                             DISPLAY_FB_SIDEBAR_CARD_H - 2U, stripe);
        if (card_selected) {
            display_framebuffer_fill_rect_packed(card_left + 1U, card_y + DISPLAY_FB_SIDEBAR_CARD_H - 5U,
                                                 card_width - 2U, 4U, accent);
        }
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + 2U, "TASKS", card_fg, title_bg);
        len = 0U;
        line1[0] = '\0';
        display_append_text(line1, &len, sizeof(line1), "USR ");
        display_append_compact_u32(line1, &len, sizeof(line1), user_count);
        display_append_text(line1, &len, sizeof(line1), " KRN ");
        display_append_compact_u32(line1, &len, sizeof(line1), kernel_count);
        len = 0U;
        line2[0] = '\0';
        display_append_text(line2, &len, sizeof(line2), "RUN ");
        display_append_compact_u32(line2, &len, sizeof(line2), running_count);
        display_append_text(line2, &len, sizeof(line2), " ALL ");
        display_append_compact_u32(line2, &len, sizeof(line2), task_count);
        len = 0U;
        line3[0] = '\0';
        display_append_text(line3, &len, sizeof(line3), focus_state);
        display_append_text(line3, &len, sizeof(line3), " ");
        display_append_text(line3, &len, sizeof(line3), focus_name);
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + DISPLAY_FB_CHAR_H + 7U,
                                             line1, card_fg, body_bg);
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + (DISPLAY_FB_CHAR_H * 2U) + 4U,
                                             line2, card_muted_fg, body_bg);
        display_framebuffer_draw_text_packed(card_left + 10U, card_y + (DISPLAY_FB_CHAR_H * 3U) + 1U,
                                             line3, card_muted_fg, body_bg);
    }
}

static void display_framebuffer_draw_footer_hud(void) {
#if DISPLAY_FB_FOOTER_ROWS > 0
    int hot_theme = g_display_fb.shell_theme == DISPLAY_SHELL_THEME_ANSI;
    int showcase = display_shell_showcase_active();
    uint32_t footer_top = g_display_fb.info.height - (DISPLAY_FB_FOOTER_ROWS * DISPLAY_FB_CHAR_H);
    uint32_t footer_bg =
        showcase ? display_framebuffer_pack_rgb(28U, 16U, 40U)
                 : (hot_theme ? display_framebuffer_pack_rgb(10U, 28U, 40U)
                              : display_framebuffer_pack_rgb(13U, 24U, 42U));
    uint32_t footer_fg = display_framebuffer_pack_rgb(224U, 233U, 246U);
    uint32_t rail_bg =
        showcase ? display_framebuffer_pack_rgb(16U, 10U, 28U)
                 : (hot_theme ? display_framebuffer_pack_rgb(8U, 18U, 30U)
                              : display_framebuffer_pack_rgb(8U, 15U, 29U));
    uint32_t rail_border =
        showcase ? display_framebuffer_pack_rgb(122U, 84U, 168U)
                 : (hot_theme ? display_framebuffer_pack_rgb(74U, 140U, 168U)
                              : display_framebuffer_pack_rgb(74U, 102U, 136U));
    uint32_t rail_left = DISPLAY_FB_FOOTER_RAIL_LEFT_COLS * DISPLAY_FB_CHAR_W;
    uint32_t rail_right = g_display_fb.info.width - DISPLAY_FB_CHAR_W;
    uint32_t rail_top = footer_top + 3U;
    uint32_t rail_h = DISPLAY_FB_CHAR_H - 6U;
    uint32_t cap_w;
    uint32_t visible_finished;
    uint32_t slots_taken;
    uint32_t active_slots = g_display_fb.command_active != 0U ? 1U : 0U;
    char legend_text[80];

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

    display_framebuffer_term_model_shift_up();
    if ((display_nav_view_t)g_display_fb.nav_view != DISPLAY_NAV_VIEW_TERM) {
        g_display_fb.cursor_row = g_display_fb.scroll_rows - 1U;
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

            display_framebuffer_term_model_set_cell(g_display_fb.cursor_row,
                                                    g_display_fb.cursor_col,
                                                    c,
                                                    g_display_fb.line_style);
            if ((display_nav_view_t)g_display_fb.nav_view != DISPLAY_NAV_VIEW_TERM) {
                goto update_cursor;
            }
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

update_cursor:
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

static void display_framebuffer_draw_desktop_chrome(void) {
    int hot_theme = g_display_fb.shell_theme == DISPLAY_SHELL_THEME_ANSI;
    int showcase = display_shell_showcase_active();
    display_nav_region_t nav_region = (display_nav_region_t)g_display_fb.nav_region;
    display_nav_view_t selected_view = (display_nav_view_t)g_display_fb.nav_view;
    display_command_health_state_t health_state =
        (display_command_health_state_t)g_display_fb.command_health_state;
    uint32_t top_bar_bg =
        showcase ? display_framebuffer_pack_rgb(48U, 18U, 72U)
                 : (hot_theme ? display_framebuffer_pack_rgb(10U, 40U, 58U)
                              : display_framebuffer_pack_rgb(16U, 26U, 40U));
    uint32_t title_fg = display_framebuffer_pack_rgb(252U, 254U, 255U);
    uint32_t title_shadow =
        showcase ? display_framebuffer_pack_rgb(20U, 4U, 32U)
                 : (hot_theme ? display_framebuffer_pack_rgb(0U, 14U, 26U)
                              : display_framebuffer_pack_rgb(4U, 16U, 38U));
    uint32_t accent =
        showcase ? display_framebuffer_pack_rgb(255U, 122U, 96U)
                 : (hot_theme ? display_framebuffer_pack_rgb(102U, 226U, 255U)
                              : display_framebuffer_pack_rgb(248U, 182U, 86U));
    uint32_t pill_bg =
        showcase ? display_framebuffer_pack_rgb(32U, 12U, 44U)
                 : (hot_theme ? display_framebuffer_pack_rgb(7U, 20U, 32U)
                              : display_framebuffer_pack_rgb(10U, 18U, 32U));
    uint32_t pill_fg = display_framebuffer_pack_rgb(216U, 228U, 242U);
    uint32_t pill_active_bg =
        showcase ? display_framebuffer_pack_rgb(144U, 52U, 94U)
                 : (hot_theme ? display_framebuffer_pack_rgb(16U, 108U, 148U)
                              : display_framebuffer_pack_rgb(64U, 146U, 220U));
    uint32_t pill_active_fg = display_framebuffer_pack_rgb(238U, 246U, 255U);
    uint32_t view_accent = display_nav_view_accent(showcase, hot_theme, selected_view);
    uint32_t dock_bg =
        showcase ? display_framebuffer_pack_rgb(16U, 10U, 24U)
                 : (hot_theme ? display_framebuffer_pack_rgb(8U, 16U, 28U)
                              : display_framebuffer_pack_rgb(10U, 17U, 29U));
    uint32_t dock_inner =
        showcase ? display_framebuffer_pack_rgb(22U, 12U, 34U)
                 : (hot_theme ? display_framebuffer_pack_rgb(5U, 18U, 28U)
                              : display_framebuffer_pack_rgb(7U, 12U, 22U));
    uint32_t dock_item_bg =
        showcase ? display_framebuffer_pack_rgb(42U, 18U, 58U)
                 : (hot_theme ? display_framebuffer_pack_rgb(10U, 34U, 48U)
                              : display_framebuffer_pack_rgb(13U, 24U, 42U));
    uint32_t dock_item_fg = display_framebuffer_pack_rgb(220U, 231U, 246U);
    uint32_t warn_bg = display_framebuffer_pack_rgb(42U, 24U, 10U);
    uint32_t warn_fg = display_framebuffer_pack_rgb(255U, 232U, 194U);
    uint32_t fail_bg = display_framebuffer_pack_rgb(34U, 10U, 13U);
    uint32_t fail_fg = display_framebuffer_pack_rgb(255U, 226U, 221U);
    uint32_t panel_border =
        showcase ? display_framebuffer_pack_rgb(96U, 56U, 128U)
                 : (hot_theme ? display_framebuffer_pack_rgb(36U, 92U, 116U)
                              : display_framebuffer_pack_rgb(44U, 60U, 84U));
    uint32_t panel_shadow = display_framebuffer_pack_rgb(2U, 4U, 8U);
    uint32_t window_title_bg =
        showcase ? display_framebuffer_pack_rgb(92U, 34U, 118U)
                 : (hot_theme ? display_framebuffer_pack_rgb(12U, 78U, 96U)
                              : display_framebuffer_pack_rgb(24U, 50U, 104U));
    uint32_t window_title_fg = display_framebuffer_pack_rgb(244U, 248U, 255U);
    uint32_t window_title_shadow =
        showcase ? display_framebuffer_pack_rgb(32U, 8U, 46U)
                 : (hot_theme ? display_framebuffer_pack_rgb(0U, 22U, 34U)
                              : display_framebuffer_pack_rgb(7U, 20U, 44U));
    uint32_t dock_item_w = g_display_fb.dock_width_px - 16U;
    uint32_t dock_item_h = 28U;
    uint32_t dock_item_x = g_display_fb.dock_left_px + 8U;
    uint32_t dock_item_y = g_display_fb.dock_top_px + DISPLAY_FB_CHAR_H + 10U;
    uint32_t panel_left = g_display_fb.panel_left_px;
    uint32_t panel_top = g_display_fb.panel_top_px;
    uint32_t panel_width = g_display_fb.panel_width_px;
    uint32_t panel_border_color = nav_region == DISPLAY_NAV_REGION_PANEL ? view_accent : panel_border;
    uint32_t dock_border_color = nav_region == DISPLAY_NAV_REGION_DOCK ? view_accent : panel_border;
    uint32_t title_status_right = panel_left + panel_width - (7U * DISPLAY_FB_CHAR_W) - 8U;
    uint32_t chip_w = 0U;
    uint32_t task_count = 0U;
    uint32_t user_count = 0U;
    uint32_t running_count = 0U;
    uint32_t task_item_bg = dock_item_bg;
    uint32_t task_item_fg = dock_item_fg;
    uint32_t fs_item_bg = dock_item_bg;
    uint32_t fs_item_fg = dock_item_fg;
    uint32_t log_item_bg = dock_item_bg;
    uint32_t log_item_fg = dock_item_fg;
    uint32_t len = 0U;
    const char *mode_label = showcase ? "SHOW" : (hot_theme ? "ANSI" : "READY");
    char title_focus_chip[12];
    char title_hud_chip[12];
    char title_state_chip[12];

    title_focus_chip[0] = '\0';
    title_hud_chip[0] = '\0';
    title_state_chip[0] = '\0';

    if (sched_is_started()) {
        struct syscall_task_snapshot_entry tasks[16];

        if (sched_collect_task_snapshot(tasks, 16U, &task_count) == 0) {
            for (uint32_t i = 0U; i < task_count; i++) {
                if ((tasks[i].flags & SYSCALL_TASK_FLAG_USER) != 0U) {
                    user_count++;
                }
                if (tasks[i].state == SYSCALL_TASK_STATE_RUNNING) {
                    running_count++;
                }
            }
        }
    }

    if (user_count > 1U || running_count > 1U) {
        task_item_bg = pill_active_bg;
        task_item_fg = pill_active_fg;
    }

    if (health_state == DISPLAY_COMMAND_HEALTH_WARN) {
        log_item_bg = warn_bg;
        log_item_fg = warn_fg;
    } else if (health_state == DISPLAY_COMMAND_HEALTH_DEGRADED) {
        log_item_bg = fail_bg;
        log_item_fg = fail_fg;
    }

    if (g_display_fb.shell_hud_enabled != 0U || showcase) {
        len = 0U;
        if (g_display_fb.shell_hud_jobs != 0U) {
            display_append_text(title_hud_chip, &len, sizeof(title_hud_chip), "J");
            display_append_compact_u32(title_hud_chip,
                                       &len,
                                       sizeof(title_hud_chip),
                                       g_display_fb.shell_hud_jobs);
        } else if (!showcase) {
            display_append_text(title_hud_chip, &len, sizeof(title_hud_chip), "HUD");
        }
    }

    len = 0U;
    display_append_text(title_focus_chip, &len, sizeof(title_focus_chip), display_nav_region_label(nav_region));

    if (g_display_fb.command_active != 0U) {
        len = 0U;
        display_append_text(title_state_chip, &len, sizeof(title_state_chip), "RUN");
    } else if (health_state == DISPLAY_COMMAND_HEALTH_WARN) {
        log_item_bg = warn_bg;
        log_item_fg = warn_fg;
        len = 0U;
        display_append_text(title_state_chip, &len, sizeof(title_state_chip), "WARN");
    } else if (health_state == DISPLAY_COMMAND_HEALTH_DEGRADED) {
        log_item_bg = fail_bg;
        log_item_fg = fail_fg;
        len = 0U;
        display_append_text(title_state_chip, &len, sizeof(title_state_chip), "DEGR");
    }

    display_framebuffer_fill_rect_packed(0U, 0U, g_display_fb.info.width, DISPLAY_FB_CHAR_H, top_bar_bg);
    display_framebuffer_fill_rect_packed(0U, DISPLAY_FB_CHAR_H - 2U, g_display_fb.info.width, 2U, accent);

    display_framebuffer_draw_text_emphasized_packed(
        DISPLAY_FB_CHAR_W,
        2U,
        "SKEZOS",
        title_fg,
        top_bar_bg,
        title_shadow);
    display_framebuffer_draw_text_packed(
        8U * DISPLAY_FB_CHAR_W,
        3U,
        "OPERATOR CONSOLE",
        pill_fg,
        top_bar_bg);

    display_framebuffer_fill_rect_packed(
        g_display_fb.dock_left_px,
        g_display_fb.dock_top_px,
        g_display_fb.dock_width_px,
        g_display_fb.dock_height_px,
        dock_border_color);
    display_framebuffer_fill_rect_packed(
        g_display_fb.dock_left_px + 1U,
        g_display_fb.dock_top_px + 1U,
        g_display_fb.dock_width_px - 2U,
        g_display_fb.dock_height_px - 2U,
        dock_bg);
    display_framebuffer_fill_rect_packed(
        g_display_fb.dock_left_px + 1U,
        g_display_fb.dock_top_px + 1U,
        g_display_fb.dock_width_px - 2U,
        DISPLAY_FB_CHAR_H,
        dock_inner);
    display_framebuffer_draw_text_packed(
        g_display_fb.dock_left_px + 10U,
        g_display_fb.dock_top_px + 2U,
        "VIEWS",
        nav_region == DISPLAY_NAV_REGION_DOCK ? pill_active_fg : pill_fg,
        dock_inner);

    display_framebuffer_fill_rect_packed(dock_item_x, dock_item_y, dock_item_w, dock_item_h,
                                         selected_view == DISPLAY_NAV_VIEW_TERM ? view_accent : dock_item_bg);
    display_framebuffer_draw_text_packed(dock_item_x + 8U, dock_item_y + 7U, "TERM",
                                         selected_view == DISPLAY_NAV_VIEW_TERM ? pill_active_fg : dock_item_fg,
                                         selected_view == DISPLAY_NAV_VIEW_TERM ? view_accent : dock_item_bg);
    dock_item_y += dock_item_h + 10U;
    display_framebuffer_fill_rect_packed(dock_item_x, dock_item_y, dock_item_w, dock_item_h,
                                         selected_view == DISPLAY_NAV_VIEW_TASK ? view_accent : task_item_bg);
    display_framebuffer_draw_text_packed(dock_item_x + 8U, dock_item_y + 7U, "TASK",
                                         selected_view == DISPLAY_NAV_VIEW_TASK ? pill_active_fg : task_item_fg,
                                         selected_view == DISPLAY_NAV_VIEW_TASK ? view_accent : task_item_bg);
    dock_item_y += dock_item_h + 10U;
    display_framebuffer_fill_rect_packed(dock_item_x, dock_item_y, dock_item_w, dock_item_h,
                                         selected_view == DISPLAY_NAV_VIEW_FS ? view_accent : fs_item_bg);
    display_framebuffer_draw_text_packed(dock_item_x + 18U, dock_item_y + 7U, "FS",
                                         selected_view == DISPLAY_NAV_VIEW_FS ? pill_active_fg : fs_item_fg,
                                         selected_view == DISPLAY_NAV_VIEW_FS ? view_accent : fs_item_bg);
    dock_item_y += dock_item_h + 10U;
    display_framebuffer_fill_rect_packed(dock_item_x, dock_item_y, dock_item_w, dock_item_h,
                                         selected_view == DISPLAY_NAV_VIEW_LOG ? view_accent : log_item_bg);
    display_framebuffer_draw_text_packed(dock_item_x + 12U, dock_item_y + 7U, "LOG",
                                         selected_view == DISPLAY_NAV_VIEW_LOG ? pill_active_fg : log_item_fg,
                                         selected_view == DISPLAY_NAV_VIEW_LOG ? view_accent : log_item_bg);

    display_framebuffer_fill_rect_packed(panel_left + 4U, panel_top + g_display_fb.panel_height_px,
                                         panel_width, 4U, panel_shadow);
    display_framebuffer_fill_rect_packed(panel_left + panel_width, panel_top + 4U,
                                         4U, g_display_fb.panel_height_px, panel_shadow);
    display_framebuffer_fill_rect_packed(panel_left, panel_top, panel_width, 1U, panel_border_color);
    display_framebuffer_fill_rect_packed(panel_left, panel_top + g_display_fb.panel_height_px - 1U,
                                         panel_width, 1U, panel_border_color);
    display_framebuffer_fill_rect_packed(panel_left, panel_top, 1U, g_display_fb.panel_height_px, panel_border_color);
    display_framebuffer_fill_rect_packed(panel_left + panel_width - 1U, panel_top,
                                         1U, g_display_fb.panel_height_px, panel_border_color);
    display_framebuffer_fill_rect_packed(panel_left + 1U, panel_top + 1U, panel_width - 2U,
                                         DISPLAY_FB_WINDOW_TITLE_H, window_title_bg);
    display_framebuffer_fill_rect_packed(panel_left + 1U, panel_top + DISPLAY_FB_WINDOW_TITLE_H,
                                         panel_width - 2U, 1U, view_accent);
    display_framebuffer_draw_text_emphasized_packed(
        panel_left + 10U,
        panel_top + 1U,
        showcase ? "SHOWCASE SHELL" : display_nav_window_label(selected_view),
        window_title_fg,
        window_title_bg,
        window_title_shadow);
    display_framebuffer_fill_rect_packed(panel_left + panel_width - (7U * DISPLAY_FB_CHAR_W), panel_top + 2U,
                                         (6U * DISPLAY_FB_CHAR_W), DISPLAY_FB_CHAR_H - 4U, pill_bg);
    display_framebuffer_draw_text_packed(panel_left + panel_width - (7U * DISPLAY_FB_CHAR_W) + 8U,
                                         panel_top + 3U, mode_label, pill_fg, pill_bg);
    if (title_state_chip[0] != '\0') {
        uint32_t state_bg = g_display_fb.command_active != 0U ? pill_active_bg
                           : (health_state == DISPLAY_COMMAND_HEALTH_WARN ? warn_bg : fail_bg);
        uint32_t state_fg = g_display_fb.command_active != 0U ? pill_active_fg
                           : (health_state == DISPLAY_COMMAND_HEALTH_WARN ? warn_fg : fail_fg);

        chip_w = display_framebuffer_draw_chip_right_packed(
            title_status_right,
            panel_top + 2U,
            title_state_chip,
            state_fg,
            state_bg);
        if (chip_w != 0U) {
            title_status_right -= chip_w + 6U;
        }
    }
    if (title_focus_chip[0] != '\0') {
        chip_w = display_framebuffer_draw_chip_right_packed(
            title_status_right,
            panel_top + 2U,
            title_focus_chip,
            pill_active_fg,
            nav_region == DISPLAY_NAV_REGION_PANEL ? view_accent : pill_bg);
        if (chip_w != 0U) {
            title_status_right -= chip_w + 6U;
        }
    }
    if (title_hud_chip[0] != '\0') {
        (void)display_framebuffer_draw_chip_right_packed(
            title_status_right,
            panel_top + 2U,
            title_hud_chip,
            pill_active_fg,
            pill_active_bg);
    }

    display_framebuffer_draw_sidebar();
    display_framebuffer_draw_prompt_strip_idle();
    display_framebuffer_draw_footer_hud();
}

static void display_framebuffer_redraw_chrome(void) {
    if (g_display_fb.ready == 0U) {
        return;
    }
    display_framebuffer_draw_desktop_chrome();
    display_framebuffer_draw_active_panel_body();
}

static void display_framebuffer_draw_shell_frame(void) {
    uint32_t desktop_bg = display_framebuffer_pack_rgb(6U, 10U, 18U);
    uint32_t panel_bg = display_framebuffer_pack_rgb(4U, 8U, 16U);
    uint32_t rail_bg = display_framebuffer_pack_rgb(10U, 17U, 29U);
    uint32_t rail_marker = display_framebuffer_pack_rgb(68U, 84U, 106U);
    uint32_t panel_left = g_display_fb.panel_left_px;
    uint32_t panel_top = g_display_fb.panel_top_px;
    uint32_t panel_width = g_display_fb.panel_width_px;
    uint32_t panel_height = g_display_fb.panel_height_px;

    display_framebuffer_fill_rect_packed(0U, 0U, g_display_fb.info.width, g_display_fb.info.height, desktop_bg);
    display_framebuffer_fill_rect_packed(panel_left + 1U, panel_top + 1U, panel_width - 2U,
                                         panel_height - 2U, panel_bg);

    display_framebuffer_fill_rect_packed(
        g_display_fb.content_left_px - DISPLAY_FB_LINE_GUTTER_TOTAL,
        g_display_fb.content_top_px,
        DISPLAY_FB_LINE_GUTTER_TOTAL,
        g_display_fb.content_bottom_px - g_display_fb.content_top_px,
        rail_bg);
    display_framebuffer_fill_rect_packed(
        g_display_fb.content_left_px,
        g_display_fb.content_top_px,
        g_display_fb.content_width_px,
        g_display_fb.content_bottom_px - g_display_fb.content_top_px,
        display_framebuffer_pack_rgb(0U, 0U, 0U));
    display_framebuffer_fill_rect_packed(
        g_display_fb.content_left_px - DISPLAY_FB_LINE_GUTTER_TOTAL,
        g_display_fb.content_top_px,
        DISPLAY_FB_LINE_GUTTER_WIDTH,
        g_display_fb.content_bottom_px - g_display_fb.content_top_px,
        rail_marker);

    display_framebuffer_draw_desktop_chrome();
    display_framebuffer_draw_active_panel_body();
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
    g_display_fb.shell_theme = DISPLAY_SHELL_THEME_PLAIN;
    g_display_fb.shell_hud_enabled = 0U;
    g_display_fb.shell_bootshow_enabled = 0U;
    g_display_fb.shell_showcase_until_ticks = 0U;
    g_display_fb.shell_hud_jobs = 0U;
    g_display_fb.shell_hud_latency_ticks = 0U;
    g_display_fb.shell_hud_last_tag[0] = '\0';
    g_display_fb.shell_hud_state[0] = '\0';
    g_display_fb.shell_cwd[0] = '/';
    g_display_fb.shell_cwd[1] = '\0';
    g_display_fb.transition_cause = DISPLAY_TRANSITION_CAUSE_NONE;
    g_display_fb.nav_region = DISPLAY_NAV_REGION_PANEL;
    g_display_fb.nav_view = DISPLAY_NAV_VIEW_TERM;
    g_display_fb.nav_sidebar_card = DISPLAY_SIDEBAR_CARD_WORKSPACE;
    display_framebuffer_term_model_clear_all();
    display_verify_font_coverage();
    vga_clear();
}

void display_late_init(void) {
    struct boot_framebuffer_info info;
    uint32_t phys_base;
    uint32_t phys_offset;
    uint32_t map_length;
    uint32_t workspace_top;
    uint32_t workspace_bottom;
    uint32_t console_width_px;
    uint32_t console_height_px;
    uint32_t console_slack_x;
    uint32_t console_slack_y;
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
    if (info.width <= (DISPLAY_FB_STAGE_MARGIN_X * 2U) + DISPLAY_FB_DOCK_WIDTH +
                          DISPLAY_FB_SIDEBAR_WIDTH + (DISPLAY_FB_STAGE_GAP * 2U) +
                          (DISPLAY_FB_PANEL_BORDER * 2U) + DISPLAY_FB_LINE_GUTTER_TOTAL +
                          (DISPLAY_FB_WINDOW_INSET_X * 2U) + DISPLAY_FB_CHAR_W ||
        info.height <= (DISPLAY_FB_HEADER_ROWS * DISPLAY_FB_CHAR_H) +
                           (DISPLAY_FB_FOOTER_ROWS * DISPLAY_FB_CHAR_H) +
                           (DISPLAY_FB_STAGE_MARGIN_Y * 2U) +
                           (DISPLAY_FB_PANEL_BORDER * 2U) +
                           DISPLAY_FB_WINDOW_TITLE_H +
                           (DISPLAY_FB_WINDOW_INSET_Y * 2U) +
                           (DISPLAY_FB_CHAR_H * 2U)) {
        KLOGW("display: framebuffer geometry too small %ux%u", info.width, info.height);
        return;
    }
    workspace_top = (DISPLAY_FB_HEADER_ROWS * DISPLAY_FB_CHAR_H) + DISPLAY_FB_STAGE_MARGIN_Y;
    workspace_bottom = info.height - (DISPLAY_FB_FOOTER_ROWS * DISPLAY_FB_CHAR_H) -
                       DISPLAY_FB_STAGE_MARGIN_Y;
    g_display_fb.dock_left_px = DISPLAY_FB_STAGE_MARGIN_X;
    g_display_fb.dock_top_px = workspace_top;
    g_display_fb.dock_width_px = DISPLAY_FB_DOCK_WIDTH;
    g_display_fb.dock_height_px = workspace_bottom - workspace_top;
    g_display_fb.sidebar_width_px = DISPLAY_FB_SIDEBAR_WIDTH;
    g_display_fb.sidebar_left_px = info.width - DISPLAY_FB_STAGE_MARGIN_X - g_display_fb.sidebar_width_px;
    g_display_fb.sidebar_top_px = workspace_top;
    g_display_fb.sidebar_height_px = workspace_bottom - workspace_top;
    g_display_fb.panel_left_px = g_display_fb.dock_left_px + g_display_fb.dock_width_px +
                                 DISPLAY_FB_STAGE_GAP;
    g_display_fb.panel_top_px = workspace_top;
    g_display_fb.panel_width_px = g_display_fb.sidebar_left_px - DISPLAY_FB_STAGE_GAP -
                                  g_display_fb.panel_left_px;
    g_display_fb.panel_height_px = workspace_bottom - workspace_top;
    console_width_px = g_display_fb.panel_width_px - (DISPLAY_FB_PANEL_BORDER * 2U) -
                       (DISPLAY_FB_WINDOW_INSET_X * 2U);
    if (console_width_px <= DISPLAY_FB_LINE_GUTTER_TOTAL + DISPLAY_FB_CHAR_W) {
        KLOGW("display: framebuffer text cols unavailable");
        return;
    }
    g_display_fb.text_cols = (console_width_px - DISPLAY_FB_LINE_GUTTER_TOTAL) / DISPLAY_FB_CHAR_W;
    if (g_display_fb.text_cols == 0U) {
        KLOGW("display: framebuffer text cols unavailable");
        return;
    }
    console_width_px = DISPLAY_FB_LINE_GUTTER_TOTAL + (g_display_fb.text_cols * DISPLAY_FB_CHAR_W);
    g_display_fb.content_width_px = g_display_fb.text_cols * DISPLAY_FB_CHAR_W;
    console_slack_x = g_display_fb.panel_width_px - (DISPLAY_FB_PANEL_BORDER * 2U) -
                      (DISPLAY_FB_WINDOW_INSET_X * 2U) - console_width_px;
    g_display_fb.content_left_px = g_display_fb.panel_left_px + DISPLAY_FB_PANEL_BORDER +
                                   DISPLAY_FB_WINDOW_INSET_X + (console_slack_x / 2U) +
                                   DISPLAY_FB_LINE_GUTTER_TOTAL;
    console_height_px = g_display_fb.panel_height_px - (DISPLAY_FB_PANEL_BORDER * 2U) -
                        DISPLAY_FB_WINDOW_TITLE_H - (DISPLAY_FB_WINDOW_INSET_Y * 2U);
    if (console_height_px <= DISPLAY_FB_CHAR_H) {
        KLOGW("display: framebuffer content window unavailable");
        return;
    }
    g_display_fb.text_rows = console_height_px / DISPLAY_FB_CHAR_H;
    if (g_display_fb.text_rows < 2U) {
        KLOGW("display: framebuffer text rows unavailable");
        return;
    }
    if (g_display_fb.text_cols > DISPLAY_FB_TERM_MODEL_COLS_MAX ||
        g_display_fb.text_rows > DISPLAY_FB_TERM_MODEL_ROWS_MAX) {
        KLOGW("display: framebuffer term model caps exceeded cols=%u rows=%u",
              g_display_fb.text_cols,
              g_display_fb.text_rows);
        return;
    }
    console_height_px = g_display_fb.text_rows * DISPLAY_FB_CHAR_H;
    console_slack_y = g_display_fb.panel_height_px - (DISPLAY_FB_PANEL_BORDER * 2U) -
                      DISPLAY_FB_WINDOW_TITLE_H - (DISPLAY_FB_WINDOW_INSET_Y * 2U) -
                      console_height_px;
    g_display_fb.content_top_px = g_display_fb.panel_top_px + DISPLAY_FB_PANEL_BORDER +
                                  DISPLAY_FB_WINDOW_TITLE_H + DISPLAY_FB_WINDOW_INSET_Y +
                                  (console_slack_y / 2U);
    g_display_fb.content_bottom_px = g_display_fb.content_top_px + console_height_px;
    g_display_fb.scroll_rows = g_display_fb.text_rows - 1U;
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
    g_display_fb.shell_theme = DISPLAY_SHELL_THEME_PLAIN;
    g_display_fb.shell_hud_enabled = 0U;
    g_display_fb.shell_bootshow_enabled = 0U;
    g_display_fb.shell_showcase_until_ticks = 0U;
    g_display_fb.shell_hud_jobs = 0U;
    g_display_fb.shell_hud_latency_ticks = 0U;
    g_display_fb.shell_hud_last_tag[0] = '\0';
    g_display_fb.shell_hud_state[0] = '\0';
    g_display_fb.shell_cwd[0] = '/';
    g_display_fb.shell_cwd[1] = '\0';
    g_display_fb.transition_cause = DISPLAY_TRANSITION_CAUSE_NONE;
    g_display_fb.nav_region = DISPLAY_NAV_REGION_PANEL;
    g_display_fb.nav_view = DISPLAY_NAV_VIEW_TERM;
    g_display_fb.nav_sidebar_card = DISPLAY_SIDEBAR_CARD_WORKSPACE;
    display_framebuffer_term_model_clear_all();
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
    KLOGI("display: gui_state_hash=%x profile=fb-shell-v6", gui_hash);
}

uint32_t display_console_enter_critical(void) {
    return vga_console_enter_critical();
}

void display_console_leave_critical(uint32_t saved_flags) {
    vga_console_leave_critical(saved_flags);
}

void display_putc(char c) {
    if (g_display_mode == DISPLAY_MODE_FRAMEBUFFER && g_display_fb.ready != 0U) {
        if (g_display_gui.active) {
            return;
        }
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

int display_handle_navigation_key(display_nav_key_t key) {
    if (g_display_gui.active ||
        g_display_mode != DISPLAY_MODE_FRAMEBUFFER ||
        g_display_fb.ready == 0U) {
        return 0;
    }
    if (display_handle_navigation_internal(key) == 0) {
        return 1;
    }
    display_framebuffer_redraw_chrome();
    return 1;
}

static int display_gui_window_total_height(const struct display_gui_window *window) {
    if (!window) {
        return 0;
    }
    return (int)(DISPLAY_GUI_FRAME_BORDER +
                 DISPLAY_GUI_TITLE_H +
                 window->height +
                 DISPLAY_GUI_FRAME_BORDER);
}

static int display_gui_window_total_width(const struct display_gui_window *window) {
    if (!window) {
        return 0;
    }
    return (int)(DISPLAY_GUI_FRAME_BORDER + window->width + DISPLAY_GUI_FRAME_BORDER);
}

static int display_gui_find_free_index(void) {
    for (uint32_t i = 0U; i < DISPLAY_GUI_MAX_WINDOWS; i++) {
        if (!g_display_gui.windows[i].used) {
            return (int)i;
        }
    }
    return -1;
}

static int display_gui_find_index_by_id(int window_id) {
    for (uint32_t i = 0U; i < DISPLAY_GUI_MAX_WINDOWS; i++) {
        if (g_display_gui.windows[i].used && g_display_gui.windows[i].window_id == window_id) {
            return (int)i;
        }
    }
    return -1;
}

static int display_gui_find_index_by_owner(int owner_pid) {
    for (uint32_t i = 0U; i < DISPLAY_GUI_MAX_WINDOWS; i++) {
        if (g_display_gui.windows[i].used && g_display_gui.windows[i].owner_pid == owner_pid) {
            return (int)i;
        }
    }
    return -1;
}

static int display_gui_window_queue_event(struct display_gui_window *window,
                                          const struct syscall_gui_event *event) {
    uint32_t slot;

    if (!window || !event) {
        return -KERR_INVAL;
    }
    if (window->event_count >= DISPLAY_GUI_EVENT_QUEUE_CAP) {
        if (event->type == SYSCALL_GUI_EVENT_MOUSE_MOVE) {
            return 0;
        }
        if (window->events[window->event_head].type == SYSCALL_GUI_EVENT_MOUSE_MOVE) {
            window->event_head = (window->event_head + 1U) % DISPLAY_GUI_EVENT_QUEUE_CAP;
            window->event_count--;
        } else {
            return -KERR_NOMEM;
        }
    }

    slot = (window->event_head + window->event_count) % DISPLAY_GUI_EVENT_QUEUE_CAP;
    window->events[slot] = *event;
    window->event_count++;
    return 0;
}

static void display_gui_emit_simple_event(int window_index,
                                          uint32_t type,
                                          int32_t x,
                                          int32_t y,
                                          uint32_t button,
                                          uint32_t buttons,
                                          uint32_t keycode,
                                          uint32_t ch,
                                          uint32_t modifiers) {
    struct display_gui_window *window;
    struct syscall_gui_event event;

    if (window_index < 0 || window_index >= (int)DISPLAY_GUI_MAX_WINDOWS) {
        return;
    }
    window = &g_display_gui.windows[window_index];
    if (!window->used) {
        return;
    }

    event.type = type;
    event.window_id = window->window_id;
    event.x = x;
    event.y = y;
    event.button = button;
    event.buttons = buttons;
    event.keycode = keycode;
    event.ch = ch;
    event.modifiers = modifiers;
    event.v0 = 0;
    event.v1 = 0;
    (void)display_gui_window_queue_event(window, &event);
}

static void display_gui_remove_from_z_order(int window_index) {
    for (uint32_t i = 0U; i < g_display_gui.z_count; i++) {
        if ((int)g_display_gui.z_order[i] != window_index) {
            continue;
        }
        for (uint32_t j = i + 1U; j < g_display_gui.z_count; j++) {
            g_display_gui.z_order[j - 1U] = g_display_gui.z_order[j];
        }
        g_display_gui.z_count--;
        return;
    }
}

static void display_gui_raise_window(int window_index) {
    if (window_index < 0 || window_index >= (int)DISPLAY_GUI_MAX_WINDOWS) {
        return;
    }
    display_gui_remove_from_z_order(window_index);
    if (g_display_gui.z_count < DISPLAY_GUI_MAX_WINDOWS) {
        g_display_gui.z_order[g_display_gui.z_count++] = (uint32_t)window_index;
    }
}

static void display_gui_focus_window(int window_index) {
    int old_focus = g_display_gui.focused_index;

    if (window_index == old_focus) {
        display_gui_raise_window(window_index);
        return;
    }
    if (old_focus >= 0) {
        display_gui_emit_simple_event(old_focus,
                                      SYSCALL_GUI_EVENT_BLUR,
                                      0,
                                      0,
                                      0U,
                                      g_display_gui.buttons,
                                      SYSCALL_GUI_KEY_NONE,
                                      0U,
                                      0U);
    }
    g_display_gui.focused_index = window_index;
    if (window_index >= 0) {
        display_gui_raise_window(window_index);
        display_gui_emit_simple_event(window_index,
                                      SYSCALL_GUI_EVENT_FOCUS,
                                      0,
                                      0,
                                      0U,
                                      g_display_gui.buttons,
                                      SYSCALL_GUI_KEY_NONE,
                                      0U,
                                      0U);
    }
}

static int display_gui_screen_to_client(const struct display_gui_window *window,
                                        uint32_t screen_x,
                                        uint32_t screen_y,
                                        int32_t *out_x,
                                        int32_t *out_y) {
    uint32_t client_left;
    uint32_t client_top;

    if (!window || !out_x || !out_y) {
        return 0;
    }

    client_left = window->x + DISPLAY_GUI_FRAME_BORDER + DISPLAY_GUI_CLIENT_INSET;
    client_top = window->y + DISPLAY_GUI_FRAME_BORDER + DISPLAY_GUI_TITLE_H + DISPLAY_GUI_CLIENT_INSET;
    if (screen_x < client_left || screen_y < client_top) {
        return 0;
    }
    if (screen_x >= client_left + window->width || screen_y >= client_top + window->height) {
        return 0;
    }

    *out_x = (int32_t)(screen_x - client_left);
    *out_y = (int32_t)(screen_y - client_top);
    return 1;
}

static int display_gui_window_at(uint32_t x, uint32_t y) {
    for (uint32_t order = g_display_gui.z_count; order > 0U; order--) {
        int idx = (int)g_display_gui.z_order[order - 1U];
        const struct display_gui_window *window;
        int total_w;
        int total_h;

        if (idx < 0 || idx >= (int)DISPLAY_GUI_MAX_WINDOWS) {
            continue;
        }
        window = &g_display_gui.windows[idx];
        if (!window->used) {
            continue;
        }
        total_w = display_gui_window_total_width(window);
        total_h = display_gui_window_total_height(window);
        if (x >= window->x && y >= window->y &&
            x < window->x + (uint32_t)total_w &&
            y < window->y + (uint32_t)total_h) {
            return idx;
        }
    }
    return -1;
}

static int display_gui_close_hit(const struct display_gui_window *window, uint32_t x, uint32_t y) {
    uint32_t close_x;
    uint32_t close_y;

    if (!window) {
        return 0;
    }
    close_x = window->x + DISPLAY_GUI_FRAME_BORDER + window->width - DISPLAY_GUI_CLOSE_W - 6U;
    close_y = window->y + 5U;
    return x >= close_x && y >= close_y &&
           x < close_x + DISPLAY_GUI_CLOSE_W &&
           y < close_y + DISPLAY_GUI_CLOSE_H;
}

static int display_gui_title_hit(const struct display_gui_window *window, uint32_t x, uint32_t y) {
    if (!window) {
        return 0;
    }
    if (x < window->x + DISPLAY_GUI_FRAME_BORDER ||
        x >= window->x + DISPLAY_GUI_FRAME_BORDER + window->width ||
        y < window->y + DISPLAY_GUI_FRAME_BORDER ||
        y >= window->y + DISPLAY_GUI_FRAME_BORDER + DISPLAY_GUI_TITLE_H) {
        return 0;
    }
    return !display_gui_close_hit(window, x, y);
}

static void display_gui_window_bounds(const struct display_gui_window *window,
                                      struct display_gui_dirty_rect *out_rect) {
    if (!out_rect) {
        return;
    }
    out_rect->x = 0U;
    out_rect->y = 0U;
    out_rect->w = 0U;
    out_rect->h = 0U;
    if (!window || !window->used) {
        return;
    }

    out_rect->x = window->x;
    out_rect->y = window->y;
    out_rect->w = (uint32_t)display_gui_window_total_width(window) + 4U;
    out_rect->h = (uint32_t)display_gui_window_total_height(window) + 4U;
}

static void display_gui_cursor_bounds_at(uint32_t x,
                                         uint32_t y,
                                         struct display_gui_dirty_rect *out_rect) {
    if (!out_rect) {
        return;
    }
    out_rect->x = x;
    out_rect->y = y;
    out_rect->w = DISPLAY_GUI_CURSOR_W;
    out_rect->h = DISPLAY_GUI_CURSOR_H;
}

static int display_gui_rect_intersects(const struct display_gui_dirty_rect *a,
                                       const struct display_gui_dirty_rect *b) {
    if (!a || !b || a->w == 0U || a->h == 0U || b->w == 0U || b->h == 0U) {
        return 0;
    }
    return a->x < b->x + b->w &&
           b->x < a->x + a->w &&
           a->y < b->y + b->h &&
           b->y < a->y + a->h;
}

static void display_gui_rect_union(struct display_gui_dirty_rect *dst,
                                   const struct display_gui_dirty_rect *src) {
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;

    if (!dst || !src || src->w == 0U || src->h == 0U) {
        return;
    }
    if (dst->w == 0U || dst->h == 0U) {
        *dst = *src;
        return;
    }

    x0 = display_min_u32(dst->x, src->x);
    y0 = display_min_u32(dst->y, src->y);
    x1 = display_max_u32(dst->x + dst->w, src->x + src->w);
    y1 = display_max_u32(dst->y + dst->h, src->y + src->h);
    dst->x = x0;
    dst->y = y0;
    dst->w = x1 - x0;
    dst->h = y1 - y0;
}

static void display_gui_mark_dirty_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    struct display_gui_dirty_rect rect;

    if (!g_display_gui.active || !display_gui_clip_rect(&x, &y, &w, &h)) {
        return;
    }

    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    for (uint32_t i = 0U; i < g_display_gui.dirty_count; i++) {
        if (display_gui_rect_intersects(&g_display_gui.dirty[i], &rect)) {
            display_gui_rect_union(&g_display_gui.dirty[i], &rect);
            return;
        }
    }
    if (g_display_gui.dirty_count >= DISPLAY_GUI_DIRTY_MAX) {
        g_display_gui.dirty_count = 1U;
        g_display_gui.dirty[0].x = 0U;
        g_display_gui.dirty[0].y = 0U;
        g_display_gui.dirty[0].w = g_display_fb.info.width;
        g_display_gui.dirty[0].h = g_display_fb.info.height;
        return;
    }

    g_display_gui.dirty[g_display_gui.dirty_count++] = rect;
}

static void display_gui_mark_dirty(const struct display_gui_dirty_rect *rect) {
    if (!rect) {
        return;
    }
    display_gui_mark_dirty_rect(rect->x, rect->y, rect->w, rect->h);
}

static void display_gui_write_fb_pixel(uint32_t x, uint32_t y, uint32_t pixel) {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint32_t packed;
    uint32_t offset;

    if (!g_display_fb.ready || !g_display_fb.base) {
        return;
    }
    if (x >= g_display_fb.info.width || y >= g_display_fb.info.height) {
        return;
    }
    if (g_display_gui.active && g_display_gui.clip_active &&
        (x < g_display_gui.clip.x ||
         y < g_display_gui.clip.y ||
         x >= g_display_gui.clip.x + g_display_gui.clip.w ||
         y >= g_display_gui.clip.y + g_display_gui.clip.h)) {
        return;
    }

    r = (uint8_t)((pixel >> 16) & 0xFFU);
    g = (uint8_t)((pixel >> 8) & 0xFFU);
    b = (uint8_t)(pixel & 0xFFU);
    packed = display_framebuffer_pack_rgb(r, g, b);
    offset = y * g_display_fb.info.pitch + x * g_display_fb.bytes_per_pixel;
    if (g_display_fb.bytes_per_pixel >= 4U) {
        *((uint32_t *)(uintptr_t)(g_display_fb.base + offset)) = packed;
    } else if (g_display_fb.bytes_per_pixel == 3U) {
        g_display_fb.base[offset + 0U] = (uint8_t)(packed & 0xFFU);
        g_display_fb.base[offset + 1U] = (uint8_t)((packed >> 8) & 0xFFU);
        g_display_fb.base[offset + 2U] = (uint8_t)((packed >> 16) & 0xFFU);
    }
}

static void display_gui_blit_surface(const struct display_gui_window *window) {
    uint32_t client_left;
    uint32_t client_top;
    uint32_t dst_x;
    uint32_t dst_y;
    uint32_t blit_w;
    uint32_t blit_h;
    uint32_t src_x;
    uint32_t src_y;

    if (!window || !window->surface) {
        return;
    }

    client_left = window->x + DISPLAY_GUI_FRAME_BORDER + DISPLAY_GUI_CLIENT_INSET;
    client_top = window->y + DISPLAY_GUI_FRAME_BORDER + DISPLAY_GUI_TITLE_H + DISPLAY_GUI_CLIENT_INSET;
    dst_x = client_left;
    dst_y = client_top;
    blit_w = window->width;
    blit_h = window->height;
    if (!display_gui_clip_rect(&dst_x, &dst_y, &blit_w, &blit_h)) {
        return;
    }

    src_x = dst_x - client_left;
    src_y = dst_y - client_top;
    for (uint32_t row = 0U; row < blit_h; row++) {
        const uint32_t *src = window->surface + ((src_y + row) * window->width) + src_x;

        for (uint32_t col = 0U; col < blit_w; col++) {
            display_gui_write_fb_pixel(dst_x + col,
                                       dst_y + row,
                                       src[col]);
        }
    }
}

static void display_gui_draw_window(const struct display_gui_window *window, int focused) {
    uint32_t frame_x;
    uint32_t frame_y;
    uint32_t frame_w;
    uint32_t frame_h;
    uint32_t frame_bg;
    uint32_t border;
    uint32_t title_bg;
    uint32_t title_fg;
    uint32_t title_shadow;
    uint32_t close_bg;
    uint32_t close_fg;
    uint32_t close_x;
    uint32_t close_y;

    if (!window || !window->used) {
        return;
    }

    frame_x = window->x;
    frame_y = window->y;
    frame_w = (uint32_t)display_gui_window_total_width(window);
    frame_h = (uint32_t)display_gui_window_total_height(window);
    if (g_display_gui.clip_active) {
        struct display_gui_dirty_rect bounds;

        display_gui_window_bounds(window, &bounds);
        if (!display_gui_rect_intersects(&bounds, &g_display_gui.clip)) {
            return;
        }
    }

    frame_bg = focused ? display_framebuffer_pack_rgb(14U, 18U, 30U)
                       : display_framebuffer_pack_rgb(8U, 11U, 20U);
    border = focused ? display_framebuffer_pack_rgb(82U, 168U, 236U)
                     : display_framebuffer_pack_rgb(58U, 72U, 96U);
    title_bg = focused ? display_framebuffer_pack_rgb(18U, 28U, 46U)
                       : display_framebuffer_pack_rgb(16U, 20U, 32U);
    title_fg = display_framebuffer_pack_rgb(238U, 244U, 252U);
    title_shadow = display_framebuffer_pack_rgb(4U, 8U, 14U);
    close_bg = focused ? display_framebuffer_pack_rgb(164U, 62U, 58U)
                       : display_framebuffer_pack_rgb(92U, 50U, 52U);
    close_fg = display_framebuffer_pack_rgb(255U, 238U, 236U);

    display_framebuffer_fill_rect_packed(frame_x + 4U, frame_y + 4U, frame_w, frame_h,
                                         display_framebuffer_pack_rgb(2U, 4U, 8U));
    display_framebuffer_fill_rect_packed(frame_x, frame_y, frame_w, frame_h, border);
    display_framebuffer_fill_rect_packed(frame_x + DISPLAY_GUI_FRAME_BORDER,
                                         frame_y + DISPLAY_GUI_FRAME_BORDER,
                                         frame_w - (DISPLAY_GUI_FRAME_BORDER * 2U),
                                         frame_h - (DISPLAY_GUI_FRAME_BORDER * 2U),
                                         frame_bg);
    display_framebuffer_fill_rect_packed(frame_x + DISPLAY_GUI_FRAME_BORDER,
                                         frame_y + DISPLAY_GUI_FRAME_BORDER,
                                         window->width,
                                         DISPLAY_GUI_TITLE_H,
                                         title_bg);
    display_framebuffer_draw_text_emphasized_packed(frame_x + 8U,
                                                    frame_y + 4U,
                                                    window->title,
                                                    title_fg,
                                                    title_bg,
                                                    title_shadow);
    close_x = frame_x + DISPLAY_GUI_FRAME_BORDER + window->width - DISPLAY_GUI_CLOSE_W - 6U;
    close_y = frame_y + 5U;
    display_framebuffer_fill_rect_packed(close_x, close_y, DISPLAY_GUI_CLOSE_W, DISPLAY_GUI_CLOSE_H, close_bg);
    display_framebuffer_draw_text_emphasized_packed(close_x + 5U,
                                                    close_y + 1U,
                                                    "X",
                                                    close_fg,
                                                    close_bg,
                                                    title_shadow);
    display_gui_blit_surface(window);
}

static void display_gui_draw_cursor(void) {
    uint32_t x = g_display_gui.cursor_x;
    uint32_t y = g_display_gui.cursor_y;
    uint32_t white = display_framebuffer_pack_rgb(248U, 252U, 255U);
    uint32_t dark = display_framebuffer_pack_rgb(10U, 14U, 22U);

    display_framebuffer_fill_rect_packed(x, y, 1U, 12U, white);
    display_framebuffer_fill_rect_packed(x, y, 8U, 1U, white);
    display_framebuffer_fill_rect_packed(x + 1U, y + 1U, 1U, 9U, dark);
    display_framebuffer_fill_rect_packed(x + 1U, y + 1U, 5U, 1U, dark);
}

static void display_gui_draw_scene(void) {
    uint32_t desktop_bg;
    uint32_t header_bg;
    uint32_t header_fg;
    uint32_t header_shadow;
    const char *header_title = "SKEZOS GUI SESSION";
    const char *header_hint = "CLICK TO FOCUS, DRAG TITLE";
    uint32_t title_right;
    uint32_t hint_width;
    uint32_t hint_x;

    if (!g_display_gui.active || !g_display_fb.ready) {
        return;
    }

    desktop_bg = display_framebuffer_pack_rgb(5U, 8U, 16U);
    header_bg = display_framebuffer_pack_rgb(10U, 16U, 28U);
    header_fg = display_framebuffer_pack_rgb(224U, 233U, 246U);
    header_shadow = display_framebuffer_pack_rgb(2U, 6U, 10U);
    display_framebuffer_fill_rect_packed(0U, 0U, g_display_fb.info.width, g_display_fb.info.height, desktop_bg);
    display_framebuffer_fill_rect_packed(0U, 0U, g_display_fb.info.width, DISPLAY_GUI_DESKTOP_TOP, header_bg);
    display_framebuffer_fill_rect_packed(0U, DISPLAY_GUI_DESKTOP_TOP - 2U, g_display_fb.info.width, 2U,
                                         display_framebuffer_pack_rgb(82U, 168U, 236U));
    display_framebuffer_draw_text_emphasized_packed(12U,
                                                    5U,
                                                    header_title,
                                                    header_fg,
                                                    header_bg,
                                                    header_shadow);
    title_right = 12U + (display_string_length(header_title) * DISPLAY_FB_CHAR_W);
    hint_width = display_string_length(header_hint) * DISPLAY_FB_CHAR_W;
    if (g_display_fb.info.width > hint_width + 12U) {
        hint_x = g_display_fb.info.width - hint_width - 12U;
        if (hint_x > title_right + DISPLAY_FB_CHAR_W) {
            display_framebuffer_draw_text_packed(hint_x, 5U, header_hint, header_fg, header_bg);
        }
    }

    for (uint32_t i = 0U; i < g_display_gui.z_count; i++) {
        int idx = (int)g_display_gui.z_order[i];
        if (idx < 0 || idx >= (int)DISPLAY_GUI_MAX_WINDOWS) {
            continue;
        }
        display_gui_draw_window(&g_display_gui.windows[idx], idx == g_display_gui.focused_index);
    }
    display_gui_draw_cursor();
}

static void display_gui_redraw(void) {
    if (!g_display_gui.active || !g_display_fb.ready) {
        return;
    }
    g_display_gui.clip_active = 0;
    g_display_gui.dirty_count = 0U;
    display_gui_draw_scene();
}

static void display_gui_redraw_rect(const struct display_gui_dirty_rect *rect) {
    if (!g_display_gui.active || !g_display_fb.ready || !rect || rect->w == 0U || rect->h == 0U) {
        return;
    }

    g_display_gui.clip = *rect;
    g_display_gui.clip_active = 1;
    display_gui_draw_scene();
    g_display_gui.clip_active = 0;
}

static void display_gui_flush_dirty(void) {
    uint32_t count = g_display_gui.dirty_count;

    if (count == 0U) {
        return;
    }
    for (uint32_t i = 0U; i < count; i++) {
        display_gui_redraw_rect(&g_display_gui.dirty[i]);
    }
    g_display_gui.dirty_count = 0U;
}

static void display_gui_destroy_window_index(int index) {
    struct display_gui_window *window;

    if (index < 0 || index >= (int)DISPLAY_GUI_MAX_WINDOWS) {
        return;
    }
    window = &g_display_gui.windows[index];
    if (!window->used) {
        return;
    }

    if (window->surface) {
        kfree(window->surface);
    }
    window->surface = 0;
    window->used = 0;
    window->window_id = 0;
    window->owner_pid = -1;
    window->title[0] = '\0';
    window->event_head = 0U;
    window->event_count = 0U;
    display_gui_remove_from_z_order(index);
    if (g_display_gui.focused_index == index) {
        g_display_gui.focused_index = -1;
        if (g_display_gui.z_count != 0U) {
            g_display_gui.focused_index = (int)g_display_gui.z_order[g_display_gui.z_count - 1U];
        }
    }
    if (g_display_gui.drag_index == index) {
        g_display_gui.drag_index = -1;
    }
}

void display_gui_enable(void) {
    if (g_display_mode != DISPLAY_MODE_FRAMEBUFFER || g_display_fb.ready == 0U) {
        return;
    }
    if (g_display_fb.bytes_per_pixel < 3U) {
        KLOGW("display: gui mode requires packed RGB framebuffer");
        return;
    }

    memset(&g_display_gui, 0, sizeof(g_display_gui));
    g_display_gui.active = 1;
    g_display_gui.focused_index = -1;
    g_display_gui.drag_index = -1;
    g_display_gui.next_window_id = 1;
    g_display_gui.cursor_x = g_display_fb.info.width / 2U;
    g_display_gui.cursor_y = g_display_fb.info.height / 2U;
    display_gui_redraw();
    KLOGI("display: gui compositor enabled");
}

int display_gui_mode_active(void) {
    return g_display_gui.active;
}

int display_gui_create_window(const struct syscall_gui_create_req *req, int owner_pid, int *out_window_id) {
    struct display_gui_window *window;
    struct display_gui_dirty_rect dirty;
    int index;
    int old_focus;
    uint32_t title_len;
    uint32_t surface_words;
    uint32_t cascade_index;
    uint32_t max_x;
    uint32_t max_y;

    if (!g_display_gui.active || !req || !out_window_id) {
        return -KERR_NOTSUP;
    }
    *out_window_id = -1;
    if (owner_pid <= 0 || req->width == 0U || req->height == 0U) {
        return -KERR_INVAL;
    }
    if (req->width > DISPLAY_GUI_MAX_WIDTH || req->height > DISPLAY_GUI_MAX_HEIGHT) {
        return -KERR_INVAL;
    }
    if (req->flags != 0U) {
        return -KERR_INVAL;
    }
    if (display_gui_find_index_by_owner(owner_pid) >= 0) {
        return -KERR_NOTSUP;
    }

    index = display_gui_find_free_index();
    if (index < 0) {
        return -KERR_NOMEM;
    }

    window = &g_display_gui.windows[index];
    memset(window, 0, sizeof(*window));
    surface_words = req->width * req->height;
    window->surface = (uint32_t *)kmalloc(surface_words * (uint32_t)sizeof(uint32_t));
    if (!window->surface) {
        return -KERR_NOMEM;
    }
    memset(window->surface, 0x00, surface_words * (uint32_t)sizeof(uint32_t));

    window->used = 1;
    window->window_id = g_display_gui.next_window_id++;
    window->owner_pid = owner_pid;
    window->width = req->width;
    window->height = req->height;
    title_len = req->title_len;
    if (title_len > DISPLAY_GUI_TITLE_MAX) {
        title_len = DISPLAY_GUI_TITLE_MAX;
    }
    if (title_len != 0U) {
        int rc = uaccess_copy_from_user(window->title, req->title_ptr, title_len);
        if (rc < 0) {
            kfree(window->surface);
            memset(window, 0, sizeof(*window));
            return rc;
        }
    }
    window->title[title_len] = '\0';
    if (window->title[0] == '\0') {
        memcpy(window->title, "WINDOW", 7U);
    }

    cascade_index = g_display_gui.z_count;
    window->x = 36U + (cascade_index * 34U);
    window->y = DISPLAY_GUI_DESKTOP_TOP + 18U + (cascade_index * 28U);
    max_x = g_display_fb.info.width - (uint32_t)display_gui_window_total_width(window) - DISPLAY_GUI_DESKTOP_PAD;
    max_y = g_display_fb.info.height - (uint32_t)display_gui_window_total_height(window) - DISPLAY_GUI_DESKTOP_PAD;
    if (window->x > max_x) {
        window->x = max_x;
    }
    if (window->y > max_y) {
        window->y = max_y;
    }

    display_gui_raise_window(index);
    old_focus = g_display_gui.focused_index;
    display_gui_focus_window(index);
    if (old_focus >= 0 && old_focus != index) {
        display_gui_window_bounds(&g_display_gui.windows[old_focus], &dirty);
        display_gui_mark_dirty(&dirty);
    }
    display_gui_emit_simple_event(index,
                                  SYSCALL_GUI_EVENT_PAINT,
                                  0,
                                  0,
                                  0U,
                                  g_display_gui.buttons,
                                  SYSCALL_GUI_KEY_NONE,
                                  0U,
                                  0U);
    display_gui_window_bounds(window, &dirty);
    display_gui_mark_dirty(&dirty);
    display_gui_cursor_bounds_at(g_display_gui.cursor_x, g_display_gui.cursor_y, &dirty);
    display_gui_mark_dirty(&dirty);
    display_gui_flush_dirty();
    *out_window_id = window->window_id;
    return 0;
}

int display_gui_flush_window(const struct syscall_gui_flush_req *req, int owner_pid) {
    struct display_gui_window *window;
    uint32_t client_left;
    uint32_t client_top;
    int index;

    if (!g_display_gui.active || !req) {
        return -KERR_NOTSUP;
    }
    index = display_gui_find_index_by_id(req->window_id);
    if (index < 0) {
        return -KERR_NOENT;
    }
    window = &g_display_gui.windows[index];
    if (window->owner_pid != owner_pid) {
        return -KERR_NOTSUP;
    }
    if (req->pixels_ptr == 0U || req->stride == 0U) {
        return -KERR_INVAL;
    }
    if (req->rect.x < 0 || req->rect.y < 0) {
        return -KERR_INVAL;
    }
    if ((uint32_t)req->rect.x > window->width || (uint32_t)req->rect.y > window->height) {
        return -KERR_INVAL;
    }
    if (req->rect.w == 0U || req->rect.h == 0U) {
        return 0;
    }
    if (req->stride < window->width) {
        return -KERR_INVAL;
    }
    if ((uint32_t)req->rect.x + req->rect.w > window->width ||
        (uint32_t)req->rect.y + req->rect.h > window->height) {
        return -KERR_INVAL;
    }

    for (uint32_t row = 0U; row < req->rect.h; row++) {
        uint64_t src_word_offset = ((uint64_t)(uint32_t)req->rect.y + row) * req->stride +
                                   (uint64_t)(uint32_t)req->rect.x;
        uint64_t src_addr = (uint64_t)req->pixels_ptr + (src_word_offset * 4U);
        int rc;

        if (src_addr > 0xFFFFFFFFULL) {
            return -KERR_FAULT;
        }
        rc = uaccess_copy_from_user(window->surface + (((uint32_t)req->rect.y + row) * window->width) +
                                        (uint32_t)req->rect.x,
                                    (uint32_t)src_addr,
                                    req->rect.w * 4U);
        if (rc < 0) {
            return rc;
        }
    }

    client_left = window->x + DISPLAY_GUI_FRAME_BORDER + DISPLAY_GUI_CLIENT_INSET;
    client_top = window->y + DISPLAY_GUI_FRAME_BORDER + DISPLAY_GUI_TITLE_H + DISPLAY_GUI_CLIENT_INSET;
    display_gui_mark_dirty_rect(client_left + (uint32_t)req->rect.x,
                                client_top + (uint32_t)req->rect.y,
                                req->rect.w,
                                req->rect.h);
    display_gui_flush_dirty();
    return 0;
}

int display_gui_poll_event(int window_id, int owner_pid, struct syscall_gui_event *out_event) {
    struct display_gui_window *window;
    uint32_t slot;
    int index;

    if (!g_display_gui.active || !out_event) {
        return -KERR_NOTSUP;
    }
    index = display_gui_find_index_by_id(window_id);
    if (index < 0) {
        return -KERR_NOENT;
    }
    window = &g_display_gui.windows[index];
    if (window->owner_pid != owner_pid) {
        return -KERR_NOTSUP;
    }
    if (window->event_count == 0U) {
        return 0;
    }

    slot = window->event_head;
    *out_event = window->events[slot];
    window->event_head = (window->event_head + 1U) % DISPLAY_GUI_EVENT_QUEUE_CAP;
    window->event_count--;
    return 1;
}

int display_gui_destroy_window(int window_id, int owner_pid) {
    int index = display_gui_find_index_by_id(window_id);
    struct display_gui_dirty_rect dirty;

    if (!g_display_gui.active) {
        return -KERR_NOTSUP;
    }
    if (index < 0) {
        return -KERR_NOENT;
    }
    if (g_display_gui.windows[index].owner_pid != owner_pid) {
        return -KERR_NOTSUP;
    }

    display_gui_window_bounds(&g_display_gui.windows[index], &dirty);
    display_gui_destroy_window_index(index);
    display_gui_mark_dirty(&dirty);
    if (g_display_gui.focused_index >= 0) {
        display_gui_window_bounds(&g_display_gui.windows[g_display_gui.focused_index], &dirty);
        display_gui_mark_dirty(&dirty);
    }
    display_gui_cursor_bounds_at(g_display_gui.cursor_x, g_display_gui.cursor_y, &dirty);
    display_gui_mark_dirty(&dirty);
    display_gui_flush_dirty();
    return 0;
}

void display_gui_notify_task_exit(int owner_pid) {
    struct display_gui_dirty_rect dirty;

    if (!g_display_gui.active || owner_pid <= 0) {
        return;
    }
    for (uint32_t i = 0U; i < DISPLAY_GUI_MAX_WINDOWS; i++) {
        if (g_display_gui.windows[i].used && g_display_gui.windows[i].owner_pid == owner_pid) {
            display_gui_window_bounds(&g_display_gui.windows[i], &dirty);
            display_gui_destroy_window_index((int)i);
            display_gui_mark_dirty(&dirty);
        }
    }
    if (g_display_gui.focused_index >= 0) {
        display_gui_window_bounds(&g_display_gui.windows[g_display_gui.focused_index], &dirty);
        display_gui_mark_dirty(&dirty);
    }
    display_gui_flush_dirty();
}

int display_gui_handle_key_event(uint32_t keycode, uint32_t ch, uint32_t modifiers, int pressed) {
    int focused = g_display_gui.focused_index;

    if (!g_display_gui.active) {
        return 0;
    }
    if (focused >= 0) {
        display_gui_emit_simple_event(focused,
                                      pressed ? SYSCALL_GUI_EVENT_KEY_DOWN : SYSCALL_GUI_EVENT_KEY_UP,
                                      0,
                                      0,
                                      0U,
                                      g_display_gui.buttons,
                                      keycode,
                                      pressed ? ch : 0U,
                                      modifiers);
    }
    return 1;
}

void display_gui_handle_mouse_motion(int32_t dx, int32_t dy) {
    int32_t next_x;
    int32_t next_y;
    uint32_t new_x;
    uint32_t new_y;
    uint32_t old_x;
    uint32_t old_y;
    struct display_gui_dirty_rect dirty;
    struct display_gui_dirty_rect old_window_dirty;
    int dragging;

    if (!g_display_gui.active) {
        return;
    }

    old_x = g_display_gui.cursor_x;
    old_y = g_display_gui.cursor_y;
    old_window_dirty.x = 0U;
    old_window_dirty.y = 0U;
    old_window_dirty.w = 0U;
    old_window_dirty.h = 0U;
    dragging = g_display_gui.drag_index >= 0 &&
               (g_display_gui.buttons & SYSCALL_GUI_BUTTON_LEFT) != 0U;
    if (dragging) {
        display_gui_window_bounds(&g_display_gui.windows[g_display_gui.drag_index], &old_window_dirty);
    }

    next_x = (int32_t)g_display_gui.cursor_x + dx;
    next_y = (int32_t)g_display_gui.cursor_y + dy;
    if (next_x < 0) {
        next_x = 0;
    }
    if (next_y < 0) {
        next_y = 0;
    }
    new_x = (uint32_t)next_x;
    new_y = (uint32_t)next_y;
    if (new_x >= g_display_fb.info.width) {
        new_x = g_display_fb.info.width - 1U;
    }
    if (new_y >= g_display_fb.info.height) {
        new_y = g_display_fb.info.height - 1U;
    }
    if (new_x == old_x && new_y == old_y && !dragging) {
        return;
    }
    g_display_gui.cursor_x = new_x;
    g_display_gui.cursor_y = new_y;

    if (dragging) {
        struct display_gui_window *window = &g_display_gui.windows[g_display_gui.drag_index];
        int32_t moved_x = (int32_t)g_display_gui.drag_window_x +
                          ((int32_t)new_x - (int32_t)g_display_gui.drag_grab_x);
        int32_t moved_y = (int32_t)g_display_gui.drag_window_y +
                          ((int32_t)new_y - (int32_t)g_display_gui.drag_grab_y);
        uint32_t max_x;
        uint32_t max_y;

        if (moved_x < (int32_t)DISPLAY_GUI_DESKTOP_PAD) {
            moved_x = (int32_t)DISPLAY_GUI_DESKTOP_PAD;
        }
        if (moved_y < (int32_t)DISPLAY_GUI_DESKTOP_TOP) {
            moved_y = (int32_t)DISPLAY_GUI_DESKTOP_TOP;
        }
        window->x = (uint32_t)moved_x;
        window->y = (uint32_t)moved_y;
        max_x = g_display_fb.info.width - (uint32_t)display_gui_window_total_width(window) - DISPLAY_GUI_DESKTOP_PAD;
        max_y = g_display_fb.info.height - (uint32_t)display_gui_window_total_height(window) - DISPLAY_GUI_DESKTOP_PAD;
        if (window->x > max_x) {
            window->x = max_x;
        }
        if (window->y > max_y) {
            window->y = max_y;
        }
        display_gui_mark_dirty(&old_window_dirty);
        display_gui_window_bounds(window, &dirty);
        display_gui_mark_dirty(&dirty);
    } else if (g_display_gui.focused_index >= 0) {
        int32_t client_x;
        int32_t client_y;
        if (display_gui_screen_to_client(&g_display_gui.windows[g_display_gui.focused_index],
                                         new_x,
                                         new_y,
                                         &client_x,
                                         &client_y)) {
            display_gui_emit_simple_event(g_display_gui.focused_index,
                                          SYSCALL_GUI_EVENT_MOUSE_MOVE,
                                          client_x,
                                          client_y,
                                          0U,
                                          g_display_gui.buttons,
                                          SYSCALL_GUI_KEY_NONE,
                                          0U,
                                          0U);
        }
    }

    display_gui_cursor_bounds_at(old_x, old_y, &dirty);
    display_gui_mark_dirty(&dirty);
    display_gui_cursor_bounds_at(new_x, new_y, &dirty);
    display_gui_mark_dirty(&dirty);
    display_gui_flush_dirty();
}

void display_gui_handle_mouse_buttons(uint32_t buttons) {
    uint32_t old_buttons = g_display_gui.buttons;
    uint32_t changed = old_buttons ^ buttons;
    int target_index;
    int old_focus;
    struct display_gui_dirty_rect dirty;

    if (!g_display_gui.active || changed == 0U) {
        g_display_gui.buttons = buttons;
        return;
    }

    g_display_gui.buttons = buttons;
    target_index = display_gui_window_at(g_display_gui.cursor_x, g_display_gui.cursor_y);
    old_focus = g_display_gui.focused_index;

    if ((changed & SYSCALL_GUI_BUTTON_LEFT) != 0U &&
        (buttons & SYSCALL_GUI_BUTTON_LEFT) != 0U) {
        if (target_index >= 0) {
            struct display_gui_window *window = &g_display_gui.windows[target_index];
            int32_t client_x;
            int32_t client_y;

            display_gui_focus_window(target_index);
            if (old_focus >= 0 && old_focus != target_index) {
                display_gui_window_bounds(&g_display_gui.windows[old_focus], &dirty);
                display_gui_mark_dirty(&dirty);
            }
            display_gui_window_bounds(window, &dirty);
            display_gui_mark_dirty(&dirty);
            if (display_gui_close_hit(window, g_display_gui.cursor_x, g_display_gui.cursor_y)) {
                display_gui_emit_simple_event(target_index,
                                              SYSCALL_GUI_EVENT_CLOSE,
                                              0,
                                              0,
                                              SYSCALL_GUI_BUTTON_LEFT,
                                              buttons,
                                              SYSCALL_GUI_KEY_NONE,
                                              0U,
                                              0U);
            } else if (display_gui_title_hit(window, g_display_gui.cursor_x, g_display_gui.cursor_y)) {
                g_display_gui.drag_index = target_index;
                g_display_gui.drag_grab_x = g_display_gui.cursor_x;
                g_display_gui.drag_grab_y = g_display_gui.cursor_y;
                g_display_gui.drag_window_x = window->x;
                g_display_gui.drag_window_y = window->y;
            } else if (display_gui_screen_to_client(window,
                                                    g_display_gui.cursor_x,
                                                    g_display_gui.cursor_y,
                                                    &client_x,
                                                    &client_y)) {
                display_gui_emit_simple_event(target_index,
                                              SYSCALL_GUI_EVENT_MOUSE_DOWN,
                                              client_x,
                                              client_y,
                                              SYSCALL_GUI_BUTTON_LEFT,
                                              buttons,
                                              SYSCALL_GUI_KEY_NONE,
                                              0U,
                                              0U);
            }
        }
    }

    if ((changed & SYSCALL_GUI_BUTTON_RIGHT) != 0U &&
        (buttons & SYSCALL_GUI_BUTTON_RIGHT) != 0U &&
        target_index >= 0) {
        struct display_gui_window *window = &g_display_gui.windows[target_index];
        int32_t client_x;
        int32_t client_y;

        display_gui_focus_window(target_index);
        if (old_focus >= 0 && old_focus != target_index) {
            display_gui_window_bounds(&g_display_gui.windows[old_focus], &dirty);
            display_gui_mark_dirty(&dirty);
        }
        display_gui_window_bounds(window, &dirty);
        display_gui_mark_dirty(&dirty);
        if (display_gui_screen_to_client(window,
                                         g_display_gui.cursor_x,
                                         g_display_gui.cursor_y,
                                         &client_x,
                                         &client_y)) {
            display_gui_emit_simple_event(target_index,
                                          SYSCALL_GUI_EVENT_MOUSE_DOWN,
                                          client_x,
                                          client_y,
                                          SYSCALL_GUI_BUTTON_RIGHT,
                                          buttons,
                                          SYSCALL_GUI_KEY_NONE,
                                          0U,
                                          0U);
        }
    }

    if ((changed & SYSCALL_GUI_BUTTON_LEFT) != 0U &&
        (buttons & SYSCALL_GUI_BUTTON_LEFT) == 0U) {
        if (g_display_gui.drag_index >= 0) {
            g_display_gui.drag_index = -1;
        } else if (g_display_gui.focused_index >= 0) {
            int32_t client_x;
            int32_t client_y;
            if (display_gui_screen_to_client(&g_display_gui.windows[g_display_gui.focused_index],
                                             g_display_gui.cursor_x,
                                             g_display_gui.cursor_y,
                                             &client_x,
                                             &client_y)) {
                display_gui_emit_simple_event(g_display_gui.focused_index,
                                              SYSCALL_GUI_EVENT_MOUSE_UP,
                                              client_x,
                                              client_y,
                                              SYSCALL_GUI_BUTTON_LEFT,
                                              buttons,
                                              SYSCALL_GUI_KEY_NONE,
                                              0U,
                                              0U);
            }
        }
    }

    if ((changed & SYSCALL_GUI_BUTTON_RIGHT) != 0U &&
        (buttons & SYSCALL_GUI_BUTTON_RIGHT) == 0U &&
        g_display_gui.focused_index >= 0) {
        int32_t client_x;
        int32_t client_y;
        if (display_gui_screen_to_client(&g_display_gui.windows[g_display_gui.focused_index],
                                         g_display_gui.cursor_x,
                                         g_display_gui.cursor_y,
                                         &client_x,
                                         &client_y)) {
            display_gui_emit_simple_event(g_display_gui.focused_index,
                                          SYSCALL_GUI_EVENT_MOUSE_UP,
                                          client_x,
                                          client_y,
                                          SYSCALL_GUI_BUTTON_RIGHT,
                                          buttons,
                                          SYSCALL_GUI_KEY_NONE,
                                          0U,
                                          0U);
        }
    }

    display_gui_flush_dirty();
}
