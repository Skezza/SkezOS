#include <stdint.h>

#include "userlib.h"

#define SHELL_LINE_MAX 128U
#define SHELL_PATH_MAX 96U

static char g_shell_line[SHELL_LINE_MAX];
static char g_shell_path[SHELL_PATH_MAX];

static const char kBanner[] = "sh: bootstrap shell online\n";
static const char kPrompt[] = "sh> ";
static const char kHelp[] =
    "sh: builtins help echo wait ps exit; external names map to /bin/<name>.elf\n";
static const char kWaitStub[] = "sh: no background jobs in bootstrap shell\n";
static const char kPsStub[] = "sh: ps unavailable in bootstrap shell\n";
static const char kSpawnFail[] = "sh: command failed\n";
static const char kWaitFail[] = "sh: waitpid failed\n";
static const char kExit[] = "sh: bootstrap shell exit\n";
static const char kNewline[] = "\n";

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

static int32_t shell_read_char_blocking(void) {
    char ch;

    for (;;) {
        int32_t rc = user_read(USER_FD_STDIN, &ch, 1U);
        if (rc == 1) {
            return (int32_t)(uint8_t)ch;
        }
        if (rc < 0) {
            return rc;
        }
        user_yield();
    }
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
        if (ch == '\b') {
            if (len > 0U) {
                len--;
            }
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

static void shell_run_external(const char *cmd, uint32_t cmd_len) {
    int path_len;
    int32_t child_pid;
    int32_t wait_status = 0;

    path_len = shell_build_spawn_path(cmd, cmd_len, g_shell_path, sizeof(g_shell_path));
    if (path_len <= 0) {
        shell_write_str(kSpawnFail);
        return;
    }

    child_pid = user_spawn(g_shell_path, (uint32_t)path_len);
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
        shell_write_str(kPsStub);
        return 1;
    }
    if (user_str_eq_n(line + cmd_start, "echo", cmd_end - cmd_start)) {
        shell_write_str(line + arg_start);
        shell_write_str(kNewline);
        return 1;
    }
    if (user_str_eq_n(line + cmd_start, "exit", cmd_end - cmd_start)) {
        shell_write_str(kExit);
        return 0;
    }

    shell_run_external(line + cmd_start, cmd_end - cmd_start);
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
