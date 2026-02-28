#include <stdint.h>

#include "userlib.h"

#define CAT_READBUF_SIZE 64U
#define CAT_PATH_MAX     96U
#define CAT_CMDLINE_MAX  128U

static const char kReadmePath[] = "/bin/readme.txt";
static const char kBinPrefix[] = "/bin/";
static const char kPrefix[] = "cat: ";
static const char kOpenFail[] = "cat: open failed\n";
static const char kReadFail[] = "cat: read failed\n";
static char g_cat_buf[CAT_READBUF_SIZE];
static char g_cat_path[CAT_PATH_MAX];
static char g_cat_cmdline[CAT_CMDLINE_MAX];

static const char *cat_resolve_path(uint32_t *out_len) {
    int32_t cmdline_len;
    uint32_t prefix_len = (uint32_t)(sizeof(kBinPrefix) - 1U);

    if (!out_len) {
        return 0;
    }

    cmdline_len = user_getcmdline(g_cat_cmdline, sizeof(g_cat_cmdline));
    if (cmdline_len < 0) {
        return 0;
    }
    if (cmdline_len == 0) {
        *out_len = (uint32_t)(sizeof(kReadmePath) - 1U);
        return kReadmePath;
    }
    if (g_cat_cmdline[0] == '/') {
        *out_len = (uint32_t)cmdline_len;
        return g_cat_cmdline;
    }
    if ((uint32_t)cmdline_len + prefix_len + 1U > sizeof(g_cat_path)) {
        return 0;
    }

    user_memcpy(g_cat_path, kBinPrefix, prefix_len);
    user_memcpy(g_cat_path + prefix_len, g_cat_cmdline, (uint32_t)cmdline_len);
    g_cat_path[prefix_len + (uint32_t)cmdline_len] = '\0';
    *out_len = prefix_len + (uint32_t)cmdline_len;
    return g_cat_path;
}

void _start(void) {
    uint32_t path_len = 0;
    const char *path = cat_resolve_path(&path_len);
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
