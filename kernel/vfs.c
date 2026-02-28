#include "vfs.h"

#include <stdint.h>

#include "kassert.h"
#include "kerrno.h"
#include "kfile.h"
#include "keyboard.h"
#include "klog.h"
#include "sched.h"
#include "serial.h"
#include "utils.h"
#include "vga.h"

#define VFS_BOOTSTRAP_ROOT_MAX_CHILDREN 8U

struct vfs_static_dir_entry {
    const char *name;
    struct vfs_node *node;
};

struct vfs_static_dir {
    const struct vfs_static_dir_entry *entries;
    uint32_t count;
};

static struct vfs_node *g_vfs_root;
static vfs_console_input_owner_t g_vfs_console_input_owner;
static int g_vfs_console_input_owner_pid;
static int g_vfs_console_write_locked;

static int vfs_static_dir_lookup(struct vfs_node *dir,
                                 const char *name,
                                 uint32_t name_len,
                                 struct vfs_node **out_node);
static int vfs_console_open(struct vfs_node *node, uint32_t open_flags, struct kfile *out_file);
static int vfs_null_open(struct vfs_node *node, uint32_t open_flags, struct kfile *out_file);
static void vfs_console_write_lock(void);
static void vfs_console_write_unlock(void);

static const struct vfs_node_ops g_vfs_static_dir_ops = {
    .lookup = vfs_static_dir_lookup,
    .open = 0,
};

static const struct vfs_node_ops g_vfs_console_node_ops = {
    .lookup = 0,
    .open = vfs_console_open,
};

static const struct vfs_node_ops g_vfs_null_node_ops = {
    .lookup = 0,
    .open = vfs_null_open,
};

static struct vfs_node g_vfs_root_node;
static struct vfs_node g_vfs_dev_node;
static struct vfs_node g_vfs_console_node;
static struct vfs_node g_vfs_null_node;

static struct vfs_static_dir_entry g_vfs_root_entries[VFS_BOOTSTRAP_ROOT_MAX_CHILDREN];

static const struct vfs_static_dir_entry g_vfs_dev_entries[] = {
    { "console", &g_vfs_console_node },
    { "null", &g_vfs_null_node },
};

static struct vfs_static_dir g_vfs_root_dir = {
    .entries = g_vfs_root_entries,
    .count = 0,
};

static const struct vfs_static_dir g_vfs_dev_dir = {
    .entries = g_vfs_dev_entries,
    .count = sizeof(g_vfs_dev_entries) / sizeof(g_vfs_dev_entries[0]),
};

static struct vfs_node g_vfs_root_node = {
    .name = "/",
    .type = VFS_NODE_DIR,
    .ops = &g_vfs_static_dir_ops,
    .backend_private = (void *)(uintptr_t)&g_vfs_root_dir,
};

static struct vfs_node g_vfs_dev_node = {
    .name = "dev",
    .type = VFS_NODE_DIR,
    .ops = &g_vfs_static_dir_ops,
    .backend_private = (void *)(uintptr_t)&g_vfs_dev_dir,
};

static struct vfs_node g_vfs_console_node = {
    .name = "console",
    .type = VFS_NODE_CHARDEV,
    .ops = &g_vfs_console_node_ops,
    .backend_private = 0,
};

static struct vfs_node g_vfs_null_node = {
    .name = "null",
    .type = VFS_NODE_CHARDEV,
    .ops = &g_vfs_null_node_ops,
    .backend_private = 0,
};

static int vfs_name_eq(const char *lit, const char *name, uint32_t name_len) {
    uint32_t i;

    if (!lit || !name) {
        return 0;
    }
    for (i = 0; i < name_len; i++) {
        if (lit[i] == '\0' || lit[i] != name[i]) {
            return 0;
        }
    }
    return lit[name_len] == '\0';
}

static int vfs_component_lookup(struct vfs_node *dir,
                                const char *name,
                                uint32_t name_len,
                                struct vfs_node **out_node) {
    if (!dir || !name || !out_node) {
        return -KERR_INVAL;
    }
    *out_node = 0;
    if (!dir->ops || !dir->ops->lookup) {
        return -KERR_NOTSUP;
    }
    return dir->ops->lookup(dir, name, name_len, out_node);
}

static int vfs_static_dir_lookup(struct vfs_node *dir,
                                 const char *name,
                                 uint32_t name_len,
                                 struct vfs_node **out_node) {
    const struct vfs_static_dir *table;

    if (!dir || !name || !out_node || name_len == 0U) {
        return -KERR_INVAL;
    }
    *out_node = 0;
    table = (const struct vfs_static_dir *)(uintptr_t)dir->backend_private;
    if (!table) {
        return -KERR_NOTSUP;
    }

    for (uint32_t i = 0; i < table->count; i++) {
        const struct vfs_static_dir_entry *entry = &table->entries[i];
        if (vfs_name_eq(entry->name, name, name_len)) {
            *out_node = entry->node;
            return 0;
        }
    }
    return -KERR_NOENT;
}

