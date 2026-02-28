#include "sched.h"

#include <stdint.h>
#include <stddef.h>

#include "kassert.h"
#include "kerrno.h"
#include "gdt.h"
#include "klog.h"
#include "kmalloc.h"
#include "proc_fd.h"
#include "syscall_abi.h"
#include "timer.h"
#include "usermode.h"
#include "utils.h"

#define SCHED_MAX_TASKS         12
#define SCHED_TASK_NAME_MAX     16
#define SCHED_CMDLINE_MAX       128U
#define SCHED_DEFAULT_STACK     (16U * 1024U)
#define SCHED_DEFAULT_TIMESLICE 5U
#define SCHED_WAIT_ANY_CHILD    (-1)

typedef enum {
    TASK_UNUSED = 0,
    TASK_RUNNABLE,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_WAIT_CHILD,
    TASK_ZOMBIE,
} task_state_t;

typedef enum {
    TASK_EXEC_KERNEL = 0,
    TASK_EXEC_USER_BOOTSTRAP,
} task_exec_mode_t;

struct task_user_ctx {
    uint32_t entry_eip;
    uint32_t entry_esp;
    uint32_t image_base;
    uint32_t image_size;
    uint32_t stack_base;
    uint32_t stack_size;
    uint32_t last_user_eip;
    uint32_t last_fault_addr;
    uint32_t last_fault_err;
    uint32_t last_syscall_nr;
    uint32_t syscall_count;
    uint32_t fault_count;
    uint32_t cmdline_len;
    char cmdline[SCHED_CMDLINE_MAX];
    int32_t exit_code;
    int exit_code_valid;
};

struct task {
    int pid;
    char name[SCHED_TASK_NAME_MAX];
    task_state_t state;
    task_exec_mode_t exec_mode;
    uint32_t esp;
    uint8_t *stack_base;
    uint32_t stack_size;
    sched_task_entry_t entry;
    void *arg;
    uint64_t wake_tick;
    uint32_t timeslice_left;
    uint32_t switches;
    uint32_t ticks_run;
    uint32_t preemptions;
    int parent_pid;
    int wait_target_pid;
    struct proc_fd_table fd_table;
    struct task_user_ctx user;
};

extern void sched_context_switch(uint32_t *old_esp, uint32_t new_esp);

static struct task g_tasks[SCHED_MAX_TASKS];
static struct task *g_current_task;
static struct task *g_idle_task;
static uint32_t g_bootstrap_esp;
static uint32_t g_next_pid = 1;
static uint32_t g_rr_cursor;
static int g_sched_started;
static int g_in_scheduler;
static uint32_t g_context_switches;
static uint32_t g_idle_ticks;
static uint32_t g_need_resched;
static void *g_deferred_stack_free;

static inline void sched_cli(void) {
    __asm__ __volatile__("cli");
}

static inline void sched_sti(void) {
    __asm__ __volatile__("sti");
}

static void sched_panic_unexpected_return(void) __attribute__((noreturn));
static int sched_spawn_task_internal(const char *name,
                                     sched_task_entry_t entry,
                                     void *arg,
                                     uint32_t stack_size,
                                     int parent_pid,
                                     int *out_pid);
static void sched_copy_task_name(char *dst, const char *src);
static struct task *sched_find_task_by_pid_locked(int pid);
static void sched_maybe_wake_parent_waiter_locked(int parent_pid, int child_pid);
static void sched_reap_task_locked(struct task *task);
static void sched_orphan_or_reap_children_locked(int parent_pid);
static int sched_find_matching_zombie_child_locked(struct task *parent,
                                                   int target_pid,
                                                   struct task **out_zombie,
                                                   int *out_has_matching_child);
static void sched_flush_deferred_stack_free_locked(void);
static int sched_range_ok(uint32_t addr, uint32_t len, uint32_t base, uint32_t size);
static int sched_ranges_overlap(uint32_t base_a,
                                uint32_t size_a,
                                uint32_t base_b,
                                uint32_t size_b);
static uint32_t sched_state_to_syscall_state(task_state_t state);

