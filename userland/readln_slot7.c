#include <stdint.h>

#include "userlib.h"

#define READLN_BUF_MAX 128U

static const char kResultPrefix[] = "readln: ";
static const char kReadFail[] = "readln: read failed\n";
static const char kNewline[] = "\n";
static const char kEraseOne[] = "\b \b";

static void readln_write_all(uint32_t fd, const char *buf, uint32_t len) {
    while (len > 0U) {
        int32_t rc = user_write(fd, buf, len);
        if (rc <= 0) {
            return;
        }
        buf += (uint32_t)rc;
        len -= (uint32_t)rc;
    }
}

void _start(int argc, char **argv) {
    char buf[READLN_BUF_MAX];
    uint32_t len = 0;

    (void)argc;
    (void)argv;

    for (;;) {
        char ch;
        int32_t rc = user_read(USER_FD_STDIN, &ch, 1U);

        if (rc <= 0) {
            readln_write_all(USER_FD_STDERR, kReadFail, (uint32_t)(sizeof(kReadFail) - 1U));
            user_exit(1);
        }

        if (ch == '\r') {
            ch = '\n';
        }
        if (ch == '\b' || ch == 0x7f) {
            if (len > 0U) {
                len--;
                readln_write_all(USER_FD_STDOUT, kEraseOne, (uint32_t)(sizeof(kEraseOne) - 1U));
            }
            continue;
        }
        if (ch == '\n') {
            readln_write_all(USER_FD_STDOUT, kNewline, (uint32_t)(sizeof(kNewline) - 1U));
            break;
        }
        if (len + 1U < sizeof(buf)) {
            buf[len++] = ch;
        }
        readln_write_all(USER_FD_STDOUT, &ch, 1U);
    }

    buf[len] = '\0';
    readln_write_all(USER_FD_STDOUT, kResultPrefix, (uint32_t)(sizeof(kResultPrefix) - 1U));
    if (len != 0U) {
        readln_write_all(USER_FD_STDOUT, buf, len);
    }
    readln_write_all(USER_FD_STDOUT, kNewline, (uint32_t)(sizeof(kNewline) - 1U));
    user_exit(0);
}
