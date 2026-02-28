#include <stdint.h>

#include "userlib.h"

#define CAT_READBUF_SIZE 64U
#define CAT_PATH_MAX     96U

static const char kReadmePath[] = "/bin/readme.txt";
static const char kBinPrefix[] = "/bin/";
static const char kPrefix[] = "cat: ";
static const char kOpenFail[] = "cat: open failed\n";
static const char kReadFail[] = "cat: read failed\n";
static char g_cat_buf[CAT_READBUF_SIZE];
static char g_cat_path[CAT_PATH_MAX];

static const char *cat_resolve_path(int argc, char **argv, uint32_t *out_len) {
    const char *arg;
    uint32_t arg_len;
    uint32_t prefix_len = (uint32_t)(sizeof(kBinPrefix) - 1U);

    if (!out_len) {
        return 0;
    }
    if (argc <= 1 || !argv[1] || argv[1][0] == '\0') {
        *out_len = (uint32_t)(sizeof(kReadmePath) - 1U);
        return kReadmePath;
    }

    arg = argv[1];
    arg_len = user_strlen(arg);
    if (arg[0] == '/') {
        *out_len = arg_len;
        return arg;
    }
    if (arg_len + prefix_len + 1U > sizeof(g_cat_path)) {
        return 0;
    }

    user_memcpy(g_cat_path, kBinPrefix, prefix_len);
    user_memcpy(g_cat_path + prefix_len, arg, arg_len);
    g_cat_path[prefix_len + arg_len] = '\0';
    *out_len = prefix_len + arg_len;
    return g_cat_path;
}

void _start(int argc, char **argv) {
    uint32_t path_len = 0;
    const char *path = cat_resolve_path(argc, argv, &path_len);
    int32_t fd;

    if (!path || path_len == 0U) {
        user_write(USER_FD_STDERR, kOpenFail, (uint32_t)(sizeof(kOpenFail) - 1U));
        user_exit(1);
    }

    fd = user_open(path, path_len, 0U);
    if (fd < 0) {
        user_write(USER_FD_STDERR, kOpenFail, (uint32_t)(sizeof(kOpenFail) - 1U));
        user_exit(1);
    }

    (void)user_write(USER_FD_STDOUT, kPrefix, (uint32_t)(sizeof(kPrefix) - 1U));
    for (;;) {
        int32_t nread = user_read((uint32_t)fd, g_cat_buf, sizeof(g_cat_buf));
        if (nread < 0) {
            user_write(USER_FD_STDERR, kReadFail, (uint32_t)(sizeof(kReadFail) - 1U));
            (void)user_close((uint32_t)fd);
            user_exit(1);
        }
        if (nread == 0) {
            break;
        }
        (void)user_write(USER_FD_STDOUT, g_cat_buf, (uint32_t)nread);
    }

    (void)user_close((uint32_t)fd);
    user_exit(0);
}
