#include "tarfs.h"

#include <stdint.h>

#include "initramfs_demo_blob.h"
#include "kassert.h"
#include "kerrno.h"
#include "kfile.h"
#include "klog.h"
#include "kmalloc.h"
#include "syscall_abi.h"
#include "utils.h"
#include "vfs.h"

#define TAR_BLOCK_SIZE 512U
#define TARFS_NAME_MAX 255U

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} __attribute__((packed));

struct tarfs_node {
    struct vfs_node vfs;
    struct tarfs_node *parent;
    struct tarfs_node *first_child;
    struct tarfs_node *next_sibling;
    const uint8_t *file_data;
    uint32_t file_size;
};

static struct tarfs_node *g_tarfs_root;
static int g_tarfs_mounted;

static int tarfs_dir_lookup(struct vfs_node *dir,
                            const char *name,
                            uint32_t name_len,
                            struct vfs_node **out_node);
static int tarfs_dir_list(struct vfs_node *dir,
                          struct vfs_dir_entry *entries,
                          uint32_t entry_cap,
                          uint32_t *out_count);
static int tarfs_file_open(struct vfs_node *node, uint32_t open_flags, struct kfile *out_file);
static int tarfs_file_read(struct kfile *file, void *buf, uint32_t len, uint32_t *out_read);
static int tarfs_file_write(struct kfile *file, const void *buf, uint32_t len, uint32_t *out_written);

static const struct vfs_node_ops g_tarfs_dir_ops = {
    .lookup = tarfs_dir_lookup,
    .list = tarfs_dir_list,
    .open = 0,
};

static const struct vfs_node_ops g_tarfs_file_ops_node = {
    .lookup = 0,
    .list = 0,
    .open = tarfs_file_open,
};

static const struct kfile_ops g_tarfs_file_ops = {
    .read = tarfs_file_read,
    .write = tarfs_file_write,
    .close = 0,
};

static uint32_t tarfs_round_up_512(uint32_t size) {
    return (size + (TAR_BLOCK_SIZE - 1U)) & ~(TAR_BLOCK_SIZE - 1U);
}

static int tarfs_name_eq(const char *a, const char *b, uint32_t b_len) {
    uint32_t i;
    if (!a || !b) {
        return 0;
    }
    for (i = 0; i < b_len; i++) {
        if (a[i] == '\0' || a[i] != b[i]) {
            return 0;
        }
    }
    return a[b_len] == '\0';
}

static void tarfs_copy_dir_entry_name(char *dst, const char *src) {
    uint32_t i = 0U;

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

static int tarfs_bytes_eq(const uint8_t *a, const char *b, uint32_t len) {
    uint32_t i;
    if (!a || !b) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (a[i] != (uint8_t)b[i]) {
            return 0;
        }
    }
    return 1;
}

static uint32_t tarfs_field_len(const char *field, uint32_t cap) {
    uint32_t i = 0;
    while (i < cap && field[i] != '\0') {
        i++;
    }
    return i;
}

static int tarfs_is_zero_block(const uint8_t *block) {
    uint32_t i;
    for (i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (block[i] != 0U) {
            return 0;
        }
    }
    return 1;
}

static int tarfs_parse_octal_u32(const char *field, uint32_t cap, uint32_t *out_value) {
    uint32_t value = 0;
    uint32_t i = 0;
    int saw_digit = 0;

    if (!field || !out_value) {
        return -KERR_INVAL;
    }

    while (i < cap && (field[i] == ' ' || field[i] == '\0')) {
        i++;
    }
    for (; i < cap; i++) {
        char c = field[i];
        if (c == '\0' || c == ' ') {
            break;
        }
        if (c < '0' || c > '7') {
            return -KERR_INVAL;
        }
        saw_digit = 1;
        value = (value << 3) + (uint32_t)(c - '0');
    }
    *out_value = saw_digit ? value : 0U;
    return 0;
}

static int tarfs_header_get_path(const struct tar_header *hdr, char *out_path, uint32_t out_cap, uint32_t *out_len) {
    uint32_t name_len;
    uint32_t prefix_len;
    uint32_t pos = 0;

    if (!hdr || !out_path || !out_len || out_cap < 2U) {
        return -KERR_INVAL;
    }

    name_len = tarfs_field_len(hdr->name, sizeof(hdr->name));
    prefix_len = tarfs_field_len(hdr->prefix, sizeof(hdr->prefix));
    if (name_len == 0U) {
        return -KERR_INVAL;
    }

    if (prefix_len != 0U) {
        if ((prefix_len + 1U + name_len + 1U) > out_cap) {
            return -KERR_INVAL;
        }
        memcpy(&out_path[pos], hdr->prefix, prefix_len);
        pos += prefix_len;
        out_path[pos++] = '/';
    } else if ((name_len + 1U) > out_cap) {
        return -KERR_INVAL;
    }

    memcpy(&out_path[pos], hdr->name, name_len);
    pos += name_len;
    out_path[pos] = '\0';
    *out_len = pos;
    return 0;
}

