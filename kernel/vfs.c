#include "vfs.h"

#include <stdint.h>

#include "display.h"
#include "kassert.h"
#include "kerrno.h"
#include "kfile.h"
#include "keyboard.h"
#include "klog.h"
#include "sched.h"
#include "serial.h"
#include "syscall_abi.h"
#include "utils.h"

#define VFS_BOOTSTRAP_ROOT_MAX_CHILDREN 32U
#define VFS_ROOT_RAMFILE_MAX 16U
#define VFS_ROOT_RAMFILE_CAPACITY 4096U

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

struct vfs_root_ram_file {
    struct vfs_node node;
    char name[VFS_DIR_ENTRY_NAME_MAX];
    uint8_t data[VFS_ROOT_RAMFILE_CAPACITY];
    uint32_t size;
    int in_use;
};

static int vfs_static_dir_lookup(struct vfs_node *dir,
                                 const char *name,
                                 uint32_t name_len,
                                 struct vfs_node **out_node);
static int vfs_static_dir_list(struct vfs_node *dir,
                               struct vfs_dir_entry *entries,
                               uint32_t entry_cap,
                               uint32_t *out_count);
static int vfs_console_open(struct vfs_node *node, uint32_t open_flags, struct kfile *out_file);
static int vfs_null_open(struct vfs_node *node, uint32_t open_flags, struct kfile *out_file);
static int vfs_root_ramfile_open(struct vfs_node *node, uint32_t open_flags, struct kfile *out_file);
static int vfs_root_ramfile_read(struct kfile *file, void *buf, uint32_t len, uint32_t *out_read);
static int vfs_root_ramfile_write(struct kfile *file,
                                  const void *buf,
                                  uint32_t len,
                                  uint32_t *out_written);
static int vfs_root_create_or_open_ramfile(const char *path,
                                           uint32_t open_flags,
                                           struct kfile *out_file);
static void vfs_console_write_lock(void);
static void vfs_console_write_unlock(void);

static const struct vfs_node_ops g_vfs_static_dir_ops = {
    .lookup = vfs_static_dir_lookup,
    .list = vfs_static_dir_list,
    .open = 0,
};

static const struct vfs_node_ops g_vfs_console_node_ops = {
    .lookup = 0,
    .list = 0,
    .open = vfs_console_open,
};

static const struct vfs_node_ops g_vfs_null_node_ops = {
    .lookup = 0,
    .list = 0,
    .open = vfs_null_open,
};

static const struct vfs_node_ops g_vfs_root_ramfile_node_ops = {
    .lookup = 0,
    .list = 0,
    .open = vfs_root_ramfile_open,
};

static struct vfs_node g_vfs_root_node;
static struct vfs_node g_vfs_dev_node;
static struct vfs_node g_vfs_console_node;
static struct vfs_node g_vfs_null_node;

static struct vfs_static_dir_entry g_vfs_root_entries[VFS_BOOTSTRAP_ROOT_MAX_CHILDREN];
static struct vfs_root_ram_file g_vfs_root_ramfiles[VFS_ROOT_RAMFILE_MAX];

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

static void vfs_copy_dir_entry_name(char *dst, const char *src) {
    uint32_t i = 0;

    if (!dst) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] != '\0' && i + 1U < VFS_DIR_ENTRY_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
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