static void sched_panic_unexpected_return(void) {
    KLOGP("scheduler returned to unexpected context");
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

static struct task *sched_find_task_by_pid_locked(int pid) {
    for (uint32_t i = 0; i < SCHED_MAX_TASKS; i++) {
        struct task *task = &g_tasks[i];
        if (task->state == TASK_UNUSED) {
            continue;
        }
        if (task->pid == pid) {
            return task;
        }
    }
    return 0;
}

static void sched_maybe_wake_parent_waiter_locked(int parent_pid, int child_pid) {
    struct task *parent;

    if (parent_pid <= 0) {
        return;
    }
    parent = sched_find_task_by_pid_locked(parent_pid);
    if (!parent) {
        return;
    }
    if (parent->state != TASK_WAIT_CHILD) {
        return;
    }
    if (parent->wait_target_pid != SCHED_WAIT_ANY_CHILD &&
        parent->wait_target_pid != child_pid) {
        return;
    }

    parent->state = TASK_RUNNABLE;
    parent->timeslice_left = SCHED_DEFAULT_TIMESLICE;
}

static void sched_reap_task_locked(struct task *task) {
    struct kmalloc_stats stats;
    int pid;
    char name[SCHED_TASK_NAME_MAX];
    uint8_t *stack_base;

    if (!task || task->state == TASK_UNUSED || task == g_current_task) {
        return;
    }

    pid = task->pid;
    sched_copy_task_name(name, task->name);
    stack_base = task->stack_base;
    if (task->name[0] != '\0') {
        usermode_notify_task_reaped(task->name);
    }
    if (stack_base) {
        kfree(stack_base);
        kmalloc_get_stats(&stats);
        KLOGI("sched: task stack reclaimed pid=%d name=%s live_large=%u",
              pid, name, (uint32_t)stats.large_bytes_used);
    }
    memset(task, 0, sizeof(*task));
    task->state = TASK_UNUSED;
    task->parent_pid = -1;
    proc_fd_table_init(&task->fd_table);
}

static void sched_orphan_or_reap_children_locked(int parent_pid) {
    for (uint32_t i = 0; i < SCHED_MAX_TASKS; i++) {
        struct task *child = &g_tasks[i];
        if (child->state == TASK_UNUSED || child->parent_pid != parent_pid) {
            continue;
        }
        if (child->state == TASK_ZOMBIE) {
            sched_reap_task_locked(child);
            continue;
        }
        child->parent_pid = -1;
    }
}

static int sched_find_matching_zombie_child_locked(struct task *parent,
                                                   int target_pid,
                                                   struct task **out_zombie,
                                                   int *out_has_matching_child) {
    if (!parent || !out_zombie || !out_has_matching_child) {
        return -KERR_INVAL;
    }

    *out_zombie = 0;
    *out_has_matching_child = 0;

    for (uint32_t i = 0; i < SCHED_MAX_TASKS; i++) {
        struct task *child = &g_tasks[i];
        if (child->state == TASK_UNUSED) {
            continue;
        }
        if (child->parent_pid != parent->pid) {
            continue;
        }
        if (target_pid != SCHED_WAIT_ANY_CHILD && child->pid != target_pid) {
            continue;
        }

        *out_has_matching_child = 1;
        if (child->state == TASK_ZOMBIE) {
            *out_zombie = child;
            return 0;
        }
    }
    return 0;
}

static void sched_flush_deferred_stack_free_locked(void) {
    struct kmalloc_stats stats;

    if (!g_deferred_stack_free) {
        return;
    }

    kfree(g_deferred_stack_free);
    g_deferred_stack_free = 0;
    kmalloc_get_stats(&stats);
    KLOGI("sched: deferred stack reclaimed live_large=%u",
          (uint32_t)stats.large_bytes_used);
}

static int sched_range_ok(uint32_t addr, uint32_t len, uint32_t base, uint32_t size) {
    uint32_t end;
    uint32_t region_end;

    if (len == 0U) {
        return 1;
    }
    if (size == 0U) {
        return 0;
    }
    if (addr < base) {
        return 0;
    }
    end = addr + len;
    if (end < addr) {
        return 0;
    }
    region_end = base + size;
    if (region_end < base) {
        return 0;
    }
    return end <= region_end;
}

static int sched_ranges_overlap(uint32_t base_a,
                                uint32_t size_a,
                                uint32_t base_b,
                                uint32_t size_b) {
    uint32_t end_a;
    uint32_t end_b;

    if (size_a == 0U || size_b == 0U) {
        return 0;
    }
    end_a = base_a + size_a;
    end_b = base_b + size_b;
    if (end_a < base_a || end_b < base_b) {
        return 1;
    }
    return base_a < end_b && base_b < end_a;
}

static void sched_copy_task_name(char *dst, const char *src) {
    size_t i = 0;
    if (!src) {
        src = "task";
    }
    for (; i + 1 < SCHED_TASK_NAME_MAX && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static const char *sched_state_name(task_state_t state) {
    switch (state) {
        case TASK_UNUSED:    return "unused";
        case TASK_RUNNABLE:  return "runnable";
        case TASK_RUNNING:   return "running";
        case TASK_SLEEPING:  return "sleep";
        case TASK_WAIT_CHILD:return "wait-child";
        case TASK_ZOMBIE:    return "zombie";
        default:             return "unknown";
    }
}

static uint32_t sched_state_to_syscall_state(task_state_t state) {
    switch (state) {
        case TASK_UNUSED:     return SYSCALL_TASK_STATE_UNUSED;
        case TASK_RUNNABLE:   return SYSCALL_TASK_STATE_RUNNABLE;
        case TASK_RUNNING:    return SYSCALL_TASK_STATE_RUNNING;
        case TASK_SLEEPING:   return SYSCALL_TASK_STATE_SLEEPING;
        case TASK_WAIT_CHILD: return SYSCALL_TASK_STATE_WAIT_CHILD;
        case TASK_ZOMBIE:     return SYSCALL_TASK_STATE_ZOMBIE;
        default:              return SYSCALL_TASK_STATE_UNUSED;
    }
}

static struct task *sched_find_task_slot(void) {
    for (uint32_t i = 0; i < SCHED_MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_UNUSED) {
            return &g_tasks[i];
        }
    }
    return 0;
}

static void sched_wake_sleepers_locked(void) {
    uint64_t now = timer_ticks;
    for (uint32_t i = 0; i < SCHED_MAX_TASKS; i++) {
        struct task *t = &g_tasks[i];
        if (t->state == TASK_SLEEPING && now >= t->wake_tick) {
            t->state = TASK_RUNNABLE;
            t->timeslice_left = SCHED_DEFAULT_TIMESLICE;
        }
    }
}

static uint32_t sched_count_runnable_locked(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < SCHED_MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_RUNNABLE || g_tasks[i].state == TASK_RUNNING) {
            count++;
        }
    }
    return count;
}

