#ifndef SYSCALL_ABI_H
#define SYSCALL_ABI_H

#include <stdint.h>

#define SYSCALL_TASK_NAME_MAX 16U
#define SYSCALL_DIR_ENTRY_NAME_MAX 32U
#define SYSCALL_CWD_MAX 96U

struct syscall_spawn_ex_req {
    uint32_t path_ptr;
    uint32_t path_len;
    uint32_t cmdline_ptr;
    uint32_t cmdline_len;
    uint32_t flags;
};

struct syscall_list_dir_req {
    uint32_t path_ptr;
    uint32_t path_len;
    uint32_t entries_ptr;
    uint32_t entry_cap;
};

struct syscall_task_snapshot_entry {
    int32_t pid;
    int32_t parent_pid;
    int32_t exit_code;
    uint32_t state;
    uint32_t flags;
    char name[SYSCALL_TASK_NAME_MAX];
};

struct syscall_time_info {
    uint32_t ticks_lo;
    uint32_t ticks_hi;
    uint32_t hz;
};

struct syscall_dir_entry {
    uint32_t type;
    char name[SYSCALL_DIR_ENTRY_NAME_MAX];
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
    SYS_TIME_INFO = 14, /* Copy out a monotonic tick snapshot plus timer frequency. */
    SYS_LIST_DIR = 15, /* Copy out a bounded directory entry list for one path. */
    SYS_CHDIR = 16, /* Change the current process working directory. */
    SYS_GETCWD = 17, /* Copy out the current process working directory. */
    SYS_PIPE = 18, /* Create an anonymous pipe and return read/write FDs. */
    SYS_DUP = 19, /* Duplicate an open FD to the lowest available dynamic slot. */
    SYS_DUP2 = 20, /* Duplicate an open FD into a specific target descriptor. */
};

enum {
    SYSCALL_SPAWN_FLAG_INHERIT_FDS = (1U << 0),
};

enum {
    SYSCALL_OPEN_FLAG_READ = 0U,
    SYSCALL_OPEN_FLAG_WRITE = (1U << 0),
    SYSCALL_OPEN_FLAG_APPEND = (1U << 1),
    SYSCALL_OPEN_FLAG_CREATE = (1U << 2),
    SYSCALL_OPEN_FLAG_TRUNC = (1U << 3),
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

enum {
    SYSCALL_NODE_TYPE_NONE = 0,
    SYSCALL_NODE_TYPE_FILE = 1,
    SYSCALL_NODE_TYPE_DIR = 2,
    SYSCALL_NODE_TYPE_CHARDEV = 3,
};

#endif /* SYSCALL_ABI_H */
