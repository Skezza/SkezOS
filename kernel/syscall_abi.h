#ifndef SYSCALL_ABI_H
#define SYSCALL_ABI_H

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
};

#endif /* SYSCALL_ABI_H */
