#include <stdint.h>

#include "userlib.h"

static const char kPrefix[] = "uptime: ";
static const char kUp[] = "up=";
static const char kTicks[] = " ticks=";
static const char kTicksSep[] = ":";
static const char kHz[] = " hz=";
static const char kQueryFail[] = "uptime: time query failed\n";
static const char kNewline[] = "\n";

static void uptime_write_all(uint32_t fd, const char *buf, uint32_t len) {
    while (len > 0U) {
        int32_t rc = user_write(fd, buf, len);
        if (rc <= 0) {
            return;
        }
        buf += (uint32_t)rc;
        len -= (uint32_t)rc;
    }
}

static void uptime_write_str(uint32_t fd, const char *s) {
    uptime_write_all(fd, s, user_strlen(s));
}

static void uptime_write_u32(uint32_t fd, uint32_t value) {
    char buf[10];
    uint32_t len = 0;

    if (value == 0U) {
        uptime_write_all(fd, "0", 1U);
        return;
    }

    while (value != 0U && len < sizeof(buf)) {
        buf[len++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (len > 0U) {
        uptime_write_all(fd, &buf[--len], 1U);
    }
}

void _start(int argc, char **argv) {
    struct syscall_time_info info;

    (void)argc;
    (void)argv;

    if (user_time_info(&info) < 0) {
        uptime_write_all(USER_FD_STDERR, kQueryFail, (uint32_t)(sizeof(kQueryFail) - 1U));
        user_exit(1);
    }

    uptime_write_str(USER_FD_STDOUT, kPrefix);
    if (info.hz != 0U && info.ticks_hi == 0U) {
        uptime_write_str(USER_FD_STDOUT, kUp);
        uptime_write_u32(USER_FD_STDOUT, info.ticks_lo / info.hz);
        uptime_write_str(USER_FD_STDOUT, "s");
    } else {
        uptime_write_str(USER_FD_STDOUT, kTicks);
        uptime_write_u32(USER_FD_STDOUT, info.ticks_hi);
        uptime_write_str(USER_FD_STDOUT, kTicksSep);
        uptime_write_u32(USER_FD_STDOUT, info.ticks_lo);
    }
    uptime_write_str(USER_FD_STDOUT, kHz);
    uptime_write_u32(USER_FD_STDOUT, info.hz);
    uptime_write_all(USER_FD_STDOUT, kNewline, (uint32_t)(sizeof(kNewline) - 1U));
    user_exit(0);
}
