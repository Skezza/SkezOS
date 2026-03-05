#ifndef KFILE_H
#define KFILE_H

#include <stdint.h>

struct vfs_node;
struct kfile;

typedef int (*kfile_read_fn_t)(struct kfile *file, void *buf, uint32_t len, uint32_t *out_read);
typedef int (*kfile_write_fn_t)(struct kfile *file, const void *buf, uint32_t len, uint32_t *out_written);
typedef int (*kfile_retain_fn_t)(struct kfile *file);
typedef int (*kfile_close_fn_t)(struct kfile *file);

struct kfile_ops {
    kfile_read_fn_t read;
    kfile_write_fn_t write;
    kfile_retain_fn_t retain;
    kfile_close_fn_t close;
};

struct kfile {
    const struct kfile_ops *ops;
    struct vfs_node *node;
    void *backend_private;
    uint32_t offset;
    uint32_t flags;
};

/* Reset a file object to an unopened state. */
void kfile_reset(struct kfile *file);

/* Initialize an open file object. */
void kfile_init(struct kfile *file,
                struct vfs_node *node,
                const struct kfile_ops *ops,
                void *backend_private,
                uint32_t flags);

/* Dispatch helpers that normalize basic validation and -KERR_* errors. */
int kfile_read(struct kfile *file, void *buf, uint32_t len, uint32_t *out_read);
int kfile_write(struct kfile *file, const void *buf, uint32_t len, uint32_t *out_written);
int kfile_clone(const struct kfile *src, struct kfile *dst);
int kfile_close(struct kfile *file);

#endif /* KFILE_H */
