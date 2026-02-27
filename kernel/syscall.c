#include "syscall.h"

#include <stdint.h>

#include "idt.h"
#include "kerrno.h"
#include "kfile.h"
#include "klog.h"
#include "memory_layout.h"
#include "sched.h"
#include "timer.h"
#include "uaccess.h"
#include "usermode.h"
#include "vfs.h"

extern void syscall_entry_stub(void);

#define SYSCALL_SPAWN_PATH_MAX 64U
#define SYSCALL_OPEN_PATH_MAX  64U

static int syscall_stdio_kfile_for_fd(uint32_t fd, int for_write, struct kfile **out_file);

static uint32_t syscall_ret_err(int err) {
    return (uint32_t)(-(int32_t)err);
}

static int syscall_stdio_path_for_fd(uint32_t fd, int for_write, const char **out_path) {
    if (!out_path) {
        return -KERR_INVAL;
    }
    *out_path = 0;

    switch (fd) {
        case SYSCALL_FD_STDIN:
            if (for_write) {
                return -KERR_INVAL;
            }
            *out_path = "/dev/console";
            return 0;
        case SYSCALL_FD_STDOUT:
        case SYSCALL_FD_STDERR:
            if (!for_write) {
                return -KERR_INVAL;
            }
            *out_path = "/dev/console";
            return 0;
        default:
            return -KERR_INVAL;
    }
}

static int syscall_kfile_for_fd(uint32_t fd, int for_write, struct kfile **out_file) {
    if (!out_file) {
        return -KERR_INVAL;
    }
    *out_file = 0;

    if (fd == SYSCALL_FD_STDIN || fd == SYSCALL_FD_STDOUT || fd == SYSCALL_FD_STDERR) {
        return syscall_stdio_kfile_for_fd(fd, for_write, out_file);
    }
    return sched_current_process_fd_get(fd, out_file);
}

static int syscall_stdio_kfile_for_fd(uint32_t fd, int for_write, struct kfile **out_file) {
    struct kfile *cached = 0;
    const char *path;
    struct kfile opened;
    int rc;

    if (!out_file) {
        return -KERR_INVAL;
    }
    *out_file = 0;

    rc = sched_current_process_fd_get(fd, &cached);
    if (rc == 0) {
        *out_file = cached;
        return 0;
    }
    if (rc != -KERR_NOENT) {
        return rc;
    }

    rc = syscall_stdio_path_for_fd(fd, for_write, &path);
    if (rc < 0) {
        return rc;
    }
    rc = vfs_open(path, 0, &opened);
    if (rc < 0) {
        return rc;
    }
    rc = sched_current_process_fd_install(fd, &opened, &cached);
    if (rc < 0) {
        kfile_close(&opened);
        return rc;
    }
    if (fd != SYSCALL_FD_STDIN) {
        KLOGI("syscall: stdio fd bind pid=%d task=%s fd=%u path=%s",
              sched_current_task_pid(),
              sched_current_task_name(),
              fd,
              path);
    }
    *out_file = cached;
    return 0;
}

static uint32_t sys_write(struct syscall_saved_regs *regs) {
    uint32_t fd = regs->ebx;
    uint32_t ptr = regs->ecx;
    uint32_t len = regs->edx;
    const char *s = (const char *)(uintptr_t)ptr;
    struct kfile *file = 0;
    uint32_t written = 0;
    int rc;

    rc = syscall_kfile_for_fd(fd, 1, &file);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    if (!uaccess_user_range_ok(ptr, len)) {
        return syscall_ret_err(KERR_FAULT);
    }
    rc = kfile_write(file, s, len, &written);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return written;
}

static uint32_t sys_read(struct syscall_saved_regs *regs) {
    uint32_t fd = regs->ebx;
    uint32_t ptr = regs->ecx;
    uint32_t len = regs->edx;
    void *dst = (void *)(uintptr_t)ptr;
    struct kfile *file = 0;
    uint32_t nread = 0;
    int rc;

    rc = syscall_kfile_for_fd(fd, 0, &file);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    if (!uaccess_user_range_ok(ptr, len)) {
        return syscall_ret_err(KERR_FAULT);
    }
    rc = kfile_read(file, dst, len, &nread);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return nread;
}

