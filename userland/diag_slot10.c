#include <stdint.h>

#include "userlib.h"

static const char kPass[] = "diag: PASS\n";
static const char kFail[] = "diag: FAIL\n";
static const char kCheckWrite[] = "diag: write bad ptr";
static const char kCheckOpen[] = "diag: open empty path";
static const char kCheckSpawnEx[] = "diag: spawn_ex bad req";
static const char kCheckUnknown[] = "diag: unknown syscall";
static const char kCheckPipePtr[] = "diag: pipe bad ptr";
static const char kCheckDupBadFd[] = "diag: dup invalid fd";
static const char kCheckDup2BadTarget[] = "diag: dup2 invalid target fd";
static const char kCheckPipeEof[] = "diag: pipe eof after writer close";
static const char kCheckPipeBroken[] = "diag: pipe broken write";
static const char kCheckReadBadFd[] = "diag: read invalid fd";
static const char kCheckCloseBadFd[] = "diag: close invalid fd";
static const char kCheckOpenMissing[] = "diag: open missing path";
static const char kCheckFdReadCloseFlow[] = "diag: fd open/read/close flow";
static const char kCheckTimeInfoBadPtr[] = "diag: time_info bad ptr";
static const char kCheckTimeInfoOk[] = "diag: time_info valid";
static const char kCheckWaitpidNohangPending[] = "diag: waitpid nohang pending";
static const char kCheckWaitpidNohangNoChild[] = "diag: waitpid nohang no-child";
static const char kCheckTaskSnapshotBadPtr[] = "diag: task_snapshot bad ptr";
static const char kCheckTaskSnapshotZeroCap[] = "diag: task_snapshot zero cap";
static const char kCheckTaskSnapshotOne[] = "diag: task_snapshot cap one";
static const char kCheckListDirBadReq[] = "diag: list_dir bad req";
static const char kCheckListDirZeroCap[] = "diag: list_dir root zero cap";
static const char kCheckListDirOne[] = "diag: list_dir root cap one";
static const char kCheckGetcwdBadPtr[] = "diag: getcwd bad ptr";
static const char kCheckGetcwdZeroLen[] = "diag: getcwd zero len";
static const char kCheckChdirNotDir[] = "diag: chdir file notsup";
static const char kCheckCwdRootRoundtrip[] = "diag: cwd root roundtrip";
static const char kCheckSpawnMissing[] = "diag: spawn missing path";
static const char kCheckSpawnExBadFlags[] = "diag: spawn_ex bad flags";
static const char kCheckSpawnWaitHello[] = "diag: spawn/wait hello";
static const char kCheckWaitReapAgain[] = "diag: waitpid reap again";
static const char kCheckWaitNonChild[] = "diag: waitpid non-child";
static const char kOk[] = " ok\n";
static const char kBad[] = " bad rc=";

static void diag_write_all(uint32_t fd, const char *buf, uint32_t len) {
    while (len > 0U) {
        int32_t rc = user_write(fd, buf, len);
        if (rc <= 0) {
            return;
        }
        buf += (uint32_t)rc;
        len -= (uint32_t)rc;
    }
}

static void diag_write_str(uint32_t fd, const char *s) {
    diag_write_all(fd, s, user_strlen(s));
}

