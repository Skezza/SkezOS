#include "syscall.h"

#include <stdint.h>

#include "display.h"
#include "idt.h"
#include "kerrno.h"
#include "kfile.h"
#include "kpipe.h"
#include "klog.h"
#include "memory_layout.h"
#include "sched.h"
#include "timer.h"
#include "uaccess.h"
#include "usermode.h"
#include "utils.h"
#include "vfs.h"

extern void syscall_entry_stub(void);

#define SYSCALL_SPAWN_PATH_MAX 64U
#define SYSCALL_CMDLINE_MAX    128U
#define SYSCALL_OPEN_PATH_MAX  96U
#define SYSCALL_UNLINK_PATH_MAX 96U
#define SYSCALL_TASK_SNAPSHOT_MAX 16U
#define SYSCALL_LIST_DIR_PATH_MAX 96U
#define SYSCALL_LIST_DIR_MAX 16U
#define SYSCALL_FRAME_EIP_WORD 12U
#define SYSCALL_FRAME_CS_WORD 13U
#define SYSCALL_FRAME_EFLAGS_WORD 14U
#define SYSCALL_FRAME_ESP_WORD 15U

static int syscall_stdio_kfile_for_fd(uint32_t fd, int for_write, struct kfile **out_file);
static int syscall_copy_user_path_raw(uint32_t path_ptr,
                                      uint32_t path_len,
                                      char *out,
                                      uint32_t out_cap);
static int syscall_resolve_path_raw(const char *raw,
                                    uint32_t raw_len,
                                    char *out,
                                    uint32_t out_cap,
                                    uint32_t *out_len);
static int syscall_kfile_for_dup(uint32_t fd, struct kfile **out_file);
static int syscall_capture_user_fork_context(struct syscall_saved_regs *regs,
                                             struct sched_user_fork_context *out_ctx);

static uint32_t syscall_ret_err(int err) {
    return (uint32_t)(-(int32_t)err);
}

static int syscall_normalize_path(const char *base,
                                  const char *path,
                                  uint32_t path_len,
                                  char *out,
                                  uint32_t out_cap,
                                  uint32_t *out_len) {
    uint32_t path_idx = 0U;
    uint32_t write_len = 0U;

    if (!path || path_len == 0U || !out || out_cap < 2U || !out_len) {
        return -KERR_INVAL;
    }
    *out_len = 0U;

    if (path[0] == '/') {
        out[0] = '/';
        out[1] = '\0';
        write_len = 1U;
        while (path_idx < path_len && path[path_idx] == '/') {
            path_idx++;
        }
    } else {
        uint32_t base_len = 0U;

        if (!base || base[0] != '/') {
            return -KERR_INVAL;
        }
        base_len = (uint32_t)strlen(base);
        if (base_len + 1U > out_cap) {
            return -KERR_INVAL;
        }
        memcpy(out, base, base_len);
        out[base_len] = '\0';
        write_len = base_len;
    }

    while (path_idx < path_len) {
        uint32_t comp_start = path_idx;
        uint32_t comp_len;

        while (path_idx < path_len && path[path_idx] != '/') {
            path_idx++;
        }
        comp_len = path_idx - comp_start;
        while (path_idx < path_len && path[path_idx] == '/') {
            path_idx++;
        }
        if (comp_len == 0U) {
            continue;
        }
        if (comp_len == 1U && path[comp_start] == '.') {
            continue;
        }
        if (comp_len == 2U &&
            path[comp_start] == '.' &&
            path[comp_start + 1U] == '.') {
            if (write_len > 1U) {
                while (write_len > 1U && out[write_len - 1U] != '/') {
                    write_len--;
                }
                if (write_len > 1U) {
                    write_len--;
                }
                out[write_len] = '\0';
            }
            continue;
        }

        if (write_len > 1U) {
            if (write_len + 1U >= out_cap) {
                return -KERR_INVAL;
            }
            out[write_len++] = '/';
        }
        if (write_len + comp_len >= out_cap) {
            return -KERR_INVAL;
        }
        memcpy(out + write_len, path + comp_start, comp_len);
        write_len += comp_len;
        out[write_len] = '\0';
    }

    if (out[0] == '\0') {
        out[0] = '/';
        out[1] = '\0';
        write_len = 1U;
    }
    *out_len = write_len;
    return 0;
}