static struct task *sched_pick_next_locked(void) {
    uint32_t start = g_rr_cursor;
    for (uint32_t n = 0; n < SCHED_MAX_TASKS; n++) {
        uint32_t idx = (start + n) % SCHED_MAX_TASKS;
        struct task *t = &g_tasks[idx];
        if (t == g_current_task || t == g_idle_task) {
            continue;
        }
        if (t->state == TASK_RUNNABLE) {
            g_rr_cursor = (idx + 1U) % SCHED_MAX_TASKS;
            return t;
        }
    }

    if (g_current_task && g_current_task->state == TASK_RUNNING) {
        return g_current_task;
    }
    if (g_current_task && g_current_task->state == TASK_RUNNABLE) {
        return g_current_task;
    }
    if (g_idle_task && g_idle_task->state == TASK_RUNNABLE) {
        return g_idle_task;
    }
    return 0;
}

static void sched_switch_locked(struct task *next) {
    struct task *prev;
    uint32_t *save_slot;

    KASSERT(next != 0);
    if (g_in_scheduler) {
        return;
    }
    if (next == g_current_task && g_sched_started) {
        if (next->state == TASK_RUNNABLE) {
            next->state = TASK_RUNNING;
        }
        return;
    }

    g_in_scheduler = 1;
    prev = g_current_task;
    if (prev && prev->state == TASK_RUNNING) {
        prev->state = TASK_RUNNABLE;
    }

    next->state = TASK_RUNNING;
    next->timeslice_left = SCHED_DEFAULT_TIMESLICE;
    next->switches++;
    g_current_task = next;
    g_context_switches++;
    tss_set_kernel_stack((uint32_t)(uintptr_t)(next->stack_base + next->stack_size));

    save_slot = prev ? &prev->esp : &g_bootstrap_esp;
    sched_context_switch(save_slot, next->esp);

    sched_flush_deferred_stack_free_locked();
    g_in_scheduler = 0;
}

