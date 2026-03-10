#include <stdint.h>

#include "userlib.h"

#define SHELL_LINE_MAX 128U
#define SHELL_PATH_MAX 96U
#define SHELL_CWD_MAX  96U
#define SHELL_PS_MAX_TASKS 16U
#define SHELL_LS_MAX_ENTRIES 16U
#define SHELL_PROMPT_MAX 128U
#define SHELL_PIPELINE_MAX 8U
#define SHELL_STAGE_CMDLINE_MAX 96U
#define SHELL_COMPLETION_MAX_MATCHES (SHELL_LS_MAX_ENTRIES + 8U)
#define SHELL_HISTORY_MAX 8U

struct shell_stage {
    char cmd[SHELL_PATH_MAX];
    uint32_t cmd_len;
    char cmdline[SHELL_STAGE_CMDLINE_MAX];
    uint32_t cmdline_len;
    char redir_in[SHELL_PATH_MAX];
    char redir_out[SHELL_PATH_MAX];
    char redir_err[SHELL_PATH_MAX];
    uint32_t has_redir_in;
    uint32_t has_redir_out;
    uint32_t has_redir_err;
    uint32_t redir_out_append;
    uint32_t redir_err_append;
};

static char g_shell_line[SHELL_LINE_MAX];
static char g_shell_path[SHELL_PATH_MAX];
static char g_shell_cwd[SHELL_CWD_MAX] = "/";
static char g_shell_prompt[SHELL_PROMPT_MAX];
static struct shell_stage g_shell_stages[SHELL_PIPELINE_MAX];
static struct syscall_task_snapshot_entry g_shell_ps_tasks[SHELL_PS_MAX_TASKS];
static struct syscall_dir_entry g_shell_ls_entries[SHELL_LS_MAX_ENTRIES];
static char g_shell_history[SHELL_HISTORY_MAX][SHELL_LINE_MAX];
static uint32_t g_shell_history_count;
static uint32_t g_shell_history_head;

static const char kBanner[] = "SkezOS shell ready\nType 'help' for commands.\n";
static const char kHelp[] =
    "builtins: help history ps ls pwd cd wait exit\n"
    "run: <name> -> /bin/<name>.elf, ./tool uses cwd\n"
    "edit: BS deletes, Esc/Ctrl+P history, Ctrl+N forward, Tab completes commands\n";
static const char kCdFail[] = "cd: chdir failed\n";
static const char kLsFail[] = "ls: list failed\n";
static const char kPwdFail[] = "pwd: getcwd failed\n";
static const char kWaitStub[] = "wait: no background jobs\n";
static const char kPsFail[] = "ps: snapshot failed\n";
static const char kHistoryEmpty[] = "history: empty\n";
static const char kSpawnFail[] = "run: launch failed\n";
static const char kWaitFail[] = "run: waitpid failed\n";
static const char kParseFail[] = "run: parse failed\n";
static const char kBuiltinPipeRedirect[] = "run: builtin does not support pipes/redirection\n";
static const char kRedirectOpenFail[] = "run: redirect open failed\n";
static const char kExit[] = "sh: exit\n";
static const char kNewline[] = "\n";
static const char kEraseOne[] = "\b \b";
static const char kHintPrefix[] = "hint: ";
static const char *kShellCompletionBuiltins[] = {
    "help",
    "history",
    "wait",
    "ps",
    "ls",
    "pwd",
    "cd",
    "exit",
};

static uint32_t shell_skip_spaces(const char *s, uint32_t idx);
static int shell_str_contains_char_n(const char *s, uint32_t len, char ch);

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

