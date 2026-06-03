#include <stdint.h>

#include "guilib.h"

#define DEMO_WIDTH 260U
#define DEMO_HEIGHT 180U

static struct gui_window g_demo_window;
static uint32_t g_demo_pixels[DEMO_WIDTH * DEMO_HEIGHT];
static uint32_t g_demo_clicks;
static uint32_t g_demo_focus;
static uint32_t g_demo_phase;
static uint32_t g_demo_toggle;

static void demo_write_u32(char *dst, uint32_t cap, uint32_t value) {
    char tmp[16];
    uint32_t len = 0U;
    uint32_t out = 0U;

    if (!dst || cap == 0U) {
        return;
    }
    if (value == 0U) {
        if (cap > 1U) {
            dst[0] = '0';
            dst[1] = '\0';
        }
        return;
    }
    while (value != 0U && len < sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (len > 0U && out + 1U < cap) {
        dst[out++] = tmp[--len];
    }
    dst[out] = '\0';
}

static void demo_append(char *dst, uint32_t cap, const char *text) {
    uint32_t len = user_strlen(dst);
    uint32_t idx = 0U;

    while (text && text[idx] != '\0' && len + 1U < cap) {
        dst[len++] = gui_upper_char(text[idx++]);
    }
    dst[len] = '\0';
}

static void demo_render(void) {
    uint32_t bg = gui_rgb(14U, 18U, g_demo_toggle ? 42U : 28U);
    uint32_t panel = gui_rgb(22U, 28U, 44U);
    uint32_t fg = gui_rgb(234U, 240U, 250U);
    uint32_t muted = gui_rgb(148U, 164U, 188U);
    uint32_t accent = g_demo_focus ? gui_rgb(96U, 200U, 122U) : gui_rgb(90U, 144U, 214U);
    uint32_t bar_x = 18U + (g_demo_phase % 140U);
    char line[48];
    char num[16];

    gui_fill_rect(&g_demo_window, 0U, 0U, DEMO_WIDTH, DEMO_HEIGHT, bg);
    gui_fill_rect(&g_demo_window, 0U, 0U, DEMO_WIDTH, 24U, panel);
    gui_draw_text(&g_demo_window, 10U, 8U, "DEMO PANEL", fg, panel);
    gui_draw_text(&g_demo_window, 150U, 8U, g_demo_focus ? "FOCUSED" : "IDLE", muted, panel);

    gui_draw_text(&g_demo_window, 14U, 38U, "CLICK BOX", fg, bg);
    gui_fill_rect(&g_demo_window, 14U, 52U, 78U, 42U, panel);
    gui_stroke_rect(&g_demo_window, 14U, 52U, 78U, 42U, accent);
    gui_draw_text(&g_demo_window, 28U, 69U, "PRESS", fg, panel);

    line[0] = '\0';
    demo_append(line, sizeof(line), "CLICKS ");
    demo_write_u32(num, sizeof(num), g_demo_clicks);
    demo_append(line, sizeof(line), num);
    gui_draw_text(&g_demo_window, 112U, 38U, line, fg, bg);

    line[0] = '\0';
    demo_append(line, sizeof(line), "ARROWS SHIFT BAR");
    if (g_demo_phase != 0U) {
        demo_append(line, sizeof(line), " ");
        demo_write_u32(num, sizeof(num), g_demo_phase / 4U);
        demo_append(line, sizeof(line), num);
    }
    gui_draw_text(&g_demo_window, 112U, 52U, line, muted, bg);

    gui_fill_rect(&g_demo_window, 18U, 122U, 220U, 14U, panel);
    gui_stroke_rect(&g_demo_window, 18U, 122U, 220U, 14U, muted);
    gui_fill_rect(&g_demo_window, bar_x, 124U, 42U, 10U, accent);
    gui_draw_text(&g_demo_window, 18U, 145U, "ARROWS MOVE THE BAR", muted, bg);

    (void)gui_present_full(&g_demo_window);
}

static int demo_handle_event(const struct syscall_gui_event *event, int *out_exit) {
    if (!event || !out_exit) {
        return 0;
    }
    if (event->type == SYSCALL_GUI_EVENT_CLOSE) {
        *out_exit = 1;
        return 0;
    }
    if (event->type == SYSCALL_GUI_EVENT_FOCUS) {
        g_demo_focus = 1U;
        return 1;
    }
    if (event->type == SYSCALL_GUI_EVENT_BLUR) {
        g_demo_focus = 0U;
        return 1;
    }
    if (event->type == SYSCALL_GUI_EVENT_MOUSE_DOWN &&
        event->button == SYSCALL_GUI_BUTTON_LEFT &&
        event->x >= 14 && event->x < 92 &&
        event->y >= 52 && event->y < 94) {
        g_demo_clicks++;
        g_demo_toggle ^= 1U;
        return 1;
    }
    if (event->type == SYSCALL_GUI_EVENT_KEY_DOWN) {
        if (event->keycode == SYSCALL_GUI_KEY_LEFT && g_demo_phase > 0U) {
            g_demo_phase -= 4U;
            return 1;
        } else if (event->keycode == SYSCALL_GUI_KEY_RIGHT) {
            g_demo_phase += 4U;
            return 1;
        }
    }
    return event->type == SYSCALL_GUI_EVENT_PAINT;
}

void _start(int argc, char **argv) {
    struct syscall_gui_event event;
    int should_exit = 0;

    (void)argc;
    (void)argv;

    if (gui_window_create(&g_demo_window,
                          "GUI DEMO",
                          DEMO_WIDTH,
                          DEMO_HEIGHT,
                          g_demo_pixels) < 0) {
        user_exit(1);
    }

    demo_render();
    while (!should_exit) {
        int32_t rc = user_gui_poll(g_demo_window.id, &event);

        if (rc < 0) {
            break;
        }
        if (rc == 0) {
            user_sleep_ticks(1U);
            continue;
        }
        if (demo_handle_event(&event, &should_exit)) {
            demo_render();
        }
    }

    (void)user_gui_destroy(g_demo_window.id);
    user_exit(0);
}
