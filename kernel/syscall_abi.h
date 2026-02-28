#ifndef SYSCALL_ABI_H
#define SYSCALL_ABI_H

#include <stdint.h>

#define SYSCALL_TASK_NAME_MAX 16U

struct syscall_spawn_ex_req {
    uint32_t path_ptr;
    uint32_t path_len;
    uint32_t cmdline_ptr;
    uint32_t cmdline_len;
};

struct syscall_task_snapshot_entry {
    int32_t pid;
    int32_t parent_pid;
    int32_t exit_code;
    uint32_t state;
    uint32_t flags;
    char name[SYSCALL_TASK_NAME_MAX];
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
    SYS_TASK_SNAPSHOT = 12, /* Copy out a bounded task snapshot table. */
    SYS_SLEEP = 13, /* Sleep the current task for at least N scheduler ticks. */
};

enum {
    SYSCALL_TASK_STATE_UNUSED = 0,
    SYSCALL_TASK_STATE_RUNNABLE = 1,
    SYSCALL_TASK_STATE_RUNNING = 2,
    SYSCALL_TASK_STATE_SLEEPING = 3,
    SYSCALL_TASK_STATE_WAIT_CHILD = 4,
    SYSCALL_TASK_STATE_ZOMBIE = 5,
};

enum {
    SYSCALL_TASK_FLAG_USER = (1U << 0),
    SYSCALL_TASK_FLAG_EXIT_VALID = (1U << 1),
};

#endif /* SYSCALL_ABI_H */