static struct tarfs_node *tarfs_alloc_node(const char *name, vfs_node_type_t type) {
    struct tarfs_node *node = (struct tarfs_node *)kmalloc(sizeof(struct tarfs_node));
    if (!node) {
        return 0;
    }
    memset(node, 0, sizeof(*node));
    node->vfs.name = name;
    node->vfs.type = type;
    node->vfs.ops = (type == VFS_NODE_DIR) ? &g_tarfs_dir_ops : &g_tarfs_file_ops_node;
    return node;
}

static char *tarfs_dup_name(const char *name, uint32_t len) {
    char *copy = (char *)kmalloc((size_t)len + 1U);
    if (!copy) {
        return 0;
    }
    memcpy(copy, name, len);
    copy[len] = '\0';
    return copy;
}

static struct tarfs_node *tarfs_find_child(struct tarfs_node *dir, const char *name, uint32_t name_len) {
    struct tarfs_node *child;
    if (!dir || dir->vfs.type != VFS_NODE_DIR) {
        return 0;
    }
    child = dir->first_child;
    while (child) {
        if (tarfs_name_eq(child->vfs.name, name, name_len)) {
            return child;
        }
        child = child->next_sibling;
    }
    return 0;
}

static int tarfs_link_child(struct tarfs_node *parent, struct tarfs_node *child) {
    struct tarfs_node *iter;
    if (!parent || !child) {
        return -KERR_INVAL;
    }
    if (parent->vfs.type != VFS_NODE_DIR) {
        return -KERR_INVAL;
    }
    child->parent = parent;
    if (!parent->first_child) {
        parent->first_child = child;
        return 0;
    }
    iter = parent->first_child;
    while (iter->next_sibling) {
        iter = iter->next_sibling;
    }
    iter->next_sibling = child;
    return 0;
}

static int tarfs_get_or_create_child(struct tarfs_node *parent,
                                     const char *name,
                                     uint32_t name_len,
                                     vfs_node_type_t type,
                                     struct tarfs_node **out_child) {
    struct tarfs_node *child;
    char *name_copy;

    if (!parent || !name || !out_child || name_len == 0U) {
        return -KERR_INVAL;
    }

    child = tarfs_find_child(parent, name, name_len);
    if (child) {
        if (type == VFS_NODE_DIR && child->vfs.type == VFS_NODE_DIR) {
            *out_child = child;
            return 0;
        }
        if (type == VFS_NODE_FILE && child->vfs.type == VFS_NODE_FILE) {
            *out_child = child;
            return 0;
        }
        return -KERR_INVAL;
    }

    name_copy = tarfs_dup_name(name, name_len);
    if (!name_copy) {
        return -KERR_NOMEM;
    }
    child = tarfs_alloc_node(name_copy, type);
    if (!child) {
        return -KERR_NOMEM;
    }
    if (tarfs_link_child(parent, child) < 0) {
        return -KERR_INVAL;
    }
    *out_child = child;
    return 0;
}

static int tarfs_add_path_entry(struct tarfs_node *root,
                                const char *raw_path,
                                uint32_t raw_path_len,
                                int is_dir,
                                const uint8_t *file_data,
                                uint32_t file_size) {
    struct tarfs_node *dir = root;
    struct tarfs_node *node = 0;
    const char *p = raw_path;
    uint32_t len = raw_path_len;

    if (!root || !raw_path) {
        return -KERR_INVAL;
    }

    while (len > 0U && *p == '/') {
        p++;
        len--;
    }
    while (len >= 2U && p[0] == '.' && p[1] == '/') {
        p += 2;
        len -= 2;
    }
    while (len > 0U && p[len - 1U] == '/') {
        len--;
    }
    if (len == 0U) {
        return 0;
    }

    for (;;) {
        uint32_t comp_len = 0;
        vfs_node_type_t want_type;
        int rc;

        while (comp_len < len && p[comp_len] != '/') {
            comp_len++;
        }
        if (comp_len == 0U) {
            return -KERR_INVAL;
        }

        want_type = (comp_len == len && !is_dir) ? VFS_NODE_FILE : VFS_NODE_DIR;
        rc = tarfs_get_or_create_child(dir, p, comp_len, want_type, &node);
        if (rc < 0) {
            return rc;
        }
        if (comp_len == len) {
            break;
        }

        p += comp_len;
        len -= comp_len;
        while (len > 0U && *p == '/') {
            p++;
            len--;
        }
        if (node->vfs.type != VFS_NODE_DIR) {
            return -KERR_INVAL;
        }
        dir = node;
    }

    if (!is_dir) {
        node->file_data = file_data;
        node->file_size = file_size;
    }
    return 0;
}

