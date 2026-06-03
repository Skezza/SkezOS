#include <stdint.h>

#include "guilib.h"

#define CONSOLE_WIDTH 640U
#define CONSOLE_HEIGHT 360U
#define CONSOLE_LINES 24U
#define CONSOLE_COLS 92U
#define CONSOLE_INPUT_MAX 63U
#define CONSOLE_TASK_CAP 8U

static struct gui_window g_console_window;
static uint32_t g_console_pixels[CONSOLE_WIDTH * CONSOLE_HEIGHT];
static char g_console_lines[CONSOLE_LINES][CONSOLE_COLS + 1U];
static uint32_t g_console_line_count;
static char g_console_input[CONSOLE_INPUT_MAX + 1U];
static uint32_t g_console_input_len;

static void console_clear_lines(void) {
    for (uint32_t i = 0U; i < CONSOLE_LINES; i++) {
        g_console_lines[i][0] = '\0';
    }
    g_console_line_count = 0U;
}

static void console_copy_upper(char *dst, uint32_t cap, const char *src) {
    uint32_t idx = 0U;

    if (!dst || cap == 0U) {
        return;
    }
    while (src && src[idx] != '\0' && idx + 1U < cap) {
        dst[idx] = gui_upper_char(src[idx]);
        idx++;
    }
    dst[idx] = '\0';
}

static void console_push_line(const char *text) {
    if (g_console_line_count < CONSOLE_LINES) {
        console_copy_upper(g_console_lines[g_console_line_count],
                           CONSOLE_COLS + 1U,
                           text);
        g_console_line_count++;
        return;
    }
    for (uint32_t i = 1U; i < CONSOLE_LINES; i++) {
        user_memcpy(g_console_lines[i - 1U], g_console_lines[i], CONSOLE_COLS + 1U);
    }
    console_copy_upper(g_console_lines[CONSOLE_LINES - 1U], CONSOLE_COLS + 1U, text);
}

