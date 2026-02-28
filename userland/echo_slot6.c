#include <stdint.h>

#include "userlib.h"

static const char kNewline[] = "\n";

void _start(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            (void)user_write(USER_FD_STDOUT, " ", 1U);
        }
        if (argv[i]) {
            (void)user_write(USER_FD_STDOUT, argv[i], user_strlen(argv[i]));
        }
    }
    (void)user_write(USER_FD_STDOUT, kNewline, (uint32_t)(sizeof(kNewline) - 1U));
    user_exit(0);
}
