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
/* Keep this below spawn-slot concurrency so overflow handling stays reachable in smoke tests. */
#define SHELL_BG_JOB_MAX 2U
#define SHELL_COMPLETION_MAX_MATCHES (SHELL_LS_MAX_ENTRIES + 8U)
#define SHELL_HISTORY_MAX 8U
#define SHELL_HISTORY_PERSIST_PATH "/persist/.sh_history"
#define SHELL_TIMELINE_MAX 24U
#define SHELL_VAR_MAX 16U
#define SHELL_VAR_NAME_MAX 24U
#define SHELL_VAR_VALUE_MAX 96U
#define SHELL_SCRIPT_CHUNK_MAX 64U
#define SHELL_SCRIPT_DEPTH_MAX 8U

enum shell_chain_op {
    SHELL_CHAIN_NONE = 0,
    SHELL_CHAIN_SEQ = 1,
    SHELL_CHAIN_AND = 2,
    SHELL_CHAIN_OR = 3,
};

struct shell_stage {
    char cmd[SHELL_PATH_MAX];
    uint32_t cmd_len;
    char cmdline[SHELL_STAGE_CMDLINE_MAX];
    uint32_t cmdline_len;
    char spawn_cmdline[SHELL_STAGE_CMDLINE_MAX];
    uint32_t spawn_cmdline_len;
    char redir_in[SHELL_PATH_MAX];
    char redir_out[SHELL_PATH_MAX];
    char redir_err[SHELL_PATH_MAX];
    uint32_t has_redir_in;
    uint32_t has_redir_out;
    uint32_t has_redir_err;
    uint32_t redir_out_append;
    uint32_t redir_err_append;
};

struct shell_bg_job {
    uint32_t used;
    uint32_t id;
    uint32_t remaining;
    int32_t pids[SHELL_PIPELINE_MAX];
    int32_t last_pid;
    int32_t last_status;
    char cmd_preview[SHELL_LINE_MAX];
};

struct shell_timeline_event {
    uint32_t seq;
    uint32_t ticks_lo;
    char tag[16];
    char detail[SHELL_LINE_MAX];
};

struct shell_var {
    uint32_t used;
    uint32_t exported;
    char name[SHELL_VAR_NAME_MAX];
    char value[SHELL_VAR_VALUE_MAX];
};

static char g_shell_line[SHELL_LINE_MAX];
static char g_shell_path[SHELL_PATH_MAX];
static char g_shell_cwd[SHELL_CWD_MAX] = "/";
static char g_shell_prompt[SHELL_PROMPT_MAX];
static struct shell_stage g_shell_stages[SHELL_PIPELINE_MAX];
static struct syscall_task_snapshot_entry g_shell_ps_tasks[SHELL_PS_MAX_TASKS];
static struct syscall_dir_entry g_shell_ls_entries[SHELL_LS_MAX_ENTRIES];
static struct shell_bg_job g_shell_bg_jobs[SHELL_BG_JOB_MAX];
static struct shell_timeline_event g_shell_timeline[SHELL_TIMELINE_MAX];
static struct shell_var g_shell_vars[SHELL_VAR_MAX];
static char g_shell_history[SHELL_HISTORY_MAX][SHELL_LINE_MAX];
static uint32_t g_shell_history_count;
static uint32_t g_shell_history_head;
static uint32_t g_shell_history_persist_muted;
static uint32_t g_shell_history_run_depth;
static uint32_t g_shell_bg_next_id = 1U;
static uint32_t g_shell_timeline_head;
static uint32_t g_shell_timeline_count;
static uint32_t g_shell_timeline_next_seq = 1U;
static uint32_t g_shell_theme_ansi;
static uint32_t g_shell_hud_enabled;
static uint32_t g_shell_last_cmd_ticks;
static uint32_t g_shell_last_cmd_health;
static uint32_t g_shell_ansi_supported;
static uint32_t g_shell_script_depth;
static int32_t g_shell_last_status;

static const char kBanner[] = "SkezOS shell ready\nType 'help' for commands.\n";
static const char kHelp[] =
    "builtins: help history [clear|run N] ps ls pwd cd jobs fg [job_id] wait timeline [N] replay [N] hud [on|off] set theme|hud ... sh source export exit\n"
    "run: <name> -> /bin/<name>.elf, ./tool uses cwd, pipeline/redir supported, suffix '&' for background\n"
    "ops: ';' '&&' '||' chaining supported, NAME=value and $NAME expansion supported\n"
    "scripts: source <path>, sh <path> (script mode), auto-runs /persist/rc.sh at boot when present\n"
    "jobs: list tracked background pipelines, fg waits one job, wait drains all tracked jobs, hud shows shell status\n"
    "hist: !!/!N/!-N/!prefix/!?term/^old^new^ recalls history\n"
    "edit: BS deletes, Ctrl+W word, Ctrl+U line, Esc/Ctrl+P history, Ctrl+N forward, Tab completes commands, Ctrl+R search\n";
static const char kCdFail[] = "cd: chdir failed\n";
static const char kLsFail[] = "ls: list failed\n";
static const char kPwdFail[] = "pwd: getcwd failed\n";
static const char kWaitStub[] = "wait: no background jobs\n";
static const char kWaitDone[] = "wait: done\n";
static const char kPsFail[] = "ps: snapshot failed\n";
static const char kHistoryEmpty[] = "history: empty\n";
static const char kHistoryCleared[] = "history: cleared\n";
static const char kHistoryUsage[] = "history: usage: history [clear|run N]\n";
static const char kHistoryRunDepthExceeded[] = "history: run depth exceeded\n";
static const char kHistoryRunPrefix[] = "history: run ";
static const char kHistorySearchPrefix[] = "search: ";
static const char kHistorySearchArrow[] = " -> ";
static const char kHistorySearchAny[] = "*";
static const char kHistorySearchNoMatch[] = "(no-match)";
static const char kHistoryEventNotFound[] = "history: event not found\n";
static const char kHistorySubstFailed[] = "history: substitution failed\n";
static const char kHistoryEventPrefix[] = "history: ";
static const char kHistoryEventArrow[] = " -> ";
static const char kSpawnFail[] = "run: launch failed\n";
static const char kWaitFail[] = "run: waitpid failed\n";
static const char kParseFail[] = "run: parse failed\n";
static const char kBuiltinPipeRedirect[] = "run: builtin does not support pipes/redirection\n";
static const char kRedirectOpenFail[] = "run: redirect open failed\n";
static const char kBgStartPrefix[] = "bg: started job=";
static const char kBgDonePrefix[] = "bg: done job=";
static const char kBgPidPrefix[] = " pid=";
static const char kBgStatusPrefix[] = " status=";
static const char kBgCmdPrefix[] = " cmd=";
static const char kBgOverflowPrefix[] = "bg: overflow cmd=";
static const char kBgTrackFail[] = "run: background tracking failed\n";
static const char kJobsNone[] = "jobs: no background jobs\n";
static const char kJobsPrefix[] = "jobs: id=";
static const char kJobsRemPrefix[] = " rem=";
static const char kFgUsage[] = "fg: usage: fg [job_id]\n";
static const char kFgNoJobs[] = "fg: no background jobs\n";
static const char kFgNotFound[] = "fg: job not found\n";
static const char kFgDonePrefix[] = "fg: done job=";
static const char kTimelineUsage[] = "timeline: usage: timeline [count]\n";
static const char kTimelineEmpty[] = "timeline: empty\n";
static const char kTimelinePrefix[] = "timeline: seq=";
static const char kTimelineTicksPrefix[] = " ticks=";
static const char kTimelineTagPrefix[] = " tag=";
static const char kTimelineDetailPrefix[] = " detail=";
static const char kReplayUsage[] = "replay: usage: replay [count]\n";
static const char kReplayEmpty[] = "replay: empty\n";
static const char kReplayPrefix[] = "replay: seq=";
static const char kHudUsage[] = "hud: usage: hud [on|off]\n";
static const char kHudPrefix[] = "hud: jobs=";
static const char kHudLastPrefix[] = " last=";
static const char kHudLatencyPrefix[] = " latency=";
static const char kHudStatePrefix[] = " state=";
static const char kHudTicksSuffix[] = "t";
static const char kSetUsage[] = "set: usage: set theme [plain|ansi] | set hud [on|off]\n";
static const char kSetThemePrefix[] = "set: theme=";
static const char kSetThemePlain[] = "plain\n";
static const char kSetThemeAnsi[] = "ansi\n";
static const char kSetThemeAnsiFallback[] = "set: ansi unsupported on this console; using plain prompt\n";
static const char kSetHudPrefix[] = "set: hud=";
static const char kSetHudOn[] = "on\n";
static const char kSetHudOff[] = "off\n";
static const char kSourceUsage[] = "source: usage: source <path>\n";
static const char kSourceFail[] = "source: open failed\n";
static const char kSourceDepthExceeded[] = "source: nesting depth exceeded\n";
static const char kSourceParseFail[] = "source: parse failed\n";
static const char kExportUsage[] = "export: usage: export NAME[=VALUE]\n";
static const char kExportPrefix[] = "export ";
static const char kExportEmpty[] = "export: empty\n";
static const char kRcRun[] = "rc: running /persist/rc.sh\n";
static const char kHealthOk[] = "ok";
static const char kHealthWarn[] = "warn";
static const char kHealthSlow[] = "slow";
static const char kAnsiCyan[] = "\x1b[36m";
static const char kAnsiReset[] = "\x1b[0m";
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
    "jobs",
    "fg",
    "timeline",
    "replay",
    "hud",
    "set",
    "sh",
    "source",
    "export",
    "exit",
};

static uint32_t shell_skip_spaces(const char *s, uint32_t idx);
static uint32_t shell_token_end(const char *s, uint32_t idx);
static int shell_is_meta_char(char ch);
static int shell_should_escape_spawn_char(char ch);
static int shell_append_escaped_arg(char *dst,
                                    uint32_t *len_io,
                                    uint32_t cap,
                                    const char *arg,
                                    uint32_t arg_len);
static int shell_str_contains_char_n(const char *s, uint32_t len, char ch);
static int shell_history_handle_command(const char *args, uint32_t args_len);
static void shell_reap_background_jobs_nonblocking(void);
static void shell_drain_background_jobs(void);
static void shell_bg_note_reaped(int32_t pid, int32_t status);
static int shell_run_jobs(void);
static int shell_run_fg(const char *args, uint32_t args_len);
static int shell_run_timeline(const char *args, uint32_t args_len);
static int shell_run_replay(const char *args, uint32_t args_len);
static int shell_run_hud(const char *args, uint32_t args_len);
static int shell_run_set(const char *args, uint32_t args_len);
static int shell_run_source(const char *args, uint32_t args_len, int interactive);
static int shell_run_export(const char *args, uint32_t args_len);
static int shell_history_search(const char *query,
                                uint32_t query_len,
                                uint32_t start_offset,
                                const char **out_line,
                                uint32_t *out_len,
                                uint32_t *out_offset);
static int shell_history_find_match_from_offset(const char *prefix,
                                                uint32_t prefix_len,
                                                uint32_t start_offset,
                                                int newer_direction,
                                                const char **out_line,
                                                uint32_t *out_len,
                                                uint32_t *out_offset);
static int shell_dispatch_line(char *line, int *out_status);

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