static int vfs_devnull_read(struct kfile *file, void *buf, uint32_t len, uint32_t *out_read) {
    (void)file;
    (void)buf;
    (void)len;
    if (out_read) {
        *out_read = 0;
    }
    return 0;
}

static int vfs_devnull_write(struct kfile *file, const void *buf, uint32_t len, uint32_t *out_written) {
    (void)file;
    (void)buf;
    if (out_written) {
        *out_written = len;
    }
    return 0;
}

static int vfs_console_read(struct kfile *file, void *buf, uint32_t len, uint32_t *out_read) {
    char *dst = (char *)buf;
    uint32_t n = 0;

    (void)file;
    if (out_read) {
        *out_read = 0;
    }
    if (!buf && len != 0U) {
        return -KERR_INVAL;
    }
    if (g_vfs_console_input_owner == VFS_CONSOLE_INPUT_OWNER_USER_TASK &&
        sched_current_task_pid() != g_vfs_console_input_owner_pid) {
        return 0;
    }

    /* Bootstrap behavior: non-blocking read from the shared keyboard ring.
     * Returns 0 bytes when no input is available.
     */
    while (n < len) {
        int ch = vfs_console_poll_input_char();
        if (ch < 0) {
            break;
        }
        dst[n++] = (char)(uint8_t)ch;
    }
    if (out_read) {
        *out_read = n;
    }
    return 0;
}

static int vfs_console_write(struct kfile *file, const void *buf, uint32_t len, uint32_t *out_written) {
    const char *s = (const char *)buf;
    (void)file;

    vfs_console_write_lock();
    for (uint32_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\n') {
            serial_writechar('\r');
        }
        serial_writechar(c);
        vga_putc(c);
    }
    if (out_written) {
        *out_written = len;
    }
    vfs_console_write_unlock();
    return 0;
}

static void vfs_console_write_lock(void) {
    for (;;) {
        uint32_t saved_flags = vga_console_enter_critical();

        if (!g_vfs_console_write_locked) {
            g_vfs_console_write_locked = 1;
            vga_console_leave_critical(saved_flags);
            return;
        }

        vga_console_leave_critical(saved_flags);
        if (sched_current_task_pid() > 0) {
            sched_yield();
        }
    }
}

static void vfs_console_write_unlock(void) {
    uint32_t saved_flags = vga_console_enter_critical();

    g_vfs_console_write_locked = 0;
    vga_console_leave_critical(saved_flags);
}

static const struct kfile_ops g_vfs_devnull_file_ops = {
    .read = vfs_devnull_read,
    .write = vfs_devnull_write,
    .close = 0,
};

static const struct kfile_ops g_vfs_console_file_ops = {
    .read = vfs_console_read,
    .write = vfs_console_write,
    .close = 0,
};

static int vfs_console_open(struct vfs_node *node, uint32_t open_flags, struct kfile *out_file) {
    if (!node || !out_file) {
        return -KERR_INVAL;
    }
    kfile_init(out_file, node, &g_vfs_console_file_ops, 0, open_flags);
    return 0;
}

static int vfs_null_open(struct vfs_node *node, uint32_t open_flags, struct kfile *out_file) {
    if (!node || !out_file) {
        return -KERR_INVAL;
    }
    kfile_init(out_file, node, &g_vfs_devnull_file_ops, 0, open_flags);
    return 0;
}

static void vfs_bootstrap_self_check(void) {
    struct vfs_node *node;
    struct kfile file;
    uint32_t amount = 0;
    char scratch[8];

    KASSERT(vfs_lookup("/", &node) == 0);
    KASSERT(node == &g_vfs_root_node);
    KASSERT(vfs_lookup("/dev", &node) == 0);
    KASSERT(node == &g_vfs_dev_node);
    KASSERT(vfs_lookup("/dev/null", &node) == 0);
    KASSERT(node == &g_vfs_null_node);
    KASSERT(vfs_lookup("/dev/console", &node) == 0);
    KASSERT(node == &g_vfs_console_node);
    KASSERT(vfs_lookup("/dev/missing", &node) == -KERR_NOENT);

    KASSERT(vfs_open("/dev/null", 0, &file) == 0);
    amount = 0;
    KASSERT(kfile_write(&file, "abc", 3, &amount) == 0);
    KASSERT(amount == 3U);
    amount = 123U;
    KASSERT(kfile_read(&file, scratch, sizeof(scratch), &amount) == 0);
    KASSERT(amount == 0U);
    KASSERT(kfile_close(&file) == 0);

    KASSERT(vfs_open("/dev/console", 0, &file) == 0);
    amount = 77U;
    KASSERT(kfile_read(&file, scratch, 0, &amount) == 0);
    KASSERT(amount == 0U);
    KASSERT(kfile_close(&file) == 0);

    KLOGI("vfs: bootstrap self-check pass (/dev, /dev/null, /dev/console)");
}