static void shell_replace_line(char *buf,
                               uint32_t *len,
                               uint32_t cap,
                               const char *src,
                               uint32_t src_len) {
    if (!buf || !len || cap == 0U) {
        return;
    }
    while (*len > 0U) {
        (*len)--;
        shell_erase_one_char();
    }
    if (!src) {
        buf[0] = '\0';
        *len = 0U;
        return;
    }
    if (src_len + 1U > cap) {
        src_len = cap - 1U;
    }
    if (src_len != 0U) {
        user_memcpy(buf, src, src_len);
        shell_write_all(src, src_len);
    }
    buf[src_len] = '\0';
    *len = src_len;
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

static void shell_append_prompt_text(char *buf,
                                     uint32_t cap,
                                     uint32_t *len,
                                     const char *text) {
    uint32_t idx = *len;

    if (!buf || !len || cap == 0U || !text) {
        return;
    }
    while (*text != '\0' && idx + 1U < cap) {
        buf[idx++] = *text++;
    }
    buf[idx] = '\0';
    *len = idx;
}

static const char *shell_prompt_mark(void) {
    if (g_shell_cwd[0] == '/' && g_shell_cwd[1] == '\0') {
        return "#";
    }
    return "$";
}

static const char *shell_prompt_path(void) {
    if (g_shell_cwd[0] == '\0') {
        return "/";
    }
    return g_shell_cwd;
}

static const char *shell_build_prompt(void) {
    uint32_t len = 0U;

    g_shell_prompt[0] = '\0';
    shell_append_prompt_text(g_shell_prompt, sizeof(g_shell_prompt), &len, "sh> ");
    shell_append_prompt_text(g_shell_prompt, sizeof(g_shell_prompt), &len, shell_prompt_path());
    shell_append_prompt_text(g_shell_prompt, sizeof(g_shell_prompt), &len, " ");
    shell_append_prompt_text(g_shell_prompt, sizeof(g_shell_prompt), &len, shell_prompt_mark());
    shell_append_prompt_text(g_shell_prompt, sizeof(g_shell_prompt), &len, " ");
    return g_shell_prompt;
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

static void shell_run_history(void) {
    uint32_t oldest;

    if (g_shell_history_count == 0U) {
        shell_write_str(kHistoryEmpty);
        return;
    }

    oldest = (g_shell_history_head + SHELL_HISTORY_MAX - g_shell_history_count) % SHELL_HISTORY_MAX;
    for (uint32_t i = 0U; i < g_shell_history_count; i++) {
        uint32_t idx = (oldest + i) % SHELL_HISTORY_MAX;
        char num_buf[12];

        shell_format_u32(num_buf, sizeof(num_buf), i + 1U);
        shell_write_padded(num_buf, 4U);
        shell_write_str(g_shell_history[idx]);
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

static int shell_history_get(uint32_t offset,
                             const char **out_line,
                             uint32_t *out_len) {
    uint32_t newest;
    uint32_t idx;

    if (!out_line || !out_len || offset >= g_shell_history_count) {
        return -1;
    }
    newest = (g_shell_history_head + SHELL_HISTORY_MAX - 1U) % SHELL_HISTORY_MAX;
    idx = (newest + SHELL_HISTORY_MAX - offset) % SHELL_HISTORY_MAX;
    *out_line = g_shell_history[idx];
    *out_len = user_strlen(g_shell_history[idx]);
    return 0;
}

static void shell_history_push(const char *line, uint32_t len) {
    uint32_t slot;

    if (!line || len == 0U) {
        return;
    }
    if (len + 1U > SHELL_LINE_MAX) {
        len = SHELL_LINE_MAX - 1U;
    }
    if (g_shell_history_count != 0U) {
        const char *latest = 0;
        uint32_t latest_len = 0U;
        uint32_t i = 0U;

        if (shell_history_get(0U, &latest, &latest_len) == 0 &&
            latest_len == len) {
            while (i < len && latest[i] == line[i]) {
                i++;
            }
            if (i == len) {
                return;
            }
        }
    }

    slot = g_shell_history_head;
    user_memcpy(g_shell_history[slot], line, len);
    g_shell_history[slot][len] = '\0';
    g_shell_history_head = (g_shell_history_head + 1U) % SHELL_HISTORY_MAX;
    if (g_shell_history_count < SHELL_HISTORY_MAX) {
        g_shell_history_count++;
    }
}

static int shell_prefix_match(const char *candidate,
                              uint32_t candidate_len,
                              const char *prefix,
                              uint32_t prefix_len) {
    if (!candidate || !prefix || prefix_len > candidate_len) {
        return 0;
    }
    for (uint32_t i = 0U; i < prefix_len; i++) {
        if (candidate[i] != prefix[i]) {
            return 0;
        }
    }
    return 1;
}

static uint32_t shell_common_prefix_len(const char *lhs,
                                        uint32_t lhs_len,
                                        const char *rhs,
                                        uint32_t rhs_len) {
    uint32_t limit = lhs_len < rhs_len ? lhs_len : rhs_len;
    uint32_t i = 0U;

    while (i < limit && lhs[i] == rhs[i]) {
        i++;
    }
    return i;
}

static int shell_match_list_contains(char matches[][SHELL_PATH_MAX],
                                     uint32_t match_count,
                                     const char *candidate,
                                     uint32_t candidate_len) {
    for (uint32_t i = 0U; i < match_count; i++) {
        uint32_t len = user_strlen(matches[i]);
        uint32_t j = 0U;

        if (len != candidate_len) {
            continue;
        }
        while (j < len && matches[i][j] == candidate[j]) {
            j++;
        }
        if (j == len) {
            return 1;
        }
    }
    return 0;
}

static void shell_match_list_add(char matches[][SHELL_PATH_MAX],
                                 uint32_t match_cap,
                                 uint32_t *match_count,
                                 const char *candidate,
                                 uint32_t candidate_len) {
    if (!matches || !match_count || !candidate || candidate_len == 0U ||
        candidate_len + 1U > SHELL_PATH_MAX || *match_count >= match_cap) {
        return;
    }
    if (shell_match_list_contains(matches, *match_count, candidate, candidate_len)) {
        return;
    }
    user_memcpy(matches[*match_count], candidate, candidate_len);
    matches[*match_count][candidate_len] = '\0';
    (*match_count)++;
}

static uint32_t shell_collect_command_matches(const char *prefix,
                                              uint32_t prefix_len,
                                              char matches[][SHELL_PATH_MAX],
                                              uint32_t match_cap) {
    uint32_t match_count = 0U;
    int32_t dir_count;

    for (uint32_t i = 0U; i < (uint32_t)(sizeof(kShellCompletionBuiltins) /
                                          sizeof(kShellCompletionBuiltins[0])); i++) {
        const char *name = kShellCompletionBuiltins[i];
        uint32_t name_len = user_strlen(name);

        if (!shell_prefix_match(name, name_len, prefix, prefix_len)) {
            continue;
        }
        shell_match_list_add(matches, match_cap, &match_count, name, name_len);
    }

    dir_count = user_list_dir("/bin", 4U, g_shell_ls_entries, SHELL_LS_MAX_ENTRIES);
    if (dir_count <= 0) {
        return match_count;
    }

    for (uint32_t i = 0U; i < (uint32_t)dir_count; i++) {
        uint32_t name_len;

        if (g_shell_ls_entries[i].type != SYSCALL_NODE_TYPE_FILE) {
            continue;
        }
        name_len = user_strlen(g_shell_ls_entries[i].name);
        if (name_len <= 4U ||
            !user_str_has_suffix_n(g_shell_ls_entries[i].name, name_len, ".elf")) {
            continue;
        }
        name_len -= 4U;
        if (!shell_prefix_match(g_shell_ls_entries[i].name, name_len, prefix, prefix_len)) {
            continue;
        }
        shell_match_list_add(matches,
                             match_cap,
                             &match_count,
                             g_shell_ls_entries[i].name,
                             name_len);
    }

    return match_count;
}

static int shell_try_complete_command(char *buf, uint32_t *len, uint32_t cap) {
    uint32_t cmd_start;
    uint32_t cmd_len;
    uint32_t match_count;
    uint32_t common_len;
    char matches[SHELL_COMPLETION_MAX_MATCHES][SHELL_PATH_MAX];

    if (!buf || !len || cap == 0U) {
        return 0;
    }
    cmd_start = shell_skip_spaces(buf, 0U);
    if (cmd_start >= *len) {
        return 0;
    }
    for (uint32_t i = cmd_start; i < *len; i++) {
        if (user_is_space(buf[i]) || buf[i] == '|' || buf[i] == '<' || buf[i] == '>') {
            return 0;
        }
    }
    cmd_len = *len - cmd_start;
    if (cmd_len == 0U || cmd_len + 1U > SHELL_PATH_MAX ||
        shell_str_contains_char_n(buf + cmd_start, cmd_len, '/')) {
        return 0;
    }

    match_count = shell_collect_command_matches(buf + cmd_start,
                                                cmd_len,
                                                matches,
                                                SHELL_COMPLETION_MAX_MATCHES);
    if (match_count == 0U) {
        return 0;
    }

    common_len = user_strlen(matches[0]);
    for (uint32_t i = 1U; i < match_count; i++) {
        common_len = shell_common_prefix_len(matches[0], common_len,
                                             matches[i], user_strlen(matches[i]));
    }

    if (common_len > cmd_len) {
        uint32_t append_len = common_len - cmd_len;
        if (*len + append_len + 1U >= cap) {
            append_len = (cap - 1U) - *len;
        }
        if (append_len != 0U) {
            user_memcpy(buf + *len, matches[0] + cmd_len, append_len);
            *len += append_len;
            buf[*len] = '\0';
            shell_write_all(matches[0] + cmd_len, append_len);
        }
        return 1;
    }

    if (match_count == 1U && common_len == cmd_len) {
        if (*len + 1U < cap) {
            buf[*len] = ' ';
            (*len)++;
            buf[*len] = '\0';
            shell_write_char(' ');
        }
        return 1;
    }

    shell_write_str(kNewline);
    shell_write_str(kHintPrefix);
    for (uint32_t i = 0U; i < match_count && i < 6U; i++) {
        if (i != 0U) {
            shell_write_char(' ');
        }
        shell_write_str(matches[i]);
    }
    if (match_count > 6U) {
        shell_write_str(" ...");
    }
    shell_write_str(kNewline);
    shell_write_str(shell_build_prompt());
    if (*len != 0U) {
        shell_write_all(buf, *len);
    }
    return 1;
}

static int32_t shell_read_line(char *buf, uint32_t cap) {
    uint32_t len = 0;
    uint32_t history_browse_active = 0U;
    uint32_t history_offset = 0U;

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
        if (ch == 27 || ch == 0x10) {
            const char *history_line = 0;
            uint32_t history_len = 0U;

            if (g_shell_history_count == 0U) {
                continue;
            }
            if (history_browse_active == 0U) {
                history_browse_active = 1U;
                history_offset = 0U;
            } else if (history_offset + 1U < g_shell_history_count) {
                history_offset++;
            }
            if (shell_history_get(history_offset, &history_line, &history_len) == 0) {
                shell_replace_line(buf, &len, cap, history_line, history_len);
            }
            continue;
        }
        if (ch == 0x0e) {
            const char *history_line = 0;
            uint32_t history_len = 0U;

            if (history_browse_active == 0U) {
                continue;
            }
            if (history_offset > 0U) {
                history_offset--;
                if (shell_history_get(history_offset, &history_line, &history_len) == 0) {
                    shell_replace_line(buf, &len, cap, history_line, history_len);
                }
            } else {
                history_browse_active = 0U;
                shell_replace_line(buf, &len, cap, 0, 0U);
            }
            continue;
        }
        if (ch == '\t') {
            if (shell_try_complete_command(buf, &len, cap)) {
                history_browse_active = 0U;
                continue;
            }
            ch = ' ';
        }
        if (ch == '\b' || ch == 0x7f) {
            history_browse_active = 0U;
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
            if (len != 0U) {
                shell_history_push(buf, len);
            }
            return (int32_t)len;
        }
        if (len + 1U >= cap) {
            continue;
        }
        history_browse_active = 0U;
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

static int shell_str_contains_char_n(const char *s, uint32_t len, char ch) {
    for (uint32_t i = 0; i < len; i++) {
        if (s[i] == ch) {
            return 1;
        }
    }
    return 0;
}

enum shell_token_kind {
    SHELL_TOK_NONE = 0,
    SHELL_TOK_WORD = 1,
    SHELL_TOK_PIPE = 2,
    SHELL_TOK_REDIR_IN = 3,
    SHELL_TOK_REDIR_OUT = 4,
    SHELL_TOK_REDIR_OUT_APPEND = 5,
    SHELL_TOK_REDIR_ERR = 6,
    SHELL_TOK_REDIR_ERR_APPEND = 7,
};

static int shell_copy_token(char *dst, uint32_t dst_cap, const char *src, uint32_t src_len) {
    if (!dst || dst_cap == 0U || !src || src_len + 1U > dst_cap) {
        return -1;
    }
    if (src_len != 0U) {
        user_memcpy(dst, src, src_len);
    }
    dst[src_len] = '\0';
    return 0;
}

static enum shell_token_kind shell_next_token(const char *line,
                                              uint32_t *io_idx,
                                              const char **out_ptr,
                                              uint32_t *out_len) {
    uint32_t idx;
    uint32_t start;

    if (!line || !io_idx || !out_ptr || !out_len) {
        return SHELL_TOK_NONE;
    }
    idx = shell_skip_spaces(line, *io_idx);
    if (line[idx] == '\0') {
        *io_idx = idx;
        *out_ptr = 0;
        *out_len = 0U;
        return SHELL_TOK_NONE;
    }

    if (line[idx] == '|') {
        *io_idx = idx + 1U;
        *out_ptr = line + idx;
        *out_len = 1U;
        return SHELL_TOK_PIPE;
    }
    if (line[idx] == '<') {
        *io_idx = idx + 1U;
        *out_ptr = line + idx;
        *out_len = 1U;
        return SHELL_TOK_REDIR_IN;
    }
    if (line[idx] == '>') {
        if (line[idx + 1U] == '>') {
            *io_idx = idx + 2U;
            *out_ptr = line + idx;
            *out_len = 2U;
            return SHELL_TOK_REDIR_OUT_APPEND;
        }
        *io_idx = idx + 1U;
        *out_ptr = line + idx;
        *out_len = 1U;
        return SHELL_TOK_REDIR_OUT;
    }
    if (line[idx] == '2' && line[idx + 1U] == '>') {
        if (line[idx + 2U] == '>') {
            *io_idx = idx + 3U;
            *out_ptr = line + idx;
            *out_len = 3U;
            return SHELL_TOK_REDIR_ERR_APPEND;
        }
        *io_idx = idx + 2U;
        *out_ptr = line + idx;
        *out_len = 2U;
        return SHELL_TOK_REDIR_ERR;
    }

    start = idx;
    while (line[idx] != '\0' && !user_is_space(line[idx])) {
        if (line[idx] == '|' || line[idx] == '<' || line[idx] == '>') {
            break;
        }
        if (line[idx] == '2' && line[idx + 1U] == '>') {
            break;
        }
        idx++;
    }

    *io_idx = idx;
    *out_ptr = line + start;
    *out_len = idx - start;
    return SHELL_TOK_WORD;
}

static int shell_sync_cwd(void) {
    if (user_getcwd(g_shell_cwd, sizeof(g_shell_cwd)) < 0) {
        return -1;
    }
    return 0;
}

static void shell_run_pwd(void) {
    if (shell_sync_cwd() < 0) {
        shell_write_str(kPwdFail);
        return;
    }
    shell_write_str(g_shell_cwd);
    shell_write_str(kNewline);
}

static void shell_run_cd(const char *arg, uint32_t arg_len) {
    if (!arg || arg_len == 0U) {
        arg = "/";
        arg_len = 1U;
    }
    if (user_chdir(arg, arg_len) < 0) {
        shell_write_str(kCdFail);
        return;
    }
    (void)shell_sync_cwd();
}

static void shell_run_ls(const char *arg, uint32_t arg_len) {
    const char *path = ".";
    uint32_t path_len = 1U;
    int32_t count;

    if (arg && arg_len != 0U) {
        path = arg;
        path_len = arg_len;
    }

    count = user_list_dir(path, path_len, g_shell_ls_entries, SHELL_LS_MAX_ENTRIES);
    if (count < 0) {
        shell_write_str(kLsFail);
        return;
    }

    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        shell_write_str(g_shell_ls_entries[i].name);
        if (g_shell_ls_entries[i].type == SYSCALL_NODE_TYPE_DIR) {
            shell_write_char('/');
        }
        shell_write_str(kNewline);
    }
}

static int shell_build_spawn_path(const char *cmd, uint32_t cmd_len, char *out, uint32_t out_cap) {
    static const char kBinPrefix[] = "/bin/";
    static const char kElfSuffix[] = ".elf";
    uint32_t total = 0U;

    if (!cmd || !out || out_cap == 0U || cmd_len == 0U) {
        return -1;
    }

    if (shell_str_contains_char_n(cmd, cmd_len, '/')) {
        if (cmd_len + 1U > out_cap) {
            return -1;
        }
        user_memcpy(out, cmd, cmd_len);
        out[cmd_len] = '\0';
        total = cmd_len;
    } else {
        total = (uint32_t)(sizeof(kBinPrefix) - 1U) + cmd_len;
        if (total + 1U > out_cap) {
            return -1;
        }
        user_memcpy(out, kBinPrefix, (uint32_t)(sizeof(kBinPrefix) - 1U));
        user_memcpy(out + (sizeof(kBinPrefix) - 1U), cmd, cmd_len);
        out[total] = '\0';
    }

    if (!user_str_has_suffix_n(out, total, kElfSuffix)) {
        uint32_t suffix_len = (uint32_t)(sizeof(kElfSuffix) - 1U);

        if (total + suffix_len + 1U > out_cap) {
            return -1;
        }
        user_memcpy(out + total, kElfSuffix, suffix_len);
        total += suffix_len;
        out[total] = '\0';
    }
    return (int)total;
}

static int shell_is_builtin_name(const char *cmd, uint32_t cmd_len) {
    if (!cmd || cmd_len == 0U) {
        return 0;
    }
    return user_str_eq_n(cmd, "help", cmd_len) ||
           user_str_eq_n(cmd, "history", cmd_len) ||
           user_str_eq_n(cmd, "wait", cmd_len) ||
           user_str_eq_n(cmd, "ps", cmd_len) ||
           user_str_eq_n(cmd, "ls", cmd_len) ||
           user_str_eq_n(cmd, "pwd", cmd_len) ||
           user_str_eq_n(cmd, "cd", cmd_len) ||
           user_str_eq_n(cmd, "exit", cmd_len);
}

static int shell_spawn_external(const char *cmd,
                                uint32_t cmd_len,
                                const char *cmdline,
                                uint32_t cmdline_len,
                                int32_t *out_pid) {
    int path_len;

    if (!out_pid) {
        return -1;
    }
    *out_pid = -1;
    path_len = shell_build_spawn_path(cmd, cmd_len, g_shell_path, sizeof(g_shell_path));
    if (path_len <= 0) {
        return -1;
    }

    *out_pid = user_spawn_ex(g_shell_path,
                             (uint32_t)path_len,
                             cmdline,
                             cmdline_len);
    if (*out_pid < 0) {
        return -1;
    }
    return 0;
}

static void shell_run_external_simple(const char *cmd,
                                      uint32_t cmd_len,
                                      const char *cmdline,
                                      uint32_t cmdline_len) {
    int32_t pid = -1;
    int32_t status = 0;

    if (shell_spawn_external(cmd, cmd_len, cmdline, cmdline_len, &pid) < 0) {
        shell_write_str(kSpawnFail);
        return;
    }
    if (user_waitpid(pid, &status, 0U) < 0) {
        shell_write_str(kWaitFail);
    }
}

static int shell_stage_append_arg(struct shell_stage *stage, const char *arg, uint32_t arg_len) {
    if (!stage || !arg) {
        return -1;
    }
    if (arg_len == 0U) {
        return 0;
    }
    if (stage->cmdline_len != 0U) {
        if (stage->cmdline_len + 1U >= sizeof(stage->cmdline)) {
            return -1;
        }
        stage->cmdline[stage->cmdline_len++] = ' ';
    }
    if (stage->cmdline_len + arg_len >= sizeof(stage->cmdline)) {
        return -1;
    }
    user_memcpy(stage->cmdline + stage->cmdline_len, arg, arg_len);
    stage->cmdline_len += arg_len;
    stage->cmdline[stage->cmdline_len] = '\0';
    return 0;
}

static int shell_parse_stages(char *line,
                              struct shell_stage *stages,
                              uint32_t stage_cap,
                              uint32_t *out_stage_count,
                              int *out_has_meta) {
    uint32_t idx = 0U;
    uint32_t stage_idx = 0U;
    struct shell_stage *stage;
    const char *tok_ptr = 0;
    uint32_t tok_len = 0U;
    enum shell_token_kind tok_kind;

    if (!line || !stages || stage_cap == 0U || !out_stage_count || !out_has_meta) {
        return -1;
    }
    *out_stage_count = 0U;
    *out_has_meta = 0;
    if (line[0] == '\0') {
        return 0;
    }
    if (stage_cap > SHELL_PIPELINE_MAX) {
        stage_cap = SHELL_PIPELINE_MAX;
    }
    for (uint32_t i = 0U; i < stage_cap; i++) {
        stages[i].cmd[0] = '\0';
        stages[i].cmd_len = 0U;
        stages[i].cmdline[0] = '\0';
        stages[i].cmdline_len = 0U;
        stages[i].redir_in[0] = '\0';
        stages[i].redir_out[0] = '\0';
        stages[i].redir_err[0] = '\0';
        stages[i].has_redir_in = 0U;
        stages[i].has_redir_out = 0U;
        stages[i].has_redir_err = 0U;
        stages[i].redir_out_append = 0U;
        stages[i].redir_err_append = 0U;
    }

    stage = &stages[stage_idx];
    for (;;) {
        tok_kind = shell_next_token(line, &idx, &tok_ptr, &tok_len);
        if (tok_kind == SHELL_TOK_NONE) {
            break;
        }
        if (tok_kind == SHELL_TOK_PIPE) {
            *out_has_meta = 1;
            if (stage->cmd_len == 0U) {
                return -1;
            }
            stage_idx++;
            if (stage_idx >= stage_cap) {
                return -1;
            }
            stage = &stages[stage_idx];
            continue;
        }
        if (tok_kind == SHELL_TOK_REDIR_IN) {
            const char *path_ptr = 0;
            uint32_t path_len = 0U;

            *out_has_meta = 1;
            if (stage->has_redir_in) {
                return -1;
            }
            tok_kind = shell_next_token(line, &idx, &path_ptr, &path_len);
            if (tok_kind != SHELL_TOK_WORD || path_len == 0U) {
                return -1;
            }
            if (shell_copy_token(stage->redir_in, sizeof(stage->redir_in), path_ptr, path_len) < 0) {
                return -1;
            }
            stage->has_redir_in = 1U;
            continue;
        }
        if (tok_kind == SHELL_TOK_REDIR_OUT || tok_kind == SHELL_TOK_REDIR_OUT_APPEND) {
            const char *path_ptr = 0;
            uint32_t path_len = 0U;
            enum shell_token_kind redir_kind = tok_kind;

            *out_has_meta = 1;
            if (stage->has_redir_out) {
                return -1;
            }
            tok_kind = shell_next_token(line, &idx, &path_ptr, &path_len);
            if (tok_kind != SHELL_TOK_WORD || path_len == 0U) {
                return -1;
            }
            if (shell_copy_token(stage->redir_out, sizeof(stage->redir_out), path_ptr, path_len) < 0) {
                return -1;
            }
            stage->has_redir_out = 1U;
            stage->redir_out_append = (redir_kind == SHELL_TOK_REDIR_OUT_APPEND);
            continue;
        }
        if (tok_kind == SHELL_TOK_REDIR_ERR || tok_kind == SHELL_TOK_REDIR_ERR_APPEND) {
            const char *path_ptr = 0;
            uint32_t path_len = 0U;
            enum shell_token_kind redir_kind = tok_kind;

            *out_has_meta = 1;
            if (stage->has_redir_err) {
                return -1;
            }
            tok_kind = shell_next_token(line, &idx, &path_ptr, &path_len);
            if (tok_kind != SHELL_TOK_WORD || path_len == 0U) {
                return -1;
            }
            if (shell_copy_token(stage->redir_err, sizeof(stage->redir_err), path_ptr, path_len) < 0) {
                return -1;
            }
            stage->has_redir_err = 1U;
            stage->redir_err_append = (redir_kind == SHELL_TOK_REDIR_ERR_APPEND);
            continue;
        }
        if (tok_kind != SHELL_TOK_WORD) {
            return -1;
        }

        if (stage->cmd_len == 0U) {
            if (shell_copy_token(stage->cmd, sizeof(stage->cmd), tok_ptr, tok_len) < 0) {
                return -1;
            }
            stage->cmd_len = tok_len;
            continue;
        }
        if (shell_stage_append_arg(stage, tok_ptr, tok_len) < 0) {
            return -1;
        }
    }

    if (stage->cmd_len == 0U) {
        if (stage_idx == 0U) {
            return 0;
        }
        return -1;
    }
    *out_stage_count = stage_idx + 1U;
    return 0;
}

static int shell_save_and_dup2(int target_fd, int source_fd, int32_t saved_fd[3]) {
    if (target_fd == source_fd) {
        return 0;
    }
    if (target_fd < 0 || target_fd > 2) {
        return -1;
    }
    if (saved_fd[target_fd] < 0) {
        saved_fd[target_fd] = user_dup((uint32_t)target_fd);
        if (saved_fd[target_fd] < 0) {
            return -1;
        }
    }
    if (user_dup2((uint32_t)source_fd, (uint32_t)target_fd) < 0) {
        return -1;
    }
    return 0;
}

static void shell_restore_stdio(int32_t saved_fd[3]) {
    for (uint32_t fd = 0U; fd < 3U; fd++) {
        if (saved_fd[fd] >= 0) {
            (void)user_dup2((uint32_t)saved_fd[fd], fd);
            (void)user_close((uint32_t)saved_fd[fd]);
            saved_fd[fd] = -1;
        }
    }
}

static int shell_open_redirect_in(const char *path) {
    return user_open(path, user_strlen(path), SYSCALL_OPEN_FLAG_READ);
}

static int shell_open_redirect_out(const char *path, uint32_t append) {
    uint32_t flags = SYSCALL_OPEN_FLAG_WRITE | SYSCALL_OPEN_FLAG_CREATE;

    if (append) {
        flags |= SYSCALL_OPEN_FLAG_APPEND;
    } else {
        flags |= SYSCALL_OPEN_FLAG_TRUNC;
    }
    return user_open(path, user_strlen(path), flags);
}

static int shell_execute_pipeline(struct shell_stage *stages, uint32_t stage_count) {
    int32_t pipe_fds[SHELL_PIPELINE_MAX - 1U][2];
    int32_t child_pids[SHELL_PIPELINE_MAX];
    uint32_t spawned = 0U;
    int32_t last_status = 0;

    for (uint32_t i = 0U; i < SHELL_PIPELINE_MAX - 1U; i++) {
        pipe_fds[i][0] = -1;
        pipe_fds[i][1] = -1;
    }
    for (uint32_t i = 0U; i < SHELL_PIPELINE_MAX; i++) {
        child_pids[i] = -1;
    }

    if (stage_count > 1U) {
        for (uint32_t i = 0U; i + 1U < stage_count; i++) {
            if (user_pipe(pipe_fds[i]) < 0) {
                shell_write_str(kSpawnFail);
                goto cleanup;
            }
        }
    }

    for (uint32_t i = 0U; i < stage_count; i++) {
        int in_fd = -1;
        int out_fd = -1;
        int err_fd = -1;
        int32_t saved_fd[3] = { -1, -1, -1 };
        int32_t pid = -1;

        if (i > 0U) {
            in_fd = pipe_fds[i - 1U][0];
        }
        if (i + 1U < stage_count) {
            out_fd = pipe_fds[i][1];
        }
        if (stages[i].has_redir_in) {
            in_fd = shell_open_redirect_in(stages[i].redir_in);
            if (in_fd < 0) {
                shell_write_str(kRedirectOpenFail);
                goto cleanup;
            }
        }
        if (stages[i].has_redir_out) {
            out_fd = shell_open_redirect_out(stages[i].redir_out, stages[i].redir_out_append);
            if (out_fd < 0) {
                if (in_fd >= 3) {
                    (void)user_close((uint32_t)in_fd);
                }
                shell_write_str(kRedirectOpenFail);
                goto cleanup;
            }
        }
        if (stages[i].has_redir_err) {
            err_fd = shell_open_redirect_out(stages[i].redir_err, stages[i].redir_err_append);
            if (err_fd < 0) {
                if (in_fd >= 3) {
                    (void)user_close((uint32_t)in_fd);
                }
                if (out_fd >= 3) {
                    (void)user_close((uint32_t)out_fd);
                }
                shell_write_str(kRedirectOpenFail);
                goto cleanup;
            }
        }

        if (in_fd >= 0 && shell_save_and_dup2(USER_FD_STDIN, in_fd, saved_fd) < 0) {
            shell_restore_stdio(saved_fd);
            if (in_fd >= 3) {
                (void)user_close((uint32_t)in_fd);
            }
            if (out_fd >= 3) {
                (void)user_close((uint32_t)out_fd);
            }
            if (err_fd >= 3) {
                (void)user_close((uint32_t)err_fd);
            }
            shell_write_str(kSpawnFail);
            goto cleanup;
        }
        if (out_fd >= 0 && shell_save_and_dup2(USER_FD_STDOUT, out_fd, saved_fd) < 0) {
            shell_restore_stdio(saved_fd);
            if (in_fd >= 3) {
                (void)user_close((uint32_t)in_fd);
            }
            if (out_fd >= 3) {
                (void)user_close((uint32_t)out_fd);
            }
            if (err_fd >= 3) {
                (void)user_close((uint32_t)err_fd);
            }
            shell_write_str(kSpawnFail);
            goto cleanup;
        }
        if (err_fd >= 0 && shell_save_and_dup2(USER_FD_STDERR, err_fd, saved_fd) < 0) {
            shell_restore_stdio(saved_fd);
            if (in_fd >= 3) {
                (void)user_close((uint32_t)in_fd);
            }
            if (out_fd >= 3) {
                (void)user_close((uint32_t)out_fd);
            }
            if (err_fd >= 3) {
                (void)user_close((uint32_t)err_fd);
            }
            shell_write_str(kSpawnFail);
            goto cleanup;
        }

        if (shell_spawn_external(stages[i].cmd,
                                 stages[i].cmd_len,
                                 stages[i].cmdline_len != 0U ? stages[i].cmdline : 0,
                                 stages[i].cmdline_len,
                                 &pid) < 0) {
            shell_restore_stdio(saved_fd);
            if (in_fd >= 3) {
                (void)user_close((uint32_t)in_fd);
            }
            if (out_fd >= 3) {
                (void)user_close((uint32_t)out_fd);
            }
            if (err_fd >= 3) {
                (void)user_close((uint32_t)err_fd);
            }
            shell_write_str(kSpawnFail);
            goto cleanup;
        }
        child_pids[spawned++] = pid;
        shell_restore_stdio(saved_fd);

        if (i > 0U && pipe_fds[i - 1U][0] >= 0) {
            (void)user_close((uint32_t)pipe_fds[i - 1U][0]);
            pipe_fds[i - 1U][0] = -1;
        }
        if (i + 1U < stage_count && pipe_fds[i][1] >= 0) {
            (void)user_close((uint32_t)pipe_fds[i][1]);
            pipe_fds[i][1] = -1;
        }
        if (in_fd >= 3) {
            (void)user_close((uint32_t)in_fd);
        }
        if (out_fd >= 3) {
            (void)user_close((uint32_t)out_fd);
        }
        if (err_fd >= 3) {
            (void)user_close((uint32_t)err_fd);
        }
    }

cleanup:
    for (uint32_t i = 0U; i < SHELL_PIPELINE_MAX - 1U; i++) {
        if (pipe_fds[i][0] >= 0) {
            (void)user_close((uint32_t)pipe_fds[i][0]);
            pipe_fds[i][0] = -1;
        }
        if (pipe_fds[i][1] >= 0) {
            (void)user_close((uint32_t)pipe_fds[i][1]);
            pipe_fds[i][1] = -1;
        }
    }

    for (uint32_t i = 0U; i < spawned; i++) {
        int32_t status = 0;
        if (user_waitpid(child_pids[i], &status, 0U) < 0) {
            shell_write_str(kWaitFail);
            return -1;
        }
        if (i + 1U == spawned) {
            last_status = status;
        }
    }
    return (int)last_status;
}

static int shell_dispatch_builtin(struct shell_stage *stage) {
    if (user_str_eq_n(stage->cmd, "help", stage->cmd_len)) {
        shell_write_str(kHelp);
        return 1;
    }
    if (user_str_eq_n(stage->cmd, "history", stage->cmd_len)) {
        shell_run_history();
        return 1;
    }
    if (user_str_eq_n(stage->cmd, "wait", stage->cmd_len)) {
        shell_write_str(kWaitStub);
        return 1;
    }
    if (user_str_eq_n(stage->cmd, "ps", stage->cmd_len)) {
        shell_run_ps();
        return 1;
    }
    if (user_str_eq_n(stage->cmd, "ls", stage->cmd_len)) {
        shell_run_ls(stage->cmdline, stage->cmdline_len);
        return 1;
    }
    if (user_str_eq_n(stage->cmd, "pwd", stage->cmd_len)) {
        shell_run_pwd();
        return 1;
    }
    if (user_str_eq_n(stage->cmd, "cd", stage->cmd_len)) {
        shell_run_cd(stage->cmdline, stage->cmdline_len);
        return 1;
    }
    if (user_str_eq_n(stage->cmd, "exit", stage->cmd_len)) {
        shell_write_str(kExit);
        return 0;
    }
    return 1;
}

static int shell_dispatch_simple_line(const char *line) {
    uint32_t cmd_start = shell_skip_spaces(line, 0U);
    uint32_t cmd_end;
    uint32_t arg_start;

    if (!line || line[cmd_start] == '\0') {
        return 1;
    }

    cmd_end = shell_token_end(line, cmd_start);
    arg_start = shell_skip_spaces(line, cmd_end);

    if (user_str_eq_n(line + cmd_start, "help", cmd_end - cmd_start)) {
        shell_write_str(kHelp);
        return 1;
    }
    if (user_str_eq_n(line + cmd_start, "history", cmd_end - cmd_start)) {
        shell_run_history();
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
    if (user_str_eq_n(line + cmd_start, "ls", cmd_end - cmd_start)) {
        shell_run_ls(line + arg_start, user_strlen(line + arg_start));
        return 1;
    }
    if (user_str_eq_n(line + cmd_start, "pwd", cmd_end - cmd_start)) {
        shell_run_pwd();
        return 1;
    }
    if (user_str_eq_n(line + cmd_start, "cd", cmd_end - cmd_start)) {
        shell_run_cd(line + arg_start, user_strlen(line + arg_start));
        return 1;
    }
    if (user_str_eq_n(line + cmd_start, "exit", cmd_end - cmd_start)) {
        shell_write_str(kExit);
        return 0;
    }

    shell_run_external_simple(line + cmd_start,
                              cmd_end - cmd_start,
                              line + arg_start,
                              user_strlen(line + arg_start));
    return 1;
}

static int shell_line_has_meta(const char *line) {
    uint32_t i = 0U;

    if (!line) {
        return 0;
    }
    while (line[i] != '\0') {
        if (line[i] == '|' || line[i] == '<' || line[i] == '>') {
            return 1;
        }
        i++;
    }
    return 0;
}

static int shell_dispatch_line(char *line) {
    uint32_t stage_count = 0U;
    int has_meta = 0;
    int rc;

    if (!line) {
        return 1;
    }
    if (!shell_line_has_meta(line)) {
        return shell_dispatch_simple_line(line);
    }

    rc = shell_parse_stages(line, g_shell_stages, SHELL_PIPELINE_MAX, &stage_count, &has_meta);
    if (rc < 0) {
        shell_write_str(kParseFail);
        return 1;
    }
    if (stage_count == 0U) {
        return 1;
    }

    if (stage_count == 1U && !has_meta &&
        shell_is_builtin_name(g_shell_stages[0].cmd, g_shell_stages[0].cmd_len)) {
        return shell_dispatch_builtin(&g_shell_stages[0]);
    }
    for (uint32_t i = 0U; i < stage_count; i++) {
        if (shell_is_builtin_name(g_shell_stages[i].cmd, g_shell_stages[i].cmd_len)) {
            shell_write_str(kBuiltinPipeRedirect);
            return 1;
        }
    }

    (void)shell_execute_pipeline(g_shell_stages, stage_count);
    return 1;
}

void _start(void) {
    (void)shell_sync_cwd();
    shell_write_str(kBanner);
    for (;;) {
        int32_t rc;

        shell_write_str(shell_build_prompt());
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