static int tarfs_parse_blob(const uint8_t *blob, uint32_t blob_len, struct tarfs_node **out_root) {
    uint32_t off = 0;
    struct tarfs_node *root;

    if (!blob || !out_root || (blob_len % TAR_BLOCK_SIZE) != 0U) {
        return -KERR_INVAL;
    }

    root = tarfs_alloc_node("/", VFS_NODE_DIR);
    if (!root) {
        return -KERR_NOMEM;
    }

    while ((off + TAR_BLOCK_SIZE) <= blob_len) {
        const struct tar_header *hdr = (const struct tar_header *)(const void *)(blob + off);
        uint32_t file_size = 0;
        char path_buf[TARFS_NAME_MAX + 1U];
        uint32_t path_len = 0;
        uint32_t payload_padded;
        int is_dir = 0;
        int rc;

        if (tarfs_is_zero_block(blob + off)) {
            break;
        }

        rc = tarfs_parse_octal_u32(hdr->size, sizeof(hdr->size), &file_size);
        if (rc < 0) {
            KLOGW("tarfs: invalid size field at offset=%u", off);
            return rc;
        }

        rc = tarfs_header_get_path(hdr, path_buf, sizeof(path_buf), &path_len);
        if (rc < 0) {
            KLOGW("tarfs: invalid path header at offset=%u", off);
            return rc;
        }

        if (hdr->typeflag == '5') {
            is_dir = 1;
        } else if (hdr->typeflag == '\0' || hdr->typeflag == '0') {
            is_dir = 0;
        } else {
            /* Skip unsupported entries for now (symlinks, devices, etc.). */
            payload_padded = tarfs_round_up_512(file_size);
            if ((off + TAR_BLOCK_SIZE + payload_padded) < off ||
                (off + TAR_BLOCK_SIZE + payload_padded) > blob_len) {
                return -KERR_FAULT;
            }
            off += TAR_BLOCK_SIZE + payload_padded;
            continue;
        }

        payload_padded = tarfs_round_up_512(file_size);
        if ((off + TAR_BLOCK_SIZE + payload_padded) < off ||
            (off + TAR_BLOCK_SIZE + payload_padded) > blob_len) {
            return -KERR_FAULT;
        }

        rc = tarfs_add_path_entry(root,
                                  path_buf,
                                  path_len,
                                  is_dir,
                                  blob + off + TAR_BLOCK_SIZE,
                                  file_size);
        if (rc < 0) {
            KLOGW("tarfs: failed to add entry path=%s rc=%d", path_buf, rc);
            return rc;
        }

        off += TAR_BLOCK_SIZE + payload_padded;
    }

    *out_root = root;
    return 0;
}

static int tarfs_dir_lookup(struct vfs_node *dir,
                            const char *name,
                            uint32_t name_len,
                            struct vfs_node **out_node) {
    struct tarfs_node *child;

    if (!dir || !name || !out_node || name_len == 0U) {
        return -KERR_INVAL;
    }
    *out_node = 0;
    child = tarfs_find_child((struct tarfs_node *)dir, name, name_len);
    if (!child) {
        return -KERR_NOENT;
    }
    *out_node = &child->vfs;
    return 0;
}

static int tarfs_dir_list(struct vfs_node *dir,
                          struct vfs_dir_entry *entries,
                          uint32_t entry_cap,
                          uint32_t *out_count) {
    struct tarfs_node *child;
    uint32_t count = 0U;

    if (!dir || !out_count) {
        return -KERR_INVAL;
    }
    *out_count = 0U;
    if (entry_cap != 0U && !entries) {
        return -KERR_INVAL;
    }

    child = ((struct tarfs_node *)dir)->first_child;
    while (child) {
        if (count >= entry_cap) {
            break;
        }
        entries[count].type = (uint32_t)child->vfs.type;
        tarfs_copy_dir_entry_name(entries[count].name, child->vfs.name);
        count++;
        child = child->next_sibling;
    }

    *out_count = count;
    return 0;
}

