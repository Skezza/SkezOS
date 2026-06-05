#include <stdint.h>

#include "guilib.h"

static const char kConsolePath[] = "/bin/gui_console.elf";
static const char kDemoPath[] = "/bin/gui_demo.elf";
static const char kBootMarker[] = "GUI: SESSION READY\n";

static void gui_session_write_all(const char *buf, uint32_t len) {
    while (len > 0U) {
        int32_t rc = user_write(USER_FD_STDOUT, buf, len);
        if (rc <= 0) {
            return;
        }
        buf += (uint32_t)rc;
        len -= (uint32_t)rc;
    }
}

void _start(int argc, char **argv) {
    int32_t console_pid;
    int32_t demo_pid;

    (void)argc;
    (void)argv;

    console_pid = user_spawn(kConsolePath, (uint32_t)(sizeof(kConsolePath) - 1U));
    demo_pid = user_spawn(kDemoPath, (uint32_t)(sizeof(kDemoPath) - 1U));
    gui_session_write_all(kBootMarker, (uint32_t)(sizeof(kBootMarker) - 1U));

    if (console_pid > 0) {
        (void)user_waitpid(console_pid, 0, 0U);
    }
    if (demo_pid > 0) {
        (void)user_waitpid(demo_pid, 0, 0U);
    }
    user_exit(0);
}
