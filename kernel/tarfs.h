#ifndef TARFS_H
#define TARFS_H

/* Parse and mount the built-in demo tar archive into the VFS root
 * (registering top-level entries such as `/bin`). Returns 0 on
 * success or negative -KERR_*.
 */
int tarfs_mount_demo_archive(void);

#endif /* TARFS_H */
