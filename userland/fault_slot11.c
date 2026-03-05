#include <stdint.h>

#include "userlib.h"

static const char kMsg[] = "fault: triggering user page fault\n";

void _start(int argc, char **argv) {
    uint32_t bad_addr = 0x1U;
    volatile uint32_t sink;

    (void)argc;
    (void)argv;

    (void)user_write(USER_FD_STDOUT, kMsg, (uint32_t)(sizeof(kMsg) - 1U));
    __asm__ __volatile__(
        "movl (%1), %0"
        : "=r"(sink)
        : "r"(bad_addr)
        : "memory");
    (void)sink;
    user_exit(0);
}