static void sched_schedule_locked(void) {
    struct task *next;
    sched_wake_sleepers_locked();
    next = sched_pick_next_locked();
    if (!next) {
        KLOGP("scheduler found no runnable task (idle missing)");
        sched_panic_unexpected_return();
    }
    sched_switch_locked(next);
}

static void sched_task_exit(void) __attribute__((noreturn));

static void sched_task_trampoline(void) {
    struct task *self = g_current_task;
    g_in_scheduler = 0;
    KASSERT(self != 0);
    KASSERT(self->entry != 0);
    KLOGI("sched: task start pid=%d name=%s", self->pid, self->name);
    sched_sti();
    self->entry(self->arg);
    sched_task_exit();
}

static void sched_init_task_stack(struct task *task) {
    uint32_t *sp;
    uintptr_t top;

    KASSERT(task != 0);
    KASSERT(task->stack_base != 0);
    top = (uintptr_t)(task->stack_base + task->stack_size);
    top &= ~((uintptr_t)0xF);
    sp = (uint32_t *)top;

    /* Initial context expected by sched_context_switch:
     * [edi][esi][ebx][ebp][ret=sched_task_trampoline]
     */
    *--sp = (uint32_t)(uintptr_t)sched_task_trampoline; /* ret */
    *--sp = 0; /* ebp */
    *--sp = 0; /* ebx */
    *--sp = 0; /* esi */
    *--sp = 0; /* edi */
    task->esp = (uint32_t)(uintptr_t)sp;
}

static void sched_idle_task(void *arg) {
    (void)arg;
    uint32_t last_report = 0;
    KLOGI("sched: idle task online");
    for (;;) {
        uint32_t now = (uint32_t)timer_ticks;
        if ((uint32_t)(now - last_report) >= 500U) {
            last_report = now;
            KLOGI("sched: idle heartbeat tick=%u idle_ticks=%u", now, g_idle_ticks);
        }
        sched_checkpoint();
        __asm__ __volatile__("hlt");
    }
}

void sched_init(void) {
    memset(g_tasks, 0, sizeof(g_tasks));
    g_current_task = 0;
    g_idle_task = 0;
    g_bootstrap_esp = 0;
    g_next_pid = 1;
    g_rr_cursor = 0;
    g_sched_started = 0;
    g_in_scheduler = 0;
    g_context_switches = 0;
    g_idle_ticks = 0;
    g_need_resched = 0;
    g_deferred_stack_free = 0;

    KLOGI("sched: init");
    if (sched_spawn_kernel_task("idle", sched_idle_task, 0, SCHED_DEFAULT_STACK) < 0) {
        KLOGP("sched: failed to create idle task");
        sched_panic_unexpected_return();
    }
    g_idle_task = &g_tasks[0];
    KASSERT(g_idle_task->entry == sched_idle_task);
}

static int sched_spawn_task_internal(const char *name,
                                     sched_task_entry_t entry,
                                     void *arg,
                                     uint32_t stack_size,
                                     int parent_pid,
                                     int *out_pid) {
    struct task *task;
    uint8_t *stack;

    if (!entry) {
        return -KERR_INVAL;
    }
    if (out_pid) {
        *out_pid = -1;
    }
    if (stack_size == 0) {
        stack_size = SCHED_DEFAULT_STACK;
    }
    stack_size = (stack_size + 0xFFFU) & ~0xFFFU;

    task = sched_find_task_slot();
    if (!task) {
        return -KERR_NOMEM;
    }

    stack = (uint8_t *)kmalloc(stack_size);
    if (!stack) {
        return -KERR_NOMEM;
    }

    memset(task, 0, sizeof(*task));
    memset(stack, 0, stack_size);
    task->pid = (int)g_next_pid++;
    sched_copy_task_name(task->name, name);
    task->state = TASK_RUNNABLE;
    task->exec_mode = TASK_EXEC_KERNEL;
    task->parent_pid = parent_pid;
    task->wait_target_pid = 0;
    task->stack_base = stack;
    task->stack_size = stack_size;
    task->entry = entry;
    task->arg = arg;
    task->timeslice_left = SCHED_DEFAULT_TIMESLICE;
    proc_fd_table_init(&task->fd_table);
    sched_init_task_stack(task);

    KLOGI("sched: task created pid=%d name=%s stack=%u bytes state=%s",
          task->pid, task->name, task->stack_size, sched_state_name(task->state));
    if (out_pid) {
        *out_pid = task->pid;
    }
    return 0;
}

