#ifndef PROC_FD_H
#define PROC_FD_H

#include <stdint.h>

#include "kfile.h"

#define PROC_FD_MAX         8U
#define PROC_FD_DYNAMIC_MIN 3U

struct proc_fd_slot {
    int in_use;
    struct kfile file;
};

struct proc_fd_table {
    struct proc_fd_slot slots[PROC_FD_MAX];
};

/* Initialize/reset a process FD table. */
void proc_fd_table_init(struct proc_fd_table *table);

/* Lookup/insert/remove helpers.
 * Returns 0 on success or negative -KERR_* on failure.
 */
int proc_fd_get(struct proc_fd_table *table, uint32_t fd, struct kfile **out_file);
int proc_fd_install(struct proc_fd_table *table,
                    uint32_t fd,
                    const struct kfile *src_file,
                    struct kfile **out_file);
int proc_fd_alloc(struct proc_fd_table *table,
                  uint32_t fd_min,
                  const struct kfile *src_file,
                  uint32_t *out_fd,
                  struct kfile **out_file);
int proc_fd_close(struct proc_fd_table *table, uint32_t fd);

/* Close all open FDs and reset table. Returns number of descriptors closed. */
uint32_t proc_fd_close_all(struct proc_fd_table *table);

#endif /* PROC_FD_H */
