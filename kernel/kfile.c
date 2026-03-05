#include "kfile.h"

#include "kerrno.h"
#include "utils.h"

void kfile_reset(struct kfile *file) {
    if (!file) {
        return;
    }
    memset(file, 0, sizeof(*file));
}

void kfile_init(struct kfile *file,
                struct vfs_node *node,
                const struct kfile_ops *ops,
                void *backend_private,
                uint32_t flags) {
    if (!file) {
        return;
    }
    file->ops = ops;
    file->node = node;
    file->backend_private = backend_private;
    file->offset = 0;
    file->flags = flags;
}

int kfile_read(struct kfile *file, void *buf, uint32_t len, uint32_t *out_read) {
    if (!file) {
        return -KERR_INVAL;
    }
    if (len != 0U && !buf) {
        return -KERR_INVAL;
    }
    if (out_read) {
        *out_read = 0;
    }
    if (len == 0U) {
        return 0;
    }
    if (!file->ops || !file->ops->read) {
        return -KERR_NOTSUP;
    }
    return file->ops->read(file, buf, len, out_read);
}

int kfile_write(struct kfile *file, const void *buf, uint32_t len, uint32_t *out_written) {
    if (!file) {
        return -KERR_INVAL;
    }
    if (len != 0U && !buf) {
        return -KERR_INVAL;
    }
    if (out_written) {
        *out_written = 0;
    }
    if (len == 0U) {
        return 0;
    }
    if (!file->ops || !file->ops->write) {
        return -KERR_NOTSUP;
    }
    return file->ops->write(file, buf, len, out_written);
}

int kfile_clone(const struct kfile *src, struct kfile *dst) {
    struct kfile tmp;
    int rc = 0;

    if (!src || !dst) {
        return -KERR_INVAL;
    }

    tmp = *src;
    if (tmp.ops && tmp.ops->retain) {
        rc = tmp.ops->retain(&tmp);
        if (rc < 0) {
            return rc;
        }
    }
    *dst = tmp;
    return 0;
}

int kfile_close(struct kfile *file) {
    int rc = 0;

    if (!file) {
        return -KERR_INVAL;
    }
    if (file->ops && file->ops->close) {
        rc = file->ops->close(file);
        if (rc < 0) {
            return rc;
        }
    }
    kfile_reset(file);
    return 0;
}
