#include <stdint.h>

#include "userlib.h"

#define CAT_READBUF_SIZE 64U

static const char kOpenFail[] = "cat: open failed\n";
static const char kReadFail[] = "cat: read failed\n";
static char g_cat_buf[CAT_READBUF_SIZE];

void _start(int argc, char **argv) {
    int32_t fd = USER_FD_STDIN;

    if (argc > 1 && argv[1] && argv[1][0] != '\0') {
        uint32_t path_len = user_strlen(argv[1]);
        fd = user_open(argv[1], path_len, SYSCALL_OPEN_FLAG_READ);
        if (fd < 0) {
            user_write(USER_FD_STDERR, kOpenFail, (uint32_t)(sizeof(kOpenFail) - 1U));
            user_exit(1);
        }
    }

    for (;;) {
        int32_t nread = user_read((uint32_t)fd, g_cat_buf, sizeof(g_cat_buf));
        if (nread < 0) {
            user_write(USER_FD_STDERR, kReadFail, (uint32_t)(sizeof(kReadFail) - 1U));
            if (fd >= 0 && fd != USER_FD_STDIN) {
                (void)user_close((uint32_t)fd);
            }
            user_exit(1);
        }
        if (nread == 0) {
            break;
        }
        (void)user_write(USER_FD_STDOUT, g_cat_buf, (uint32_t)nread);
    }

    if (fd >= 0 && fd != USER_FD_STDIN) {
        (void)user_close((uint32_t)fd);
    }
    user_exit(0);
}
