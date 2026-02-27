#include <stdint.h>

#include "userlib.h"

#define CAT_READBUF_SIZE 64U

static const char kReadmePath[] = "/bin/readme.txt";
static const char kPrefix[] = "cat: ";
static const char kOpenFail[] = "cat: open failed\n";
static const char kReadFail[] = "cat: read failed\n";
static char g_cat_buf[CAT_READBUF_SIZE];

void _start(void) {
    int32_t fd = user_open(kReadmePath, (uint32_t)(sizeof(kReadmePath) - 1U), 0U);
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
