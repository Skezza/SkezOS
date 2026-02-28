#ifndef SYSCALL_ABI_H
#define SYSCALL_ABI_H

#include <stdint.h>

struct syscall_spawn_ex_req {
    uint32_t path_ptr;
    uint32_t path_len;
    uint32_t cmdline_ptr;
    uint32_t cmdline_len;
};

enum {
    SYS_WRITE = 1,
    SYS_YIELD = 2,
    SYS_EXIT  = 3,
    SYS_TIME  = 4,
    SYS_READ  = 5,
    SYS_SPAWN = 6, /* Bootstrap spawn hook (returns child pid on success). */
    SYS_OPEN  = 7, /* Bootstrap VFS open -> per-task FD table (read-only focus). */
    SYS_CLOSE = 8, /* Bootstrap FD close (pairs with SYS_OPEN and stdio cache). */
    SYS_WAITPID = 9, /* Minimal wait/reap syscall for parent->child lifecycle. */
    SYS_SPAWN_EX = 10, /* Spawn with flat cmdline handoff via syscall_spawn_ex_req. */
    SYS_GETCMDLINE = 11, /* Copy the current process cmdline into a user buffer. */
};

#endif /* SYSCALL_ABI_H */
