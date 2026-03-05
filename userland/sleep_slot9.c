#include <stdint.h>

#include "userlib.h"

static const char kPrefix[] = "sleep: requested=";
static const char kObserved[] = " observed=";
static const char kTicksSuffix[] = " ticks";
static const char kInvalidTicks[] = "sleep: invalid ticks\n";
static const char kTimeFail[] = "sleep: time query failed\n";
static const char kNewline[] = "\n";

static void sleep_write_all(uint32_t fd, const char *buf, uint32_t len) {
    while (len > 0U) {
        int32_t rc = user_write(fd, buf, len);
        if (rc <= 0) {
            return;
        }
        buf += (uint32_t)rc;
        len -= (uint32_t)rc;
    }
}

static void sleep_write_str(uint32_t fd, const char *s) {
    sleep_write_all(fd, s, user_strlen(s));
}

static void sleep_write_u32(uint32_t fd, uint32_t value) {
    char buf[10];
    uint32_t len = 0;

    if (value == 0U) {
        sleep_write_all(fd, "0", 1U);
        return;
    }

    while (value != 0U && len < sizeof(buf)) {
        buf[len++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (len > 0U) {
        sleep_write_all(fd, &buf[--len], 1U);
    }
}

static int sleep_parse_ticks(const char *s, uint32_t *out_ticks) {
    uint32_t value = 0;
    uint32_t idx = 0;

    if (!s || !out_ticks || s[0] == '\0') {
        return -1;
    }

    while (s[idx] != '\0') {
        uint32_t digit;

        if (s[idx] < '0' || s[idx] > '9') {
            return -1;
        }
        digit = (uint32_t)(s[idx] - '0');
        if (value > 429496729U || (value == 429496729U && digit > 5U)) {
            return -1;
        }
        value = (value * 10U) + digit;
        idx++;
    }

    *out_ticks = value;
    return 0;
}

void _start(int argc, char **argv) {
    struct syscall_time_info before;
    struct syscall_time_info after;
    uint32_t ticks = 1U;
    uint64_t before_ticks;
    uint64_t after_ticks;
    uint32_t elapsed;

    if (argc > 1) {
        if (!argv[1] || sleep_parse_ticks(argv[1], &ticks) < 0) {
            sleep_write_all(USER_FD_STDERR, kInvalidTicks, (uint32_t)(sizeof(kInvalidTicks) - 1U));
            user_exit(1);
        }
    }

    if (user_time_info(&before) < 0) {
        sleep_write_all(USER_FD_STDERR, kTimeFail, (uint32_t)(sizeof(kTimeFail) - 1U));
        user_exit(1);
    }

    user_sleep_ticks(ticks);

    if (user_time_info(&after) < 0) {
        sleep_write_all(USER_FD_STDERR, kTimeFail, (uint32_t)(sizeof(kTimeFail) - 1U));
        user_exit(1);
    }

    before_ticks = ((uint64_t)before.ticks_hi << 32) | (uint64_t)before.ticks_lo;
    after_ticks = ((uint64_t)after.ticks_hi << 32) | (uint64_t)after.ticks_lo;
    elapsed = (uint32_t)(after_ticks - before_ticks);

    sleep_write_str(USER_FD_STDOUT, kPrefix);
    sleep_write_u32(USER_FD_STDOUT, ticks);
    sleep_write_str(USER_FD_STDOUT, kObserved);
    sleep_write_u32(USER_FD_STDOUT, elapsed);
    sleep_write_str(USER_FD_STDOUT, kTicksSuffix);
    sleep_write_all(USER_FD_STDOUT, kNewline, (uint32_t)(sizeof(kNewline) - 1U));
    user_exit(0);
}