static int syscall_copy_user_path_raw(uint32_t path_ptr,
                                      uint32_t path_len,
                                      char *out,
                                      uint32_t out_cap) {
    int rc;

    if (!out || out_cap == 0U) {
        return -KERR_INVAL;
    }
    if (path_len == 0U || path_len >= out_cap) {
        return -KERR_INVAL;
    }

    rc = uaccess_copy_from_user(out, path_ptr, path_len);
    if (rc < 0) {
        return rc;
    }
    out[path_len] = '\0';
    return 0;
}

static int syscall_resolve_path_raw(const char *raw,
                                    uint32_t raw_len,
                                    char *out,
                                    uint32_t out_cap,
                                    uint32_t *out_len) {
    char cwd[SYSCALL_CWD_MAX];
    uint32_t cwd_len = 0U;
    int rc;

    if (!raw || raw_len == 0U || !out || out_cap < 2U || !out_len) {
        return -KERR_INVAL;
    }
    if (raw[0] == '/') {
        if (raw_len + 1U > out_cap) {
            return -KERR_INVAL;
        }
        memcpy(out, raw, raw_len);
        out[raw_len] = '\0';
        *out_len = raw_len;
        return 0;
    }

    rc = sched_copy_current_task_cwd(cwd, sizeof(cwd), &cwd_len);
    if (rc < 0) {
        return rc;
    }
    return syscall_normalize_path(cwd, raw, raw_len, out, out_cap, out_len);
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

    rc = syscall_stdio_path_for_fd(fd, for_write, &path);
    if (rc < 0) {
        return rc;
    }

    rc = sched_current_process_fd_get(fd, &cached);
    if (rc == 0) {
        *out_file = cached;
        return 0;
    }
    if (rc != -KERR_NOENT) {
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
    (void)kfile_close(&opened);
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

static int syscall_kfile_for_dup(uint32_t fd, struct kfile **out_file) {
    int rc;

    if (!out_file) {
        return -KERR_INVAL;
    }
    *out_file = 0;

    if (fd == SYSCALL_FD_STDIN) {
        return syscall_stdio_kfile_for_fd(fd, 0, out_file);
    }
    if (fd == SYSCALL_FD_STDOUT || fd == SYSCALL_FD_STDERR) {
        return syscall_stdio_kfile_for_fd(fd, 1, out_file);
    }

    rc = sched_current_process_fd_get(fd, out_file);
    return rc;
}

static int syscall_capture_user_fork_context(struct syscall_saved_regs *regs,
                                             struct sched_user_fork_context *out_ctx) {
    uint32_t *words;
    uint32_t cs;

    if (!regs || !out_ctx) {
        return -KERR_INVAL;
    }

    words = (uint32_t *)(uintptr_t)regs;
    cs = words[SYSCALL_FRAME_CS_WORD];
    if ((cs & 0x3U) != 0x3U) {
        return -KERR_NOTSUP;
    }

    out_ctx->eax = regs->eax;
    out_ctx->ebx = regs->ebx;
    out_ctx->ecx = regs->ecx;
    out_ctx->edx = regs->edx;
    out_ctx->esi = regs->esi;
    out_ctx->edi = regs->edi;
    out_ctx->ebp = regs->ebp;
    out_ctx->eip = words[SYSCALL_FRAME_EIP_WORD];
    out_ctx->esp = words[SYSCALL_FRAME_ESP_WORD];
    out_ctx->eflags = words[SYSCALL_FRAME_EFLAGS_WORD];
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
    char raw_path[SYSCALL_SPAWN_PATH_MAX];
    char path[SYSCALL_SPAWN_PATH_MAX];
    uint32_t resolved_len = 0U;
    int child_pid;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }
    rc = syscall_copy_user_path_raw(path_ptr, path_len, raw_path, sizeof(raw_path));
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    rc = syscall_resolve_path_raw(raw_path, path_len, path, sizeof(path), &resolved_len);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    child_pid = usermode_spawn_path_task(path);
    if (child_pid < 0) {
        return syscall_ret_err(-child_pid);
    }
    return (uint32_t)child_pid;
}

static uint32_t sys_spawn_ex(struct syscall_saved_regs *regs) {
    uint32_t req_ptr = regs->ebx;
    struct syscall_spawn_ex_req req;
    char raw_path[SYSCALL_SPAWN_PATH_MAX];
    char path[SYSCALL_SPAWN_PATH_MAX];
    char cmdline[SYSCALL_CMDLINE_MAX];
    uint32_t resolved_len = 0U;
    int child_pid;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    rc = uaccess_copy_from_user(&req, req_ptr, (uint32_t)sizeof(req));
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    if (req.cmdline_len >= SYSCALL_CMDLINE_MAX) {
        return syscall_ret_err(KERR_INVAL);
    }
    if (req.cmdline_len != 0U && req.cmdline_ptr == 0U) {
        return syscall_ret_err(KERR_INVAL);
    }
    if ((req.flags & ~(SYSCALL_SPAWN_FLAG_INHERIT_FDS | SYSCALL_SPAWN_FLAG_FOREGROUND)) != 0U) {
        return syscall_ret_err(KERR_INVAL);
    }

    rc = syscall_copy_user_path_raw(req.path_ptr, req.path_len, raw_path, sizeof(raw_path));
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    rc = syscall_resolve_path_raw(raw_path, req.path_len, path, sizeof(path), &resolved_len);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    cmdline[0] = '\0';
    if (req.cmdline_len != 0U) {
        rc = uaccess_copy_from_user(cmdline, req.cmdline_ptr, req.cmdline_len);
        if (rc < 0) {
            return syscall_ret_err(-rc);
        }
    }
    cmdline[req.cmdline_len] = '\0';

    child_pid = usermode_spawn_path_task_ex(path, cmdline, req.cmdline_len, req.flags);
    if (child_pid < 0) {
        KLOGW("sys_spawn_ex: spawn failed pid=%d task=%s path=%s rc=%d flags=%x",
              sched_current_task_pid(),
              sched_current_task_name(),
              path,
              child_pid,
              req.flags);
        return syscall_ret_err(-child_pid);
    }
    if ((req.flags & SYSCALL_SPAWN_FLAG_FOREGROUND) != 0U &&
        vfs_console_input_owner_is_task(sched_current_task_pid())) {
        (void)vfs_console_set_input_owner_task(child_pid);
    }
    return (uint32_t)child_pid;
}

static uint32_t sys_open(struct syscall_saved_regs *regs) {
    uint32_t path_ptr = regs->ebx;
    uint32_t path_len = regs->ecx;
    uint32_t open_flags = regs->edx;
    char raw_path[SYSCALL_OPEN_PATH_MAX];
    char path[SYSCALL_OPEN_PATH_MAX];
    uint32_t resolved_len = 0U;
    struct kfile file;
    uint32_t fd = 0;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }
    rc = syscall_copy_user_path_raw(path_ptr, path_len, raw_path, sizeof(raw_path));
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    rc = syscall_resolve_path_raw(raw_path, path_len, path, sizeof(path), &resolved_len);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    rc = vfs_open(path, open_flags, &file);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    rc = sched_current_process_fd_alloc(&file, &fd, 0);
    if (rc < 0) {
        kfile_close(&file);
        return syscall_ret_err(-rc);
    }
    (void)kfile_close(&file);

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

static uint32_t sys_unlink(struct syscall_saved_regs *regs) {
    uint32_t path_ptr = regs->ebx;
    uint32_t path_len = regs->ecx;
    char raw_path[SYSCALL_UNLINK_PATH_MAX];
    char path[SYSCALL_UNLINK_PATH_MAX];
    uint32_t resolved_len = 0U;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }
    rc = syscall_copy_user_path_raw(path_ptr, path_len, raw_path, sizeof(raw_path));
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    rc = syscall_resolve_path_raw(raw_path, path_len, path, sizeof(path), &resolved_len);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    rc = vfs_unlink(path);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return 0;
}

