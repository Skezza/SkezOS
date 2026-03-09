#include <stdint.h>

#include "userlib.h"

#define RR_BUF_SMALL 64U
#define RR_BUF_MEDIUM 256U
#define RR_MAX_SCENARIOS 3U

struct rr_args {
    uint32_t seed;
    uint32_t fuzz_lite;
    char script[16];
    char replay[24];
};

struct rr_ctx {
    uint32_t seed;
    uint32_t seq;
    uint32_t events;
    uint32_t failures;
    uint32_t trace_hash;
};

struct rr_case {
    const char *name;
    int32_t (*run)(struct rr_ctx *ctx);
};

static void rr_write_all(const char *buf, uint32_t len) {
    while (len > 0U) {
        int32_t rc = user_write(USER_FD_STDOUT, buf, len);
        if (rc <= 0) {
            return;
        }
        buf += (uint32_t)rc;
        len -= (uint32_t)rc;
    }
}

static void rr_write_str(const char *s) {
    rr_write_all(s, user_strlen(s));
}

static uint32_t rr_u32_to_dec(char *buf, uint32_t cap, uint32_t value) {
    char digits[10];
    uint32_t n = 0U;
    uint32_t out = 0U;

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
    while (value != 0U && n < sizeof(digits)) {
        digits[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (n > 0U && out + 1U < cap) {
        buf[out++] = digits[--n];
    }
    buf[out] = '\0';
    return out;
}

static uint32_t rr_i32_to_dec(char *buf, uint32_t cap, int32_t value) {
    if (!buf || cap == 0U) {
        return 0U;
    }
    if (value < 0) {
        if (cap < 2U) {
            buf[0] = '\0';
            return 0U;
        }
        buf[0] = '-';
        return 1U + rr_u32_to_dec(buf + 1, cap - 1U, (uint32_t)(-(value + 1)) + 1U);
    }
    return rr_u32_to_dec(buf, cap, (uint32_t)value);
}

static int rr_starts_with(const char *text, const char *prefix) {
    uint32_t i = 0U;

    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int rr_eq(const char *lhs, const char *rhs) {
    uint32_t i = 0U;

    if (!lhs || !rhs) {
        return 0;
    }
    while (lhs[i] != '\0' || rhs[i] != '\0') {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static uint32_t rr_parse_u32(const char *text) {
    uint32_t value = 0U;
    uint32_t i = 0U;

    if (!text) {
        return 0U;
    }
    while (text[i] >= '0' && text[i] <= '9') {
        value = (value * 10U) + (uint32_t)(text[i] - '0');
        i++;
    }
    return value;
}

static void rr_emit_json_case(const char *name, uint32_t ok, int32_t rc) {
    char num[16];

    rr_write_str("{\"type\":\"case\",\"name\":\"");
    rr_write_str(name);
    rr_write_str("\",\"ok\":");
    rr_write_str(ok ? "true" : "false");
    rr_write_str(",\"rc\":");
    rr_i32_to_dec(num, sizeof(num), rc);
    rr_write_str(num);
    rr_write_str("}\n");
}

static void rr_emit_json_meta(uint32_t seed, const char *script) {
    char num[16];

    rr_write_str("{\"type\":\"meta\",\"seed\":");
    rr_u32_to_dec(num, sizeof(num), seed);
    rr_write_str(num);
    rr_write_str(",\"script\":\"");
    rr_write_str(script);
    rr_write_str("\"}\n");
}

static void rr_emit_json_summary(uint32_t total, uint32_t failures) {
    char num[16];

    rr_write_str("{\"type\":\"summary\",\"total\":");
    rr_u32_to_dec(num, sizeof(num), total);
    rr_write_str(num);
    rr_write_str(",\"failures\":");
    rr_u32_to_dec(num, sizeof(num), failures);
    rr_write_str(num);
    rr_write_str(",\"ok\":");
    rr_write_str(failures == 0U ? "true" : "false");
    rr_write_str("}\n");
}

static void rr_emit_json_event(struct rr_ctx *ctx,
                               const char *scenario,
                               const char *action,
                               int32_t rc) {
    char num[16];

    if (!ctx) {
        return;
    }
    ctx->seq++;
    rr_write_str("{\"type\":\"event\",\"seq\":");
    rr_u32_to_dec(num, sizeof(num), ctx->seq);
    rr_write_str(num);
    rr_write_str(",\"scenario\":\"");
    rr_write_str(scenario);
    rr_write_str("\",\"action\":\"");
    rr_write_str(action);
    rr_write_str("\",\"rc\":");
    rr_i32_to_dec(num, sizeof(num), rc);
    rr_write_str(num);
    rr_write_str("}\n");
    ctx->trace_hash = (ctx->trace_hash * 16777619U) ^ (uint32_t)rc;
    ctx->trace_hash = (ctx->trace_hash * 16777619U) ^ (ctx->seq + 1U);
}

static void rr_emit_json_meta_ext(const struct rr_args *args) {
    char num[16];

    if (!args) {
        return;
    }
    rr_write_str("{\"type\":\"meta_ext\",\"replay\":\"");
    rr_write_str(args->replay);
    rr_write_str("\",\"fuzz_lite\":");
    rr_u32_to_dec(num, sizeof(num), args->fuzz_lite);
    rr_write_str(num);
    rr_write_str("}\n");
}

static void rr_emit_json_trace_summary(const struct rr_ctx *ctx) {
    char num[16];

    if (!ctx) {
        return;
    }
    rr_write_str("{\"type\":\"trace_summary\",\"seq\":");
    rr_u32_to_dec(num, sizeof(num), ctx->seq);
    rr_write_str(num);
    rr_write_str(",\"hash\":");
    rr_u32_to_dec(num, sizeof(num), ctx->trace_hash);
    rr_write_str(num);
    rr_write_str("}\n");
}

static int32_t rr_snapshot_user_task_count(void) {
    struct syscall_task_snapshot_entry entries[16];
    int32_t count = user_task_snapshot(entries, 16U);
    int32_t user_count = 0;

    if (count < 0) {
        return count;
    }
    for (uint32_t i = 0U; i < (uint32_t)count; i++) {
        if ((entries[i].flags & SYSCALL_TASK_FLAG_USER) != 0U) {
            user_count++;
        }
    }
    return user_count;
}

static int32_t rr_probe_lowest_fd(void) {
    int32_t fd = user_open("/bin/readme.txt", 15U, SYSCALL_OPEN_FLAG_READ);

    if (fd < 0) {
        return fd;
    }
    if (user_close((uint32_t)fd) < 0) {
        return -22;
    }
    return fd;
}

static int32_t rr_wait_expect_ok(int32_t pid) {
    int32_t status = -1;
    int32_t waited = user_waitpid(pid, &status, 0U);

    if (waited < 0) {
        return waited;
    }
    if (waited != pid || status != 0) {
        return -22;
    }
    return 0;
}

static int32_t rr_scenario_proc_redirect_reap(struct rr_ctx *ctx) {
    int32_t baseline_tasks;
    int32_t baseline_fd;
    int32_t pipe_fds[2] = { -1, -1 };
    int32_t stdout_dup = -1;
    int32_t pid;
    char out[RR_BUF_MEDIUM];
    int32_t nread;

    (void)ctx;

    baseline_tasks = rr_snapshot_user_task_count();
    rr_emit_json_event(ctx, "proc_redirect_reap", "snapshot_tasks_before", baseline_tasks);
    if (baseline_tasks < 0) {
        return baseline_tasks;
    }
    baseline_fd = rr_probe_lowest_fd();
    rr_emit_json_event(ctx, "proc_redirect_reap", "probe_fd_before", baseline_fd);
    if (baseline_fd < 0) {
        return baseline_fd;
    }
    if (user_pipe(pipe_fds) < 0) {
        rr_emit_json_event(ctx, "proc_redirect_reap", "pipe_create", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "proc_redirect_reap", "pipe_create", 0);

    stdout_dup = user_dup(USER_FD_STDOUT);
    if (stdout_dup < 0) {
        rr_emit_json_event(ctx, "proc_redirect_reap", "dup_stdout", stdout_dup);
        (void)user_close((uint32_t)pipe_fds[0]);
        (void)user_close((uint32_t)pipe_fds[1]);
        return stdout_dup;
    }
    rr_emit_json_event(ctx, "proc_redirect_reap", "dup_stdout", 0);
    if (user_dup2((uint32_t)pipe_fds[1], USER_FD_STDOUT) < 0) {
        (void)user_close((uint32_t)stdout_dup);
        (void)user_close((uint32_t)pipe_fds[0]);
        (void)user_close((uint32_t)pipe_fds[1]);
        return -22;
    }

    pid = user_spawn_ex("/bin/hello.elf", 14U, "", 0U);
    rr_emit_json_event(ctx, "proc_redirect_reap", "spawn_hello", pid > 0 ? 0 : pid);

    (void)user_dup2((uint32_t)stdout_dup, USER_FD_STDOUT);
    (void)user_close((uint32_t)stdout_dup);
    (void)user_close((uint32_t)pipe_fds[1]);

    if (pid <= 0) {
        (void)user_close((uint32_t)pipe_fds[0]);
        return pid < 0 ? pid : -22;
    }
    if (rr_wait_expect_ok(pid) < 0) {
        rr_emit_json_event(ctx, "proc_redirect_reap", "wait_hello", -22);
        (void)user_close((uint32_t)pipe_fds[0]);
        return -22;
    }
    rr_emit_json_event(ctx, "proc_redirect_reap", "wait_hello", 0);

    nread = user_read((uint32_t)pipe_fds[0], out, sizeof(out));
    rr_emit_json_event(ctx, "proc_redirect_reap", "read_redirect", nread);
    (void)user_close((uint32_t)pipe_fds[0]);
    if (nread <= 0) {
        return -22;
    }
    if (rr_snapshot_user_task_count() != baseline_tasks) {
        rr_emit_json_event(ctx, "proc_redirect_reap", "snapshot_tasks_after", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "proc_redirect_reap", "snapshot_tasks_after", baseline_tasks);
    if (rr_probe_lowest_fd() != baseline_fd) {
        rr_emit_json_event(ctx, "proc_redirect_reap", "probe_fd_after", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "proc_redirect_reap", "probe_fd_after", baseline_fd);
    return 0;
}

static int32_t rr_scenario_cwd_drift(struct rr_ctx *ctx) {
    char cwd[SYSCALL_CWD_MAX];
    int32_t baseline_fd;
    int32_t fd;
    int32_t pid;

    (void)ctx;

    baseline_fd = rr_probe_lowest_fd();
    rr_emit_json_event(ctx, "cwd_path_drift", "probe_fd_before", baseline_fd);
    if (baseline_fd < 0) {
        return baseline_fd;
    }

    if (user_chdir("/bin", 4U) < 0) {
        rr_emit_json_event(ctx, "cwd_path_drift", "chdir_bin", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "cwd_path_drift", "chdir_bin", 0);
    if (user_getcwd(cwd, sizeof(cwd)) < 0 || !rr_eq(cwd, "/bin")) {
        rr_emit_json_event(ctx, "cwd_path_drift", "getcwd_bin", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "cwd_path_drift", "getcwd_bin", 0);
    fd = user_open("readme.txt", 10U, SYSCALL_OPEN_FLAG_READ);
    if (fd < 0) {
        rr_emit_json_event(ctx, "cwd_path_drift", "open_relative_readme", fd);
        return fd;
    }
    rr_emit_json_event(ctx, "cwd_path_drift", "open_relative_readme", 0);
    if (user_close((uint32_t)fd) < 0) {
        return -22;
    }
    pid = user_spawn_ex("./pwd.elf", 9U, "", 0U);
    rr_emit_json_event(ctx, "cwd_path_drift", "spawn_pwd", pid > 0 ? 0 : pid);
    if (pid <= 0) {
        return pid < 0 ? pid : -22;
    }
    if (rr_wait_expect_ok(pid) < 0) {
        rr_emit_json_event(ctx, "cwd_path_drift", "wait_pwd", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "cwd_path_drift", "wait_pwd", 0);
    if (user_chdir("/", 1U) < 0) {
        rr_emit_json_event(ctx, "cwd_path_drift", "chdir_root", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "cwd_path_drift", "chdir_root", 0);
    if (user_getcwd(cwd, sizeof(cwd)) < 0 || !rr_eq(cwd, "/")) {
        rr_emit_json_event(ctx, "cwd_path_drift", "getcwd_root", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "cwd_path_drift", "getcwd_root", 0);
    if (rr_probe_lowest_fd() != baseline_fd) {
        rr_emit_json_event(ctx, "cwd_path_drift", "probe_fd_after", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "cwd_path_drift", "probe_fd_after", baseline_fd);
    return 0;
}

static int32_t rr_scenario_pipe_close_order_parent_child(struct rr_ctx *ctx) {
    int32_t baseline_tasks;
    int32_t baseline_fd;
    int32_t in_pipe[2] = { -1, -1 };
    int32_t out_pipe[2] = { -1, -1 };
    int32_t stdin_dup;
    int32_t stdout_dup;
    int32_t pid;
    static const char kPayload[] = "pipe-parent-child\n";
    char out[RR_BUF_MEDIUM];
    int32_t nread;

    (void)ctx;

    baseline_tasks = rr_snapshot_user_task_count();
    rr_emit_json_event(ctx, "pipe_close_order_parent_child", "snapshot_tasks_before", baseline_tasks);
    if (baseline_tasks < 0) {
        return baseline_tasks;
    }
    baseline_fd = rr_probe_lowest_fd();
    rr_emit_json_event(ctx, "pipe_close_order_parent_child", "probe_fd_before", baseline_fd);
    if (baseline_fd < 0) {
        return baseline_fd;
    }
    if (user_pipe(in_pipe) < 0 || user_pipe(out_pipe) < 0) {
        rr_emit_json_event(ctx, "pipe_close_order_parent_child", "pipe_create", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "pipe_close_order_parent_child", "pipe_create", 0);
    if (user_write((uint32_t)in_pipe[1], kPayload, (uint32_t)(sizeof(kPayload) - 1U)) !=
        (int32_t)(sizeof(kPayload) - 1U)) {
        rr_emit_json_event(ctx, "pipe_close_order_parent_child", "write_payload", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "pipe_close_order_parent_child", "write_payload", 0);
    (void)user_close((uint32_t)in_pipe[1]);
    in_pipe[1] = -1;

    stdin_dup = user_dup(USER_FD_STDIN);
    stdout_dup = user_dup(USER_FD_STDOUT);
    if (stdin_dup < 0 || stdout_dup < 0) {
        rr_emit_json_event(ctx, "pipe_close_order_parent_child", "dup_stdio", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "pipe_close_order_parent_child", "dup_stdio", 0);
    if (user_dup2((uint32_t)in_pipe[0], USER_FD_STDIN) < 0 ||
        user_dup2((uint32_t)out_pipe[1], USER_FD_STDOUT) < 0) {
        return -22;
    }

    pid = user_spawn_ex("/bin/cat.elf", 12U, "", 0U);
    rr_emit_json_event(ctx, "pipe_close_order_parent_child", "spawn_cat", pid > 0 ? 0 : pid);

    (void)user_dup2((uint32_t)stdin_dup, USER_FD_STDIN);
    (void)user_dup2((uint32_t)stdout_dup, USER_FD_STDOUT);
    (void)user_close((uint32_t)stdin_dup);
    (void)user_close((uint32_t)stdout_dup);
    (void)user_close((uint32_t)in_pipe[0]);
    (void)user_close((uint32_t)out_pipe[1]);

    if (pid <= 0) {
        return pid < 0 ? pid : -22;
    }
    if (rr_wait_expect_ok(pid) < 0) {
        rr_emit_json_event(ctx, "pipe_close_order_parent_child", "wait_cat", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "pipe_close_order_parent_child", "wait_cat", 0);
    nread = user_read((uint32_t)out_pipe[0], out, sizeof(out));
    rr_emit_json_event(ctx, "pipe_close_order_parent_child", "read_cat_output", nread);
    (void)user_close((uint32_t)out_pipe[0]);
    if (nread < (int32_t)(sizeof(kPayload) - 1U)) {
        return -22;
    }
    {
        uint32_t found = 0U;
        uint32_t need = (uint32_t)(sizeof(kPayload) - 1U);
        uint32_t have = (uint32_t)nread;

        for (uint32_t off = 0U; off + need <= have; off++) {
            uint32_t match = 1U;
            for (uint32_t i = 0U; i < need; i++) {
                if (out[off + i] != kPayload[i]) {
                    match = 0U;
                    break;
                }
            }
            if (match) {
                found = 1U;
                break;
            }
        }
        if (!found) {
            return -22;
        }
    }
    if (rr_snapshot_user_task_count() != baseline_tasks) {
        rr_emit_json_event(ctx, "pipe_close_order_parent_child", "snapshot_tasks_after", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "pipe_close_order_parent_child", "snapshot_tasks_after", baseline_tasks);
    if (rr_probe_lowest_fd() != baseline_fd) {
        rr_emit_json_event(ctx, "pipe_close_order_parent_child", "probe_fd_after", -22);
        return -22;
    }
    rr_emit_json_event(ctx, "pipe_close_order_parent_child", "probe_fd_after", baseline_fd);
    return 0;
}

static uint32_t rr_next_rand(uint32_t *state) {
    *state = (*state * 1664525U) + 1013904223U;
    return *state;
}

static void rr_shuffle(uint8_t *order, uint32_t count, uint32_t seed) {
    uint32_t state = seed == 0U ? 1U : seed;

    for (uint32_t i = 0U; i < count; i++) {
        order[i] = (uint8_t)i;
    }
    for (uint32_t i = count; i > 1U; i--) {
        uint32_t j = rr_next_rand(&state) % i;
        uint8_t tmp = order[i - 1U];
        order[i - 1U] = order[j];
        order[j] = tmp;
    }
}

static void rr_parse_args(int argc, char **argv, struct rr_args *out) {
    if (!out) {
        return;
    }
    out->seed = 1U;
    out->fuzz_lite = 0U;
    out->script[0] = 'a';
    out->script[1] = 'l';
    out->script[2] = 'l';
    out->script[3] = '\0';
    out->replay[0] = '-';
    out->replay[1] = '\0';

    for (int i = 1; i < argc; i++) {
        if (!argv[i]) {
            continue;
        }
        if (rr_starts_with(argv[i], "--seed=")) {
            out->seed = rr_parse_u32(argv[i] + 7);
            continue;
        }
        if (rr_starts_with(argv[i], "--script=")) {
            const char *src = argv[i] + 9;
            uint32_t n = 0U;
            while (src[n] != '\0' && n + 1U < sizeof(out->script)) {
                out->script[n] = src[n];
                n++;
            }
            out->script[n] = '\0';
            continue;
        }
        if (rr_starts_with(argv[i], "--replay=")) {
            const char *src = argv[i] + 9;
            uint32_t n = 0U;
            while (src[n] != '\0' && n + 1U < sizeof(out->replay)) {
                out->replay[n] = src[n];
                n++;
            }
            out->replay[n] = '\0';
            continue;
        }
        if (rr_starts_with(argv[i], "--fuzz-lite=")) {
            out->fuzz_lite = rr_parse_u32(argv[i] + 12);
            continue;
        }
    }
}

static void rr_apply_replay_profile(struct rr_args *args) {
    if (!args || rr_eq(args->replay, "-")) {
        return;
    }
    if (rr_eq(args->replay, "all_seed1337")) {
        args->seed = 1337U;
        args->script[0] = 'a';
        args->script[1] = 'l';
        args->script[2] = 'l';
        args->script[3] = '\0';
        return;
    }
    if (rr_eq(args->replay, "quick_seed1337")) {
        args->seed = 1337U;
        args->script[0] = 'q';
        args->script[1] = 'u';
        args->script[2] = 'i';
        args->script[3] = 'c';
        args->script[4] = 'k';
        args->script[5] = '\0';
        return;
    }
}

void _start(int argc, char **argv) {
    static const struct rr_case cases[RR_MAX_SCENARIOS] = {
        { "proc_redirect_reap", rr_scenario_proc_redirect_reap },
        { "cwd_path_drift", rr_scenario_cwd_drift },
        { "pipe_close_order_parent_child", rr_scenario_pipe_close_order_parent_child },
    };
    struct rr_args args;
    struct rr_ctx ctx;
    uint8_t order[RR_MAX_SCENARIOS];
    uint32_t limit = RR_MAX_SCENARIOS;

    rr_parse_args(argc, argv, &args);
    rr_apply_replay_profile(&args);
    if (args.fuzz_lite != 0U) {
        args.seed ^= (args.fuzz_lite * 2654435761U);
    }
    if (rr_eq(args.script, "quick")) {
        limit = 1U;
    }

    ctx.seed = args.seed;
    ctx.seq = 0U;
    ctx.events = 0U;
    ctx.failures = 0U;
    ctx.trace_hash = 2166136261U ^ args.seed;

    rr_write_str("rr: start\n");
    rr_emit_json_meta(args.seed, args.script);
    rr_emit_json_meta_ext(&args);
    rr_shuffle(order, RR_MAX_SCENARIOS, args.seed);

    for (uint32_t i = 0U; i < limit; i++) {
        const struct rr_case *c = &cases[order[i]];
        int32_t rc = c->run(&ctx);
        uint32_t ok = (rc == 0);

        ctx.events++;
        if (!ok) {
            ctx.failures++;
        }
        rr_emit_json_case(c->name, ok, rc);
    }

    rr_emit_json_summary(ctx.events, ctx.failures);
    rr_emit_json_trace_summary(&ctx);
    if (ctx.failures == 0U) {
        rr_write_str("rr: PASS\n");
        user_exit(0);
    }
    rr_write_str("rr: FAIL\n");
    user_exit(1);
}