int sched_spawn_kernel_task(const char *name, sched_task_entry_t entry, void *arg, uint32_t stack_size) {
    return sched_spawn_task_internal(name, entry, arg, stack_size, -1, 0);
}

int sched_spawn_user_child_task(const char *name,
                                sched_task_entry_t entry,
                                void *arg,
                                uint32_t stack_size,
                                int *out_pid) {
    if (!g_current_task || !sched_current_task_is_user()) {
        return -KERR_NOTSUP;
    }
    if (!out_pid) {
        return -KERR_INVAL;
    }
    return sched_spawn_task_internal(name, entry, arg, stack_size, g_current_task->pid, out_pid);
}

int sched_mark_current_task_user_bootstrap(uint32_t user_eip, uint32_t user_esp) {
    struct task *task = g_current_task;

    if (!task) {
        return -KERR_INVAL;
    }

    task->exec_mode = TASK_EXEC_USER_BOOTSTRAP;
    task->user.entry_eip = user_eip;
    task->user.entry_esp = user_esp;
    task->user.last_user_eip = user_eip;
    return 0;
}

int sched_set_current_user_layout(uint32_t image_base,
                                  uint32_t image_size,
                                  uint32_t stack_base,
                                  uint32_t stack_size) {
    struct task *task = g_current_task;

    if (!task) {
        return -KERR_INVAL;
    }
    if ((image_size != 0U && image_base + image_size < image_base) ||
        (stack_size != 0U && stack_base + stack_size < stack_base)) {
        return -KERR_INVAL;
    }

    task->user.image_base = image_base;
    task->user.image_size = image_size;
    task->user.stack_base = stack_base;
    task->user.stack_size = stack_size;
    return 0;
}

int sched_current_user_range_ok(uint32_t addr, uint32_t len) {
    struct task *task = g_current_task;

    if (len == 0U) {
        return 1;
    }
    if (!task || task->exec_mode != TASK_EXEC_USER_BOOTSTRAP) {
        return 0;
    }
    if (sched_range_ok(addr, len, task->user.image_base, task->user.image_size)) {
        return 1;
    }
    if (sched_range_ok(addr, len, task->user.stack_base, task->user.stack_size)) {
        return 1;
    }
    return 0;
}

int sched_set_current_task_cmdline(const char *src, uint32_t len) {
    struct task *task = g_current_task;
    uint32_t copy_len;

    if (!task) {
        return -KERR_INVAL;
    }
    if (!src && len != 0U) {
        return -KERR_INVAL;
    }
    if (len >= SCHED_CMDLINE_MAX) {
        return -KERR_INVAL;
    }

    task->user.cmdline_len = 0U;
    task->user.cmdline[0] = '\0';
    if (len == 0U) {
        return 0;
    }

    copy_len = len;
    memcpy(task->user.cmdline, src, copy_len);
    task->user.cmdline[copy_len] = '\0';
    task->user.cmdline_len = copy_len;
    return 0;
}

