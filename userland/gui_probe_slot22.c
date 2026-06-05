#include <stdint.h>

#include "guilib.h"

#define PROBE_WIDTH 180U
#define PROBE_HEIGHT 96U
#define PROBE_MONITOR_TICKS 3000U

static struct gui_window g_probe_window;
static uint32_t g_probe_pixels[PROBE_WIDTH * PROBE_HEIGHT];

static void probe_write_all(const char *buf, uint32_t len) {
    while (len > 0U) {
        int32_t rc = user_write(USER_FD_STDOUT, buf, len);
        if (rc <= 0) {
            return;
        }
        buf += (uint32_t)rc;
        len -= (uint32_t)rc;
    }
}

static void probe_write_str(const char *text) {
    probe_write_all(text, user_strlen(text));
}

static void probe_fail(const char *label, int32_t rc) {
    (void)rc;
    probe_write_str("GUI_PROBE_FAIL ");
    probe_write_str(label);
    probe_write_str("\n");
    user_exit(1);
}

static void probe_pass(const char *label) {
    probe_write_str(label);
    probe_write_str("\n");
}

static void probe_render(uint32_t color_seed) {
    uint32_t bg = gui_rgb(16U, 20U, (uint8_t)(34U + (color_seed & 0x1FU)));
    uint32_t panel = gui_rgb(26U, 34U, 52U);
    uint32_t fg = gui_rgb(232U, 240U, 250U);
    uint32_t accent = gui_rgb(100U, 190U, 132U);

    gui_fill_rect(&g_probe_window, 0U, 0U, PROBE_WIDTH, PROBE_HEIGHT, bg);
    gui_fill_rect(&g_probe_window, 0U, 0U, PROBE_WIDTH, 20U, panel);
    gui_draw_text(&g_probe_window, 8U, 7U, "GUI PROBE", fg, panel);
    gui_draw_text(&g_probe_window, 12U, 36U, "API SELFTEST", fg, bg);
    gui_fill_rect(&g_probe_window, 12U, 58U, 130U, 14U, panel);
    gui_stroke_rect(&g_probe_window, 12U, 58U, 130U, 14U, accent);
    (void)gui_present_full(&g_probe_window);
}

static void probe_create_pressure_window(void) {
    struct syscall_gui_window_info window_info;

    if (gui_window_create(&g_probe_window,
                          "GUI PRESSURE",
                          PROBE_WIDTH,
                          PROBE_HEIGHT,
                          g_probe_pixels) < 0) {
        probe_fail("PRESSURE_CREATE", -1);
    }
    probe_render(17U);
    if (gui_query_window(&g_probe_window, &window_info) < 0 ||
        window_info.window_id != g_probe_window.id ||
        window_info.focused == 0U) {
        probe_fail("PRESSURE_INFO", -1);
    }
    probe_pass("GUI_PROBE_PRESSURE_WINDOW_OK");
}

static int32_t probe_owner_denied_child(int32_t window_id) {
    struct syscall_gui_window_info window_info;
    struct syscall_gui_flush_req flush_req;
    uint32_t pixel = gui_rgb(250U, 20U, 20U);
    int32_t rc_info;
    int32_t rc_flush;

    rc_info = user_gui_window_info(window_id, &window_info);
    flush_req.window_id = window_id;
    flush_req.pixels_ptr = (uint32_t)(uintptr_t)&pixel;
    flush_req.stride = 1U;
    flush_req.rect.x = 0;
    flush_req.rect.y = 0;
    flush_req.rect.w = 1U;
    flush_req.rect.h = 1U;
    rc_flush = user_gui_flush(&flush_req);
    if (rc_info < 0 && rc_flush < 0) {
        return 0;
    }
    return 1;
}