static uint32_t sys_spawn(struct syscall_saved_regs *regs) {
    uint32_t path_ptr = regs->ebx;
    uint32_t path_len = regs->ecx;
    char path[SYSCALL_SPAWN_PATH_MAX];
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }
    if (path_len == 0U || path_len >= SYSCALL_SPAWN_PATH_MAX) {
        return syscall_ret_err(KERR_INVAL);
    }

    rc = uaccess_copy_from_user(path, path_ptr, path_len);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    path[path_len] = '\0';

    rc = usermode_spawn_path_task(path);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return (uint32_t)rc;
}

static uint32_t sys_open(struct syscall_saved_regs *regs) {
    uint32_t path_ptr = regs->ebx;
    uint32_t path_len = regs->ecx;
    uint32_t open_flags = regs->edx;
    char path[SYSCALL_OPEN_PATH_MAX];
    struct kfile file;
    uint32_t fd = 0;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }
    if (path_len == 0U || path_len >= SYSCALL_OPEN_PATH_MAX) {
        return syscall_ret_err(KERR_INVAL);
    }
    rc = uaccess_copy_from_user(path, path_ptr, path_len);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    path[path_len] = '\0';

    rc = vfs_open(path, open_flags, &file);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    rc = sched_current_process_fd_alloc(&file, &fd, 0);
    if (rc < 0) {
        kfile_close(&file);
        return syscall_ret_err(-rc);
    }

    KLOGI("sys_open: pid=%d task=%s fd=%u path=%s flags=%x",
          sched_current_task_pid(),
          sched_current_task_name(),
          fd,
          path,
          open_flags);
    return fd;
}

static uint32_t sys_close(struct syscall_saved_regs *regs) {
    uint32_t fd = regs->ebx;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    rc = sched_current_process_fd_close(fd);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return 0;
}

static uint32_t sys_waitpid(struct syscall_saved_regs *regs) {
    int target_pid = (int)regs->ebx;
    uint32_t status_ptr = regs->ecx;
    uint32_t options = regs->edx;
    int waited_pid;
    int32_t waited_exit = 0;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }
    if (options != 0U) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    rc = sched_waitpid(target_pid, &waited_pid, &waited_exit);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    if (status_ptr != 0U) {
        rc = uaccess_copy_to_user(status_ptr, &waited_exit, (uint32_t)sizeof(waited_exit));
        if (rc < 0) {
            return syscall_ret_err(-rc);
        }
    }

    KLOGI("sys_waitpid: parent_pid=%d task=%s waited_pid=%d exit=%d",
          sched_current_task_pid(),
          sched_current_task_name(),
          waited_pid,
          waited_exit);
    return (uint32_t)waited_pid;
}

static uint32_t sys_yield(void) {
    sched_yield();
    return 0;
}

static uint32_t sys_time(void) {
    return (uint32_t)timer_ticks;
}

static void sys_exit(struct syscall_saved_regs *regs) __attribute__((noreturn));

static void sys_exit(struct syscall_saved_regs *regs) {
    sched_note_current_exit_code((int32_t)regs->ebx);
    KLOGI("sys_exit: pid=%d task=%s code=%d",
          sched_current_task_pid(),
          sched_current_task_name(),
          (int32_t)regs->ebx);
    sched_exit_current();
}

void syscall_init(void) {
    idt_set_gate_user(SYSCALL_VECTOR, (uint32_t)(uintptr_t)syscall_entry_stub);
    KLOGI("syscall: int 0x80 gate installed");
}

uint32_t syscall_dispatch(struct syscall_saved_regs *regs) {
    if (!regs) {
        return (uint32_t)(-(int32_t)KERR_INVAL);
    }

    sched_note_current_syscall(regs->eax);

    switch (regs->eax) {
        case SYS_WRITE:
            return sys_write(regs);
        case SYS_YIELD:
            return sys_yield();
        case SYS_EXIT:
            sys_exit(regs);
        case SYS_TIME:
            return sys_time();
        case SYS_READ:
            return sys_read(regs);
        case SYS_SPAWN:
            return sys_spawn(regs);
        case SYS_OPEN:
            return sys_open(regs);
        case SYS_CLOSE:
            return sys_close(regs);
        case SYS_WAITPID:
            return sys_waitpid(regs);
        default:
            KLOGW("syscall: unknown nr=%u ebx=%x ecx=%x edx=%x",
                  regs->eax, regs->ebx, regs->ecx, regs->edx);
            return syscall_ret_err(KERR_NOTSUP);
    }
}