static int tarfs_file_open(struct vfs_node *node, uint32_t open_flags, struct kfile *out_file) {
    if (!node || !out_file) {
        return -KERR_INVAL;
    }
    if (node->type != VFS_NODE_FILE) {
        return -KERR_INVAL;
    }
    if ((open_flags & (SYSCALL_OPEN_FLAG_WRITE |
                       SYSCALL_OPEN_FLAG_CREATE |
                       SYSCALL_OPEN_FLAG_TRUNC |
                       SYSCALL_OPEN_FLAG_APPEND)) != 0U) {
        return -KERR_NOTSUP;
    }
    kfile_init(out_file, node, &g_tarfs_file_ops, 0, open_flags);
    return 0;
}

static int tarfs_file_read(struct kfile *file, void *buf, uint32_t len, uint32_t *out_read) {
    struct tarfs_node *node;
    uint32_t remaining;
    uint32_t n;

    if (!file || (len != 0U && !buf)) {
        return -KERR_INVAL;
    }
    node = (struct tarfs_node *)file->node;
    if (!node || node->vfs.type != VFS_NODE_FILE) {
        return -KERR_FAULT;
    }
    if (out_read) {
        *out_read = 0;
    }
    if (file->offset >= node->file_size || len == 0U) {
        return 0;
    }

    remaining = node->file_size - file->offset;
    n = (len < remaining) ? len : remaining;
    memcpy(buf, node->file_data + file->offset, n);
    file->offset += n;
    if (out_read) {
        *out_read = n;
    }
    return 0;
}

static int tarfs_file_write(struct kfile *file, const void *buf, uint32_t len, uint32_t *out_written) {
    (void)file;
    (void)buf;
    (void)len;
    if (out_written) {
        *out_written = 0;
    }
    return -KERR_NOTSUP;
}

static int tarfs_register_top_level_children(struct tarfs_node *root) {
    struct tarfs_node *child;
    int mounted = 0;

    if (!root) {
        return -KERR_INVAL;
    }
    for (child = root->first_child; child; child = child->next_sibling) {
        int rc = vfs_register_root_child(child->vfs.name, &child->vfs);
        if (rc < 0) {
            KLOGW("tarfs: failed to register /%s rc=%d", child->vfs.name, rc);
            return rc;
        }
        mounted++;
    }
    if (mounted == 0) {
        return -KERR_INVAL;
    }
    return 0;
}

static void tarfs_self_check(void) {
    static const char expected[] = "SkezOS tarfs demo\n";
    struct vfs_node *node;
    struct kfile file;
    uint8_t buf[32];
    uint32_t n = 0;

    KASSERT(vfs_lookup("/bin", &node) == 0);
    KASSERT(node != 0 && node->type == VFS_NODE_DIR);
    KASSERT(vfs_lookup("/bin/readme.txt", &node) == 0);
    KASSERT(node != 0 && node->type == VFS_NODE_FILE);

    KASSERT(vfs_open("/bin/readme.txt", 0, &file) == 0);
    KASSERT(kfile_read(&file, buf, sizeof(buf), &n) == 0);
    KASSERT(n == (uint32_t)(sizeof(expected) - 1U));
    KASSERT(tarfs_bytes_eq(buf, expected, n));
    KASSERT(kfile_close(&file) == 0);

    KLOGI("tarfs: self-check pass (/bin/readme.txt)");
}

int tarfs_mount_demo_archive(void) {
    int rc;

    if (g_tarfs_mounted) {
        return 0;
    }
    KASSERT(sizeof(struct tar_header) == TAR_BLOCK_SIZE);

    rc = tarfs_parse_blob(g_initramfs_demo_blob, g_initramfs_demo_blob_len, &g_tarfs_root);
    if (rc < 0) {
        KLOGW("tarfs: parse failed rc=%d", rc);
        return rc;
    }

    rc = tarfs_register_top_level_children(g_tarfs_root);
    if (rc < 0) {
        return rc;
    }

    g_tarfs_mounted = 1;
    KLOGI("tarfs: mounted built-in demo archive (%u bytes)", (uint32_t)g_initramfs_demo_blob_len);
    tarfs_self_check();
    return 0;
}
