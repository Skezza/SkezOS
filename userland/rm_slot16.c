#include <stdint.h>

#include "userlib.h"

static const char kUsage[] = "rm: usage: rm <path>\n";
static const char kFail[] = "rm: unlink failed\n";

void _start(int argc, char **argv) {
    uint32_t path_len;

    if (argc < 2 || !argv[1] || argv[1][0] == '\0') {
        (void)user_write(USER_FD_STDERR, kUsage, (uint32_t)(sizeof(kUsage) - 1U));
        user_exit(1);
    }

    path_len = user_strlen(argv[1]);
    if (user_unlink(argv[1], path_len) < 0) {
        (void)user_write(USER_FD_STDERR, kFail, (uint32_t)(sizeof(kFail) - 1U));
        user_exit(1);
    }

    user_exit(0);
}