static uint32_t sys_fork(struct syscall_saved_regs *regs) {
    struct sched_user_fork_context fork_ctx;
    int child_pid = -1;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    rc = syscall_capture_user_fork_context(regs, &fork_ctx);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    rc = sched_fork_current_user_task(&fork_ctx, &child_pid);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return (uint32_t)child_pid;
}

static uint32_t sys_pipe(struct syscall_saved_regs *regs) {
    uint32_t pair_ptr = regs->ebx;
    struct kfile read_end;
    struct kfile write_end;
    uint32_t fds[2];
    int32_t user_fds[2];
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }
    if (!uaccess_user_range_ok(pair_ptr, (uint32_t)sizeof(user_fds))) {
        return syscall_ret_err(KERR_FAULT);
    }

    rc = kpipe_create(&read_end, &write_end);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    rc = sched_current_process_fd_alloc(&read_end, &fds[0], 0);
    (void)kfile_close(&read_end);
    if (rc < 0) {
        (void)kfile_close(&write_end);
        return syscall_ret_err(-rc);
    }

    rc = sched_current_process_fd_alloc(&write_end, &fds[1], 0);
    (void)kfile_close(&write_end);
    if (rc < 0) {
        (void)sched_current_process_fd_close(fds[0]);
        return syscall_ret_err(-rc);
    }

    user_fds[0] = (int32_t)fds[0];
    user_fds[1] = (int32_t)fds[1];
    rc = uaccess_copy_to_user(pair_ptr, user_fds, (uint32_t)sizeof(user_fds));
    if (rc < 0) {
        (void)sched_current_process_fd_close(fds[0]);
        (void)sched_current_process_fd_close(fds[1]);
        return syscall_ret_err(-rc);
    }
    return 0;
}

