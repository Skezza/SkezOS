#include <stdint.h>

#include "userlib.h"

#define READLN_BUF_MAX 128U

static const char kResultPrefix[] = "readln: ";
static const char kReadFail[] = "readln: read failed\n";
static const char kNewline[] = "\n";

void _start(int argc, char **argv) {
    char buf[READLN_BUF_MAX];
    uint32_t len = 0;

    (void)argc;
    (void)argv;

    for (;;) {
        char ch;
        int32_t rc = user_read(USER_FD_STDIN, &ch, 1U);

        if (rc < 0) {
            (void)user_write(USER_FD_STDERR, kReadFail, (uint32_t)(sizeof(kReadFail) - 1U));
            user_exit(1);
        }
        if (rc == 0) {
            user_sleep_ticks(1U);
            continue;
        }

        if (ch == '\r') {
            ch = '\n';
        }
        if (ch == '\n') {
            (void)user_write(USER_FD_STDOUT, kNewline, (uint32_t)(sizeof(kNewline) - 1U));
            break;
        }
        if (len + 1U < sizeof(buf)) {
            buf[len++] = ch;
        }
        (void)user_write(USER_FD_STDOUT, &ch, 1U);
    }

    buf[len] = '\0';
    (void)user_write(USER_FD_STDOUT, kResultPrefix, (uint32_t)(sizeof(kResultPrefix) - 1U));
    if (len != 0U) {
        (void)user_write(USER_FD_STDOUT, buf, len);
    }
    (void)user_write(USER_FD_STDOUT, kNewline, (uint32_t)(sizeof(kNewline) - 1U));
    user_exit(0);
}
