#include <stdint.h>

#include "userlib.h"

#define SHELL_LINE_MAX 128U
#define SHELL_PATH_MAX 96U
#define SHELL_PS_MAX_TASKS 16U

static char g_shell_line[SHELL_LINE_MAX];
static char g_shell_path[SHELL_PATH_MAX];
static struct syscall_task_snapshot_entry g_shell_ps_tasks[SHELL_PS_MAX_TASKS];

static const char kBanner[] = "SkezOS shell ready\nType 'help' for commands.\n";
static const char kPrompt[] = "sh> ";
static const char kHelp[] =
    "builtins: help ps wait exit\n"
    "run: <name> -> /bin/<name>.elf\n";
static const char kWaitStub[] = "wait: no background jobs\n";
static const char kPsFail[] = "ps: snapshot failed\n";
static const char kSpawnFail[] = "run: launch failed\n";
static const char kWaitFail[] = "run: waitpid failed\n";
static const char kExit[] = "sh: exit\n";
static const char kNewline[] = "\n";
static const char kEraseOne[] = "\b \b";

static void shell_write_all(const char *buf, uint32_t len) {
    while (len > 0U) {
        int32_t rc = user_write(USER_FD_STDOUT, buf, len);
        if (rc <= 0) {
            return;
        }
        buf += (uint32_t)rc;
        len -= (uint32_t)rc;
    }
}

static void shell_write_str(const char *s) {
    shell_write_all(s, user_strlen(s));
}

static void shell_write_char(char ch) {
    shell_write_all(&ch, 1U);
}

static void shell_write_spaces(uint32_t count) {
    while (count > 0U) {
        shell_write_char(' ');
        count--;
    }
}

static void shell_erase_one_char(void) {
    shell_write_all(kEraseOne, (uint32_t)(sizeof(kEraseOne) - 1U));
}

