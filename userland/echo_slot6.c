#include <stdint.h>

#include "userlib.h"

#define ECHO_CMDLINE_MAX 128U

static const char kNewline[] = "\n";
static char g_echo_cmdline[ECHO_CMDLINE_MAX];

void _start(void) {
    int32_t cmdline_len = user_getcmdline(g_echo_cmdline, sizeof(g_echo_cmdline));

    if (cmdline_len < 0) {
        user_exit(1);
    }
    if (cmdline_len > 0) {
        (void)user_write(USER_FD_STDOUT, g_echo_cmdline, (uint32_t)cmdline_len);
    }
    (void)user_write(USER_FD_STDOUT, kNewline, (uint32_t)(sizeof(kNewline) - 1U));
    user_exit(0);
}
