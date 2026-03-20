#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>

/* Spawn a temporary user-mode demo task backed by an in-memory code blob.
 * Returns 0 on success or negative -KERR_*.
 */
int usermode_spawn_demo_task(void);

/* Spawn a temporary user-mode task that intentionally faults to test
 * user fault reporting and recovery.
 */
int usermode_spawn_fault_task(void);

/* Spawn Phase 4 demo tasks loaded from /bin hello ELF files via tarfs + VFS. */
int usermode_spawn_elf_demo_task_a(void);
int usermode_spawn_elf_demo_task_b(void);

/* Spawn the Phase 6 fixed-slot bootstrap shell task. */
int usermode_spawn_shell_task(void);

/* Minimal syscall-facing spawn hook for a path-only child launch.
 * Returns child pid (>0) on success or negative -KERR_* on failure.
 */
int usermode_spawn_path_task(const char *path);
int usermode_spawn_path_task_ex(const char *path,
                                const char *cmdline,
                                uint32_t cmdline_len,
                                uint32_t spawn_flags);

/* Scheduler callback used to release bootstrap spawn slots on task reap. */
void usermode_notify_task_reaped(int pid);

#endif /* USERMODE_H */
