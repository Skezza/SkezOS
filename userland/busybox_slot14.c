#include <stdint.h>

#include "userlib.h"

#define BUSYBOX_READBUF_SIZE 64U
#define BUSYBOX_LS_MAX       16U

static const char kReadmePath[] = "/bin/readme.txt";
static const char kDotPath[] = ".";
static const char kApplets[] = "busybox: applets: cat echo ls pwd uptime\n";
static const char kUnknown[] = "busybox: unknown applet\n";
static const char kCatOpenFail[] = "busybox cat: open failed\n";
static const char kCatReadFail[] = "busybox cat: read failed\n";
static const char kLsFail[] = "busybox ls: list failed\n";
static const char kTimeFail[] = "busybox uptime: time query failed\n";
static const char kPwdFail[] = "busybox pwd: getcwd failed\n";

static char g_busybox_buf[BUSYBOX_READBUF_SIZE];
static char g_busybox_cwd[SYSCALL_CWD_MAX];
static struct syscall_dir_entry g_busybox_ls_entries[BUSYBOX_LS_MAX];

static void busybox_write_all(uint32_t fd, const char *buf, uint32_t len) {
    while (len > 0U) {
        int32_t rc = user_write(fd, buf, len);
        if (rc <= 0) {
            return;
        }
        buf += (uint32_t)rc;
        len -= (uint32_t)rc;
    }
}

static void busybox_write_str(uint32_t fd, const char *s) {
    busybox_write_all(fd, s, user_strlen(s));
}

static void busybox_write_u32(uint32_t fd, uint32_t value) {
    char buf[10];
    uint32_t len = 0U;

    if (value == 0U) {
        busybox_write_all(fd, "0", 1U);
        return;
    }

    while (value != 0U && len < sizeof(buf)) {
        buf[len++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (len > 0U) {
        busybox_write_all(fd, &buf[--len], 1U);
    }
}

static int busybox_str_eq(const char *lhs, const char *rhs) {
    uint32_t i = 0U;

    if (!lhs || !rhs) {
        return 0;
    }
    while (lhs[i] != '\0' && rhs[i] != '\0') {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
        i++;
    }
    return lhs[i] == rhs[i];
}

static const char *busybox_pick_path(const char *arg, const char *fallback, uint32_t *out_len) {
    if (!out_len) {
        return 0;
    }
    if (!arg || arg[0] == '\0') {
        *out_len = user_strlen(fallback);
        return fallback;
    }
    *out_len = user_strlen(arg);
    return arg;
}

static int busybox_run_echo(int argc, char **argv, int arg_start) {
    for (int i = arg_start; i < argc; i++) {
        if (i > arg_start) {
            busybox_write_all(USER_FD_STDOUT, " ", 1U);
        }
        if (argv[i]) {
            busybox_write_all(USER_FD_STDOUT, argv[i], user_strlen(argv[i]));
        }
    }
    busybox_write_all(USER_FD_STDOUT, "\n", 1U);
    return 0;
}

static int busybox_run_pwd(void) {
    if (user_getcwd(g_busybox_cwd, sizeof(g_busybox_cwd)) < 0) {
        busybox_write_str(USER_FD_STDERR, kPwdFail);
        return 1;
    }
    busybox_write_str(USER_FD_STDOUT, g_busybox_cwd);
    busybox_write_all(USER_FD_STDOUT, "\n", 1U);
    return 0;
}

static int busybox_run_cat(const char *arg) {
    uint32_t path_len = 0U;
    const char *path = busybox_pick_path(arg, kReadmePath, &path_len);
    int32_t fd;

    if (!path || path_len == 0U) {
        busybox_write_str(USER_FD_STDERR, kCatOpenFail);
        return 1;
    }

    fd = user_open(path, path_len, 0U);
    if (fd < 0) {
        busybox_write_str(USER_FD_STDERR, kCatOpenFail);
        return 1;
    }

    for (;;) {
        int32_t nread = user_read((uint32_t)fd, g_busybox_buf, sizeof(g_busybox_buf));
        if (nread < 0) {
            busybox_write_str(USER_FD_STDERR, kCatReadFail);
            (void)user_close((uint32_t)fd);
            return 1;
        }
        if (nread == 0) {
            break;
        }
        busybox_write_all(USER_FD_STDOUT, g_busybox_buf, (uint32_t)nread);
    }

    (void)user_close((uint32_t)fd);
    return 0;
}

static int busybox_run_ls(const char *arg) {
    uint32_t path_len = 0U;
    const char *path = busybox_pick_path(arg, kDotPath, &path_len);
    int32_t count;

    if (!path || path_len == 0U) {
        busybox_write_str(USER_FD_STDERR, kLsFail);
        return 1;
    }

    count = user_list_dir(path, path_len, g_busybox_ls_entries, BUSYBOX_LS_MAX);
    if (count < 0) {
        busybox_write_str(USER_FD_STDERR, kLsFail);
        return 1;
    }

    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        busybox_write_str(USER_FD_STDOUT, g_busybox_ls_entries[i].name);
        if (g_busybox_ls_entries[i].type == SYSCALL_NODE_TYPE_DIR) {
            busybox_write_all(USER_FD_STDOUT, "/", 1U);
        }
        busybox_write_all(USER_FD_STDOUT, "\n", 1U);
    }
    return 0;
}

static int busybox_run_uptime(void) {
    struct syscall_time_info info;

    if (user_time_info(&info) < 0) {
        busybox_write_str(USER_FD_STDERR, kTimeFail);
        return 1;
    }

    busybox_write_str(USER_FD_STDOUT, "uptime: ");
    if (info.hz != 0U && info.ticks_hi == 0U) {
        busybox_write_str(USER_FD_STDOUT, "up=");
        busybox_write_u32(USER_FD_STDOUT, info.ticks_lo / info.hz);
        busybox_write_str(USER_FD_STDOUT, "s");
    } else {
        busybox_write_str(USER_FD_STDOUT, "ticks=");
        busybox_write_u32(USER_FD_STDOUT, info.ticks_hi);
        busybox_write_all(USER_FD_STDOUT, ":", 1U);
        busybox_write_u32(USER_FD_STDOUT, info.ticks_lo);
    }
    busybox_write_str(USER_FD_STDOUT, " hz=");
    busybox_write_u32(USER_FD_STDOUT, info.hz);
    busybox_write_all(USER_FD_STDOUT, "\n", 1U);
    return 0;
}

void _start(int argc, char **argv) {
    const char *applet;
    int rc = 0;

    if (argc <= 1 || !argv[1] || argv[1][0] == '\0') {
        busybox_write_str(USER_FD_STDOUT, kApplets);
        user_exit(0);
    }

    applet = argv[1];
    if (busybox_str_eq(applet, "echo")) {
        rc = busybox_run_echo(argc, argv, 2);
    } else if (busybox_str_eq(applet, "pwd")) {
        rc = busybox_run_pwd();
    } else if (busybox_str_eq(applet, "cat")) {
        rc = busybox_run_cat(argc > 2 ? argv[2] : 0);
    } else if (busybox_str_eq(applet, "ls")) {
        rc = busybox_run_ls(argc > 2 ? argv[2] : 0);
    } else if (busybox_str_eq(applet, "uptime")) {
        rc = busybox_run_uptime();
    } else {
        busybox_write_str(USER_FD_STDERR, kUnknown);
        rc = 1;
    }

    user_exit(rc);
}
