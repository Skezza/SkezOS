#ifndef USERLIB_H
#define USERLIB_H

#include <stdint.h>

#include "../kernel/syscall_abi.h"

enum {
    USER_FD_STDIN = 0,
    USER_FD_STDOUT = 1,
    USER_FD_STDERR = 2,
};

static inline int32_t user_syscall0(uint32_t nr) {
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "a"(nr)
        : "memory");
    return ret;
}

static inline int32_t user_syscall1(uint32_t nr, uint32_t arg0) {
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "a"(nr), "b"(arg0)
        : "memory");
    return ret;
}

static inline int32_t user_syscall2(uint32_t nr, uint32_t arg0, uint32_t arg1) {
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "a"(nr), "b"(arg0), "c"(arg1)
        : "memory");
    return ret;
}

static inline int32_t user_syscall3(uint32_t nr, uint32_t arg0, uint32_t arg1, uint32_t arg2) {
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "a"(nr), "b"(arg0), "c"(arg1), "d"(arg2)
        : "memory");
    return ret;
}

static inline int32_t user_read(uint32_t fd, void *buf, uint32_t len) {
    return user_syscall3(SYS_READ, fd, (uint32_t)(uintptr_t)buf, len);
}

static inline int32_t user_write(uint32_t fd, const void *buf, uint32_t len) {
    return user_syscall3(SYS_WRITE, fd, (uint32_t)(uintptr_t)buf, len);
}

static inline int32_t user_spawn(const char *path, uint32_t path_len) {
    return user_syscall2(SYS_SPAWN, (uint32_t)(uintptr_t)path, path_len);
}

static inline int32_t user_spawn_ex(const char *path,
                                    uint32_t path_len,
                                    const char *cmdline,
                                    uint32_t cmdline_len) {
    struct syscall_spawn_ex_req req;

    req.path_ptr = (uint32_t)(uintptr_t)path;
    req.path_len = path_len;
    req.cmdline_ptr = cmdline_len == 0U ? 0U : (uint32_t)(uintptr_t)cmdline;
    req.cmdline_len = cmdline_len;
    return user_syscall1(SYS_SPAWN_EX, (uint32_t)(uintptr_t)&req);
}

static inline int32_t user_open(const char *path, uint32_t path_len, uint32_t flags) {
    return user_syscall3(SYS_OPEN, (uint32_t)(uintptr_t)path, path_len, flags);
}

static inline int32_t user_close(uint32_t fd) {
    return user_syscall1(SYS_CLOSE, fd);
}

static inline int32_t user_waitpid(int32_t pid, int32_t *status, uint32_t options) {
    return user_syscall3(SYS_WAITPID,
                         (uint32_t)pid,
                         (uint32_t)(uintptr_t)status,
                         options);
}

static inline int32_t user_getcmdline(char *buf, uint32_t buf_len) {
    return user_syscall2(SYS_GETCMDLINE, (uint32_t)(uintptr_t)buf, buf_len);
}

static inline void user_yield(void) {
    (void)user_syscall0(SYS_YIELD);
}

static inline __attribute__((noreturn)) void user_exit(int32_t code) {
    (void)user_syscall1(SYS_EXIT, (uint32_t)code);
    for (;;) {
        __asm__ __volatile__("nop");
    }
}

static inline uint32_t user_strlen(const char *s) {
    uint32_t len = 0;
    while (s && s[len] != '\0') {
        len++;
    }
    return len;
}

static inline int user_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static inline int user_str_eq_n(const char *lhs, const char *rhs, uint32_t len) {
    uint32_t i;

    if (!lhs || !rhs) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (lhs[i] == '\0' || rhs[i] == '\0' || lhs[i] != rhs[i]) {
            return 0;
        }
    }
    return rhs[len] == '\0';
}

static inline int user_str_has_suffix_n(const char *s, uint32_t len, const char *suffix) {
    uint32_t suffix_len = user_strlen(suffix);
    uint32_t i;

    if (!s || !suffix || len < suffix_len) {
        return 0;
    }
    for (i = 0; i < suffix_len; i++) {
        if (s[len - suffix_len + i] != suffix[i]) {
            return 0;
        }
    }
    return 1;
}

static inline void user_memcpy(void *dst, const void *src, uint32_t len) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    uint32_t i;

    for (i = 0; i < len; i++) {
        out[i] = in[i];
    }
}

#endif /* USERLIB_H */
