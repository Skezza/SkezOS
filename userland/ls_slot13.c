#include <stdint.h>

#include "userlib.h"

#define LS_MAX_ENTRIES 16U

static const char kDotPath[] = ".";
static const char kListFail[] = "ls: list failed\n";
static struct syscall_dir_entry g_ls_entries[LS_MAX_ENTRIES];

static const char *ls_resolve_path(int argc, char **argv, uint32_t *out_len) {
    if (!out_len) {
        return 0;
    }
    if (argc <= 1 || !argv[1] || argv[1][0] == '\0') {
        *out_len = (uint32_t)(sizeof(kDotPath) - 1U);
        return kDotPath;
    }
    *out_len = user_strlen(argv[1]);
    return argv[1];
}

void _start(int argc, char **argv) {
    uint32_t path_len = 0U;
    const char *path = ls_resolve_path(argc, argv, &path_len);
    int32_t count;

    if (!path || path_len == 0U) {
        user_write(USER_FD_STDERR, kListFail, (uint32_t)(sizeof(kListFail) - 1U));
        user_exit(1);
    }

    count = user_list_dir(path, path_len, g_ls_entries, LS_MAX_ENTRIES);
    if (count < 0) {
        user_write(USER_FD_STDERR, kListFail, (uint32_t)(sizeof(kListFail) - 1U));
        user_exit(1);
    }

    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        user_write(USER_FD_STDOUT, g_ls_entries[i].name, user_strlen(g_ls_entries[i].name));
        if (g_ls_entries[i].type == SYSCALL_NODE_TYPE_DIR) {
            user_write(USER_FD_STDOUT, "/", 1U);
        }
        user_write(USER_FD_STDOUT, "\n", 1U);
    }

    user_exit(0);
}