static void console_write_u32(char *dst, uint32_t cap, uint32_t value) {
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

static void console_append_text(char *dst, uint32_t cap, const char *text) {
    uint32_t len = user_strlen(dst);
    uint32_t idx = 0U;

    while (text && text[idx] != '\0' && len + 1U < cap) {
        dst[len++] = gui_upper_char(text[idx++]);
    }
    dst[len] = '\0';
}

static const char *console_state_name(uint32_t state) {
    switch (state) {
    case SYSCALL_TASK_STATE_RUNNABLE: return "RUN";
    case SYSCALL_TASK_STATE_RUNNING: return "CPU";
    case SYSCALL_TASK_STATE_SLEEPING: return "SLP";
    case SYSCALL_TASK_STATE_WAIT_CHILD: return "WAIT";
    case SYSCALL_TASK_STATE_ZOMBIE: return "ZMB";
    default: return "UNK";
    }
}

static void console_render(void) {
    uint32_t bg = gui_rgb(9U, 12U, 20U);
    uint32_t panel = gui_rgb(13U, 18U, 28U);
    uint32_t line = gui_rgb(30U, 42U, 60U);
    uint32_t fg = gui_rgb(228U, 236U, 248U);
    uint32_t muted = gui_rgb(140U, 156U, 178U);
    uint32_t accent = gui_rgb(88U, 180U, 242U);

    gui_fill_rect(&g_console_window, 0U, 0U, CONSOLE_WIDTH, CONSOLE_HEIGHT, bg);
    gui_fill_rect(&g_console_window, 0U, 0U, CONSOLE_WIDTH, 22U, panel);
    gui_draw_text(&g_console_window, 10U, 7U, "GUI CONSOLE", fg, panel);
    gui_draw_text(&g_console_window, 430U, 7U, "HELP CLEAR TIME TASKS ABOUT", muted, panel);
    gui_fill_rect(&g_console_window, 0U, 23U, CONSOLE_WIDTH, 1U, line);

    for (uint32_t i = 0U; i < g_console_line_count; i++) {
        gui_draw_text(&g_console_window, 10U, 34U + (i * 12U), g_console_lines[i], fg, bg);
    }

    gui_fill_rect(&g_console_window, 8U, CONSOLE_HEIGHT - 28U, CONSOLE_WIDTH - 16U, 20U, panel);
    gui_stroke_rect(&g_console_window, 8U, CONSOLE_HEIGHT - 28U, CONSOLE_WIDTH - 16U, 20U, line);
    gui_draw_text(&g_console_window, 14U, CONSOLE_HEIGHT - 23U, "]", accent, panel);
    gui_draw_text(&g_console_window, 26U, CONSOLE_HEIGHT - 23U, g_console_input, fg, panel);
    (void)gui_present_full(&g_console_window);
}

static void console_boot_lines(void) {
    console_push_line("GUI CONSOLE READY");
    console_push_line("TYPE HELP FOR BUILT INS");
}

static void console_run_tasks(void) {
    struct syscall_task_snapshot_entry tasks[CONSOLE_TASK_CAP];
    int32_t count = user_task_snapshot(tasks, CONSOLE_TASK_CAP);

    if (count < 0) {
        console_push_line("TASK SNAPSHOT FAILED");
        return;
    }
    console_push_line("PID MODE STATE NAME");
    for (int32_t i = 0; i < count; i++) {
        char line_buf[CONSOLE_COLS + 1U];
        char pid_buf[16];

        line_buf[0] = '\0';
        console_write_u32(pid_buf, sizeof(pid_buf), (uint32_t)tasks[i].pid);
        console_append_text(line_buf, sizeof(line_buf), pid_buf);
        console_append_text(line_buf, sizeof(line_buf), " ");
        console_append_text(line_buf, sizeof(line_buf),
                            (tasks[i].flags & SYSCALL_TASK_FLAG_USER) != 0U ? "USR " : "KRN ");
        console_append_text(line_buf, sizeof(line_buf), console_state_name(tasks[i].state));
        console_append_text(line_buf, sizeof(line_buf), " ");
        console_append_text(line_buf, sizeof(line_buf), tasks[i].name);
        console_push_line(line_buf);
    }
}

static void console_run_time(void) {
    struct syscall_time_info info;
    char line_buf[CONSOLE_COLS + 1U];
    char num_buf[16];

    if (user_time_info(&info) < 0) {
        console_push_line("TIME QUERY FAILED");
        return;
    }

    line_buf[0] = '\0';
    console_append_text(line_buf, sizeof(line_buf), "TICKS ");
    console_write_u32(num_buf, sizeof(num_buf), info.ticks_lo);
    console_append_text(line_buf, sizeof(line_buf), num_buf);
    console_append_text(line_buf, sizeof(line_buf), " HZ ");
    console_write_u32(num_buf, sizeof(num_buf), info.hz);
    console_append_text(line_buf, sizeof(line_buf), num_buf);
    console_push_line(line_buf);
}

static void console_run_command(void) {
    char line_buf[CONSOLE_COLS + 1U];

    line_buf[0] = '\0';
    console_append_text(line_buf, sizeof(line_buf), "] ");
    console_append_text(line_buf, sizeof(line_buf), g_console_input);
    console_push_line(line_buf);

    if (gui_str_eq(g_console_input, "HELP")) {
        console_push_line("HELP CLEAR TIME TASKS ABOUT");
    } else if (gui_str_eq(g_console_input, "CLEAR")) {
        console_clear_lines();
        console_push_line("SCREEN CLEARED");
    } else if (gui_str_eq(g_console_input, "TIME")) {
        console_run_time();
    } else if (gui_str_eq(g_console_input, "TASKS")) {
        console_run_tasks();
    } else if (gui_str_eq(g_console_input, "ABOUT")) {
        console_push_line("FIRST GUI CONSOLE ON THE NEW WINDOW ABI");
    } else if (g_console_input[0] != '\0') {
        console_push_line("UNKNOWN COMMAND");
    }

    g_console_input_len = 0U;
    g_console_input[0] = '\0';
}

static int console_handle_key(const struct syscall_gui_event *event) {
    uint32_t ch;

    if (!event || event->type != SYSCALL_GUI_EVENT_KEY_DOWN) {
        return 0;
    }

    if (event->keycode == SYSCALL_GUI_KEY_BACKSPACE) {
        if (g_console_input_len != 0U) {
            g_console_input[--g_console_input_len] = '\0';
        }
        return 1;
    }
    if (event->keycode == SYSCALL_GUI_KEY_ENTER) {
        console_run_command();
        return 1;
    }

    ch = event->ch;
    if (ch < 32U || ch > 126U || g_console_input_len + 1U >= sizeof(g_console_input)) {
        return 0;
    }
    g_console_input[g_console_input_len++] = gui_upper_char((char)ch);
    g_console_input[g_console_input_len] = '\0';
    return 1;
}

void _start(int argc, char **argv) {
    struct syscall_gui_event event;

    (void)argc;
    (void)argv;

    if (gui_window_create(&g_console_window,
                          "GUI CONSOLE",
                          CONSOLE_WIDTH,
                          CONSOLE_HEIGHT,
                          g_console_pixels) < 0) {
        user_exit(1);
    }

    console_clear_lines();
    console_boot_lines();
    console_render();

    for (;;) {
        int32_t rc = user_gui_poll(g_console_window.id, &event);

        if (rc < 0) {
            break;
        }
        if (rc == 0) {
            user_sleep_ticks(1U);
            continue;
        }
        if (event.type == SYSCALL_GUI_EVENT_CLOSE) {
            break;
        }
        if (event.type == SYSCALL_GUI_EVENT_PAINT ||
            event.type == SYSCALL_GUI_EVENT_FOCUS ||
            event.type == SYSCALL_GUI_EVENT_BLUR ||
            console_handle_key(&event)) {
            console_render();
        }
    }

    (void)user_gui_destroy(g_console_window.id);
    user_exit(0);
}
