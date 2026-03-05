#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>

struct kfile;
struct syscall_task_snapshot_entry;

typedef void (*sched_task_entry_t)(void *arg);

/* Initialize scheduler state and create the idle task. */
void sched_init(void);

/* Spawn a kernel-mode task with its own kernel stack.
 * Returns 0 on success or a negative -KERR_* code.
 */
int sched_spawn_kernel_task(const char *name, sched_task_entry_t entry, void *arg, uint32_t stack_size);

/* Mark the current scheduler task as a user-mode bootstrap task that
 * will drop to Ring 3 at USER_EIP/USER_ESP.
 */
int sched_mark_current_task_user_bootstrap(uint32_t user_eip, uint32_t user_esp);
int sched_set_current_user_layout(uint32_t image_base,
                                  uint32_t image_size,
                                  uint32_t stack_base,
                                  uint32_t stack_size);
int sched_current_user_range_ok(uint32_t addr, uint32_t len);
int sched_set_current_task_cmdline(const char *src, uint32_t len);
int sched_copy_current_task_cmdline_to_user(uint32_t dst_addr, uint32_t dst_len, uint32_t *out_len);
int sched_set_current_task_cwd(const char *src, uint32_t len);
int sched_copy_current_task_cwd_to_user(uint32_t dst_addr, uint32_t dst_len, uint32_t *out_len);
int sched_copy_current_task_cwd(char *dst, uint32_t dst_cap, uint32_t *out_len);
int sched_user_image_range_available(uint32_t image_base, uint32_t image_size);

/* User-task diagnostics hooks used by the Phase 3 syscall/fault path. */
void sched_note_current_syscall(uint32_t syscall_nr);
void sched_note_current_user_fault(uint32_t user_eip, uint32_t fault_addr, uint32_t error_code);
void sched_note_current_exit_code(int32_t exit_code);

/* Lightweight task identity helpers for diagnostics. */
int sched_current_task_is_user(void);
int sched_current_task_pid(void);
const char *sched_current_task_name(void);
int sched_current_task_owns_child_pid(int pid);
int sched_collect_task_snapshot(struct syscall_task_snapshot_entry *entries,
                                uint32_t cap,
                                uint32_t *out_count);

/* Process-owned FD table helpers used by syscall I/O path. */
int sched_current_process_fd_get(uint32_t fd, struct kfile **out_file);
int sched_current_process_fd_install(uint32_t fd, const struct kfile *src_file, struct kfile **out_file);
int sched_current_process_fd_alloc(const struct kfile *src_file, uint32_t *out_fd, struct kfile **out_file);
int sched_current_process_fd_close(uint32_t fd);
int sched_current_process_fd_dup(uint32_t oldfd, uint32_t *out_newfd, struct kfile **out_file);
int sched_current_process_fd_dup2(uint32_t oldfd, uint32_t newfd, struct kfile **out_file);

/* Spawn a waitable child task owned by the current user task.
 * Returns 0 on success and writes child pid to OUT_PID.
 */
int sched_spawn_user_child_task(const char *name,
                                sched_task_entry_t entry,
                                void *arg,
                                uint32_t stack_size,
                                int inherit_fds,
                                int *out_pid);

/* Wait for a child task to exit.
 * TARGET_PID supports:
 *   -1 : wait for any child
 *   >0 : wait for one specific child pid
 * Returns 0 on success and writes the reaped pid/exit code.
 */
int sched_waitpid(int target_pid, int *out_waited_pid, int32_t *out_exit_code);

/* Start the scheduler by switching from boot context to the first runnable task.
 * Does not return.
 */
void sched_start(void) __attribute__((noreturn));

/* Voluntarily yield the CPU to another runnable task. */
void sched_yield(void);

/* Terminate the current task and schedule another runnable task. */
void sched_exit_current(void) __attribute__((noreturn));

/* Sleep the current task for at least TICKS scheduler ticks. */
void sched_sleep_ticks(uint32_t ticks);

/* Safe-point helper for long-running task loops. */
void sched_checkpoint(void);

/* Timer IRQ hook. Called from IRQ0 handler path after timer_ticks increments. */
void sched_on_timer_tick_irq(void);

/* Basic scheduler self-test/demo status for boot logs. */
uint32_t sched_runnable_count(void);

#endif /* SCHED_H */
