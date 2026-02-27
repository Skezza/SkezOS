#ifndef VFS_H
#define VFS_H

#include <stdint.h>

struct kfile;
struct vfs_node;

typedef enum {
    VFS_NODE_NONE = 0,
    VFS_NODE_FILE,
    VFS_NODE_DIR,
    VFS_NODE_CHARDEV,
} vfs_node_type_t;

typedef int (*vfs_lookup_fn_t)(struct vfs_node *dir,
                               const char *name,
                               uint32_t name_len,
                               struct vfs_node **out_node);
typedef int (*vfs_open_fn_t)(struct vfs_node *node, uint32_t open_flags, struct kfile *out_file);

struct vfs_node_ops {
    vfs_lookup_fn_t lookup;
    vfs_open_fn_t open;
};

struct vfs_node {
    const char *name;
    vfs_node_type_t type;
    const struct vfs_node_ops *ops;
    void *backend_private;
};

typedef enum {
    VFS_CONSOLE_INPUT_OWNER_KERNEL = 0,
    VFS_CONSOLE_INPUT_OWNER_USER_SHELL = 1,
} vfs_console_input_owner_t;

/* VFS bootstrap lifecycle. */
void vfs_init(void);

/* Root node registration/getter for the active backend. */
int vfs_set_root(struct vfs_node *root);
struct vfs_node *vfs_get_root(void);

/* Bootstrap root registration (used by built-in subsystems like
 * `dev` and the first archive backend). Names are copied by pointer
 * and must remain valid for kernel lifetime.
 */
int vfs_register_root_child(const char *name, struct vfs_node *node);

/* Bootstrap lookup/open helpers.
 * Current implementation supports absolute path traversal through a
 * small built-in root (`/dev`, `/dev/null`, `/dev/console`, etc.) and will
 * later be extended with archive-backed nodes.
 */
int vfs_lookup(const char *path, struct vfs_node **out_node);
int vfs_open(const char *path, uint32_t open_flags, struct kfile *out_file);

/* Phase 6 keeps `/dev/console` input single-owner: the kernel console task
 * starts as the owner, then hands stdin to the bootstrap shell.
 */
void vfs_console_set_input_owner(vfs_console_input_owner_t owner);
vfs_console_input_owner_t vfs_console_get_input_owner(void);
int vfs_console_poll_input_char(void);

#endif /* VFS_H */
