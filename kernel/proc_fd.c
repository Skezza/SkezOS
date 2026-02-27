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

    slot->file = *src_file;
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

    for (uint32_t fd = fd_min; fd < PROC_FD_MAX; fd++) {
        struct proc_fd_slot *slot = &table->slots[fd];
        if (slot->in_use) {
            continue;
        }
        slot->file = *src_file;
        slot->in_use = 1;
        *out_fd = fd;
        if (out_file) {
            *out_file = &slot->file;
        }
        return 0;
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
