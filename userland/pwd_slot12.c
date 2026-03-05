#include <stdint.h>

#include "userlib.h"

static char g_pwd_buf[SYSCALL_CWD_MAX];
static const char kNewline[] = "\n";
static const char kFail[] = "pwd: getcwd failed\n";

void _start(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (user_getcwd(g_pwd_buf, sizeof(g_pwd_buf)) < 0) {
        (void)user_write(USER_FD_STDERR, kFail, (uint32_t)(sizeof(kFail) - 1U));
        user_exit(1);
    }

    (void)user_write(USER_FD_STDOUT, g_pwd_buf, user_strlen(g_pwd_buf));
    (void)user_write(USER_FD_STDOUT, kNewline, (uint32_t)(sizeof(kNewline) - 1U));
    user_exit(0);
}