static uint32_t sys_dup(struct syscall_saved_regs *regs) {
    uint32_t oldfd = regs->ebx;
    struct kfile *src = 0;
    uint32_t newfd = 0;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }
    rc = syscall_kfile_for_dup(oldfd, &src);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    rc = sched_current_process_fd_dup(oldfd, &newfd, 0);
    if (rc < 0) {
        if (oldfd <= SYSCALL_FD_STDERR) {
            struct kfile *file = 0;
            rc = sched_current_process_fd_alloc(src, &newfd, &file);
            if (rc < 0) {
                return syscall_ret_err(-rc);
            }
            return newfd;
        }
        return syscall_ret_err(-rc);
    }
    return newfd;
}

static uint32_t sys_dup2(struct syscall_saved_regs *regs) {
    uint32_t oldfd = regs->ebx;
    uint32_t newfd = regs->ecx;
    struct kfile *src = 0;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    rc = syscall_kfile_for_dup(oldfd, &src);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    if (oldfd == newfd) {
        return newfd;
    }

    if (newfd <= SYSCALL_FD_STDERR) {
        (void)sched_current_process_fd_close(newfd);
        rc = sched_current_process_fd_install(newfd, src, 0);
        if (rc < 0) {
            return syscall_ret_err(-rc);
        }
        return newfd;
    }

    rc = sched_current_process_fd_dup2(oldfd, newfd, 0);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return newfd;
}

static uint32_t sys_waitpid(struct syscall_saved_regs *regs) {
    int target_pid = (int)regs->ebx;
    uint32_t status_ptr = regs->ecx;
    uint32_t options = regs->edx;
    int nohang = (options & SYSCALL_WAITPID_FLAG_NOHANG) != 0U;
    int parent_pid;
    int waited_pid;
    int32_t waited_exit = 0;
    int restore_stdin = 0;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }
    if ((options & ~SYSCALL_WAITPID_FLAG_NOHANG) != 0U) {
        return syscall_ret_err(KERR_NOTSUP);
    }
    if (status_ptr != 0U &&
        !uaccess_user_range_ok(status_ptr, (uint32_t)sizeof(waited_exit))) {
        return syscall_ret_err(KERR_FAULT);
    }

    parent_pid = sched_current_task_pid();
    if (!nohang &&
        target_pid > 0 &&
        parent_pid > 0 &&
        sched_current_task_owns_child_pid(target_pid)) {
        if (vfs_console_input_owner_is_task(parent_pid)) {
            if (vfs_console_set_input_owner_task(target_pid) == 0) {
                restore_stdin = 1;
            }
        } else if (vfs_console_input_owner_is_task(target_pid)) {
            restore_stdin = 1;
        }
    }

    rc = sched_waitpid_ex(target_pid, options, &waited_pid, &waited_exit);
    if (restore_stdin) {
        (void)vfs_console_set_input_owner_task(parent_pid);
    }
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    if (status_ptr != 0U && waited_pid > 0) {
        rc = uaccess_copy_to_user(status_ptr, &waited_exit, (uint32_t)sizeof(waited_exit));
        if (rc < 0) {
            return syscall_ret_err(-rc);
        }
    }

    if (waited_pid > 0) {
        KLOGI("sys_waitpid: parent_pid=%d task=%s waited_pid=%d exit=%d",
              sched_current_task_pid(),
              sched_current_task_name(),
              waited_pid,
              waited_exit);
        KLOGI("SMOKE_LIFECYCLE_WAIT_REAP parent_pid=%d waited_pid=%d exit=%d",
              sched_current_task_pid(),
              waited_pid,
              waited_exit);
    }
    return (uint32_t)waited_pid;
}

