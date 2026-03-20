#include <stdint.h>

#include "userlib.h"

static volatile int g_static_probe = 7;

static void ft_write_all(const char *buf, uint32_t len) {
    while (len > 0U) {
        int32_t rc = user_write(USER_FD_STDOUT, buf, len);
        if (rc <= 0) {
            return;
        }
        buf += (uint32_t)rc;
        len -= (uint32_t)rc;
    }
}

static void ft_write_str(const char *s) {
    ft_write_all(s, user_strlen(s));
}

static void ft_fail(const char *reason) {
    ft_write_str(reason);
    ft_write_str("\n");
    user_exit(1);
}

static uint32_t ft_parse_u32(const char *s, int *ok) {
    uint32_t value = 0U;
    uint32_t i = 0U;

    if (ok) {
        *ok = 0;
    }
    if (!s || s[0] == '\0') {
        return 0U;
    }

    while (s[i] != '\0') {
        char c = s[i];
        if (c < '0' || c > '9') {
            return 0U;
        }
        value = value * 10U + (uint32_t)(c - '0');
        i++;
    }

    if (ok) {
        *ok = 1;
    }
    return value;
}

static void ft_run_pressure(void) {
    int32_t children[24];
    uint32_t child_count = 0U;

    for (;;) {
        int32_t pid = user_fork();
        if (pid < 0) {
            break;
        }
        if (pid == 0) {
            user_sleep_ticks(120U);
            user_exit(0);
        }
        if (child_count >= (uint32_t)(sizeof(children) / sizeof(children[0]))) {
            ft_fail("forktest: pressure child cap exceeded");
        }
        children[child_count++] = pid;
    }

    if (child_count == 0U) {
        ft_fail("forktest: pressure expected fork failure");
    }
    ft_write_str("forktest: pressure fork failure ok\n");

    for (uint32_t i = 0U; i < child_count; i++) {
        int32_t status = -1;
        int32_t waited = user_waitpid(children[i], &status, 0U);
        if (waited != children[i] || status != 0) {
            ft_fail("forktest: pressure wait failed");
        }
    }
    ft_write_str("forktest: pressure reap ok\n");
    ft_write_str("forktest: pressure PASS\n");
}

static void ft_run_once(void) {
    volatile int stack_probe = 11;
    static const char kSleepPath[] = "/bin/sleep.elf";
    static const char kSleepTicksArg[] = "80";
    int32_t pid;
    int32_t waited;
    int32_t sleeper_a;
    int32_t sleeper_b;
    int32_t sleeper_c;
    int32_t status = -1;
    g_static_probe = 7;

    pid = user_fork();
    if (pid < 0) {
        ft_fail("forktest: fork failed");
    }

    if (pid == 0) {
        g_static_probe = 42;
        stack_probe = 99;
        if (g_static_probe != 42 || stack_probe != 99) {
            ft_fail("forktest: child probe write failed");
        }
        ft_write_str("forktest: child return 0 ok\n");
        user_exit(13);
    }

    ft_write_str("forktest: parent return child_pid ok\n");
    waited = user_waitpid(pid, &status, 0U);
    if (waited != pid || status != 13) {
        ft_fail("forktest: waitpid status bad");
    }

    if (g_static_probe != 7) {
        ft_fail("forktest: cow static bad");
    }
    ft_write_str("forktest: cow static ok\n");

    if (stack_probe != 11) {
        ft_fail("forktest: cow stack bad");
    }
    ft_write_str("forktest: cow stack ok\n");

    sleeper_a = user_spawn_ex(kSleepPath,
                              (uint32_t)(sizeof(kSleepPath) - 1U),
                              kSleepTicksArg,
                              (uint32_t)(sizeof(kSleepTicksArg) - 1U));
    if (sleeper_a <= 0) {
        ft_fail("forktest: slot pressure spawn a failed");
    }
    sleeper_b = user_spawn_ex(kSleepPath,
                              (uint32_t)(sizeof(kSleepPath) - 1U),
                              kSleepTicksArg,
                              (uint32_t)(sizeof(kSleepTicksArg) - 1U));
    if (sleeper_b <= 0) {
        ft_fail("forktest: slot pressure spawn b failed");
    }
    sleeper_c = user_spawn_ex(kSleepPath,
                              (uint32_t)(sizeof(kSleepPath) - 1U),
                              kSleepTicksArg,
                              (uint32_t)(sizeof(kSleepTicksArg) - 1U));
    if (sleeper_c >= 0) {
        ft_fail("forktest: slot pressure expected saturation");
    }
    waited = user_waitpid(sleeper_a, &status, 0U);
    if (waited != sleeper_a || status != 0) {
        ft_fail("forktest: slot pressure wait a failed");
    }
    waited = user_waitpid(sleeper_b, &status, 0U);
    if (waited != sleeper_b || status != 0) {
        ft_fail("forktest: slot pressure wait b failed");
    }
    ft_write_str("forktest: slot pressure ok\n");

    ft_write_str("forktest: waitpid status ok\n");
    ft_write_str("forktest: PASS\n");
}

void _start(int argc, char **argv) {
    uint32_t loops = 1U;

    if (argc >= 2) {
        if (user_str_eq_n(argv[1], "pressure", 8U) && argv[1][8] == '\0') {
            ft_run_pressure();
            user_exit(0);
        }
        int ok = 0;
        uint32_t parsed = ft_parse_u32(argv[1], &ok);
        if (!ok || parsed == 0U || parsed > 16U) {
            ft_fail("forktest: usage forktest [1..16|pressure]");
        }
        loops = parsed;
    }

    for (uint32_t i = 0; i < loops; i++) {
        ft_run_once();
    }
    user_exit(0);
}