int vfs_register_root_child(const char *name, struct vfs_node *node) {
    uint32_t i;

    if (!name || !node) {
        return -KERR_INVAL;
    }
    if (name[0] == '\0') {
        return -KERR_INVAL;
    }

    for (i = 0; i < g_vfs_root_dir.count; i++) {
        struct vfs_static_dir_entry *entry = &g_vfs_root_entries[i];
        uint32_t name_len = (uint32_t)strlen(name);
        if (vfs_name_eq(entry->name, name, name_len)) {
            return -KERR_INVAL;
        }
    }
    if (g_vfs_root_dir.count >= VFS_BOOTSTRAP_ROOT_MAX_CHILDREN) {
        return -KERR_NOMEM;
    }

    g_vfs_root_entries[g_vfs_root_dir.count].name = name;
    g_vfs_root_entries[g_vfs_root_dir.count].node = node;
    g_vfs_root_dir.count++;
    return 0;
}

int vfs_set_root(struct vfs_node *root) {
    if (!root) {
        return -KERR_INVAL;
    }
    if (root->type != VFS_NODE_DIR) {
        return -KERR_INVAL;
    }
    g_vfs_root = root;
    KLOGI("vfs: root mounted name=%s", root->name ? root->name : "/");
    return 0;
}

struct vfs_node *vfs_get_root(void) {
    return g_vfs_root;
}

void vfs_init(void) {
    g_vfs_root = 0;
    g_vfs_console_input_owner = VFS_CONSOLE_INPUT_OWNER_KERNEL;
    g_vfs_console_input_owner_pid = -1;
    g_vfs_console_write_locked = 0;
    memset(g_vfs_root_entries, 0, sizeof(g_vfs_root_entries));
    g_vfs_root_dir.count = 0;
    KASSERT(vfs_set_root(&g_vfs_root_node) == 0);
    KASSERT(vfs_register_root_child("dev", &g_vfs_dev_node) == 0);
    vfs_bootstrap_self_check();
}

int vfs_lookup(const char *path, struct vfs_node **out_node) {
    const char *p;
    struct vfs_node *current;

    if (!path || !out_node) {
        return -KERR_INVAL;
    }
    *out_node = 0;
    if (path[0] != '/') {
        return -KERR_INVAL;
    }
    if (!g_vfs_root) {
        return -KERR_NOTSUP;
    }

    current = g_vfs_root;
    p = path + 1U;
    while (*p == '/') {
        p++;
    }
    if (*p == '\0') {
        *out_node = current;
        return 0;
    }

    for (;;) {
        const char *start = p;
        uint32_t name_len = 0;
        struct vfs_node *next;
        int rc;

        while (*p != '\0' && *p != '/') {
            p++;
            name_len++;
        }
        if (name_len == 0U) {
            return -KERR_INVAL;
        }

        rc = vfs_component_lookup(current, start, name_len, &next);
        if (rc < 0) {
            return rc;
        }
        if (!next) {
            return -KERR_FAULT;
        }
        current = next;

        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            *out_node = current;
            return 0;
        }
    }
}

int vfs_open(const char *path, uint32_t open_flags, struct kfile *out_file) {
    struct vfs_node *node;
    int rc;

    if (!out_file) {
        return -KERR_INVAL;
    }

    kfile_reset(out_file);
    rc = vfs_lookup(path, &node);
    if (rc < 0) {
        return rc;
    }
    if (!node || !node->ops || !node->ops->open) {
        return -KERR_NOTSUP;
    }
    return node->ops->open(node, open_flags, out_file);
}

void vfs_console_set_input_owner_kernel(void) {
    g_vfs_console_input_owner = VFS_CONSOLE_INPUT_OWNER_KERNEL;
    g_vfs_console_input_owner_pid = -1;
    KLOGI("vfs: console input owner=kernel");
}

int vfs_console_set_input_owner_task(int pid) {
    if (pid <= 0) {
        return -KERR_INVAL;
    }

    g_vfs_console_input_owner = VFS_CONSOLE_INPUT_OWNER_USER_TASK;
    g_vfs_console_input_owner_pid = pid;
    KLOGI("vfs: console input owner=pid=%d", pid);
    return 0;
}

vfs_console_input_owner_t vfs_console_get_input_owner(void) {
    return g_vfs_console_input_owner;
}

int vfs_console_get_input_owner_pid(void) {
    if (g_vfs_console_input_owner != VFS_CONSOLE_INPUT_OWNER_USER_TASK) {
        return -1;
    }
    return g_vfs_console_input_owner_pid;
}

int vfs_console_input_owner_is_task(int pid) {
    if (pid <= 0) {
        return 0;
    }
    return g_vfs_console_input_owner == VFS_CONSOLE_INPUT_OWNER_USER_TASK &&
           g_vfs_console_input_owner_pid == pid;
}

int vfs_console_poll_input_char(void) {
    int ch = kbd_getchar();
    int serial_ch;
    if (ch >= 0) {
        return ch;
    }

    serial_ch = serial_readchar();
    if (serial_ch < 0) {
        return -1;
    }

    kbd_feed_ascii((char)serial_ch);
    ch = kbd_getchar();
    if (ch >= 0) {
        return ch;
    }
    return serial_ch;
}