static int vfs_static_dir_list(struct vfs_node *dir,
                               struct vfs_dir_entry *entries,
                               uint32_t entry_cap,
                               uint32_t *out_count) {
    const struct vfs_static_dir *table;
    uint32_t count = 0U;

    if (!dir || !out_count) {
        return -KERR_INVAL;
    }
    *out_count = 0U;
    if (entry_cap != 0U && !entries) {
        return -KERR_INVAL;
    }
    table = (const struct vfs_static_dir *)(uintptr_t)dir->backend_private;
    if (!table) {
        return -KERR_NOTSUP;
    }

    for (uint32_t i = 0; i < table->count; i++) {
        const struct vfs_static_dir_entry *entry = &table->entries[i];

        if (count >= entry_cap) {
            break;
        }
        entries[count].type = entry->node ? (uint32_t)entry->node->type : (uint32_t)VFS_NODE_NONE;
        vfs_copy_dir_entry_name(entries[count].name, entry->name);
        count++;
    }

    *out_count = count;
    return 0;
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
    int should_block;

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

    should_block = sched_current_task_is_user() &&
                   g_vfs_console_input_owner == VFS_CONSOLE_INPUT_OWNER_USER_TASK &&
                   sched_current_task_pid() == g_vfs_console_input_owner_pid;

    /* The console owner now gets blocking reads from the shared input path.
     * Keep returning short reads after the first byte so existing callers
     * still receive whatever is already buffered.
     */
    while (n < len) {
        int ch = vfs_console_poll_input_char();
        if (ch < 0) {
            if (n != 0U || !should_block) {
                break;
            }
            /* Keep polling through the existing path so serial-fed scripted
             * input still gets folded into the keyboard buffer.
             */
            sched_sleep_ticks(1U);
            continue;
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
        display_putc(c);
    }
    if (out_written) {
        *out_written = len;
    }
    vfs_console_write_unlock();
    return 0;
}

static void vfs_console_write_lock(void) {
    for (;;) {
        uint32_t saved_flags = display_console_enter_critical();

        if (!g_vfs_console_write_locked) {
            g_vfs_console_write_locked = 1;
            display_console_leave_critical(saved_flags);
            return;
        }

        display_console_leave_critical(saved_flags);
        if (sched_current_task_pid() > 0) {
            sched_yield();
        }
    }
}

static void vfs_console_write_unlock(void) {
    uint32_t saved_flags = display_console_enter_critical();

    g_vfs_console_write_locked = 0;
    display_console_leave_critical(saved_flags);
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

static const struct kfile_ops g_vfs_root_ramfile_file_ops = {
    .read = vfs_root_ramfile_read,
    .write = vfs_root_ramfile_write,
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

static int vfs_root_ramfile_open(struct vfs_node *node, uint32_t open_flags, struct kfile *out_file) {
    struct vfs_root_ram_file *file;

    if (!node || !out_file) {
        return -KERR_INVAL;
    }
    file = (struct vfs_root_ram_file *)(uintptr_t)node->backend_private;
    if (!file || !file->in_use) {
        return -KERR_NOENT;
    }
    if ((open_flags & SYSCALL_OPEN_FLAG_TRUNC) != 0U) {
        if ((open_flags & SYSCALL_OPEN_FLAG_WRITE) == 0U) {
            return -KERR_INVAL;
        }
        file->size = 0U;
    }
    kfile_init(out_file, node, &g_vfs_root_ramfile_file_ops, file, open_flags);
    if ((open_flags & SYSCALL_OPEN_FLAG_APPEND) != 0U) {
        out_file->offset = file->size;
    }
    return 0;
}

static int vfs_root_ramfile_read(struct kfile *file, void *buf, uint32_t len, uint32_t *out_read) {
    struct vfs_root_ram_file *ram;
    uint32_t remaining;
    uint32_t n;

    if (!file || (len != 0U && !buf)) {
        return -KERR_INVAL;
    }
    ram = (struct vfs_root_ram_file *)(uintptr_t)file->backend_private;
    if (!ram || !ram->in_use) {
        return -KERR_NOENT;
    }
    if (out_read) {
        *out_read = 0U;
    }
    if (len == 0U || file->offset >= ram->size) {
        return 0;
    }
    remaining = ram->size - file->offset;
    n = len < remaining ? len : remaining;
    memcpy(buf, ram->data + file->offset, n);
    file->offset += n;
    if (out_read) {
        *out_read = n;
    }
    return 0;
}

static int vfs_root_ramfile_write(struct kfile *file,
                                  const void *buf,
                                  uint32_t len,
                                  uint32_t *out_written) {
    struct vfs_root_ram_file *ram;
    uint32_t room;
    uint32_t n;
    uint32_t start;

    if (!file || (len != 0U && !buf)) {
        return -KERR_INVAL;
    }
    if ((file->flags & SYSCALL_OPEN_FLAG_WRITE) == 0U) {
        return -KERR_NOTSUP;
    }
    ram = (struct vfs_root_ram_file *)(uintptr_t)file->backend_private;
    if (!ram || !ram->in_use) {
        return -KERR_NOENT;
    }
    if (out_written) {
        *out_written = 0U;
    }
    if ((file->flags & SYSCALL_OPEN_FLAG_APPEND) != 0U) {
        file->offset = ram->size;
    }
    if (file->offset >= VFS_ROOT_RAMFILE_CAPACITY) {
        return -KERR_NOMEM;
    }
    start = file->offset;
    room = VFS_ROOT_RAMFILE_CAPACITY - start;
    n = len < room ? len : room;
    if (n != 0U) {
        memcpy(ram->data + start, buf, n);
        file->offset += n;
        if (ram->size < file->offset) {
            ram->size = file->offset;
        }
    }
    if (out_written) {
        *out_written = n;
    }
    if (n < len) {
        return -KERR_NOMEM;
    }
    return 0;
}

static int vfs_root_create_or_open_ramfile(const char *path,
                                           uint32_t open_flags,
                                           struct kfile *out_file) {
    const char *name;
    uint32_t name_len = 0U;
    struct vfs_root_ram_file *slot = 0;
    uint32_t free_idx = VFS_ROOT_RAMFILE_MAX;
    int rc;

    if (!path || !out_file || path[0] != '/') {
        return -KERR_INVAL;
    }
    if (path[1] == '\0') {
        return -KERR_INVAL;
    }
    name = path + 1U;
    while (name[name_len] != '\0') {
        if (name[name_len] == '/') {
            return -KERR_NOENT;
        }
        name_len++;
    }
    if (name_len == 0U || name_len >= VFS_DIR_ENTRY_NAME_MAX) {
        return -KERR_INVAL;
    }

    for (uint32_t i = 0U; i < VFS_ROOT_RAMFILE_MAX; i++) {
        struct vfs_root_ram_file *candidate = &g_vfs_root_ramfiles[i];
        if (candidate->in_use &&
            vfs_name_eq(candidate->name, name, name_len)) {
            slot = candidate;
            break;
        }
        if (!candidate->in_use && free_idx == VFS_ROOT_RAMFILE_MAX) {
            free_idx = i;
        }
    }

    if (!slot) {
        if ((open_flags & SYSCALL_OPEN_FLAG_CREATE) == 0U) {
            return -KERR_NOENT;
        }
        if (free_idx >= VFS_ROOT_RAMFILE_MAX ||
            g_vfs_root_dir.count >= VFS_BOOTSTRAP_ROOT_MAX_CHILDREN) {
            return -KERR_NOMEM;
        }
        slot = &g_vfs_root_ramfiles[free_idx];
        memset(slot, 0, sizeof(*slot));
        memcpy(slot->name, name, name_len);
        slot->name[name_len] = '\0';
        slot->node.name = slot->name;
        slot->node.type = VFS_NODE_FILE;
        slot->node.ops = &g_vfs_root_ramfile_node_ops;
        slot->node.backend_private = (void *)(uintptr_t)slot;
        slot->in_use = 1;
        g_vfs_root_entries[g_vfs_root_dir.count].name = slot->name;
        g_vfs_root_entries[g_vfs_root_dir.count].node = &slot->node;
        g_vfs_root_dir.count++;
    }

    rc = vfs_root_ramfile_open(&slot->node, open_flags, out_file);
    return rc;
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
    memset(g_vfs_root_ramfiles, 0, sizeof(g_vfs_root_ramfiles));
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
        if (rc == -KERR_NOENT && (open_flags & SYSCALL_OPEN_FLAG_CREATE) != 0U) {
            return vfs_root_create_or_open_ramfile(path, open_flags, out_file);
        }
        return rc;
    }
    if (!node || !node->ops || !node->ops->open) {
        return -KERR_NOTSUP;
    }
    return node->ops->open(node, open_flags, out_file);
}

int vfs_list_dir(const char *path,
                 struct vfs_dir_entry *entries,
                 uint32_t entry_cap,
                 uint32_t *out_count) {
    struct vfs_node *node;
    int rc;

    if (!out_count) {
        return -KERR_INVAL;
    }
    *out_count = 0U;
    if (entry_cap != 0U && !entries) {
        return -KERR_INVAL;
    }

    rc = vfs_lookup(path, &node);
    if (rc < 0) {
        return rc;
    }
    if (!node || node->type != VFS_NODE_DIR) {
        return -KERR_NOTSUP;
    }
    if (!node->ops || !node->ops->list) {
        return -KERR_NOTSUP;
    }
    return node->ops->list(node, entries, entry_cap, out_count);
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