int sched_copy_current_task_cmdline_to_user(uint32_t dst_addr, uint32_t dst_len, uint32_t *out_len) {
    struct task *task = g_current_task;
    uint32_t copy_len = 0;

    if (!out_len) {
        return -KERR_INVAL;
    }
    *out_len = 0U;
    if (!task || task->exec_mode != TASK_EXEC_USER_BOOTSTRAP) {
        return -KERR_NOTSUP;
    }
    if (dst_len == 0U) {
        return 0;
    }
    if (!sched_current_user_range_ok(dst_addr, dst_len)) {
        return -KERR_FAULT;
    }

    if (task->user.cmdline_len < (dst_len - 1U)) {
        copy_len = task->user.cmdline_len;
    } else {
        copy_len = dst_len - 1U;
    }
    if (copy_len != 0U) {
        memcpy((void *)(uintptr_t)dst_addr, task->user.cmdline, copy_len);
    }
    ((char *)(uintptr_t)dst_addr)[copy_len] = '\0';
    *out_len = copy_len;
    return 0;
}

int sched_user_image_range_available(uint32_t image_base, uint32_t image_size) {
    if (image_size == 0U || image_base + image_size < image_base) {
        return 0;
    }

    for (uint32_t i = 0; i < SCHED_MAX_TASKS; i++) {
        const struct task *task = &g_tasks[i];

        if (task->state == TASK_UNUSED || task->exec_mode != TASK_EXEC_USER_BOOTSTRAP) {
            continue;
        }
        if (sched_ranges_overlap(image_base,
                                 image_size,
                                 task->user.image_base,
                                 task->user.image_size) ||
            sched_ranges_overlap(image_base,
                                 image_size,
                                 task->user.stack_base,
                                 task->user.stack_size)) {
            return 0;
        }
    }

    return 1;
}

void sched_note_current_syscall(uint32_t syscall_nr) {
    struct task *task = g_current_task;

    if (!task || task->exec_mode != TASK_EXEC_USER_BOOTSTRAP) {
        return;
    }

    task->user.last_syscall_nr = syscall_nr;
    task->user.syscall_count++;
}

void sched_note_current_user_fault(uint32_t user_eip, uint32_t fault_addr, uint32_t error_code) {
    struct task *task = g_current_task;

    if (!task || task->exec_mode != TASK_EXEC_USER_BOOTSTRAP) {
        return;
    }

    task->user.last_user_eip = user_eip;
    task->user.last_fault_addr = fault_addr;
    task->user.last_fault_err = error_code;
    task->user.fault_count++;
}

void sched_note_current_exit_code(int32_t exit_code) {
    struct task *task = g_current_task;

    if (!task || task->exec_mode != TASK_EXEC_USER_BOOTSTRAP) {
        return;
    }

    task->user.exit_code = exit_code;
    task->user.exit_code_valid = 1;
}

int sched_current_task_is_user(void) {
    return g_current_task && g_current_task->exec_mode == TASK_EXEC_USER_BOOTSTRAP;
}

int sched_current_task_pid(void) {
    if (!g_current_task) {
        return -1;
    }
    return g_current_task->pid;
}

const char *sched_current_task_name(void) {
    if (!g_current_task) {
        return "none";
    }
    return g_current_task->name;
}

int sched_current_task_owns_child_pid(int pid) {
    int owns = 0;
    struct task *child;

    if (pid <= 0 || !g_current_task || !sched_current_task_is_user()) {
        return 0;
    }

    sched_cli();
    child = sched_find_task_by_pid_locked(pid);
    if (child && child->parent_pid == g_current_task->pid) {
        owns = 1;
    }
    sched_sti();
    return owns;
}

int sched_collect_task_snapshot(struct syscall_task_snapshot_entry *entries,
                                uint32_t cap,
                                uint32_t *out_count) {
    uint32_t count = 0;

    if (!out_count) {
        return -KERR_INVAL;
    }
    *out_count = 0U;
    if (cap != 0U && !entries) {
        return -KERR_INVAL;
    }

    sched_cli();
    for (uint32_t i = 0; i < SCHED_MAX_TASKS; i++) {
        const struct task *task = &g_tasks[i];
        struct syscall_task_snapshot_entry *entry;

        if (task->state == TASK_UNUSED) {
            continue;
        }
        if (count >= cap) {
            break;
        }

        entry = &entries[count];
        memset(entry, 0, sizeof(*entry));
        entry->pid = task->pid;
        entry->parent_pid = task->parent_pid;
        entry->exit_code = task->user.exit_code_valid ? task->user.exit_code : 0;
        entry->state = sched_state_to_syscall_state(task->state);
        if (task->exec_mode == TASK_EXEC_USER_BOOTSTRAP) {
            entry->flags |= SYSCALL_TASK_FLAG_USER;
        }
        if (task->user.exit_code_valid) {
            entry->flags |= SYSCALL_TASK_FLAG_EXIT_VALID;
        }
        sched_copy_task_name(entry->name, task->name);
        count++;
    }
    sched_sti();

    *out_count = count;
    return 0;
}