static uint32_t shell_format_u32(char *buf, uint32_t cap, uint32_t value) {
    char digits[10];
    uint32_t len = 0;
    uint32_t out_len = 0;

    if (!buf || cap == 0U) {
        return 0U;
    }
    if (value == 0U) {
        if (cap < 2U) {
            buf[0] = '\0';
            return 0U;
        }
        buf[0] = '0';
        buf[1] = '\0';
        return 1U;
    }

    while (value != 0U && len < sizeof(digits)) {
        digits[len++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (len > 0U && out_len + 1U < cap) {
        buf[out_len++] = digits[--len];
    }
    buf[out_len] = '\0';
    return out_len;
}

static uint32_t shell_format_i32(char *buf, uint32_t cap, int32_t value) {
    uint32_t magnitude;

    if (!buf || cap == 0U) {
        return 0U;
    }
    if (value < 0) {
        if (cap < 2U) {
            buf[0] = '\0';
            return 0U;
        }
        buf[0] = '-';
        magnitude = (uint32_t)(-(value + 1)) + 1U;
        return 1U + shell_format_u32(buf + 1, cap - 1U, magnitude);
    }
    return shell_format_u32(buf, cap, (uint32_t)value);
}

static void shell_write_padded(const char *text, uint32_t width) {
    uint32_t len = 0U;

    if (text) {
        len = user_strlen(text);
        shell_write_all(text, len);
    }
    if (width > len) {
        shell_write_spaces(width - len);
    }
}

static const char *shell_task_state_name(uint32_t state) {
    switch (state) {
        case SYSCALL_TASK_STATE_RUNNABLE:
            return "ready";
        case SYSCALL_TASK_STATE_RUNNING:
            return "run";
        case SYSCALL_TASK_STATE_SLEEPING:
            return "sleep";
        case SYSCALL_TASK_STATE_WAIT_CHILD:
            return "wait";
        case SYSCALL_TASK_STATE_ZOMBIE:
            return "zombie";
        default:
            return "unknown";
    }
}

static const char *shell_task_mode_name(uint32_t flags) {
    if ((flags & SYSCALL_TASK_FLAG_USER) != 0U) {
        return "user";
    }
    return "kern";
}

static void shell_run_ps(void) {
    int32_t count = user_task_snapshot(g_shell_ps_tasks, SHELL_PS_MAX_TASKS);

    if (count < 0) {
        shell_write_str(kPsFail);
        return;
    }

    shell_write_padded("PID", 5U);
    shell_write_padded("PPID", 6U);
    shell_write_padded("MODE", 6U);
    shell_write_padded("STATE", 8U);
    shell_write_padded("EXIT", 6U);
    shell_write_str("NAME\n");

    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        const struct syscall_task_snapshot_entry *entry = &g_shell_ps_tasks[i];
        char num_buf[12];
        char exit_buf[12];
        const char *exit_text = "-";

        shell_format_i32(num_buf, sizeof(num_buf), entry->pid);
        shell_write_padded(num_buf, 5U);
        shell_format_i32(num_buf, sizeof(num_buf), entry->parent_pid);
        shell_write_padded(num_buf, 6U);
        shell_write_padded(shell_task_mode_name(entry->flags), 6U);
        shell_write_padded(shell_task_state_name(entry->state), 8U);
        if ((entry->flags & SYSCALL_TASK_FLAG_EXIT_VALID) != 0U) {
            shell_format_i32(exit_buf, sizeof(exit_buf), entry->exit_code);
            exit_text = exit_buf;
        }
        shell_write_padded(exit_text, 6U);
        if (entry->name[0] != '\0') {
            shell_write_str(entry->name);
        } else {
            shell_write_str("?");
        }
        shell_write_str(kNewline);
    }
}

static int32_t shell_read_char_blocking(void) {
    char ch;
    int32_t rc = user_read(USER_FD_STDIN, &ch, 1U);

    if (rc == 1) {
        return (int32_t)(uint8_t)ch;
    }
    if (rc < 0) {
        return rc;
    }
    return -1;
}

static int32_t shell_read_line(char *buf, uint32_t cap) {
    uint32_t len = 0;

    if (!buf || cap == 0U) {
        return -1;
    }

    for (;;) {
        int32_t ch = shell_read_char_blocking();
        if (ch < 0) {
            return ch;
        }
        if (ch == '\r') {
            ch = '\n';
        }
        if (ch == '\t') {
            ch = ' ';
        }
        if (ch == '\b' || ch == 0x7f) {
            if (len > 0U) {
                len--;
                shell_erase_one_char();
            }
            continue;
        }
        if ((uint32_t)ch < 0x20U && ch != '\n') {
            continue;
        }
        if (ch == '\n') {
            shell_write_str(kNewline);
            buf[len] = '\0';
            return (int32_t)len;
        }
        if (len + 1U >= cap) {
            continue;
        }
        buf[len++] = (char)ch;
        shell_write_all(&buf[len - 1U], 1U);
    }
}

static uint32_t shell_skip_spaces(const char *s, uint32_t idx) {
    while (s[idx] != '\0' && user_is_space(s[idx])) {
        idx++;
    }
    return idx;
}

static uint32_t shell_token_end(const char *s, uint32_t idx) {
    while (s[idx] != '\0' && !user_is_space(s[idx])) {
        idx++;
    }
    return idx;
}

static int shell_build_spawn_path(const char *cmd, uint32_t cmd_len, char *out, uint32_t out_cap) {
    static const char kBinPrefix[] = "/bin/";
    static const char kElfSuffix[] = ".elf";
    uint32_t total;
    int add_suffix;

    if (!cmd || !out || out_cap == 0U || cmd_len == 0U) {
        return -1;
    }

    if (cmd[0] == '/') {
        if (cmd_len + 1U > out_cap) {
            return -1;
        }
        user_memcpy(out, cmd, cmd_len);
        out[cmd_len] = '\0';
        return (int)cmd_len;
    }

    add_suffix = !user_str_has_suffix_n(cmd, cmd_len, kElfSuffix);
    total = (uint32_t)(sizeof(kBinPrefix) - 1U) + cmd_len + (add_suffix ? (uint32_t)(sizeof(kElfSuffix) - 1U) : 0U);
    if (total + 1U > out_cap) {
        return -1;
    }

    user_memcpy(out, kBinPrefix, (uint32_t)(sizeof(kBinPrefix) - 1U));
    user_memcpy(out + (sizeof(kBinPrefix) - 1U), cmd, cmd_len);
    if (add_suffix) {
        user_memcpy(out + (sizeof(kBinPrefix) - 1U) + cmd_len, kElfSuffix, (uint32_t)(sizeof(kElfSuffix) - 1U));
    }
    out[total] = '\0';
    return (int)total;
}

static void shell_run_external(const char *cmd,
                               uint32_t cmd_len,
                               const char *cmdline,
                               uint32_t cmdline_len) {
    int path_len;
    int32_t child_pid;
    int32_t wait_status = 0;

    path_len = shell_build_spawn_path(cmd, cmd_len, g_shell_path, sizeof(g_shell_path));
    if (path_len <= 0) {
        shell_write_str(kSpawnFail);
        return;
    }

    child_pid = user_spawn_ex(g_shell_path,
                              (uint32_t)path_len,
                              cmdline,
                              cmdline_len);
    if (child_pid < 0) {
        shell_write_str(kSpawnFail);
        return;
    }

    if (user_waitpid(child_pid, &wait_status, 0U) < 0) {
        shell_write_str(kWaitFail);
    }
}

static int shell_dispatch_line(const char *line) {
    uint32_t cmd_start = shell_skip_spaces(line, 0U);
    uint32_t cmd_end;
    uint32_t arg_start;

    if (line[cmd_start] == '\0') {
        return 1;
    }

    cmd_end = shell_token_end(line, cmd_start);
    arg_start = shell_skip_spaces(line, cmd_end);

    if (user_str_eq_n(line + cmd_start, "help", cmd_end - cmd_start)) {
        shell_write_str(kHelp);
        return 1;
    }
    if (user_str_eq_n(line + cmd_start, "wait", cmd_end - cmd_start)) {
        shell_write_str(kWaitStub);
        return 1;
    }
    if (user_str_eq_n(line + cmd_start, "ps", cmd_end - cmd_start)) {
        shell_run_ps();
        return 1;
    }
    if (user_str_eq_n(line + cmd_start, "exit", cmd_end - cmd_start)) {
        shell_write_str(kExit);
        return 0;
    }

    shell_run_external(line + cmd_start,
                       cmd_end - cmd_start,
                       line + arg_start,
                       user_strlen(line + arg_start));
    return 1;
}

void _start(void) {
    shell_write_str(kBanner);
    for (;;) {
        int32_t rc;

        shell_write_str(kPrompt);
        rc = shell_read_line(g_shell_line, sizeof(g_shell_line));
        if (rc < 0) {
            shell_write_str(kExit);
            user_exit(1);
        }
        if (!shell_dispatch_line(g_shell_line)) {
            user_exit(0);
        }
    }
}
