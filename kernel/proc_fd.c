#include "proc_fd.h"

#include <stdint.h>

#include "kerrno.h"
#include "kfile.h"
#include "utils.h"

static int proc_fd_slot_for(struct proc_fd_table *table, uint32_t fd, struct proc_fd_slot **out_slot) {
    if (!table || !out_slot) {
        return -KERR_INVAL;
    }
    *out_slot = 0;
    if (fd >= PROC_FD_MAX) {
        return -KERR_INVAL;
    }
    *out_slot = &table->slots[fd];
    return 0;
}

void proc_fd_table_init(struct proc_fd_table *table) {
    if (!table) {
        return;
    }
    memset(table, 0, sizeof(*table));
}

int proc_fd_get(struct proc_fd_table *table, uint32_t fd, struct kfile **out_file) {
    struct proc_fd_slot *slot = 0;
    int rc;

    if (!out_file) {
        return -KERR_INVAL;
    }
    *out_file = 0;
    rc = proc_fd_slot_for(table, fd, &slot);
    if (rc < 0) {
        return rc;
    }
    if (!slot || !slot->in_use) {
        return -KERR_NOENT;
    }
    *out_file = &slot->file;
    return 0;
}

int proc_fd_install(struct proc_fd_table *table,
                    uint32_t fd,
                    const struct kfile *src_file,
                    struct kfile **out_file) {
    struct proc_fd_slot *slot = 0;
    struct kfile cloned;
    int rc;

    if (!src_file) {
        return -KERR_INVAL;
    }
    if (out_file) {
        *out_file = 0;
    }

    rc = proc_fd_slot_for(table, fd, &slot);
    if (rc < 0) {
        return rc;
    }
    if (!slot) {
        return -KERR_FAULT;
    }
    if (slot->in_use) {
        if (out_file) {
            *out_file = &slot->file;
        }
        return -KERR_INVAL;
    }

    rc = kfile_clone(src_file, &cloned);
    if (rc < 0) {
        return rc;
    }
    slot->file = cloned;
    slot->in_use = 1;
    if (out_file) {
        *out_file = &slot->file;
    }
    return 0;
}

int proc_fd_alloc(struct proc_fd_table *table,
                  uint32_t fd_min,
                  const struct kfile *src_file,
                  uint32_t *out_fd,
                  struct kfile **out_file) {
    struct kfile cloned;
    int cloned_valid = 0;
    int rc;

    if (!table || !src_file || !out_fd) {
        return -KERR_INVAL;
    }
    *out_fd = 0;
    if (out_file) {
        *out_file = 0;
    }

    if (fd_min >= PROC_FD_MAX) {
        return -KERR_INVAL;
    }

    rc = kfile_clone(src_file, &cloned);
    if (rc < 0) {
        return rc;
    }
    cloned_valid = 1;

    for (uint32_t fd = fd_min; fd < PROC_FD_MAX; fd++) {
        struct proc_fd_slot *slot = &table->slots[fd];
        if (slot->in_use) {
            continue;
        }
        slot->file = cloned;
        slot->in_use = 1;
        *out_fd = fd;
        if (out_file) {
            *out_file = &slot->file;
        }
        return 0;
    }

    if (cloned_valid) {
        (void)kfile_close(&cloned);
    }
    return -KERR_NOMEM;
}

int proc_fd_close(struct proc_fd_table *table, uint32_t fd) {
    struct proc_fd_slot *slot = 0;
    int rc;

    rc = proc_fd_slot_for(table, fd, &slot);
    if (rc < 0) {
        return rc;
    }
    if (!slot || !slot->in_use) {
        return -KERR_NOENT;
    }
    rc = kfile_close(&slot->file);
    if (rc < 0) {
        return rc;
    }
    slot->in_use = 0;
    kfile_reset(&slot->file);
    return 0;
}

int proc_fd_dup(struct proc_fd_table *table,
                uint32_t oldfd,
                uint32_t fd_min,
                uint32_t *out_newfd,
                struct kfile **out_file) {
    struct proc_fd_slot *old_slot = 0;
    int rc;

    if (!table || !out_newfd) {
        return -KERR_INVAL;
    }
    *out_newfd = 0;
    if (out_file) {
        *out_file = 0;
    }

    rc = proc_fd_slot_for(table, oldfd, &old_slot);
    if (rc < 0) {
        return rc;
    }
    if (!old_slot || !old_slot->in_use) {
        return -KERR_NOENT;
    }

    return proc_fd_alloc(table, fd_min, &old_slot->file, out_newfd, out_file);
}

int proc_fd_dup2(struct proc_fd_table *table,
                 uint32_t oldfd,
                 uint32_t newfd,
                 struct kfile **out_file) {
    struct proc_fd_slot *old_slot = 0;
    struct proc_fd_slot *new_slot = 0;
    struct kfile cloned;
    int rc;

    if (!table) {
        return -KERR_INVAL;
    }
    if (out_file) {
        *out_file = 0;
    }

    rc = proc_fd_slot_for(table, oldfd, &old_slot);
    if (rc < 0) {
        return rc;
    }
    rc = proc_fd_slot_for(table, newfd, &new_slot);
    if (rc < 0) {
        return rc;
    }
    if (!old_slot || !old_slot->in_use || !new_slot) {
        return -KERR_NOENT;
    }
    if (oldfd == newfd) {
        if (out_file) {
            *out_file = &new_slot->file;
        }
        return 0;
    }

    rc = kfile_clone(&old_slot->file, &cloned);
    if (rc < 0) {
        return rc;
    }

    if (new_slot->in_use) {
        rc = kfile_close(&new_slot->file);
        if (rc < 0) {
            (void)kfile_close(&cloned);
            return rc;
        }
    }
    new_slot->file = cloned;
    new_slot->in_use = 1;
    if (out_file) {
        *out_file = &new_slot->file;
    }
    return 0;
}

int proc_fd_table_clone(struct proc_fd_table *dst, const struct proc_fd_table *src) {
    if (!dst || !src) {
        return -KERR_INVAL;
    }

    proc_fd_table_init(dst);
    for (uint32_t fd = 0; fd < PROC_FD_MAX; fd++) {
        int rc;

        if (!src->slots[fd].in_use) {
            continue;
        }
        rc = kfile_clone(&src->slots[fd].file, &dst->slots[fd].file);
        if (rc < 0) {
            (void)proc_fd_close_all(dst);
            return rc;
        }
        dst->slots[fd].in_use = 1;
    }
    return 0;
}

uint32_t proc_fd_close_all(struct proc_fd_table *table) {
    uint32_t closed = 0;

    if (!table) {
        return 0;
    }

    for (uint32_t fd = 0; fd < PROC_FD_MAX; fd++) {
        struct proc_fd_slot *slot = &table->slots[fd];
        if (!slot->in_use) {
            continue;
        }
        if (kfile_close(&slot->file) == 0) {
            closed++;
        }
        slot->in_use = 0;
        kfile_reset(&slot->file);
    }
    return closed;
}