int sched_current_process_fd_get(uint32_t fd, struct kfile **out_file) {
    if (!out_file) {
        return -KERR_INVAL;
    }
    *out_file = 0;
    if (!g_current_task) {
        return -KERR_INVAL;
    }
    if (!sched_current_task_is_user()) {
        return -KERR_NOTSUP;
    }
    return proc_fd_get(&g_current_task->fd_table, fd, out_file);
}

int sched_current_process_fd_install(uint32_t fd, const struct kfile *src_file, struct kfile **out_file) {
    if (!src_file) {
        return -KERR_INVAL;
    }
    if (out_file) {
        *out_file = 0;
    }
    if (!g_current_task) {
        return -KERR_INVAL;
    }
    if (!sched_current_task_is_user()) {
        return -KERR_NOTSUP;
    }
    return proc_fd_install(&g_current_task->fd_table, fd, src_file, out_file);
}

int sched_current_process_fd_alloc(const struct kfile *src_file, uint32_t *out_fd, struct kfile **out_file) {
    if (!src_file || !out_fd) {
        return -KERR_INVAL;
    }
    *out_fd = 0;
    if (out_file) {
        *out_file = 0;
    }
    if (!g_current_task) {
        return -KERR_INVAL;
    }
    if (!sched_current_task_is_user()) {
        return -KERR_NOTSUP;
    }
    return proc_fd_alloc(&g_current_task->fd_table, PROC_FD_DYNAMIC_MIN, src_file, out_fd, out_file);
}

int sched_current_process_fd_close(uint32_t fd) {
    if (!g_current_task) {
        return -KERR_INVAL;
    }
    if (!sched_current_task_is_user()) {
        return -KERR_NOTSUP;
    }
    return proc_fd_close(&g_current_task->fd_table, fd);
}

int sched_waitpid(int target_pid, int *out_waited_pid, int32_t *out_exit_code) {
    struct task *parent;

    if (!out_waited_pid) {
        return -KERR_INVAL;
    }
    *out_waited_pid = -1;
    if (out_exit_code) {
        *out_exit_code = 0;
    }
    if (!g_current_task || !sched_current_task_is_user()) {
        return -KERR_NOTSUP;
    }
    if (target_pid <= 0 && target_pid != SCHED_WAIT_ANY_CHILD) {
        return -KERR_INVAL;
    }

    parent = g_current_task;
    for (;;) {
        struct task *zombie = 0;
        int has_matching_child = 0;
        int rc;

        sched_cli();
        rc = sched_find_matching_zombie_child_locked(parent,
                                                     target_pid,
                                                     &zombie,
                                                     &has_matching_child);
        if (rc < 0) {
            sched_sti();
            return rc;
        }

        if (zombie) {
            int waited_pid = zombie->pid;
            int32_t waited_exit = zombie->user.exit_code_valid ? zombie->user.exit_code : 0;

            sched_reap_task_locked(zombie);
            parent->wait_target_pid = 0;
            sched_sti();

            *out_waited_pid = waited_pid;
            if (out_exit_code) {
                *out_exit_code = waited_exit;
            }
            return 0;
        }

        if (!has_matching_child) {
            sched_sti();
            return -KERR_NOENT;
        }

        parent->wait_target_pid = target_pid;
        parent->state = TASK_WAIT_CHILD;
        g_need_resched = 0;
        sched_schedule_locked();
        sched_sti();
    }
}

void sched_start(void) {
    sched_cli();
    KASSERT(!g_sched_started);
    g_sched_started = 1;
    sched_schedule_locked();
    sched_sti();
    sched_panic_unexpected_return();
}

void sched_yield(void) {
    if (!g_sched_started || !g_current_task) {
        return;
    }
    sched_cli();
    g_need_resched = 0;
    sched_schedule_locked();
    sched_sti();
}