static void shell_copy_text(char *dst, uint32_t cap, const char *src) {
    uint32_t i = 0U;

    if (!dst || cap == 0U) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    while (src[i] != '\0' && i + 1U < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int shell_is_name_start_char(char ch) {
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           ch == '_';
}

static int shell_is_name_char(char ch) {
    return shell_is_name_start_char(ch) || (ch >= '0' && ch <= '9');
}

static int shell_var_find_slot(const char *name, uint32_t name_len) {
    if (!name || name_len == 0U) {
        return -1;
    }
    for (uint32_t i = 0U; i < SHELL_VAR_MAX; i++) {
        uint32_t len = user_strlen(g_shell_vars[i].name);
        if (g_shell_vars[i].used == 0U || len != name_len) {
            continue;
        }
        if (user_str_eq_n(name, g_shell_vars[i].name, name_len)) {
            return (int)i;
        }
    }
    return -1;
}

static int shell_var_alloc_slot(void) {
    for (uint32_t i = 0U; i < SHELL_VAR_MAX; i++) {
        if (g_shell_vars[i].used == 0U) {
            return (int)i;
        }
    }
    return -1;
}

static int shell_var_put(const char *name,
                         uint32_t name_len,
                         const char *value,
                         uint32_t value_len,
                         int set_export,
                         uint32_t export_value) {
    int slot;

    if (!name || name_len == 0U || name_len + 1U > SHELL_VAR_NAME_MAX) {
        return -1;
    }
    if (value_len + 1U > SHELL_VAR_VALUE_MAX) {
        return -1;
    }
    slot = shell_var_find_slot(name, name_len);
    if (slot < 0) {
        slot = shell_var_alloc_slot();
        if (slot < 0) {
            return -1;
        }
        g_shell_vars[slot].used = 1U;
        g_shell_vars[slot].exported = 0U;
        user_memcpy(g_shell_vars[slot].name, name, name_len);
        g_shell_vars[slot].name[name_len] = '\0';
    }
    if (set_export != 0) {
        g_shell_vars[slot].exported = export_value != 0U ? 1U : 0U;
    }
    if (value_len != 0U) {
        user_memcpy(g_shell_vars[slot].value, value, value_len);
    }
    g_shell_vars[slot].value[value_len] = '\0';
    return 0;
}

static int shell_var_mark_exported(const char *name, uint32_t name_len) {
    int slot;

    if (!name || name_len == 0U || name_len + 1U > SHELL_VAR_NAME_MAX) {
        return -1;
    }
    slot = shell_var_find_slot(name, name_len);
    if (slot < 0) {
        if (shell_var_put(name, name_len, "", 0U, 1, 1U) < 0) {
            return -1;
        }
        return 0;
    }
    g_shell_vars[slot].exported = 1U;
    return 0;
}

static const char *shell_var_get(const char *name, uint32_t name_len) {
    int slot = shell_var_find_slot(name, name_len);
    if (slot < 0) {
        return 0;
    }
    return g_shell_vars[slot].value;
}

static int shell_parse_assignment_token(const char *token,
                                        uint32_t token_len,
                                        uint32_t *out_eq_idx) {
    uint32_t eq = 0U;

    if (!token || token_len < 3U || !out_eq_idx) {
        return -1;
    }
    for (; eq < token_len; eq++) {
        if (token[eq] == '=') {
            break;
        }
    }
    if (eq == 0U || eq >= token_len) {
        return -1;
    }
    if (!shell_is_name_start_char(token[0])) {
        return -1;
    }
    for (uint32_t i = 1U; i < eq; i++) {
        if (!shell_is_name_char(token[i])) {
            return -1;
        }
    }
    *out_eq_idx = eq;
    return 0;
}

static int shell_apply_assignment_token(const char *token, uint32_t token_len, uint32_t exported) {
    uint32_t eq = 0U;

    if (shell_parse_assignment_token(token, token_len, &eq) < 0) {
        return -1;
    }
    return shell_var_put(token, eq, token + eq + 1U, token_len - (eq + 1U), 1, exported != 0U);
}

static int shell_expand_vars(const char *src, char *dst, uint32_t cap) {
    uint32_t i = 0U;
    uint32_t out = 0U;
    int in_single = 0;
    int in_double = 0;

    if (!src || !dst || cap == 0U) {
        return -1;
    }
    while (src[i] != '\0') {
        char ch = src[i];

        if (ch == '\\') {
            if (out + 1U >= cap) {
                return -1;
            }
            dst[out++] = src[i++];
            if (src[i] == '\0') {
                break;
            }
            if (out + 1U >= cap) {
                return -1;
            }
            dst[out++] = src[i++];
            continue;
        }
        if (!in_double && ch == '\'') {
            if (out + 1U >= cap) {
                return -1;
            }
            in_single = !in_single;
            dst[out++] = src[i++];
            continue;
        }
        if (!in_single && ch == '"') {
            if (out + 1U >= cap) {
                return -1;
            }
            in_double = !in_double;
            dst[out++] = src[i++];
            continue;
        }
        if (!in_single && ch == '$') {
            uint32_t start;
            uint32_t end;
            const char *value;

            if (src[i + 1U] == '?') {
                char num[16];
                uint32_t len = shell_format_i32(num, sizeof(num), g_shell_last_status);
                if (out + len + 1U > cap) {
                    return -1;
                }
                if (len != 0U) {
                    user_memcpy(dst + out, num, len);
                    out += len;
                }
                i += 2U;
                continue;
            }
            if (src[i + 1U] == '{') {
                start = i + 2U;
                end = start;
                while (src[end] != '\0' && src[end] != '}') {
                    end++;
                }
                if (src[end] != '}' || end == start || !shell_is_name_start_char(src[start])) {
                    return -1;
                }
                for (uint32_t k = start + 1U; k < end; k++) {
                    if (!shell_is_name_char(src[k])) {
                        return -1;
                    }
                }
                value = shell_var_get(src + start, end - start);
                if (value) {
                    uint32_t len = user_strlen(value);
                    if (out + len + 1U > cap) {
                        return -1;
                    }
                    if (len != 0U) {
                        user_memcpy(dst + out, value, len);
                        out += len;
                    }
                }
                i = end + 1U;
                continue;
            }
            if (shell_is_name_start_char(src[i + 1U])) {
                start = i + 1U;
                end = start + 1U;
                while (shell_is_name_char(src[end])) {
                    end++;
                }
                value = shell_var_get(src + start, end - start);
                if (value) {
                    uint32_t len = user_strlen(value);
                    if (out + len + 1U > cap) {
                        return -1;
                    }
                    if (len != 0U) {
                        user_memcpy(dst + out, value, len);
                        out += len;
                    }
                }
                i = end;
                continue;
            }
        }
        if (out + 1U >= cap) {
            return -1;
        }
        dst[out++] = src[i++];
    }
    if (in_single || in_double) {
        return -1;
    }
    dst[out] = '\0';
    return 0;
}

static uint32_t shell_now_ticks_lo(void) {
    struct syscall_time_info info;

    if (user_time_info(&info) < 0) {
        return 0U;
    }
    return info.ticks_lo;
}

static void shell_timeline_record(const char *tag, const char *detail) {
    struct shell_timeline_event *ev = &g_shell_timeline[g_shell_timeline_head];

    ev->seq = g_shell_timeline_next_seq++;
    if (g_shell_timeline_next_seq == 0U) {
        g_shell_timeline_next_seq = 1U;
    }
    ev->ticks_lo = shell_now_ticks_lo();
    shell_copy_text(ev->tag, sizeof(ev->tag), tag);
    shell_copy_text(ev->detail, sizeof(ev->detail), detail);

    g_shell_timeline_head = (g_shell_timeline_head + 1U) % SHELL_TIMELINE_MAX;
    if (g_shell_timeline_count < SHELL_TIMELINE_MAX) {
        g_shell_timeline_count++;
    }
}

static int shell_parse_optional_count(const char *args,
                                      uint32_t args_len,
                                      uint32_t *out_count,
                                      uint32_t default_count,
                                      uint32_t max_count) {
    uint32_t start;
    uint32_t end;
    uint32_t value = 0U;

    if (!out_count) {
        return -1;
    }
    *out_count = default_count;
    if (!args) {
        return 0;
    }

    start = shell_skip_spaces(args, 0U);
    if (start >= args_len || args[start] == '\0') {
        return 0;
    }
    end = shell_token_end(args, start);
    if (shell_skip_spaces(args, end) != args_len) {
        return -1;
    }
    for (uint32_t i = start; i < end; i++) {
        uint32_t digit;
        if (args[i] < '0' || args[i] > '9') {
            return -1;
        }
        digit = (uint32_t)(args[i] - '0');
        if (value > 429496729U || (value == 429496729U && digit > 5U)) {
            return -1;
        }
        value = value * 10U + digit;
    }
    if (value == 0U) {
        return -1;
    }
    if (value > max_count) {
        value = max_count;
    }
    *out_count = value;
    return 0;
}

static uint32_t shell_bg_active_count(void) {
    uint32_t active = 0U;

    for (uint32_t i = 0U; i < SHELL_BG_JOB_MAX; i++) {
        if (g_shell_bg_jobs[i].used != 0U) {
            active++;
        }
    }
    return active;
}

static const char *shell_timeline_latest_tag(void) {
    uint32_t idx;

    if (g_shell_timeline_count == 0U) {
        return "-";
    }
    idx = (g_shell_timeline_head + SHELL_TIMELINE_MAX - 1U) % SHELL_TIMELINE_MAX;
    if (g_shell_timeline[idx].tag[0] == '\0') {
        return "-";
    }
    return g_shell_timeline[idx].tag;
}

static const char *shell_cmd_health_name(void) {
    if (g_shell_last_cmd_health == 0U) {
        return kHealthOk;
    }
    if (g_shell_last_cmd_health == 1U) {
        return kHealthWarn;
    }
    return kHealthSlow;
}

static void shell_update_cmd_health(uint32_t ticks) {
    g_shell_last_cmd_ticks = ticks;
    if (ticks > 100U) {
        g_shell_last_cmd_health = 2U;
    } else if (ticks > 40U) {
        g_shell_last_cmd_health = 1U;
    } else {
        g_shell_last_cmd_health = 0U;
    }
}

static int shell_parse_on_off(const char *args,
                              uint32_t args_len,
                              uint32_t *out_value,
                              uint32_t *out_present) {
    uint32_t start;
    uint32_t end;

    if (!out_value || !out_present) {
        return -1;
    }
    *out_value = 0U;
    *out_present = 0U;
    if (!args) {
        return 0;
    }
    start = shell_skip_spaces(args, 0U);
    if (start >= args_len || args[start] == '\0') {
        return 0;
    }
    end = shell_token_end(args, start);
    if (shell_skip_spaces(args, end) != args_len) {
        return -1;
    }
    if (user_str_eq_n(args + start, "on", end - start)) {
        *out_value = 1U;
    } else if (user_str_eq_n(args + start, "off", end - start)) {
        *out_value = 0U;
    } else {
        return -1;
    }
    *out_present = 1U;
    return 0;
}

static void shell_hud_print_line(void) {
    char num[12];

    shell_write_str(kHudPrefix);
    shell_format_u32(num, sizeof(num), shell_bg_active_count());
    shell_write_str(num);
    shell_write_str(kHudLastPrefix);
    shell_write_str(shell_timeline_latest_tag());
    shell_write_str(kHudLatencyPrefix);
    shell_format_u32(num, sizeof(num), g_shell_last_cmd_ticks);
    shell_write_str(num);
    shell_write_str(kHudTicksSuffix);
    shell_write_str(kHudStatePrefix);
    shell_write_str(shell_cmd_health_name());
    shell_write_str(kNewline);
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
    if (g_shell_theme_ansi != 0U && g_shell_ansi_supported != 0U) {
        shell_append_prompt_text(g_shell_prompt, sizeof(g_shell_prompt), &len, kAnsiCyan);
    }
    shell_append_prompt_text(g_shell_prompt, sizeof(g_shell_prompt), &len, "sh>");
    if (g_shell_theme_ansi != 0U && g_shell_ansi_supported != 0U) {
        shell_append_prompt_text(g_shell_prompt, sizeof(g_shell_prompt), &len, kAnsiReset);
    }
    shell_append_prompt_text(g_shell_prompt, sizeof(g_shell_prompt), &len, " ");
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

static void shell_write_fd_all(uint32_t fd, const char *buf, uint32_t len) {
    while (len > 0U) {
        int32_t rc = user_write(fd, buf, len);
        if (rc <= 0) {
            return;
        }
        buf += (uint32_t)rc;
        len -= (uint32_t)rc;
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

static int shell_history_get_by_index(uint32_t display_index,
                                      const char **out_line,
                                      uint32_t *out_len) {
    uint32_t oldest;
    uint32_t idx;

    if (!out_line || !out_len || display_index == 0U || display_index > g_shell_history_count) {
        return -1;
    }
    oldest = (g_shell_history_head + SHELL_HISTORY_MAX - g_shell_history_count) % SHELL_HISTORY_MAX;
    idx = (oldest + (display_index - 1U)) % SHELL_HISTORY_MAX;
    *out_line = g_shell_history[idx];
    *out_len = user_strlen(g_shell_history[idx]);
    return 0;
}

static void shell_history_persist_flush(void) {
    uint32_t oldest;
    int32_t fd;

    if (g_shell_history_persist_muted != 0U) {
        return;
    }

    fd = user_open(SHELL_HISTORY_PERSIST_PATH,
                   (uint32_t)(sizeof(SHELL_HISTORY_PERSIST_PATH) - 1U),
                   SYSCALL_OPEN_FLAG_WRITE |
                       SYSCALL_OPEN_FLAG_CREATE |
                       SYSCALL_OPEN_FLAG_TRUNC);
    if (fd < 0) {
        return;
    }

    oldest = (g_shell_history_head + SHELL_HISTORY_MAX - g_shell_history_count) % SHELL_HISTORY_MAX;
    for (uint32_t i = 0U; i < g_shell_history_count; i++) {
        uint32_t idx = (oldest + i) % SHELL_HISTORY_MAX;
        uint32_t len = user_strlen(g_shell_history[idx]);

        if (len != 0U) {
            shell_write_fd_all((uint32_t)fd, g_shell_history[idx], len);
        }
        shell_write_fd_all((uint32_t)fd, "\n", 1U);
    }
    (void)user_close((uint32_t)fd);
}

static void shell_history_push_internal(const char *line, uint32_t len, int persist) {
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
    if (persist != 0) {
        shell_history_persist_flush();
    }
}

static void shell_history_push(const char *line, uint32_t len) {
    shell_history_push_internal(line, len, 1);
}

static void shell_history_load_persisted(void) {
    char chunk[64];
    char line[SHELL_LINE_MAX];
    uint32_t line_len = 0U;
    uint32_t line_overflow = 0U;
    int32_t fd;

    fd = user_open(SHELL_HISTORY_PERSIST_PATH,
                   (uint32_t)(sizeof(SHELL_HISTORY_PERSIST_PATH) - 1U),
                   0U);
    if (fd < 0) {
        return;
    }

    g_shell_history_persist_muted = 1U;
    for (;;) {
        int32_t rc = user_read((uint32_t)fd, chunk, (uint32_t)sizeof(chunk));
        if (rc <= 0) {
            break;
        }

        for (uint32_t i = 0U; i < (uint32_t)rc; i++) {
            char ch = chunk[i];

            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                if (line_len != 0U) {
                    shell_history_push_internal(line, line_len, 0);
                }
                line_len = 0U;
                line_overflow = 0U;
                continue;
            }
            if (line_overflow != 0U) {
                continue;
            }
            if (line_len + 1U < SHELL_LINE_MAX) {
                line[line_len++] = ch;
            } else {
                line_overflow = 1U;
            }
        }
    }

    if (line_len != 0U) {
        shell_history_push_internal(line, line_len, 0);
    }
    g_shell_history_persist_muted = 0U;
    (void)user_close((uint32_t)fd);
}

static void shell_history_clear(void) {
    g_shell_history_count = 0U;
    g_shell_history_head = 0U;
    shell_history_persist_flush();
}

static int shell_history_line_contains(const char *line,
                                       uint32_t line_len,
                                       const char *needle,
                                       uint32_t needle_len) {
    if (!line || !needle || needle_len > line_len) {
        return 0;
    }
    if (needle_len == 0U) {
        return 1;
    }
    for (uint32_t i = 0U; i + needle_len <= line_len; i++) {
        uint32_t j = 0U;
        while (j < needle_len && line[i + j] == needle[j]) {
            j++;
        }
        if (j == needle_len) {
            return 1;
        }
    }
    return 0;
}

static int shell_history_line_starts_with(const char *line,
                                          uint32_t line_len,
                                          const char *prefix,
                                          uint32_t prefix_len) {
    if (!line || !prefix || prefix_len > line_len) {
        return 0;
    }
    for (uint32_t i = 0U; i < prefix_len; i++) {
        if (line[i] != prefix[i]) {
            return 0;
        }
    }
    return 1;
}

static int shell_history_find_prefix(const char *prefix,
                                     uint32_t prefix_len,
                                     const char **out_line,
                                     uint32_t *out_len) {
    if (!prefix || prefix_len == 0U || !out_line || !out_len) {
        return -1;
    }
    for (uint32_t offset = 0U; offset < g_shell_history_count; offset++) {
        const char *line = 0;
        uint32_t line_len = 0U;

        if (shell_history_get(offset, &line, &line_len) < 0) {
            continue;
        }
        if (!shell_history_line_starts_with(line, line_len, prefix, prefix_len)) {
            continue;
        }
        *out_line = line;
        *out_len = line_len;
        return 0;
    }
    return -1;
}

static int shell_history_find_match_from_offset(const char *prefix,
                                                uint32_t prefix_len,
                                                uint32_t start_offset,
                                                int newer_direction,
                                                const char **out_line,
                                                uint32_t *out_len,
                                                uint32_t *out_offset) {
    if (!out_line || !out_len || !out_offset || start_offset >= g_shell_history_count) {
        return -1;
    }

    if (newer_direction != 0) {
        uint32_t offset = start_offset;
        for (;;) {
            const char *line = 0;
            uint32_t line_len = 0U;

            if (shell_history_get(offset, &line, &line_len) == 0 &&
                (prefix_len == 0U || shell_history_line_starts_with(line, line_len, prefix, prefix_len))) {
                *out_line = line;
                *out_len = line_len;
                *out_offset = offset;
                return 0;
            }
            if (offset == 0U) {
                break;
            }
            offset--;
        }
        return -1;
    }

    for (uint32_t offset = start_offset; offset < g_shell_history_count; offset++) {
        const char *line = 0;
        uint32_t line_len = 0U;

        if (shell_history_get(offset, &line, &line_len) < 0) {
            continue;
        }
        if (prefix_len != 0U &&
            !shell_history_line_starts_with(line, line_len, prefix, prefix_len)) {
            continue;
        }
        *out_line = line;
        *out_len = line_len;
        *out_offset = offset;
        return 0;
    }
    return -1;
}

static int shell_history_search(const char *query,
                                uint32_t query_len,
                                uint32_t start_offset,
                                const char **out_line,
                                uint32_t *out_len,
                                uint32_t *out_offset) {
    uint32_t count = g_shell_history_count;

    if (count == 0U || !out_line || !out_len || !out_offset) {
        return -1;
    }
    if (query_len != 0U && !query) {
        return -1;
    }
    if (start_offset >= count) {
        start_offset = 0U;
    }

    for (uint32_t pass = 0U; pass < 2U; pass++) {
        uint32_t begin = pass == 0U ? start_offset : 0U;
        uint32_t end = pass == 0U ? count : start_offset;

        for (uint32_t offset = begin; offset < end; offset++) {
            const char *line = 0;
            uint32_t line_len = 0U;

            if (shell_history_get(offset, &line, &line_len) < 0) {
                continue;
            }
            if (!shell_history_line_contains(line, line_len, query, query_len)) {
                continue;
            }
            *out_line = line;
            *out_len = line_len;
            *out_offset = offset;
            return 0;
        }
    }
    return -1;
}

static int shell_find_substring(const char *line,
                                uint32_t line_len,
                                const char *needle,
                                uint32_t needle_len,
                                uint32_t *out_index) {
    if (!line || !needle || !out_index || needle_len == 0U || needle_len > line_len) {
        return -1;
    }
    for (uint32_t i = 0U; i + needle_len <= line_len; i++) {
        uint32_t j = 0U;
        while (j < needle_len && line[i + j] == needle[j]) {
            j++;
        }
        if (j == needle_len) {
            *out_index = i;
            return 0;
        }
    }
    return -1;
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
    char history_browse_query[SHELL_LINE_MAX];
    uint32_t history_browse_query_len = 0U;
    uint32_t history_search_active = 0U;
    uint32_t history_search_next_offset = 0U;
    char history_search_query[SHELL_LINE_MAX];
    uint32_t history_search_query_len = 0U;

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
        if (ch == 0x12) {
            const char *match_line = 0;
            uint32_t match_len = 0U;
            uint32_t match_offset = 0U;

            if (g_shell_history_count == 0U) {
                continue;
            }
            if (history_search_active == 0U) {
                history_search_query_len = len;
                if (history_search_query_len + 1U > sizeof(history_search_query)) {
                    history_search_query_len = (uint32_t)sizeof(history_search_query) - 1U;
                }
                if (history_search_query_len != 0U) {
                    user_memcpy(history_search_query, buf, history_search_query_len);
                }
                history_search_query[history_search_query_len] = '\0';
                history_search_next_offset = 0U;
                history_search_active = 1U;
            }

            if (shell_history_search(history_search_query,
                                     history_search_query_len,
                                     history_search_next_offset,
                                     &match_line,
                                     &match_len,
                                     &match_offset) == 0) {
                shell_replace_line(buf, &len, cap, match_line, match_len);
                history_search_next_offset = match_offset + 1U;
                if (history_search_next_offset >= g_shell_history_count) {
                    history_search_next_offset = 0U;
                }
                history_browse_active = 0U;
                shell_write_str(kNewline);
                shell_write_str(kHistorySearchPrefix);
                if (history_search_query_len == 0U) {
                    shell_write_str(kHistorySearchAny);
                } else {
                    shell_write_all(history_search_query, history_search_query_len);
                }
                shell_write_str(kHistorySearchArrow);
                shell_write_all(match_line, match_len);
                shell_write_str(kNewline);
                shell_write_str(shell_build_prompt());
                if (len != 0U) {
                    shell_write_all(buf, len);
                }
            } else {
                history_search_active = 0U;
                history_search_next_offset = 0U;
                history_search_query_len = 0U;
                shell_write_str(kNewline);
                shell_write_str(kHistorySearchPrefix);
                shell_write_str(kHistorySearchNoMatch);
                shell_write_str(kNewline);
                shell_write_str(shell_build_prompt());
                if (len != 0U) {
                    shell_write_all(buf, len);
                }
            }
            continue;
        }
        if (history_search_active != 0U) {
            history_search_active = 0U;
            history_search_next_offset = 0U;
            history_search_query_len = 0U;
        }
        if (ch == 27 || ch == 0x10) {
            const char *history_line = 0;
            uint32_t history_len = 0U;
            uint32_t match_offset = 0U;
            uint32_t search_start = 0U;

            if (g_shell_history_count == 0U) {
                continue;
            }
            if (history_browse_active == 0U) {
                history_browse_query_len = len;
                if (history_browse_query_len + 1U > sizeof(history_browse_query)) {
                    history_browse_query_len = (uint32_t)sizeof(history_browse_query) - 1U;
                }
                if (history_browse_query_len != 0U) {
                    user_memcpy(history_browse_query, buf, history_browse_query_len);
                }
                history_browse_query[history_browse_query_len] = '\0';
                history_browse_active = 1U;
                search_start = 0U;
            } else {
                search_start = history_offset + 1U;
                if (search_start >= g_shell_history_count) {
                    continue;
                }
            }

            if (shell_history_find_match_from_offset(history_browse_query,
                                                     history_browse_query_len,
                                                     search_start,
                                                     0,
                                                     &history_line,
                                                     &history_len,
                                                     &match_offset) == 0) {
                history_offset = match_offset;
                shell_replace_line(buf, &len, cap, history_line, history_len);
            }
            continue;
        }
        if (ch == 0x0e) {
            const char *history_line = 0;
            uint32_t history_len = 0U;
            uint32_t match_offset = 0U;

            if (history_browse_active == 0U) {
                continue;
            }
            if (history_offset == 0U) {
                history_browse_active = 0U;
                shell_replace_line(buf, &len, cap, history_browse_query, history_browse_query_len);
                continue;
            }

            if (shell_history_find_match_from_offset(history_browse_query,
                                                     history_browse_query_len,
                                                     history_offset - 1U,
                                                     1,
                                                     &history_line,
                                                     &history_len,
                                                     &match_offset) == 0) {
                history_offset = match_offset;
                shell_replace_line(buf, &len, cap, history_line, history_len);
            } else {
                history_browse_active = 0U;
                shell_replace_line(buf, &len, cap, history_browse_query, history_browse_query_len);
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
        if (ch == 0x17) {
            history_browse_active = 0U;
            while (len > 0U && user_is_space(buf[len - 1U])) {
                len--;
                shell_erase_one_char();
            }
            while (len > 0U && !user_is_space(buf[len - 1U])) {
                len--;
                shell_erase_one_char();
            }
            continue;
        }
        if (ch == 0x15) {
            history_browse_active = 0U;
            while (len > 0U) {
                len--;
                shell_erase_one_char();
            }
            continue;
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

static int shell_is_meta_char(char ch) {
    return ch == '|' || ch == '<' || ch == '>' || ch == '&' || ch == ';';
}

static int shell_should_escape_spawn_char(char ch) {
    return user_is_space(ch) || ch == '\\' || ch == '\'' || ch == '"' || shell_is_meta_char(ch);
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
    SHELL_TOK_AMP = 8,
    SHELL_TOK_INVALID = 255,
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

static int shell_append_escaped_arg(char *dst,
                                    uint32_t *len_io,
                                    uint32_t cap,
                                    const char *arg,
                                    uint32_t arg_len) {
    uint32_t len;

    if (!dst || !len_io || cap == 0U || (!arg && arg_len != 0U)) {
        return -1;
    }
    len = *len_io;
    if (len >= cap) {
        return -1;
    }
    if (len != 0U) {
        if (len + 1U >= cap) {
            return -1;
        }
        dst[len++] = ' ';
    }
    if (arg_len == 0U) {
        if (len + 2U >= cap) {
            return -1;
        }
        dst[len++] = '"';
        dst[len++] = '"';
        dst[len] = '\0';
        *len_io = len;
        return 0;
    }
    for (uint32_t i = 0U; i < arg_len; i++) {
        char ch = arg[i];
        if (shell_should_escape_spawn_char(ch)) {
            if (len + 2U >= cap) {
                return -1;
            }
            dst[len++] = '\\';
            dst[len++] = ch;
        } else {
            if (len + 1U >= cap) {
                return -1;
            }
            dst[len++] = ch;
        }
    }
    dst[len] = '\0';
    *len_io = len;
    return 0;
}

static enum shell_token_kind shell_next_token(const char *line,
                                              uint32_t *io_idx,
                                              char *out_buf,
                                              uint32_t out_cap,
                                              uint32_t *out_len) {
    uint32_t idx;
    uint32_t tok_len = 0U;
    int in_single = 0;
    int in_double = 0;
    int saw_token = 0;

    if (!line || !io_idx || !out_buf || out_cap == 0U || !out_len) {
        return SHELL_TOK_INVALID;
    }
    idx = shell_skip_spaces(line, *io_idx);
    if (line[idx] == '\0') {
        *io_idx = idx;
        out_buf[0] = '\0';
        *out_len = 0U;
        return SHELL_TOK_NONE;
    }

    if (line[idx] == '|') {
        *io_idx = idx + 1U;
        out_buf[0] = '\0';
        *out_len = 0U;
        return SHELL_TOK_PIPE;
    }
    if (line[idx] == '&') {
        *io_idx = idx + 1U;
        out_buf[0] = '\0';
        *out_len = 0U;
        return SHELL_TOK_AMP;
    }
    if (line[idx] == '<') {
        *io_idx = idx + 1U;
        out_buf[0] = '\0';
        *out_len = 0U;
        return SHELL_TOK_REDIR_IN;
    }
    if (line[idx] == '>') {
        if (line[idx + 1U] == '>') {
            *io_idx = idx + 2U;
            out_buf[0] = '\0';
            *out_len = 0U;
            return SHELL_TOK_REDIR_OUT_APPEND;
        }
        *io_idx = idx + 1U;
        out_buf[0] = '\0';
        *out_len = 0U;
        return SHELL_TOK_REDIR_OUT;
    }
    if (line[idx] == '2' && line[idx + 1U] == '>') {
        if (line[idx + 2U] == '>') {
            *io_idx = idx + 3U;
            out_buf[0] = '\0';
            *out_len = 0U;
            return SHELL_TOK_REDIR_ERR_APPEND;
        }
        *io_idx = idx + 2U;
        out_buf[0] = '\0';
        *out_len = 0U;
        return SHELL_TOK_REDIR_ERR;
    }

    while (line[idx] != '\0') {
        char ch = line[idx];

        if (!in_single && ch == '\\') {
            idx++;
            if (line[idx] == '\0') {
                return SHELL_TOK_INVALID;
            }
            if (tok_len + 1U >= out_cap) {
                return SHELL_TOK_INVALID;
            }
            out_buf[tok_len++] = line[idx++];
            saw_token = 1;
            continue;
        }
        if (!in_double && ch == '\'') {
            in_single = !in_single;
            idx++;
            saw_token = 1;
            continue;
        }
        if (!in_single && ch == '"') {
            in_double = !in_double;
            idx++;
            saw_token = 1;
            continue;
        }
        if (!in_single && !in_double) {
            if (user_is_space(ch) || ch == '|' || ch == '&' || ch == '<' || ch == '>') {
                break;
            }
            if (ch == '2' && line[idx + 1U] == '>') {
                break;
            }
        }
        if (tok_len + 1U >= out_cap) {
            return SHELL_TOK_INVALID;
        }
        out_buf[tok_len++] = ch;
        idx++;
        saw_token = 1;
    }

    if (in_single || in_double || !saw_token) {
        return SHELL_TOK_INVALID;
    }
    out_buf[tok_len] = '\0';
    *io_idx = idx;
    *out_len = tok_len;
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
           user_str_eq_n(cmd, "jobs", cmd_len) ||
           user_str_eq_n(cmd, "fg", cmd_len) ||
           user_str_eq_n(cmd, "timeline", cmd_len) ||
           user_str_eq_n(cmd, "replay", cmd_len) ||
           user_str_eq_n(cmd, "hud", cmd_len) ||
           user_str_eq_n(cmd, "set", cmd_len) ||
           user_str_eq_n(cmd, "sh", cmd_len) ||
           user_str_eq_n(cmd, "source", cmd_len) ||
           user_str_eq_n(cmd, "export", cmd_len) ||
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

static int shell_stage_append_arg(struct shell_stage *stage, const char *arg, uint32_t arg_len) {
    if (!stage || !arg) {
        return -1;
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
    if (shell_append_escaped_arg(stage->spawn_cmdline,
                                 &stage->spawn_cmdline_len,
                                 sizeof(stage->spawn_cmdline),
                                 arg,
                                 arg_len) < 0) {
        return -1;
    }
    return 0;
}

static int shell_parse_stages(char *line,
                              struct shell_stage *stages,
                              uint32_t stage_cap,
                              uint32_t *out_stage_count,
                              int *out_has_meta,
                              uint32_t *out_background) {
    uint32_t idx = 0U;
    uint32_t stage_idx = 0U;
    struct shell_stage *stage;
    char tok_buf[SHELL_LINE_MAX];
    uint32_t tok_len = 0U;
    enum shell_token_kind tok_kind;

    if (!line || !stages || stage_cap == 0U || !out_stage_count || !out_has_meta || !out_background) {
        return -1;
    }
    *out_stage_count = 0U;
    *out_has_meta = 0;
    *out_background = 0U;
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
        stages[i].spawn_cmdline[0] = '\0';
        stages[i].spawn_cmdline_len = 0U;
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
        tok_kind = shell_next_token(line, &idx, tok_buf, sizeof(tok_buf), &tok_len);
        if (tok_kind == SHELL_TOK_INVALID) {
            return -1;
        }
        if (tok_kind == SHELL_TOK_NONE) {
            break;
        }
        if (tok_kind == SHELL_TOK_PIPE) {
            *out_has_meta = 1;
            if (*out_background != 0U || stage->cmd_len == 0U) {
                return -1;
            }
            stage_idx++;
            if (stage_idx >= stage_cap) {
                return -1;
            }
            stage = &stages[stage_idx];
            continue;
        }
        if (tok_kind == SHELL_TOK_AMP) {
            *out_has_meta = 1;
            if (*out_background != 0U || stage->cmd_len == 0U) {
                return -1;
            }
            *out_background = 1U;
            tok_kind = shell_next_token(line, &idx, tok_buf, sizeof(tok_buf), &tok_len);
            if (tok_kind != SHELL_TOK_NONE) {
                return -1;
            }
            break;
        }
        if (tok_kind == SHELL_TOK_REDIR_IN) {
            uint32_t path_len = 0U;

            *out_has_meta = 1;
            if (stage->has_redir_in) {
                return -1;
            }
            tok_kind = shell_next_token(line, &idx, tok_buf, sizeof(tok_buf), &path_len);
            if (tok_kind != SHELL_TOK_WORD || path_len == 0U) {
                return -1;
            }
            if (shell_copy_token(stage->redir_in, sizeof(stage->redir_in), tok_buf, path_len) < 0) {
                return -1;
            }
            stage->has_redir_in = 1U;
            continue;
        }
        if (tok_kind == SHELL_TOK_REDIR_OUT || tok_kind == SHELL_TOK_REDIR_OUT_APPEND) {
            uint32_t path_len = 0U;
            enum shell_token_kind redir_kind = tok_kind;

            *out_has_meta = 1;
            if (stage->has_redir_out) {
                return -1;
            }
            tok_kind = shell_next_token(line, &idx, tok_buf, sizeof(tok_buf), &path_len);
            if (tok_kind != SHELL_TOK_WORD || path_len == 0U) {
                return -1;
            }
            if (shell_copy_token(stage->redir_out, sizeof(stage->redir_out), tok_buf, path_len) < 0) {
                return -1;
            }
            stage->has_redir_out = 1U;
            stage->redir_out_append = (redir_kind == SHELL_TOK_REDIR_OUT_APPEND);
            continue;
        }
        if (tok_kind == SHELL_TOK_REDIR_ERR || tok_kind == SHELL_TOK_REDIR_ERR_APPEND) {
            uint32_t path_len = 0U;
            enum shell_token_kind redir_kind = tok_kind;

            *out_has_meta = 1;
            if (stage->has_redir_err) {
                return -1;
            }
            tok_kind = shell_next_token(line, &idx, tok_buf, sizeof(tok_buf), &path_len);
            if (tok_kind != SHELL_TOK_WORD || path_len == 0U) {
                return -1;
            }
            if (shell_copy_token(stage->redir_err, sizeof(stage->redir_err), tok_buf, path_len) < 0) {
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
            if (shell_copy_token(stage->cmd, sizeof(stage->cmd), tok_buf, tok_len) < 0) {
                return -1;
            }
            stage->cmd_len = tok_len;
            continue;
        }
        if (shell_stage_append_arg(stage, tok_buf, tok_len) < 0) {
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

static int shell_bg_has_active_jobs(void) {
    for (uint32_t i = 0U; i < SHELL_BG_JOB_MAX; i++) {
        if (g_shell_bg_jobs[i].used != 0U) {
            return 1;
        }
    }
    return 0;
}

static int shell_bg_has_free_slot(void) {
    for (uint32_t i = 0U; i < SHELL_BG_JOB_MAX; i++) {
        if (g_shell_bg_jobs[i].used == 0U) {
            return 1;
        }
    }
    return 0;
}

static void shell_bg_copy_preview(char *dst, uint32_t cap, const char *src) {
    uint32_t start = 0U;
    uint32_t len = 0U;

    if (!dst || cap == 0U) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    while (src[start] != '\0' && user_is_space(src[start])) {
        start++;
    }
    while (src[start + len] != '\0' && len + 1U < cap) {
        dst[len] = src[start + len];
        len++;
    }
    dst[len] = '\0';
}

static void shell_bg_print_started(const struct shell_bg_job *job) {
    char num[12];

    if (!job) {
        return;
    }
    shell_write_str(kBgStartPrefix);
    shell_format_u32(num, sizeof(num), job->id);
    shell_write_str(num);
    shell_write_str(kBgPidPrefix);
    shell_format_i32(num, sizeof(num), job->last_pid);
    shell_write_str(num);
    shell_write_str(kBgCmdPrefix);
    shell_write_str(job->cmd_preview);
    shell_write_str(kNewline);
    shell_timeline_record("bg-start", job->cmd_preview);
}

static void shell_bg_print_done(const struct shell_bg_job *job) {
    char num[12];

    if (!job) {
        return;
    }
    shell_write_str(kBgDonePrefix);
    shell_format_u32(num, sizeof(num), job->id);
    shell_write_str(num);
    shell_write_str(kBgPidPrefix);
    shell_format_i32(num, sizeof(num), job->last_pid);
    shell_write_str(num);
    shell_write_str(kBgStatusPrefix);
    shell_format_i32(num, sizeof(num), job->last_status);
    shell_write_str(num);
    shell_write_str(kBgCmdPrefix);
    shell_write_str(job->cmd_preview);
    shell_write_str(kNewline);
    shell_timeline_record("bg-done", job->cmd_preview);
}

static void shell_bg_print_overflow(const char *cmd_preview) {
    shell_write_str(kBgOverflowPrefix);
    if (cmd_preview) {
        shell_write_str(cmd_preview);
    }
    shell_write_str(kNewline);
    shell_timeline_record("bg-overflow", cmd_preview ? cmd_preview : "");
}

static struct shell_bg_job *shell_bg_find_latest_job(void) {
    struct shell_bg_job *best = 0;

    for (uint32_t i = 0U; i < SHELL_BG_JOB_MAX; i++) {
        struct shell_bg_job *job = &g_shell_bg_jobs[i];
        if (job->used == 0U) {
            continue;
        }
        if (!best || job->id > best->id) {
            best = job;
        }
    }
    return best;
}

static struct shell_bg_job *shell_bg_find_job_by_id(uint32_t id) {
    for (uint32_t i = 0U; i < SHELL_BG_JOB_MAX; i++) {
        struct shell_bg_job *job = &g_shell_bg_jobs[i];
        if (job->used != 0U && job->id == id) {
            return job;
        }
    }
    return 0;
}

static int shell_run_jobs(void) {
    uint32_t listed = 0U;
    char num[12];

    shell_reap_background_jobs_nonblocking();
    for (uint32_t i = 0U; i < SHELL_BG_JOB_MAX; i++) {
        struct shell_bg_job *job = &g_shell_bg_jobs[i];
        if (job->used == 0U) {
            continue;
        }

        listed++;
        shell_write_str(kJobsPrefix);
        shell_format_u32(num, sizeof(num), job->id);
        shell_write_str(num);
        shell_write_str(kBgPidPrefix);
        shell_format_i32(num, sizeof(num), job->last_pid);
        shell_write_str(num);
        shell_write_str(kJobsRemPrefix);
        shell_format_u32(num, sizeof(num), job->remaining);
        shell_write_str(num);
        shell_write_str(kBgCmdPrefix);
        shell_write_str(job->cmd_preview);
        shell_write_str(kNewline);
    }

    if (listed == 0U) {
        shell_write_str(kJobsNone);
    }
    return 1;
}

static int shell_run_fg(const char *args, uint32_t args_len) {
    struct shell_bg_job *target = 0;
    uint32_t job_id = 0U;
    uint32_t have_job_id = 0U;
    uint32_t arg_start = 0U;
    uint32_t arg_end = 0U;
    int32_t target_last_pid = 0;
    int32_t target_last_status = 0;
    int32_t status = 0;
    char cmd_preview[SHELL_LINE_MAX];
    char num[12];

    shell_reap_background_jobs_nonblocking();

    if (!args) {
        args = "";
        args_len = 0U;
    }
    arg_start = shell_skip_spaces(args, 0U);
    if (arg_start < args_len && args[arg_start] != '\0') {
        arg_end = shell_token_end(args, arg_start);
        if (shell_skip_spaces(args, arg_end) != args_len) {
            shell_write_str(kFgUsage);
            shell_timeline_record("fg-usage", args);
            return 1;
        }

        for (uint32_t i = arg_start; i < arg_end; i++) {
            uint32_t digit;
            if (args[i] < '0' || args[i] > '9') {
                shell_write_str(kFgUsage);
                shell_timeline_record("fg-usage", args);
                return 1;
            }
            digit = (uint32_t)(args[i] - '0');
            if (job_id > 429496729U || (job_id == 429496729U && digit > 5U)) {
                shell_write_str(kFgUsage);
                shell_timeline_record("fg-usage", args);
                return 1;
            }
            job_id = job_id * 10U + digit;
        }
        if (job_id == 0U) {
            shell_write_str(kFgUsage);
            shell_timeline_record("fg-usage", args);
            return 1;
        }
        have_job_id = 1U;
    }

    if (have_job_id != 0U) {
        target = shell_bg_find_job_by_id(job_id);
        if (!target) {
            shell_write_str(kFgNotFound);
            shell_timeline_record("fg-miss", args);
            return 1;
        }
    } else {
        target = shell_bg_find_latest_job();
        if (!target) {
            shell_write_str(kFgNoJobs);
            shell_timeline_record("fg-empty", "");
            return 1;
        }
        job_id = target->id;
    }
    target_last_pid = target->last_pid;
    target_last_status = target->last_status;
    shell_bg_copy_preview(cmd_preview, sizeof(cmd_preview), target->cmd_preview);

    while (target->used != 0U) {
        int32_t waited = user_waitpid(-1, &status, 0U);
        if (waited <= 0) {
            shell_write_str(kWaitFail);
            return 1;
        }
        if (waited == target_last_pid) {
            target_last_status = status;
        }
        shell_bg_note_reaped(waited, status);
    }

    shell_write_str(kFgDonePrefix);
    shell_format_u32(num, sizeof(num), job_id);
    shell_write_str(num);
    shell_write_str(kBgPidPrefix);
    shell_format_i32(num, sizeof(num), target_last_pid);
    shell_write_str(num);
    shell_write_str(kBgStatusPrefix);
    shell_format_i32(num, sizeof(num), target_last_status);
    shell_write_str(num);
    shell_write_str(kBgCmdPrefix);
    shell_write_str(cmd_preview);
    shell_write_str(kNewline);
    shell_timeline_record("fg-done", cmd_preview);
    return 1;
}

static int shell_run_timeline(const char *args, uint32_t args_len) {
    uint32_t count = 8U;
    char num[12];

    if (shell_parse_optional_count(args, args_len, &count, 8U, SHELL_TIMELINE_MAX) < 0) {
        shell_write_str(kTimelineUsage);
        return 1;
    }
    if (g_shell_timeline_count == 0U) {
        shell_write_str(kTimelineEmpty);
        return 1;
    }
    if (count > g_shell_timeline_count) {
        count = g_shell_timeline_count;
    }

    for (uint32_t i = 0U; i < count; i++) {
        uint32_t idx = (g_shell_timeline_head + SHELL_TIMELINE_MAX - 1U - i) % SHELL_TIMELINE_MAX;
        const struct shell_timeline_event *ev = &g_shell_timeline[idx];

        shell_write_str(kTimelinePrefix);
        shell_format_u32(num, sizeof(num), ev->seq);
        shell_write_str(num);
        shell_write_str(kTimelineTicksPrefix);
        shell_format_u32(num, sizeof(num), ev->ticks_lo);
        shell_write_str(num);
        shell_write_str(kTimelineTagPrefix);
        shell_write_str(ev->tag);
        if (ev->detail[0] != '\0') {
            shell_write_str(kTimelineDetailPrefix);
            shell_write_str(ev->detail);
        }
        shell_write_str(kNewline);
    }
    return 1;
}

static int shell_run_replay(const char *args, uint32_t args_len) {
    uint32_t count = 8U;
    uint32_t start_offset;
    char num[12];

    if (shell_parse_optional_count(args, args_len, &count, 8U, SHELL_TIMELINE_MAX) < 0) {
        shell_write_str(kReplayUsage);
        return 1;
    }
    if (g_shell_timeline_count == 0U) {
        shell_write_str(kReplayEmpty);
        return 1;
    }
    if (count > g_shell_timeline_count) {
        count = g_shell_timeline_count;
    }
    start_offset = g_shell_timeline_count - count;

    for (uint32_t offset = start_offset; offset < g_shell_timeline_count; offset++) {
        uint32_t idx = (g_shell_timeline_head + SHELL_TIMELINE_MAX - g_shell_timeline_count + offset) %
                       SHELL_TIMELINE_MAX;
        const struct shell_timeline_event *ev = &g_shell_timeline[idx];

        shell_write_str(kReplayPrefix);
        shell_format_u32(num, sizeof(num), ev->seq);
        shell_write_str(num);
        shell_write_str(kTimelineTicksPrefix);
        shell_format_u32(num, sizeof(num), ev->ticks_lo);
        shell_write_str(num);
        shell_write_str(kTimelineTagPrefix);
        shell_write_str(ev->tag);
        if (ev->detail[0] != '\0') {
            shell_write_str(kTimelineDetailPrefix);
            shell_write_str(ev->detail);
        }
        shell_write_str(kNewline);
    }
    return 1;
}

static int shell_run_hud(const char *args, uint32_t args_len) {
    uint32_t value = 0U;
    uint32_t present = 0U;

    if (shell_parse_on_off(args, args_len, &value, &present) < 0) {
        shell_write_str(kHudUsage);
        return 1;
    }
    if (present != 0U) {
        g_shell_hud_enabled = value;
        shell_write_str(kSetHudPrefix);
        shell_write_str(g_shell_hud_enabled != 0U ? kSetHudOn : kSetHudOff);
        shell_timeline_record("set-hud", g_shell_hud_enabled != 0U ? "on" : "off");
    }
    shell_hud_print_line();
    return 1;
}

static int shell_run_set(const char *args, uint32_t args_len) {
    uint32_t arg_start;
    uint32_t arg_end;
    uint32_t val_start;
    uint32_t val_end;
    uint32_t value = 0U;
    uint32_t present = 0U;
    const char *arg_name;
    uint32_t arg_len;

    if (!args) {
        args = "";
        args_len = 0U;
    }
    arg_start = shell_skip_spaces(args, 0U);
    if (arg_start >= args_len || args[arg_start] == '\0') {
        shell_write_str(kSetThemePrefix);
        shell_write_str(g_shell_theme_ansi != 0U ? kSetThemeAnsi : kSetThemePlain);
        shell_write_str(kSetHudPrefix);
        shell_write_str(g_shell_hud_enabled != 0U ? kSetHudOn : kSetHudOff);
        shell_timeline_record("set-theme", g_shell_theme_ansi != 0U ? "ansi" : "plain");
        return 1;
    }

    arg_end = shell_token_end(args, arg_start);
    arg_name = args + arg_start;
    arg_len = arg_end - arg_start;
    if (!user_str_eq_n(arg_name, "theme", arg_len) &&
        !user_str_eq_n(arg_name, "hud", arg_len)) {
        shell_write_str(kSetUsage);
        return 1;
    }
    val_start = shell_skip_spaces(args, arg_end);
    if (val_start >= args_len || args[val_start] == '\0') {
        if (user_str_eq_n(arg_name, "theme", arg_len)) {
            shell_write_str(kSetThemePrefix);
            shell_write_str(g_shell_theme_ansi != 0U ? kSetThemeAnsi : kSetThemePlain);
        } else {
            shell_write_str(kSetHudPrefix);
            shell_write_str(g_shell_hud_enabled != 0U ? kSetHudOn : kSetHudOff);
        }
        return 1;
    }
    val_end = shell_token_end(args, val_start);
    if (shell_skip_spaces(args, val_end) != args_len) {
        shell_write_str(kSetUsage);
        return 1;
    }
    if (user_str_eq_n(arg_name, "theme", arg_len)) {
        if (user_str_eq_n(args + val_start, "plain", val_end - val_start)) {
            g_shell_theme_ansi = 0U;
        } else if (user_str_eq_n(args + val_start, "ansi", val_end - val_start)) {
            g_shell_theme_ansi = 1U;
            if (g_shell_ansi_supported == 0U) {
                shell_write_str(kSetThemeAnsiFallback);
            }
        } else {
            shell_write_str(kSetUsage);
            return 1;
        }
        shell_write_str(kSetThemePrefix);
        shell_write_str(g_shell_theme_ansi != 0U ? kSetThemeAnsi : kSetThemePlain);
        shell_timeline_record("set-theme", g_shell_theme_ansi != 0U ? "ansi" : "plain");
        return 1;
    }

    if (shell_parse_on_off(args + val_start,
                           args_len - val_start,
                           &value,
                           &present) < 0 ||
        present == 0U) {
        shell_write_str(kSetUsage);
        return 1;
    }
    g_shell_hud_enabled = value;
    shell_write_str(kSetHudPrefix);
    shell_write_str(g_shell_hud_enabled != 0U ? kSetHudOn : kSetHudOff);
    shell_timeline_record("set-hud", g_shell_hud_enabled != 0U ? "on" : "off");
    return 1;
}

static int shell_parse_single_arg_token(const char *args,
                                        uint32_t args_len,
                                        char *out,
                                        uint32_t out_cap) {
    char tok[SHELL_LINE_MAX];
    uint32_t idx = 0U;
    uint32_t tok_len = 0U;
    enum shell_token_kind kind;

    if (!out || out_cap == 0U) {
        return -1;
    }
    out[0] = '\0';
    if (!args || args_len == 0U) {
        return -1;
    }
    kind = shell_next_token(args, &idx, tok, sizeof(tok), &tok_len);
    if (kind != SHELL_TOK_WORD || tok_len == 0U || tok_len + 1U > out_cap) {
        return -1;
    }
    if (shell_next_token(args, &idx, tok, sizeof(tok), &tok_len) != SHELL_TOK_NONE) {
        return -1;
    }
    user_memcpy(out, tok, tok_len);
    out[tok_len] = '\0';
    return 0;
}

static int shell_trim_line(char *line, uint32_t *out_len) {
    uint32_t start;
    uint32_t len;
    uint32_t end;

    if (!line || !out_len) {
        return -1;
    }
    len = user_strlen(line);
    start = shell_skip_spaces(line, 0U);
    end = len;
    while (end > start && user_is_space(line[end - 1U])) {
        end--;
    }
    if (start != 0U && end > start) {
        user_memcpy(line, line + start, end - start);
    }
    if (end <= start) {
        line[0] = '\0';
        *out_len = 0U;
        return 0;
    }
    line[end - start] = '\0';
    *out_len = end - start;
    return 0;
}

static int shell_run_script_path(const char *path, uint32_t path_len, int interactive, int *out_status) {
    char chunk[SHELL_SCRIPT_CHUNK_MAX];
    char line[SHELL_LINE_MAX];
    uint32_t line_len = 0U;
    uint32_t line_overflow = 0U;
    int32_t fd;
    int last_status = 0;
    int keep_running = 1;

    if (out_status) {
        *out_status = 1;
    }
    if (!path || path_len == 0U) {
        if (interactive != 0) {
            shell_write_str(kSourceUsage);
        }
        return 1;
    }
    if (g_shell_script_depth >= SHELL_SCRIPT_DEPTH_MAX) {
        if (interactive != 0) {
            shell_write_str(kSourceDepthExceeded);
        }
        return 1;
    }
    fd = user_open(path, path_len, SYSCALL_OPEN_FLAG_READ);
    if (fd < 0) {
        if (interactive != 0) {
            shell_write_str(kSourceFail);
        }
        return 1;
    }

    g_shell_script_depth++;
    for (;;) {
        int32_t rc = user_read((uint32_t)fd, chunk, (uint32_t)sizeof(chunk));
        if (rc <= 0) {
            break;
        }

        for (uint32_t i = 0U; i < (uint32_t)rc; i++) {
            char ch = chunk[i];
            int line_status = 0;

            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                if (line_overflow != 0U) {
                    if (interactive != 0) {
                        shell_write_str(kSourceParseFail);
                    }
                    last_status = 1;
                    keep_running = 0;
                    line_len = 0U;
                    line_overflow = 0U;
                    break;
                }
                line[line_len] = '\0';
                line_len = 0U;
                if (shell_trim_line(line, &line_len) < 0) {
                    last_status = 1;
                    keep_running = 0;
                    break;
                }
                if (line_len == 0U || line[0] == '#') {
                    continue;
                }
                keep_running = shell_dispatch_line(line, &line_status);
                last_status = line_status;
                if (!keep_running) {
                    break;
                }
                continue;
            }
            if (line_overflow != 0U) {
                continue;
            }
            if (line_len + 1U < sizeof(line)) {
                line[line_len++] = ch;
            } else {
                line_overflow = 1U;
            }
        }
        if (!keep_running) {
            break;
        }
    }

    if (keep_running && line_len != 0U) {
        int line_status = 0;

        if (line_overflow != 0U) {
            if (interactive != 0) {
                shell_write_str(kSourceParseFail);
            }
            last_status = 1;
            keep_running = 0;
        } else {
            line[line_len] = '\0';
            line_len = 0U;
            if (shell_trim_line(line, &line_len) == 0 &&
                line_len != 0U &&
                line[0] != '#') {
                keep_running = shell_dispatch_line(line, &line_status);
                last_status = line_status;
            }
        }
    }

    g_shell_script_depth--;
    (void)user_close((uint32_t)fd);
    if (out_status) {
        *out_status = last_status;
    }
    return keep_running;
}

static int shell_run_source(const char *args, uint32_t args_len, int interactive) {
    char path[SHELL_PATH_MAX];
    int status = 1;

    if (!args) {
        if (interactive != 0) {
            shell_write_str(kSourceUsage);
        }
        return 1;
    }
    args_len = user_strlen(args);
    if (args_len == 0U) {
        const char *latest = 0;
        uint32_t latest_len = 0U;
        const char *fallbacks[2] = { g_shell_line, 0 };
        uint32_t fallback_lens[2] = { user_strlen(g_shell_line), 0U };

        if (shell_history_get(0U, &latest, &latest_len) == 0 && latest && latest_len != 0U) {
            fallbacks[1] = latest;
            fallback_lens[1] = latest_len;
        }
        for (uint32_t f = 0U; f < 2U; f++) {
            const char *full = fallbacks[f];
            uint32_t full_len = fallback_lens[f];
            uint32_t start;
            uint32_t cmd_end;
            uint32_t end;

            if (!full || full_len == 0U) {
                continue;
            }
            start = shell_skip_spaces(full, 0U);
            cmd_end = shell_token_end(full, start);
            if (cmd_end <= start ||
                (!user_str_eq_n(full + start, "source", cmd_end - start) &&
                 !user_str_eq_n(full + start, "sh", cmd_end - start))) {
                continue;
            }
            start = shell_skip_spaces(full, cmd_end);
            if (start >= full_len || full[start] == '\0') {
                continue;
            }
            end = full_len;
            while (end > start && user_is_space(full[end - 1U])) {
                end--;
            }
            if (end <= start || end - start + 1U > sizeof(path)) {
                continue;
            }
            user_memcpy(path, full + start, end - start);
            path[end - start] = '\0';
            return shell_run_script_path(path, user_strlen(path), interactive, &status);
        }
    }
    if (shell_parse_single_arg_token(args, args_len, path, sizeof(path)) < 0) {
        uint32_t start;
        uint32_t end;
        uint32_t len;

        start = shell_skip_spaces(args, 0U);
        if (start >= args_len || args[start] == '\0') {
            if (interactive != 0) {
                shell_write_str(kSourceUsage);
            }
            return 1;
        }
        end = args_len;
        while (end > start && user_is_space(args[end - 1U])) {
            end--;
        }
        len = end - start;
        if (len == 0U || len + 1U > sizeof(path)) {
            if (interactive != 0) {
                shell_write_str(kSourceUsage);
            }
            return 1;
        }
        user_memcpy(path, args + start, len);
        path[len] = '\0';
    }
    return shell_run_script_path(path, user_strlen(path), interactive, &status);
}

static int shell_run_export(const char *args, uint32_t args_len) {
    char tok[SHELL_LINE_MAX];
    uint32_t idx = 0U;
    uint32_t tok_len = 0U;
    enum shell_token_kind kind;
    uint32_t exported = 0U;

    (void)args_len;
    if (!args) {
        args = "";
    }
    kind = shell_next_token(args, &idx, tok, sizeof(tok), &tok_len);
    if (kind == SHELL_TOK_NONE) {
        for (uint32_t i = 0U; i < SHELL_VAR_MAX; i++) {
            if (g_shell_vars[i].used == 0U || g_shell_vars[i].exported == 0U) {
                continue;
            }
            shell_write_str(kExportPrefix);
            shell_write_str(g_shell_vars[i].name);
            shell_write_char('=');
            shell_write_str(g_shell_vars[i].value);
            shell_write_str(kNewline);
            exported++;
        }
        if (exported == 0U) {
            shell_write_str(kExportEmpty);
        }
        return 1;
    }
    while (kind == SHELL_TOK_WORD) {
        uint32_t eq = 0U;

        if (shell_parse_assignment_token(tok, tok_len, &eq) == 0) {
            if (shell_apply_assignment_token(tok, tok_len, 1U) < 0) {
                shell_write_str(kExportUsage);
                return 1;
            }
        } else {
            if (!shell_is_name_start_char(tok[0])) {
                shell_write_str(kExportUsage);
                return 1;
            }
            for (uint32_t i = 1U; i < tok_len; i++) {
                if (!shell_is_name_char(tok[i])) {
                    shell_write_str(kExportUsage);
                    return 1;
                }
            }
            if (shell_var_mark_exported(tok, tok_len) < 0) {
                shell_write_str(kExportUsage);
                return 1;
            }
        }
        kind = shell_next_token(args, &idx, tok, sizeof(tok), &tok_len);
    }
    if (kind != SHELL_TOK_NONE) {
        shell_write_str(kExportUsage);
    }
    return 1;
}

static int shell_bg_register_job(const int32_t pids[SHELL_PIPELINE_MAX],
                                 uint32_t count,
                                 const char *cmd_preview) {
    struct shell_bg_job *job = 0;

    if (!pids || count == 0U || count > SHELL_PIPELINE_MAX) {
        return -1;
    }
    for (uint32_t i = 0U; i < SHELL_BG_JOB_MAX; i++) {
        if (g_shell_bg_jobs[i].used == 0U) {
            job = &g_shell_bg_jobs[i];
            break;
        }
    }
    if (!job) {
        return -1;
    }

    job->used = 1U;
    job->id = g_shell_bg_next_id++;
    if (g_shell_bg_next_id == 0U) {
        g_shell_bg_next_id = 1U;
    }
    job->remaining = count;
    job->last_pid = pids[count - 1U];
    job->last_status = 0;
    for (uint32_t i = 0U; i < SHELL_PIPELINE_MAX; i++) {
        job->pids[i] = (i < count) ? pids[i] : 0;
    }
    shell_bg_copy_preview(job->cmd_preview, sizeof(job->cmd_preview), cmd_preview);
    shell_bg_print_started(job);
    return 0;
}

static void shell_bg_note_reaped(int32_t pid, int32_t status) {
    for (uint32_t i = 0U; i < SHELL_BG_JOB_MAX; i++) {
        struct shell_bg_job *job = &g_shell_bg_jobs[i];

        if (job->used == 0U) {
            continue;
        }
        for (uint32_t j = 0U; j < SHELL_PIPELINE_MAX; j++) {
            if (job->pids[j] != pid) {
                continue;
            }
            job->pids[j] = 0;
            if (job->remaining > 0U) {
                job->remaining--;
            }
            if (pid == job->last_pid) {
                job->last_status = status;
            }
            if (job->remaining == 0U) {
                shell_bg_print_done(job);
                job->used = 0U;
            }
            return;
        }
    }
}

static void shell_reap_background_jobs_nonblocking(void) {
    for (;;) {
        int32_t status = 0;
        int32_t waited = user_waitpid(-1, &status, SYSCALL_WAITPID_FLAG_NOHANG);

        if (waited <= 0) {
            return;
        }
        shell_bg_note_reaped(waited, status);
    }
}

static void shell_drain_background_jobs(void) {
    while (shell_bg_has_active_jobs()) {
        int32_t status = 0;
        int32_t waited = user_waitpid(-1, &status, 0U);

        if (waited <= 0) {
            break;
        }
        shell_bg_note_reaped(waited, status);
    }
}

static void shell_wait_background_jobs(void) {
    if (!shell_bg_has_active_jobs()) {
        shell_write_str(kWaitStub);
        shell_timeline_record("wait-empty", "");
        return;
    }
    shell_drain_background_jobs();
    shell_write_str(kWaitDone);
    shell_timeline_record("wait-done", "");
}

static int shell_execute_pipeline(struct shell_stage *stages,
                                  uint32_t stage_count,
                                  uint32_t background,
                                  const char *cmd_preview) {
    int32_t pipe_fds[SHELL_PIPELINE_MAX - 1U][2];
    int32_t child_pids[SHELL_PIPELINE_MAX];
    uint32_t spawned = 0U;
    int32_t last_status = 0;
    int launch_failed = 0;

    for (uint32_t i = 0U; i < SHELL_PIPELINE_MAX - 1U; i++) {
        pipe_fds[i][0] = -1;
        pipe_fds[i][1] = -1;
    }
    for (uint32_t i = 0U; i < SHELL_PIPELINE_MAX; i++) {
        child_pids[i] = -1;
    }

    if (background != 0U && shell_bg_has_free_slot() == 0) {
        shell_bg_print_overflow(cmd_preview);
        return -1;
    }

    if (stage_count > 1U) {
        for (uint32_t i = 0U; i + 1U < stage_count; i++) {
            if (user_pipe(pipe_fds[i]) < 0) {
                shell_write_str(kSpawnFail);
                launch_failed = 1;
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
                                 stages[i].spawn_cmdline_len != 0U ? stages[i].spawn_cmdline : 0,
                                 stages[i].spawn_cmdline_len,
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
            launch_failed = 1;
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

    if (!launch_failed && background != 0U) {
        if (shell_bg_register_job(child_pids, spawned, cmd_preview) == 0) {
            return 0;
        }
        shell_bg_print_overflow(cmd_preview);
        shell_write_str(kBgTrackFail);
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
    if (launch_failed) {
        return -1;
    }
    return (int)last_status;
}

static int shell_history_handle_command(const char *args, uint32_t args_len) {
    uint32_t arg_start;
    uint32_t arg_end;

    if (!args) {
        shell_run_history();
        return 1;
    }
    arg_start = shell_skip_spaces(args, 0U);
    if (args[arg_start] == '\0') {
        shell_run_history();
        return 1;
    }

    arg_end = shell_token_end(args, arg_start);
    if (user_str_eq_n(args + arg_start, "clear", arg_end - arg_start)) {
        if (shell_skip_spaces(args, arg_end) != args_len) {
            shell_write_str(kHistoryUsage);
            return 1;
        }
        shell_history_clear();
        shell_write_str(kHistoryCleared);
        return 1;
    }
    if (user_str_eq_n(args + arg_start, "run", arg_end - arg_start)) {
        uint32_t idx_start = shell_skip_spaces(args, arg_end);
        uint32_t idx_end;
        uint32_t display_index = 0U;
        const char *run_line = 0;
        uint32_t run_len = 0U;
        char run_buf[SHELL_LINE_MAX];
        char index_buf[12];
        int rc;

        if (idx_start >= args_len || args[idx_start] == '\0') {
            shell_write_str(kHistoryUsage);
            return 1;
        }
        idx_end = shell_token_end(args, idx_start);
        if (shell_skip_spaces(args, idx_end) != args_len) {
            shell_write_str(kHistoryUsage);
            return 1;
        }

        for (uint32_t i = idx_start; i < idx_end; i++) {
            uint32_t digit;
            if (args[i] < '0' || args[i] > '9') {
                shell_write_str(kHistoryUsage);
                return 1;
            }
            digit = (uint32_t)(args[i] - '0');
            if (display_index > 429496729U || (display_index == 429496729U && digit > 5U)) {
                shell_write_str(kHistoryEventNotFound);
                return 1;
            }
            display_index = display_index * 10U + digit;
        }
        if (shell_history_get_by_index(display_index, &run_line, &run_len) < 0 || !run_line) {
            shell_write_str(kHistoryEventNotFound);
            return 1;
        }
        if (run_len + 1U > sizeof(run_buf)) {
            shell_write_str(kHistoryEventNotFound);
            return 1;
        }
        if (g_shell_history_run_depth >= 4U) {
            shell_write_str(kHistoryRunDepthExceeded);
            return 1;
        }

        user_memcpy(run_buf, run_line, run_len);
        run_buf[run_len] = '\0';
        shell_format_u32(index_buf, sizeof(index_buf), display_index);
        shell_write_str(kHistoryRunPrefix);
        shell_write_str(index_buf);
        shell_write_str(kHistoryEventArrow);
        shell_write_str(run_buf);
        shell_write_str(kNewline);

        shell_history_push(run_buf, run_len);
        g_shell_history_run_depth++;
        rc = shell_dispatch_line(run_buf, &g_shell_last_status);
        g_shell_history_run_depth--;
        return rc;
    }

    shell_write_str(kHistoryUsage);
    return 1;
}

static int shell_try_expand_history_event(char *line, uint32_t cap) {
    uint32_t start;
    uint32_t end;
    uint32_t after;
    uint32_t offset = 0U;
    uint32_t token_len;
    uint32_t line_len;
    const char *match_line = 0;
    uint32_t match_len = 0U;
    char event_token[SHELL_LINE_MAX];

    if (!line || cap == 0U) {
        return 0;
    }
    start = shell_skip_spaces(line, 0U);
    if (line[start] != '!') {
        return 0;
    }
    end = shell_token_end(line, start);
    after = shell_skip_spaces(line, end);
    line_len = user_strlen(line);
    if (after != line_len) {
        return 0;
    }

    token_len = end - start;
    if (token_len < 2U) {
        return 0;
    }
    if (line[start + 1U] == '!') {
        if (token_len != 2U || g_shell_history_count == 0U) {
            shell_write_str(kHistoryEventNotFound);
            return -1;
        }
        offset = 0U;
    } else if (line[start + 1U] == '-') {
        uint32_t value = 0U;
        uint32_t idx = start + 2U;

        if (token_len < 3U) {
            shell_write_str(kHistoryEventNotFound);
            return -1;
        }
        while (idx < end) {
            uint32_t digit;
            if (line[idx] < '0' || line[idx] > '9') {
                shell_write_str(kHistoryEventNotFound);
                return -1;
            }
            digit = (uint32_t)(line[idx] - '0');
            if (value > 429496729U || (value == 429496729U && digit > 5U)) {
                shell_write_str(kHistoryEventNotFound);
                return -1;
            }
            value = value * 10U + digit;
            idx++;
        }
        if (value == 0U || value > g_shell_history_count) {
            shell_write_str(kHistoryEventNotFound);
            return -1;
        }
        offset = value - 1U;
    } else if (line[start + 1U] == '?') {
        const char *query = line + start + 2U;
        uint32_t query_len = token_len - 2U;
        uint32_t match_offset = 0U;

        if (query_len == 0U) {
            shell_write_str(kHistoryEventNotFound);
            return -1;
        }
        if (shell_history_search(query,
                                 query_len,
                                 0U,
                                 &match_line,
                                 &match_len,
                                 &match_offset) < 0) {
            shell_write_str(kHistoryEventNotFound);
            return -1;
        }
    } else {
        uint32_t idx = start + 1U;
        int all_digits = 1;

        while (idx < end) {
            if (line[idx] < '0' || line[idx] > '9') {
                all_digits = 0;
                break;
            }
            idx++;
        }

        if (all_digits != 0) {
            uint32_t value = 0U;

            idx = start + 1U;
            while (idx < end) {
                uint32_t digit = (uint32_t)(line[idx] - '0');

                if (value > 429496729U || (value == 429496729U && digit > 5U)) {
                    shell_write_str(kHistoryEventNotFound);
                    return -1;
                }
                value = value * 10U + digit;
                idx++;
            }
            if (value == 0U || value > g_shell_history_count) {
                shell_write_str(kHistoryEventNotFound);
                return -1;
            }
            offset = g_shell_history_count - value;
        } else {
            const char *prefix = line + start + 1U;
            uint32_t prefix_len = token_len - 1U;

            if (prefix_len == 0U ||
                shell_history_find_prefix(prefix, prefix_len, &match_line, &match_len) < 0) {
                shell_write_str(kHistoryEventNotFound);
                return -1;
            }
        }
    }

    if (!match_line && (shell_history_get(offset, &match_line, &match_len) < 0 || !match_line)) {
        shell_write_str(kHistoryEventNotFound);
        return -1;
    }
    if (match_len + 1U > cap) {
        shell_write_str(kHistoryEventNotFound);
        return -1;
    }
    if (token_len + 1U > sizeof(event_token)) {
        shell_write_str(kHistoryEventNotFound);
        return -1;
    }

    user_memcpy(event_token, line + start, token_len);
    event_token[token_len] = '\0';

    user_memcpy(line, match_line, match_len);
    line[match_len] = '\0';
    shell_write_str(kHistoryEventPrefix);
    shell_write_str(event_token);
    shell_write_str(kHistoryEventArrow);
    shell_write_str(match_line);
    shell_write_str(kNewline);
    return 1;
}

static int shell_try_expand_history_subst(char *line, uint32_t cap) {
    uint32_t start;
    uint32_t end;
    uint32_t after;
    uint32_t line_len;
    uint32_t token_len;
    uint32_t old_start;
    uint32_t old_len;
    uint32_t new_start;
    uint32_t new_len;
    uint32_t split = 0U;
    const char *latest_line = 0;
    uint32_t latest_len = 0U;
    uint32_t match_idx = 0U;
    uint32_t out_len = 0U;
    char expanded[SHELL_LINE_MAX];
    char token_buf[SHELL_LINE_MAX];

    if (!line || cap == 0U) {
        return 0;
    }
    start = shell_skip_spaces(line, 0U);
    if (line[start] != '^') {
        return 0;
    }
    end = shell_token_end(line, start);
    after = shell_skip_spaces(line, end);
    line_len = user_strlen(line);
    if (after != line_len) {
        return 0;
    }

    token_len = end - start;
    if (token_len < 3U || token_len + 1U > sizeof(token_buf)) {
        return 0;
    }

    for (uint32_t i = start + 1U; i < end; i++) {
        if (line[i] == '^') {
            split = i;
            break;
        }
    }
    if (split == 0U) {
        return 0;
    }

    old_start = start + 1U;
    old_len = split - old_start;
    if (old_len == 0U) {
        shell_write_str(kHistorySubstFailed);
        return -1;
    }
    new_start = split + 1U;
    if (new_start > end) {
        shell_write_str(kHistorySubstFailed);
        return -1;
    }
    new_len = end - new_start;
    if (new_len > 0U && line[end - 1U] == '^') {
        new_len--;
    }

    if (g_shell_history_count == 0U ||
        shell_history_get(0U, &latest_line, &latest_len) < 0 ||
        !latest_line) {
        shell_write_str(kHistorySubstFailed);
        return -1;
    }
    if (shell_find_substring(latest_line, latest_len, line + old_start, old_len, &match_idx) < 0) {
        shell_write_str(kHistorySubstFailed);
        return -1;
    }

    out_len = match_idx + new_len + (latest_len - (match_idx + old_len));
    if (out_len + 1U > sizeof(expanded) || out_len + 1U > cap) {
        shell_write_str(kHistorySubstFailed);
        return -1;
    }
    if (match_idx != 0U) {
        user_memcpy(expanded, latest_line, match_idx);
    }
    if (new_len != 0U) {
        user_memcpy(expanded + match_idx, line + new_start, new_len);
    }
    if (latest_len > match_idx + old_len) {
        user_memcpy(expanded + match_idx + new_len,
                    latest_line + match_idx + old_len,
                    latest_len - (match_idx + old_len));
    }
    expanded[out_len] = '\0';

    user_memcpy(token_buf, line + start, token_len);
    token_buf[token_len] = '\0';
    user_memcpy(line, expanded, out_len);
    line[out_len] = '\0';

    shell_write_str(kHistoryEventPrefix);
    shell_write_str(token_buf);
    shell_write_str(kHistoryEventArrow);
    shell_write_str(line);
    shell_write_str(kNewline);
    return 1;
}

static int shell_dispatch_builtin(struct shell_stage *stage) {
    if (user_str_eq_n(stage->cmd, "help", stage->cmd_len)) {
        shell_write_str(kHelp);
        return 1;
    }
    if (user_str_eq_n(stage->cmd, "history", stage->cmd_len)) {
        return shell_history_handle_command(stage->cmdline, stage->cmdline_len);
    }
    if (user_str_eq_n(stage->cmd, "jobs", stage->cmd_len)) {
        return shell_run_jobs();
    }
    if (user_str_eq_n(stage->cmd, "fg", stage->cmd_len)) {
        return shell_run_fg(stage->cmdline, stage->cmdline_len);
    }
    if (user_str_eq_n(stage->cmd, "timeline", stage->cmd_len)) {
        return shell_run_timeline(stage->cmdline, stage->cmdline_len);
    }
    if (user_str_eq_n(stage->cmd, "replay", stage->cmd_len)) {
        return shell_run_replay(stage->cmdline, stage->cmdline_len);
    }
    if (user_str_eq_n(stage->cmd, "hud", stage->cmd_len)) {
        return shell_run_hud(stage->cmdline, stage->cmdline_len);
    }
    if (user_str_eq_n(stage->cmd, "set", stage->cmd_len)) {
        return shell_run_set(stage->cmdline, stage->cmdline_len);
    }
    if (user_str_eq_n(stage->cmd, "sh", stage->cmd_len)) {
        return shell_run_source(stage->cmdline, stage->cmdline_len, 1);
    }
    if (user_str_eq_n(stage->cmd, "source", stage->cmd_len)) {
        return shell_run_source(stage->cmdline, stage->cmdline_len, 1);
    }
    if (user_str_eq_n(stage->cmd, "export", stage->cmd_len)) {
        return shell_run_export(stage->cmdline, stage->cmdline_len);
    }
    if (user_str_eq_n(stage->cmd, "wait", stage->cmd_len)) {
        shell_wait_background_jobs();
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

static int shell_chain_next_segment(char *line,
                                    uint32_t *cursor_io,
                                    char **out_segment,
                                    enum shell_chain_op *out_next_op) {
    uint32_t cursor;
    uint32_t start;
    uint32_t i;
    uint32_t delim_len = 0U;
    enum shell_chain_op next_op = SHELL_CHAIN_NONE;
    int in_single = 0;
    int in_double = 0;

    if (!line || !cursor_io || !out_segment || !out_next_op) {
        return -1;
    }
    cursor = *cursor_io;
    start = shell_skip_spaces(line, cursor);
    if (line[start] == '\0') {
        *cursor_io = start;
        return 0;
    }

    i = start;
    while (line[i] != '\0') {
        char ch = line[i];

        if (!in_single && ch == '\\') {
            if (line[i + 1U] == '\0') {
                break;
            }
            i += 2U;
            continue;
        }
        if (!in_double && ch == '\'') {
            in_single = !in_single;
            i++;
            continue;
        }
        if (!in_single && ch == '"') {
            in_double = !in_double;
            i++;
            continue;
        }
        if (!in_single && !in_double) {
            if (ch == ';') {
                delim_len = 1U;
                next_op = SHELL_CHAIN_SEQ;
                break;
            }
            if (ch == '&' && line[i + 1U] == '&') {
                delim_len = 2U;
                next_op = SHELL_CHAIN_AND;
                break;
            }
            if (ch == '|' && line[i + 1U] == '|') {
                delim_len = 2U;
                next_op = SHELL_CHAIN_OR;
                break;
            }
        }
        i++;
    }

    {
        uint32_t end = i;
        while (end > start && user_is_space(line[end - 1U])) {
            end--;
        }
        if (end <= start) {
            return -1;
        }
        line[end] = '\0';
        *out_segment = line + start;
    }

    if (delim_len != 0U) {
        *cursor_io = i + delim_len;
        *out_next_op = next_op;
    } else {
        *cursor_io = i;
        *out_next_op = SHELL_CHAIN_NONE;
    }
    return 1;
}

static int shell_try_assignment_only_line(const char *line, int *out_handled, int *out_status) {
    char tok[SHELL_LINE_MAX];
    uint32_t idx = 0U;
    uint32_t tok_len = 0U;
    enum shell_token_kind kind;
    uint32_t handled = 0U;

    if (!line || !out_handled || !out_status) {
        return -1;
    }
    *out_handled = 0;
    *out_status = 0;

    kind = shell_next_token(line, &idx, tok, sizeof(tok), &tok_len);
    while (kind == SHELL_TOK_WORD) {
        if (shell_apply_assignment_token(tok, tok_len, 0U) < 0) {
            *out_handled = 0;
            *out_status = 0;
            return 0;
        }
        handled = 1U;
        kind = shell_next_token(line, &idx, tok, sizeof(tok), &tok_len);
    }
    if (kind != SHELL_TOK_NONE) {
        *out_handled = 0;
        *out_status = 0;
        return 0;
    }
    if (handled != 0U) {
        *out_handled = 1;
        *out_status = 0;
    }
    return 0;
}

static int shell_dispatch_single_line(char *line, int *out_status) {
    uint32_t raw_start = 0U;
    uint32_t raw_cmd_end = 0U;
    uint32_t stage_count = 0U;
    uint32_t background = 0U;
    int has_meta = 0;
    int rc;
    int exec_rc = 0;

    if (!line) {
        if (out_status) {
            *out_status = 1;
        }
        return 1;
    }
    raw_start = shell_skip_spaces(line, 0U);
    raw_cmd_end = shell_token_end(line, raw_start);
    if (raw_cmd_end > raw_start &&
        (user_str_eq_n(line + raw_start, "source", raw_cmd_end - raw_start) ||
         user_str_eq_n(line + raw_start, "sh", raw_cmd_end - raw_start))) {
        const char *builtin_tag = user_str_eq_n(line + raw_start,
                                                "source",
                                                raw_cmd_end - raw_start)
                                      ? "source"
                                      : "sh";
        uint32_t arg_start = shell_skip_spaces(line, raw_cmd_end);
        uint32_t arg_end = user_strlen(line);

        while (arg_end > arg_start && user_is_space(line[arg_end - 1U])) {
            arg_end--;
        }
        shell_timeline_record("builtin", builtin_tag);
        if (out_status) {
            *out_status = 0;
        }
        return shell_run_source(line + arg_start, arg_end - arg_start, 1);
    }

    rc = shell_parse_stages(line,
                            g_shell_stages,
                            SHELL_PIPELINE_MAX,
                            &stage_count,
                            &has_meta,
                            &background);
    if (rc < 0) {
        shell_write_str(kParseFail);
        shell_timeline_record("parse-fail", line);
        if (out_status) {
            *out_status = 1;
        }
        return 1;
    }
    if (stage_count == 0U) {
        if (out_status) {
            *out_status = 0;
        }
        return 1;
    }

    if (stage_count == 1U && !has_meta &&
        shell_is_builtin_name(g_shell_stages[0].cmd, g_shell_stages[0].cmd_len)) {
        shell_timeline_record("builtin", g_shell_stages[0].cmd);
        if (out_status) {
            *out_status = 0;
        }
        return shell_dispatch_builtin(&g_shell_stages[0]);
    }
    for (uint32_t i = 0U; i < stage_count; i++) {
        if (shell_is_builtin_name(g_shell_stages[i].cmd, g_shell_stages[i].cmd_len)) {
            shell_write_str(kBuiltinPipeRedirect);
            shell_timeline_record("builtin-meta", g_shell_stages[i].cmd);
            if (out_status) {
                *out_status = 1;
            }
            return 1;
        }
    }

    shell_timeline_record(background != 0U ? "run-bg" : "run-fg", line);
    exec_rc = shell_execute_pipeline(g_shell_stages, stage_count, background, line);
    if (out_status) {
        *out_status = exec_rc == 0 ? 0 : 1;
    }
    return 1;
}

static int shell_dispatch_line(char *line, int *out_status) {
    uint32_t cursor = 0U;
    enum shell_chain_op gate = SHELL_CHAIN_SEQ;
    int need_segment = 0;
    int last_status = 0;

    if (!line) {
        if (out_status) {
            *out_status = 1;
        }
        return 1;
    }

    for (;;) {
        char *segment = 0;
        enum shell_chain_op next_op = SHELL_CHAIN_NONE;
        int seg_rc;

        seg_rc = shell_chain_next_segment(line, &cursor, &segment, &next_op);
        if (seg_rc < 0) {
            shell_write_str(kParseFail);
            shell_timeline_record("parse-fail", line);
            if (out_status) {
                *out_status = 1;
            }
            return 1;
        }
        if (seg_rc == 0) {
            if (need_segment != 0) {
                shell_write_str(kParseFail);
                shell_timeline_record("parse-fail", line);
                if (out_status) {
                    *out_status = 1;
                }
            } else if (out_status) {
                *out_status = last_status;
            }
            return 1;
        }

        need_segment = (next_op != SHELL_CHAIN_NONE);
        if ((gate == SHELL_CHAIN_AND && last_status != 0) ||
            (gate == SHELL_CHAIN_OR && last_status == 0)) {
            gate = next_op;
            if (next_op == SHELL_CHAIN_NONE && out_status) {
                *out_status = last_status;
            }
            continue;
        }

        {
            char expanded[SHELL_LINE_MAX];
            int handled_assign = 0;

            if (shell_expand_vars(segment, expanded, sizeof(expanded)) < 0) {
                shell_write_str(kParseFail);
                shell_timeline_record("expand-fail", segment);
                if (out_status) {
                    *out_status = 1;
                }
                return 1;
            }
            (void)shell_try_assignment_only_line(expanded, &handled_assign, &last_status);
            if (handled_assign == 0) {
                int keep_running = shell_dispatch_single_line(expanded, &last_status);
                if (!keep_running) {
                    if (out_status) {
                        *out_status = last_status;
                    }
                    return 0;
                }
            }
        }
        gate = next_op;
        if (next_op == SHELL_CHAIN_NONE) {
            if (out_status) {
                *out_status = last_status;
            }
            return 1;
        }
    }
}

static int shell_try_run_boot_rc_script(void) {
    static const char kRcPath[] = "/persist/rc.sh";
    int32_t fd;
    int status = 0;
    int keep_running = 1;

    fd = user_open(kRcPath, (uint32_t)(sizeof(kRcPath) - 1U), SYSCALL_OPEN_FLAG_READ);
    if (fd < 0) {
        return 1;
    }
    (void)user_close((uint32_t)fd);

    shell_write_str(kRcRun);
    shell_timeline_record("rc", kRcPath);
    keep_running = shell_run_script_path(kRcPath, (uint32_t)(sizeof(kRcPath) - 1U), 0, &status);
    g_shell_last_status = status;
    return keep_running;
}

void _start(void) {
    char boot_cmdline[SHELL_LINE_MAX];
    int32_t boot_cmdline_len;

    g_shell_ansi_supported = 0U;
    g_shell_last_status = 0;
    (void)shell_sync_cwd();
    shell_history_load_persisted();

    boot_cmdline_len = user_getcmdline(boot_cmdline, sizeof(boot_cmdline));
    if (boot_cmdline_len > 0) {
        char tok[SHELL_LINE_MAX];
        uint32_t idx = 0U;
        uint32_t tok_len = 0U;
        enum shell_token_kind kind;
        int status = 0;

        if ((uint32_t)boot_cmdline_len >= sizeof(boot_cmdline)) {
            boot_cmdline_len = (int32_t)sizeof(boot_cmdline) - 1;
        }
        boot_cmdline[(uint32_t)boot_cmdline_len] = '\0';
        kind = shell_next_token(boot_cmdline, &idx, tok, sizeof(tok), &tok_len);
        if (kind == SHELL_TOK_WORD && tok_len != 0U) {
            (void)shell_run_script_path(tok, tok_len, 0, &status);
            shell_drain_background_jobs();
            user_exit(status == 0 ? 0 : 1);
        }
    }

    shell_write_str(kBanner);
    if (!shell_try_run_boot_rc_script()) {
        shell_drain_background_jobs();
        user_exit(g_shell_last_status == 0 ? 0 : 1);
    }
    for (;;) {
        int32_t rc;
        uint32_t cmd_start = 0U;
        uint32_t cmd_end = 0U;
        uint32_t cmd_ticks = 0U;

        shell_reap_background_jobs_nonblocking();
        if (g_shell_hud_enabled != 0U) {
            shell_hud_print_line();
        }
        shell_write_str(shell_build_prompt());
        rc = shell_read_line(g_shell_line, sizeof(g_shell_line));
        if (rc < 0) {
            shell_drain_background_jobs();
            shell_write_str(kExit);
            user_exit(1);
        }
        if (shell_try_expand_history_subst(g_shell_line, sizeof(g_shell_line)) < 0) {
            continue;
        }
        if (shell_try_expand_history_event(g_shell_line, sizeof(g_shell_line)) < 0) {
            continue;
        }
        if (g_shell_line[0] != '\0') {
            shell_history_push(g_shell_line, user_strlen(g_shell_line));
            cmd_start = shell_now_ticks_lo();
        }
        if (!shell_dispatch_line(g_shell_line, &g_shell_last_status)) {
            shell_drain_background_jobs();
            user_exit(g_shell_last_status == 0 ? 0 : 1);
        }
        if (g_shell_line[0] != '\0') {
            cmd_end = shell_now_ticks_lo();
            if (cmd_end >= cmd_start) {
                cmd_ticks = cmd_end - cmd_start;
            } else {
                cmd_ticks = (0xFFFFFFFFU - cmd_start) + cmd_end + 1U;
            }
            shell_update_cmd_health(cmd_ticks);
        }
        shell_reap_background_jobs_nonblocking();
    }
}