static uint32_t sys_chdir(struct syscall_saved_regs *regs) {
    uint32_t path_ptr = regs->ebx;
    uint32_t path_len = regs->ecx;
    char raw_path[SYSCALL_CWD_MAX];
    char path[SYSCALL_CWD_MAX];
    uint32_t resolved_len = 0U;
    struct vfs_node *node = 0;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    rc = syscall_copy_user_path_raw(path_ptr, path_len, raw_path, sizeof(raw_path));
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    rc = syscall_resolve_path_raw(raw_path, path_len, path, sizeof(path), &resolved_len);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    rc = vfs_lookup(path, &node);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    if (!node || node->type != VFS_NODE_DIR) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    rc = sched_set_current_task_cwd(path, resolved_len);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return 0;
}

static uint32_t sys_getcwd(struct syscall_saved_regs *regs) {
    uint32_t dst_ptr = regs->ebx;
    uint32_t dst_len = regs->ecx;
    uint32_t copied_len = 0U;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    rc = sched_copy_current_task_cwd_to_user(dst_ptr, dst_len, &copied_len);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return copied_len;
}

static uint32_t sys_task_snapshot(struct syscall_saved_regs *regs) {
    uint32_t entries_ptr = regs->ebx;
    uint32_t entry_cap = regs->ecx;
    struct syscall_task_snapshot_entry snapshot[SYSCALL_TASK_SNAPSHOT_MAX];
    uint32_t capped_cap = entry_cap;
    uint32_t count = 0;
    uint32_t copy_bytes = 0;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    if (capped_cap > SYSCALL_TASK_SNAPSHOT_MAX) {
        capped_cap = SYSCALL_TASK_SNAPSHOT_MAX;
    }
    if (capped_cap != 0U) {
        copy_bytes = capped_cap * (uint32_t)sizeof(snapshot[0]);
        if (entries_ptr == 0U || !uaccess_user_range_ok(entries_ptr, copy_bytes)) {
            return syscall_ret_err(KERR_FAULT);
        }
    }

    rc = sched_collect_task_snapshot(snapshot, capped_cap, &count);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    if (count != 0U) {
        copy_bytes = count * (uint32_t)sizeof(snapshot[0]);
        rc = uaccess_copy_to_user(entries_ptr, snapshot, copy_bytes);
        if (rc < 0) {
            return syscall_ret_err(-rc);
        }
    }

    return count;
}

static uint32_t sys_list_dir(struct syscall_saved_regs *regs) {
    uint32_t req_ptr = regs->ebx;
    struct syscall_list_dir_req req;
    char raw_path[SYSCALL_LIST_DIR_PATH_MAX];
    char path[SYSCALL_LIST_DIR_PATH_MAX];
    struct vfs_dir_entry kernel_entries[SYSCALL_LIST_DIR_MAX];
    struct syscall_dir_entry user_entries[SYSCALL_LIST_DIR_MAX];
    uint32_t capped_cap;
    uint32_t count = 0U;
    uint32_t resolved_len = 0U;
    uint32_t copy_bytes = 0U;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    rc = uaccess_copy_from_user(&req, req_ptr, (uint32_t)sizeof(req));
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    capped_cap = req.entry_cap;
    if (capped_cap > SYSCALL_LIST_DIR_MAX) {
        capped_cap = SYSCALL_LIST_DIR_MAX;
    }
    if (capped_cap != 0U) {
        copy_bytes = capped_cap * (uint32_t)sizeof(user_entries[0]);
        if (req.entries_ptr == 0U || !uaccess_user_range_ok(req.entries_ptr, copy_bytes)) {
            return syscall_ret_err(KERR_FAULT);
        }
    }

    rc = syscall_copy_user_path_raw(req.path_ptr, req.path_len, raw_path, sizeof(raw_path));
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    rc = syscall_resolve_path_raw(raw_path, req.path_len, path, sizeof(path), &resolved_len);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    rc = vfs_list_dir(path, kernel_entries, capped_cap, &count);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    for (uint32_t i = 0; i < count; i++) {
        user_entries[i].type = kernel_entries[i].type;
        memcpy(user_entries[i].name, kernel_entries[i].name, sizeof(user_entries[i].name));
    }
    if (count != 0U) {
        copy_bytes = count * (uint32_t)sizeof(user_entries[0]);
        rc = uaccess_copy_to_user(req.entries_ptr, user_entries, copy_bytes);
        if (rc < 0) {
            return syscall_ret_err(-rc);
        }
    }

    return count;
}

static uint32_t sys_getcmdline(struct syscall_saved_regs *regs) {
    uint32_t dst_ptr = regs->ebx;
    uint32_t dst_len = regs->ecx;
    uint32_t copied_len = 0;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    rc = sched_copy_current_task_cmdline_to_user(dst_ptr, dst_len, &copied_len);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return copied_len;
}