void sched_exit_current(void) {
    sched_task_exit();
}

void sched_sleep_ticks(uint32_t ticks) {
    if (!g_sched_started || !g_current_task) {
        return;
    }
    if (ticks == 0) {
        sched_yield();
        return;
    }
    sched_cli();
    g_current_task->wake_tick = timer_ticks + (uint64_t)ticks;
    g_current_task->state = TASK_SLEEPING;
    g_need_resched = 0;
    sched_schedule_locked();
    sched_sti();
}

void sched_checkpoint(void) {
    if (g_need_resched) {
        sched_yield();
    }
}

void sched_on_timer_tick_irq(void) {
    struct task *next;

    if (!g_sched_started || !g_current_task || g_in_scheduler) {
        return;
    }

    sched_wake_sleepers_locked();

    g_current_task->ticks_run++;
    if (g_current_task == g_idle_task) {
        g_idle_ticks++;
    }

    if (g_current_task->timeslice_left > 0) {
        g_current_task->timeslice_left--;
    }
    if (g_current_task->timeslice_left > 0) {
        return;
    }

    g_current_task->timeslice_left = SCHED_DEFAULT_TIMESLICE;
    next = sched_pick_next_locked();
    if (!next || next == g_current_task) {
        return;
    }

    g_current_task->preemptions++;
    g_need_resched = 0;
    sched_switch_locked(next);
}

static void sched_task_exit(void) {
    sched_cli();
    if (g_current_task) {
        struct task *self = g_current_task;
        uint32_t closed_fds = 0;
        int keep_zombie = 0;

        if (self->exec_mode == TASK_EXEC_USER_BOOTSTRAP) {
            closed_fds = proc_fd_close_all(&self->fd_table);
            if (closed_fds != 0U) {
                KLOGI("sched: process fds closed pid=%d name=%s count=%u",
                      self->pid, self->name, closed_fds);
            }
            if (self->user.exit_code_valid) {
                KLOGI("sched: user task stats pid=%d name=%s syscalls=%u faults=%u last_syscall=%u exit=%d",
                      self->pid,
                      self->name,
                      self->user.syscall_count,
                      self->user.fault_count,
                      self->user.last_syscall_nr,
                      self->user.exit_code);
            } else {
                KLOGI("sched: user task stats pid=%d name=%s syscalls=%u faults=%u last_fault_eip=%x cr2=%x err=%x",
                      self->pid,
                      self->name,
                      self->user.syscall_count,
                      self->user.fault_count,
                      self->user.last_user_eip,
                      self->user.last_fault_addr,
                      self->user.last_fault_err);
            }
        }
        KLOGI("sched: task exit pid=%d name=%s ticks=%u switches=%u preemptions=%u",
              self->pid,
              self->name,
              self->ticks_run,
              self->switches,
              self->preemptions);

        sched_orphan_or_reap_children_locked(self->pid);

        if (self->parent_pid > 0) {
            struct task *parent = sched_find_task_by_pid_locked(self->parent_pid);
            if (parent && parent->state != TASK_UNUSED && parent->state != TASK_ZOMBIE) {
                keep_zombie = 1;
            }
        }

        if (keep_zombie) {
            self->state = TASK_ZOMBIE;
            sched_maybe_wake_parent_waiter_locked(self->parent_pid, self->pid);
        } else {
            if (self->name[0] != '\0') {
                usermode_notify_task_reaped(self->name);
            }
            if (g_deferred_stack_free) {
                sched_flush_deferred_stack_free_locked();
            }
            g_deferred_stack_free = self->stack_base;
            self->state = TASK_UNUSED;
            self->pid = 0;
            self->exec_mode = TASK_EXEC_KERNEL;
            self->parent_pid = -1;
            self->wait_target_pid = 0;
            self->stack_base = 0;
            self->stack_size = 0;
        }
    }
    sched_schedule_locked();
    sched_panic_unexpected_return();
}

uint32_t sched_runnable_count(void) {
    uint32_t count;
    sched_cli();
    count = sched_count_runnable_locked();
    sched_sti();
    return count;
}