static void diag_write_u32(uint32_t fd, uint32_t value) {
    char buf[10];
    uint32_t len = 0;

    if (value == 0U) {
        diag_write_all(fd, "0", 1U);
        return;
    }
    while (value != 0U && len < sizeof(buf)) {
        buf[len++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (len > 0U) {
        diag_write_all(fd, &buf[--len], 1U);
    }
}

static void diag_write_i32(uint32_t fd, int32_t value) {
    uint32_t magnitude;

    if (value < 0) {
        diag_write_all(fd, "-", 1U);
        magnitude = (uint32_t)(-(value + 1)) + 1U;
    } else {
        magnitude = (uint32_t)value;
    }
    diag_write_u32(fd, magnitude);
}

static int diag_expect(const char *label, int32_t rc, int32_t expected) {
    diag_write_str(USER_FD_STDOUT, label);
    if (rc == expected) {
        diag_write_all(USER_FD_STDOUT, kOk, (uint32_t)(sizeof(kOk) - 1U));
        return 1;
    }
    diag_write_all(USER_FD_STDOUT, kBad, (uint32_t)(sizeof(kBad) - 1U));
    diag_write_i32(USER_FD_STDOUT, rc);
    diag_write_all(USER_FD_STDOUT, "\n", 1U);
    return 0;
}

static int32_t diag_pipe_eof_after_writer_close(void) {
    int32_t pipe_fds[2] = { -1, -1 };
    char byte = 0;
    int32_t rc = user_pipe(pipe_fds);

    if (rc < 0) {
        return rc;
    }
    (void)user_close((uint32_t)pipe_fds[1]);
    rc = user_read((uint32_t)pipe_fds[0], &byte, 1U);
    (void)user_close((uint32_t)pipe_fds[0]);
    return rc;
}

static int32_t diag_pipe_write_after_reader_close(void) {
    int32_t pipe_fds[2] = { -1, -1 };
    char byte = 'x';
    int32_t rc = user_pipe(pipe_fds);

    if (rc < 0) {
        return rc;
    }
    (void)user_close((uint32_t)pipe_fds[0]);
    rc = user_write((uint32_t)pipe_fds[1], &byte, 1U);
    (void)user_close((uint32_t)pipe_fds[1]);
    return rc;
}

static int32_t diag_time_info_valid(void) {
    struct syscall_time_info info;
    int32_t rc = user_time_info(&info);

    if (rc < 0) {
        return rc;
    }
    if (info.hz == 0U) {
        return -22;
    }
    return 0;
}

static int32_t diag_task_snapshot_cap_one(void) {
    struct syscall_task_snapshot_entry entry;
    int32_t rc = user_task_snapshot(&entry, 1U);

    if (rc < 0) {
        return rc;
    }
    if (rc != 1) {
        return -22;
    }
    if (entry.pid <= 0) {
        return -22;
    }
    return 0;
}

static int32_t diag_list_dir_root_cap_one(void) {
    struct syscall_dir_entry entry;
    int32_t rc = user_list_dir("/", 1U, &entry, 1U);

    if (rc < 0) {
        return rc;
    }
    if (rc != 1) {
        return -22;
    }
    if (entry.name[0] == '\0') {
        return -22;
    }
    return 0;
}

static int32_t diag_cwd_root_roundtrip(void) {
    char cwd[SYSCALL_CWD_MAX];
    int32_t rc = user_chdir("/", 1U);

    if (rc < 0) {
        return rc;
    }
    rc = user_getcwd(cwd, sizeof(cwd));
    if (rc < 0) {
        return rc;
    }
    if (rc != 1 || cwd[0] != '/' || cwd[1] != '\0') {
        return -22;
    }
    return 0;
}

static int32_t diag_fd_open_read_close_flow(void) {
    char buf[4];
    int32_t fd = user_open("/bin/readme.txt", 15U, SYSCALL_OPEN_FLAG_READ);
    int32_t rc;

    if (fd < 0) {
        return fd;
    }
    rc = user_read((uint32_t)fd, buf, sizeof(buf));
    if (rc <= 0) {
        (void)user_close((uint32_t)fd);
        return rc < 0 ? rc : -22;
    }
    rc = user_close((uint32_t)fd);
    if (rc < 0) {
        return rc;
    }
    rc = user_close((uint32_t)fd);
    if (rc != -2) {
        return rc < 0 ? rc : -22;
    }
    return 0;
}

static int32_t diag_spawn_wait_hello(int32_t *out_pid) {
    int32_t status = -1;
    int32_t pid = user_spawn("/bin/hello.elf", 14U);
    int32_t waited;

    if (out_pid) {
        *out_pid = -1;
    }
    if (pid <= 0) {
        return pid < 0 ? pid : -22;
    }
    waited = user_waitpid(pid, &status, 0U);
    if (waited < 0) {
        return waited;
    }
    if (waited != pid || status != 0) {
        return -22;
    }
    if (out_pid) {
        *out_pid = pid;
    }
    return 0;
}

static int32_t diag_waitpid_nohang_pending(void) {
    int32_t pid = user_spawn_ex("/bin/sleep.elf", 14U, "200", 3U);
    int32_t status = -1;
    int32_t waited;

    if (pid <= 0) {
        return pid < 0 ? pid : -22;
    }
    waited = user_waitpid(pid, &status, SYSCALL_WAITPID_FLAG_NOHANG);
    if (waited < 0) {
        (void)user_waitpid(pid, 0, 0U);
        return waited;
    }
    if (waited != 0) {
        (void)user_waitpid(pid, 0, 0U);
        return -22;
    }
    waited = user_waitpid(pid, &status, 0U);
    if (waited != pid || status != 0) {
        return -22;
    }
    return 0;
}

void _start(int argc, char **argv) {
    int ok = 1;
    int32_t waited_pid = -1;
    struct syscall_spawn_ex_req bad_flags_req;

    (void)argc;
    (void)argv;

    if (!diag_expect(kCheckWrite,
                     user_write(USER_FD_STDOUT, (const void *)(uintptr_t)0x1U, 4U),
                     -14)) {
        ok = 0;
    }
    if (!diag_expect(kCheckOpen,
                     user_syscall3(SYS_OPEN, (uint32_t)(uintptr_t)"", 0U, 0U),
                     -22)) {
        ok = 0;
    }
    if (!diag_expect(kCheckSpawnEx,
                     user_syscall1(SYS_SPAWN_EX, 0U),
                     -14)) {
        ok = 0;
    }
    bad_flags_req.path_ptr = (uint32_t)(uintptr_t)"/bin/hello.elf";
    bad_flags_req.path_len = 14U;
    bad_flags_req.cmdline_ptr = 0U;
    bad_flags_req.cmdline_len = 0U;
    bad_flags_req.flags = (SYSCALL_SPAWN_FLAG_INHERIT_FDS | (1U << 8));
    if (!diag_expect(kCheckSpawnExBadFlags,
                     user_syscall1(SYS_SPAWN_EX, (uint32_t)(uintptr_t)&bad_flags_req),
                     -22)) {
        ok = 0;
    }
    if (!diag_expect(kCheckSpawnMissing,
                     user_spawn("/bin/nope.elf", 13U),
                     -2)) {
        ok = 0;
    }
    if (!diag_expect(kCheckSpawnWaitHello,
                     diag_spawn_wait_hello(&waited_pid),
                     0)) {
        ok = 0;
    }
    if (!diag_expect(kCheckWaitReapAgain,
                     user_waitpid(waited_pid, 0, 0U),
                     -2)) {
        ok = 0;
    }
    if (!diag_expect(kCheckWaitNonChild,
                     user_waitpid(9999, 0, 0U),
                     -2)) {
        ok = 0;
    }
    if (!diag_expect(kCheckUnknown,
                     user_syscall0(99U),
                     -95)) {
        ok = 0;
    }
    if (!diag_expect(kCheckPipePtr,
                     user_syscall1(SYS_PIPE, 1U),
                     -14)) {
        ok = 0;
    }
    if (!diag_expect(kCheckDupBadFd,
                     user_dup(99U),
                     -22)) {
        ok = 0;
    }
    if (!diag_expect(kCheckDup2BadTarget,
                     user_dup2(USER_FD_STDOUT, 99U),
                     -22)) {
        ok = 0;
    }
    if (!diag_expect(kCheckReadBadFd,
                     user_read(99U, (void *)(uintptr_t)1U, 1U),
                     -22)) {
        ok = 0;
    }
    if (!diag_expect(kCheckCloseBadFd,
                     user_close(99U),
                     -22)) {
        ok = 0;
    }
    if (!diag_expect(kCheckOpenMissing,
                     user_open("/bin/nope.txt", 13U, SYSCALL_OPEN_FLAG_READ),
                     -2)) {
        ok = 0;
    }
    if (!diag_expect(kCheckFdReadCloseFlow,
                     diag_fd_open_read_close_flow(),
                     0)) {
        ok = 0;
    }
    if (!diag_expect(kCheckPipeEof,
                     diag_pipe_eof_after_writer_close(),
                     0)) {
        ok = 0;
    }
    if (!diag_expect(kCheckPipeBroken,
                     diag_pipe_write_after_reader_close(),
                     -32)) {
        ok = 0;
    }
    if (!diag_expect(kCheckTimeInfoBadPtr,
                     user_syscall1(SYS_TIME_INFO, 0U),
                     -14)) {
        ok = 0;
    }
    if (!diag_expect(kCheckTimeInfoOk,
                     diag_time_info_valid(),
                     0)) {
        ok = 0;
    }
    if (!diag_expect(kCheckWaitpidNohangPending,
                     diag_waitpid_nohang_pending(),
                     0)) {
        ok = 0;
    }
    if (!diag_expect(kCheckWaitpidNohangNoChild,
                     user_waitpid(-1, 0, SYSCALL_WAITPID_FLAG_NOHANG),
                     -2)) {
        ok = 0;
    }
    if (!diag_expect(kCheckTaskSnapshotBadPtr,
                     user_syscall2(SYS_TASK_SNAPSHOT, 0U, 1U),
                     -14)) {
        ok = 0;
    }
    if (!diag_expect(kCheckTaskSnapshotZeroCap,
                     user_task_snapshot(0, 0U),
                     0)) {
        ok = 0;
    }
    if (!diag_expect(kCheckTaskSnapshotOne,
                     diag_task_snapshot_cap_one(),
                     0)) {
        ok = 0;
    }
    if (!diag_expect(kCheckListDirBadReq,
                     user_syscall1(SYS_LIST_DIR, 0U),
                     -14)) {
        ok = 0;
    }
    if (!diag_expect(kCheckListDirZeroCap,
                     user_list_dir("/", 1U, 0, 0U),
                     0)) {
        ok = 0;
    }
    if (!diag_expect(kCheckListDirOne,
                     diag_list_dir_root_cap_one(),
                     0)) {
        ok = 0;
    }
    if (!diag_expect(kCheckGetcwdBadPtr,
                     user_getcwd((char *)(uintptr_t)1U, 1U),
                     -14)) {
        ok = 0;
    }
    if (!diag_expect(kCheckGetcwdZeroLen,
                     user_getcwd((char *)(uintptr_t)1U, 0U),
                     0)) {
        ok = 0;
    }
    if (!diag_expect(kCheckChdirNotDir,
                     user_chdir("/bin/diag.elf", 13U),
                     -95)) {
        ok = 0;
    }
    if (!diag_expect(kCheckCwdRootRoundtrip,
                     diag_cwd_root_roundtrip(),
                     0)) {
        ok = 0;
    }

    if (ok) {
        diag_write_all(USER_FD_STDOUT, kPass, (uint32_t)(sizeof(kPass) - 1U));
        user_exit(0);
    }
    diag_write_all(USER_FD_STDOUT, kFail, (uint32_t)(sizeof(kFail) - 1U));
    user_exit(1);
}