static uint32_t sys_gui_create(struct syscall_saved_regs *regs) {
    uint32_t req_ptr = regs->ebx;
    struct syscall_gui_create_req req;
    int window_id = -1;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    rc = uaccess_copy_from_user(&req, req_ptr, (uint32_t)sizeof(req));
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    rc = display_gui_create_window(&req, sched_current_task_pid(), &window_id);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return (uint32_t)window_id;
}

static uint32_t sys_gui_flush(struct syscall_saved_regs *regs) {
    uint32_t req_ptr = regs->ebx;
    struct syscall_gui_flush_req req;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    rc = uaccess_copy_from_user(&req, req_ptr, (uint32_t)sizeof(req));
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }

    rc = display_gui_flush_window(&req, sched_current_task_pid());
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return 0;
}

static uint32_t sys_gui_poll(struct syscall_saved_regs *regs) {
    int32_t window_id = (int32_t)regs->ebx;
    uint32_t event_ptr = regs->ecx;
    struct syscall_gui_event event;
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }
    if (event_ptr == 0U ||
        !uaccess_user_range_ok(event_ptr, (uint32_t)sizeof(event))) {
        return syscall_ret_err(KERR_FAULT);
    }

    rc = display_gui_poll_event(window_id, sched_current_task_pid(), &event);
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    if (rc == 0) {
        return 0U;
    }

    rc = uaccess_copy_to_user(event_ptr, &event, (uint32_t)sizeof(event));
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return 1U;
}

static uint32_t sys_gui_destroy(struct syscall_saved_regs *regs) {
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    rc = display_gui_destroy_window((int32_t)regs->ebx, sched_current_task_pid());
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return 0U;
}

static uint32_t sys_yield(void) {
    sched_yield();
    return 0;
}

static uint32_t sys_sleep(struct syscall_saved_regs *regs) {
    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    sched_sleep_ticks(regs->ebx);
    return 0;
}

static uint32_t sys_time_info(struct syscall_saved_regs *regs) {
    uint32_t dst_ptr = regs->ebx;
    struct syscall_time_info info;
    uint64_t ticks = timer_ticks_snapshot();
    int rc;

    if (!sched_current_task_is_user()) {
        return syscall_ret_err(KERR_NOTSUP);
    }

    info.ticks_lo = (uint32_t)ticks;
    info.ticks_hi = (uint32_t)(ticks >> 32);
    info.hz = timer_frequency_hz();

    rc = uaccess_copy_to_user(dst_ptr, &info, (uint32_t)sizeof(info));
    if (rc < 0) {
        return syscall_ret_err(-rc);
    }
    return 0;
}

static uint32_t sys_time(void) {
    return (uint32_t)timer_ticks_snapshot();
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
        case SYS_SPAWN_EX:
            return sys_spawn_ex(regs);
        case SYS_OPEN:
            return sys_open(regs);
        case SYS_CLOSE:
            return sys_close(regs);
        case SYS_WAITPID:
            return sys_waitpid(regs);
        case SYS_TASK_SNAPSHOT:
            return sys_task_snapshot(regs);
        case SYS_SLEEP:
            return sys_sleep(regs);
        case SYS_TIME_INFO:
            return sys_time_info(regs);
        case SYS_GETCMDLINE:
            return sys_getcmdline(regs);
        case SYS_LIST_DIR:
            return sys_list_dir(regs);
        case SYS_CHDIR:
            return sys_chdir(regs);
        case SYS_GETCWD:
            return sys_getcwd(regs);
        case SYS_PIPE:
            return sys_pipe(regs);
        case SYS_DUP:
            return sys_dup(regs);
        case SYS_DUP2:
            return sys_dup2(regs);
        case SYS_UNLINK:
            return sys_unlink(regs);
        case SYS_FORK:
            return sys_fork(regs);
        case SYS_GUI_CREATE:
            return sys_gui_create(regs);
        case SYS_GUI_FLUSH:
            return sys_gui_flush(regs);
        case SYS_GUI_POLL:
            return sys_gui_poll(regs);
        case SYS_GUI_DESTROY:
            return sys_gui_destroy(regs);
        default:
            KLOGW("syscall: unknown nr=%u ebx=%x ecx=%x edx=%x",
                  regs->eax, regs->ebx, regs->ecx, regs->edx);
            return syscall_ret_err(KERR_NOTSUP);
    }
}