static void probe_selftest(uint32_t *out_start_mouse_drops,
                           uint32_t *out_start_overflow_drops) {
    struct syscall_gui_info info;
    struct syscall_gui_window_info window_info;
    struct syscall_gui_event events[SYSCALL_GUI_POLL_BATCH_MAX];
    struct syscall_gui_create_req bad_create;
    struct syscall_gui_flush_req bad_flush;
    int32_t rc;
    int32_t child;
    int32_t waited;
    int32_t status = -1;

    if (user_gui_info(&info) < 0 ||
        info.version != SYSCALL_GUI_INFO_VERSION ||
        info.active == 0U ||
        info.max_windows < 3U ||
        info.event_queue_cap == 0U) {
        probe_fail("INFO", -1);
    }
    probe_pass("GUI_PROBE_INFO_OK");

    if (gui_window_create(&g_probe_window,
                          "GUI PROBE",
                          PROBE_WIDTH,
                          PROBE_HEIGHT,
                          g_probe_pixels) < 0) {
        probe_fail("CREATE", -1);
    }
    probe_render(0U);
    probe_pass("GUI_PROBE_CREATE_OK");

    if (gui_query_window(&g_probe_window, &window_info) < 0 ||
        window_info.window_id != g_probe_window.id ||
        window_info.width != PROBE_WIDTH ||
        window_info.height != PROBE_HEIGHT) {
        probe_fail("WINDOW_INFO", -1);
    }
    probe_pass("GUI_PROBE_WINDOW_INFO_OK");

    rc = gui_poll_events(&g_probe_window, events, SYSCALL_GUI_POLL_BATCH_MAX + 4U);
    if (rc <= 0 || rc > (int32_t)SYSCALL_GUI_POLL_BATCH_MAX) {
        probe_fail("BATCH", rc);
    }
    probe_pass("GUI_PROBE_BATCH_OK");

    rc = gui_window_create(&g_probe_window,
                           "GUI PROBE DUP",
                           PROBE_WIDTH,
                           PROBE_HEIGHT,
                           g_probe_pixels);
    if (rc >= 0) {
        probe_fail("DUPLICATE", rc);
    }
    probe_pass("GUI_PROBE_DUPLICATE_OK");

    bad_create.width = 641U;
    bad_create.height = 32U;
    bad_create.title_ptr = (uint32_t)(uintptr_t)"BAD";
    bad_create.title_len = 3U;
    bad_create.flags = 0U;
    rc = user_gui_create(&bad_create);
    if (rc >= 0) {
        probe_fail("OVERSIZE", rc);
    }
    probe_pass("GUI_PROBE_OVERSIZE_OK");

    bad_flush.window_id = g_probe_window.id;
    bad_flush.pixels_ptr = (uint32_t)(uintptr_t)g_probe_pixels;
    bad_flush.stride = g_probe_window.stride;
    bad_flush.rect.x = (int32_t)(PROBE_WIDTH - 2U);
    bad_flush.rect.y = 0;
    bad_flush.rect.w = 4U;
    bad_flush.rect.h = 1U;
    rc = user_gui_flush(&bad_flush);
    if (rc >= 0) {
        probe_fail("BAD_FLUSH", rc);
    }
    probe_pass("GUI_PROBE_BAD_FLUSH_OK");

    child = user_fork();
    if (child < 0) {
        probe_fail("FORK", child);
    }
    if (child == 0) {
        user_exit(probe_owner_denied_child(g_probe_window.id));
    }
    waited = user_waitpid(child, &status, 0U);
    if (waited != child || status != 0) {
        probe_fail("OWNER_DENIED", status);
    }
    probe_pass("GUI_PROBE_OWNER_DENIED_OK");

    if (user_gui_destroy(g_probe_window.id) < 0) {
        probe_fail("DESTROY", -1);
    }
    rc = gui_query_window(&g_probe_window, &window_info);
    if (rc >= 0) {
        probe_fail("DESTROY_INFO", rc);
    }
    probe_pass("GUI_PROBE_DESTROY_OK");
    probe_pass("GUI_PROBE_SELFTEST_OK");

    if (user_gui_info(&info) < 0) {
        probe_fail("INFO_AFTER", -1);
    }
    if (out_start_mouse_drops) {
        *out_start_mouse_drops = info.total_dropped_mouse_move_events;
    }
    if (out_start_overflow_drops) {
        *out_start_overflow_drops = info.total_overflow_drops;
    }
}

static void probe_monitor_queue_pressure(uint32_t start_mouse_drops,
                                         uint32_t start_overflow_drops) {
    for (uint32_t tick = 0U; tick < PROBE_MONITOR_TICKS; tick++) {
        struct syscall_gui_info info;

        if (user_gui_info(&info) == 0) {
            if (info.total_dropped_mouse_move_events > start_mouse_drops) {
                probe_pass("GUI_PROBE_QUEUE_MOUSE_DROP_OK");
                probe_pass("GUI_PROBE_QUEUE_DROP_OK");
                return;
            }
            if (info.total_overflow_drops > start_overflow_drops) {
                probe_pass("GUI_PROBE_QUEUE_OVERFLOW_OK");
                probe_pass("GUI_PROBE_QUEUE_DROP_OK");
                return;
            }
        }
        user_sleep_ticks(1U);
    }
    probe_pass("GUI_PROBE_QUEUE_DROP_SKIP");
}

void _start(int argc, char **argv) {
    uint32_t start_mouse_drops = 0U;
    uint32_t start_overflow_drops = 0U;

    (void)argc;
    (void)argv;
    probe_pass("GUI_PROBE_START");
    probe_selftest(&start_mouse_drops, &start_overflow_drops);
    probe_create_pressure_window();
    probe_monitor_queue_pressure(start_mouse_drops, start_overflow_drops);
    user_sleep_ticks(500U);
    user_exit(0);
}
