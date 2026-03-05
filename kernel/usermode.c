#include "usermode.h"

#include <stdint.h>

#include "elf32_loader.h"
#include "gdt.h"
#include "kassert.h"
#include "kerrno.h"
#include "klog.h"
#include "memory_layout.h"
#include "paging.h"
#include "sched.h"
#include "syscall_abi.h"
#include "utils.h"
#include "vfs.h"

extern uint8_t user_demo_blob_start;
extern uint8_t user_demo_blob_end;

static int g_user_demo_spawned;
static int g_user_demo_pages_prepared;
static int g_user_fault_spawned;
static int g_user_fault_stack_prepared;
static int g_user_elf_demo_a_spawned;
static int g_user_elf_demo_b_spawned;
static int g_user_shell_spawned;

#define USERMODE_SPAWN_PATH_MAX 64U
#define USERMODE_CMDLINE_MAX    128U
#define USERMODE_ARG_MAX        16U

struct usermode_elf_demo_cfg {
    const char *path;
    const char *tag;
    uint32_t image_base;
    uint32_t image_size;
    uint32_t stack_base;
    uint32_t stack_size;
};

struct usermode_child_slot {
    char path[USERMODE_SPAWN_PATH_MAX];
    char cmdline[USERMODE_CMDLINE_MAX];
    const char *tag;
    const char *task_name;
    uint32_t image_base;
    uint32_t image_size;
    uint32_t stack_base;
    uint32_t stack_size;
    uint32_t cmdline_len;
    int used;
};

static const struct usermode_elf_demo_cfg g_user_elf_demo_a_cfg = {
    .path = "/bin/hello.elf",
    .tag = "elf-demo-a",
    .image_base = USER_ELF_SLOT0_IMAGE_BASE,
    .image_size = USER_ELF_SLOT0_IMAGE_SIZE_BYTES,
    .stack_base = USER_ELF_SLOT0_STACK_BASE,
    .stack_size = USER_ELF_SLOT0_STACK_SIZE_BYTES,
};

static const struct usermode_elf_demo_cfg g_user_elf_demo_b_cfg = {
    .path = "/bin/hello2.elf",
    .tag = "elf-demo-b",
    .image_base = USER_ELF_SLOT1_IMAGE_BASE,
    .image_size = USER_ELF_SLOT1_IMAGE_SIZE_BYTES,
    .stack_base = USER_ELF_SLOT1_STACK_BASE,
    .stack_size = USER_ELF_SLOT1_STACK_SIZE_BYTES,
};

static const struct usermode_elf_demo_cfg g_user_shell_cfg = {
    .path = "/bin/sh.elf",
    .tag = "shell-bootstrap",
    .image_base = USER_ELF_SLOT4_IMAGE_BASE,
    .image_size = USER_ELF_SLOT4_IMAGE_SIZE_BYTES,
    .stack_base = USER_ELF_SLOT4_STACK_BASE,
    .stack_size = USER_ELF_SLOT4_STACK_SIZE_BYTES,
};

static struct usermode_child_slot g_user_child_slot2 = {
    .path = { 0 },
    .cmdline = { 0 },
    .tag = "child-slot-a",
    .task_name = "user-spawn-c",
    .image_base = 0U,
    .image_size = 0U,
    .stack_base = USER_ELF_SLOT2_STACK_BASE,
    .stack_size = USER_ELF_SLOT2_STACK_SIZE_BYTES,
    .cmdline_len = 0U,
    .used = 0,
};

static struct usermode_child_slot g_user_child_slot3 = {
    .path = { 0 },
    .cmdline = { 0 },
    .tag = "child-slot-b",
    .task_name = "user-spawn-d",
    .image_base = 0U,
    .image_size = 0U,
    .stack_base = USER_ELF_SLOT3_STACK_BASE,
    .stack_size = USER_ELF_SLOT3_STACK_SIZE_BYTES,
    .cmdline_len = 0U,
    .used = 0,
};

static struct usermode_child_slot g_user_child_slot5 = {
    .path = { 0 },
    .cmdline = { 0 },
    .tag = "child-slot-c",
    .task_name = "user-cat",
    .image_base = 0U,
    .image_size = 0U,
    .stack_base = USER_ELF_SLOT5_STACK_BASE,
    .stack_size = USER_ELF_SLOT5_STACK_SIZE_BYTES,
    .cmdline_len = 0U,
    .used = 0,
};

static struct usermode_child_slot *g_user_child_slots[] = {
    &g_user_child_slot2,
    &g_user_child_slot3,
    &g_user_child_slot5,
};

#define USERMODE_CHILD_SLOT_COUNT \
    ((uint32_t)(sizeof(g_user_child_slots) / sizeof(g_user_child_slots[0])))

static int usermode_str_eq(const char *a, const char *b) {
    uint32_t i = 0;

    if (!a || !b) {
        return 0;
    }
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int usermode_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int usermode_copy_cstr(char *dst, uint32_t dst_cap, const char *src) {
    uint32_t i;

    if (!dst || !src || dst_cap == 0U) {
        return -KERR_INVAL;
    }
    for (i = 0; i + 1U < dst_cap; i++) {
        char c = src[i];
        dst[i] = c;
        if (c == '\0') {
            return 0;
        }
    }
    dst[dst_cap - 1U] = '\0';
    return -KERR_INVAL;
}

static int usermode_prepare_child_start_stack(const struct usermode_child_slot *slot,
                                              uint32_t stack_top,
                                              uint32_t *out_entry_esp) {
    const char *argv_src[USERMODE_ARG_MAX];
    uint32_t argv_len[USERMODE_ARG_MAX];
    uint32_t argv_user[USERMODE_ARG_MAX];
    uint32_t argc = 0;
    uint32_t sp = stack_top;
    uint32_t argv_table_addr;
    uint32_t frame_addr;
    uint32_t idx = 0;

    if (!slot || !out_entry_esp) {
        return -KERR_INVAL;
    }
    if (slot->path[0] == '\0') {
        return -KERR_INVAL;
    }

    argv_src[argc] = slot->path;
    argv_len[argc] = (uint32_t)strlen(slot->path);
    argc++;

    while (idx < slot->cmdline_len) {
        uint32_t start;

        while (idx < slot->cmdline_len && usermode_is_space(slot->cmdline[idx])) {
            idx++;
        }
        if (idx >= slot->cmdline_len) {
            break;
        }

        start = idx;
        while (idx < slot->cmdline_len && !usermode_is_space(slot->cmdline[idx])) {
            idx++;
        }
        if (argc >= USERMODE_ARG_MAX) {
            return -KERR_NOMEM;
        }

        argv_src[argc] = slot->cmdline + start;
        argv_len[argc] = idx - start;
        argc++;
    }

    for (uint32_t i = argc; i > 0U; i--) {
        uint32_t arg_len = argv_len[i - 1U];

        if (sp < slot->stack_base + arg_len + 1U) {
            return -KERR_NOMEM;
        }
        sp -= arg_len + 1U;
        memcpy((void *)(uintptr_t)sp, argv_src[i - 1U], arg_len);
        ((char *)(uintptr_t)sp)[arg_len] = '\0';
        argv_user[i - 1U] = sp;
    }

    sp &= ~0x3U;
    if (sp < slot->stack_base + ((argc + 4U) * (uint32_t)sizeof(uint32_t))) {
        return -KERR_NOMEM;
    }

    argv_table_addr = sp - ((argc + 1U) * (uint32_t)sizeof(uint32_t));
    for (uint32_t i = 0; i < argc; i++) {
        ((uint32_t *)(uintptr_t)argv_table_addr)[i] = argv_user[i];
    }
    ((uint32_t *)(uintptr_t)argv_table_addr)[argc] = 0U;

    frame_addr = argv_table_addr - (3U * (uint32_t)sizeof(uint32_t));
    ((uint32_t *)(uintptr_t)frame_addr)[0] = 0U;
    ((uint32_t *)(uintptr_t)frame_addr)[1] = argc;
    ((uint32_t *)(uintptr_t)frame_addr)[2] = argv_table_addr;

    *out_entry_esp = frame_addr;
    return 0;
}

static struct usermode_child_slot *usermode_child_slot_acquire(uint32_t image_base, uint32_t image_size) {
    for (uint32_t i = 0; i < USERMODE_CHILD_SLOT_COUNT; i++) {
        struct usermode_child_slot *slot = g_user_child_slots[i];
        if (!slot || slot->used) {
            continue;
        }
        if ((image_base < (slot->stack_base + slot->stack_size)) &&
            (slot->stack_base < (image_base + image_size))) {
            continue;
        }
        if (slot) {
            return slot;
        }
    }
    return 0;
}

static struct usermode_child_slot *usermode_child_slot_by_task_name(const char *task_name) {
    for (uint32_t i = 0; i < USERMODE_CHILD_SLOT_COUNT; i++) {
        struct usermode_child_slot *slot = g_user_child_slots[i];
        if (slot && slot->task_name && usermode_str_eq(task_name, slot->task_name)) {
            return slot;
        }
    }
    return 0;
}

static void usermode_prepare_user_rw_page(uint32_t base, uint32_t size, const char *tag) {
    int rc;
    uint8_t *dst = (uint8_t *)(uintptr_t)base;

    memset(dst, 0x00, size);
    rc = paging_mark_user_region(base, size);
    if (rc < 0) {
        KLOGP("usermode: failed to mark %s page user (rc=%d)", tag ? tag : "user", rc);
        for (;;) { __asm__ __volatile__("hlt"); }
    }
}

static void usermode_demo_prepare_pages(void) {
    uint32_t code_len = (uint32_t)((uintptr_t)&user_demo_blob_end - (uintptr_t)&user_demo_blob_start);
    uint8_t *code_dst = (uint8_t *)(uintptr_t)USER_DEMO_CODE_BASE;
    uint8_t *stack_dst = (uint8_t *)(uintptr_t)USER_DEMO_STACK_BASE;

    if (g_user_demo_pages_prepared) {
        return;
    }

    if (code_len > USER_DEMO_CODE_SIZE_BYTES) {
        KLOGP("usermode: demo blob too large (%u > %u)", code_len, USER_DEMO_CODE_SIZE_BYTES);
        for (;;) {
            __asm__ __volatile__("hlt");
        }
    }

    memset(code_dst, 0x90, USER_DEMO_CODE_SIZE_BYTES); /* fill with NOPs */
    memcpy(code_dst, &user_demo_blob_start, code_len);
    memset(stack_dst, 0x00, USER_DEMO_STACK_SIZE_BYTES);

    int rc = paging_mark_user_region(USER_DEMO_CODE_BASE, USER_DEMO_CODE_SIZE_BYTES);
    if (rc < 0) {
        KLOGP("usermode: failed to mark code page user (rc=%d)", rc);
        for (;;) { __asm__ __volatile__("hlt"); }
    }
    rc = paging_mark_user_region(USER_DEMO_STACK_BASE, USER_DEMO_STACK_SIZE_BYTES);
    if (rc < 0) {
        KLOGP("usermode: failed to mark stack page user (rc=%d)", rc);
        for (;;) { __asm__ __volatile__("hlt"); }
    }

    g_user_demo_pages_prepared = 1;
    KLOGI("usermode: demo pages prepared code=%x..%x stack=%x..%x size=%u",
          USER_DEMO_CODE_BASE,
          USER_DEMO_CODE_BASE + USER_DEMO_CODE_SIZE_BYTES,
          USER_DEMO_STACK_BASE,
          USER_DEMO_STACK_BASE + USER_DEMO_STACK_SIZE_BYTES,
          code_len);
}

static void usermode_fault_prepare_pages(void) {
    if (g_user_fault_stack_prepared) {
        return;
    }

    usermode_prepare_user_rw_page(USER_FAULT_STACK_BASE, USER_FAULT_STACK_SIZE_BYTES, "fault-stack");
    g_user_fault_stack_prepared = 1;
    KLOGI("usermode: fault-demo stack prepared stack=%x..%x bad_eip=%x",
          USER_FAULT_STACK_BASE,
          USER_FAULT_STACK_BASE + USER_FAULT_STACK_SIZE_BYTES,
          USER_FAULT_BAD_EIP);
}

static void usermode_demo_task(void *arg) {
    (void)arg;
    usermode_demo_prepare_pages();
    KASSERT(sched_set_current_user_layout(USER_DEMO_CODE_BASE,
                                          USER_DEMO_CODE_SIZE_BYTES,
                                          USER_DEMO_STACK_BASE,
                                          USER_DEMO_STACK_SIZE_BYTES) == 0);
    KASSERT(sched_mark_current_task_user_bootstrap(USER_DEMO_CODE_BASE, USER_DEMO_STACK_TOP) == 0);
    KLOGI("usermode: entering ring3 demo eip=%x esp=%x",
          USER_DEMO_CODE_BASE, USER_DEMO_STACK_TOP);
    enter_user_mode(USER_DEMO_CODE_BASE, USER_DEMO_STACK_TOP);
}

static void usermode_fault_task(void *arg) {
    (void)arg;
    usermode_fault_prepare_pages();
    KASSERT(sched_set_current_user_layout(0U,
                                          0U,
                                          USER_FAULT_STACK_BASE,
                                          USER_FAULT_STACK_SIZE_BYTES) == 0);
    KASSERT(sched_mark_current_task_user_bootstrap(USER_FAULT_BAD_EIP, USER_FAULT_STACK_TOP) == 0);
    KLOGI("usermode: entering ring3 fault demo eip=%x esp=%x",
          USER_FAULT_BAD_EIP, USER_FAULT_STACK_TOP);
    enter_user_mode(USER_FAULT_BAD_EIP, USER_FAULT_STACK_TOP);
}

static void usermode_elf_demo_task(void *arg) {
    const struct usermode_elf_demo_cfg *cfg = (const struct usermode_elf_demo_cfg *)arg;
    struct elf32_user_image image;
    int rc;

    if (!cfg) {
        KLOGW("usermode: null elf demo config");
        return;
    }

    rc = elf32_load_user_static_path(cfg->path,
                                     cfg->image_base,
                                     cfg->image_size,
                                     cfg->stack_base,
                                     cfg->stack_size,
                                     &image);
    if (rc < 0) {
        KLOGW("usermode: failed to load %s path=%s rc=%d", cfg->tag, cfg->path, rc);
        return;
    }

    KASSERT(sched_set_current_user_layout(image.image_base,
                                          image.image_size,
                                          cfg->stack_base,
                                          cfg->stack_size) == 0);
    if (cfg == &g_user_shell_cfg) {
        KASSERT(vfs_console_set_input_owner_task(sched_current_task_pid()) == 0);
    }
    KASSERT(sched_mark_current_task_user_bootstrap(image.entry_eip, image.stack_top) == 0);
    KLOGI("usermode: entering ring3 %s path=%s eip=%x esp=%x",
          cfg->tag, cfg->path, image.entry_eip, image.stack_top);
    enter_user_mode(image.entry_eip, image.stack_top);
}

static void usermode_child_slot_task(void *arg) {
    struct usermode_child_slot *slot = (struct usermode_child_slot *)arg;
    struct elf32_user_image image;
    uint32_t entry_esp;
    int rc;

    if (!slot) {
        KLOGW("usermode: null child slot");
        return;
    }

    rc = elf32_load_user_static_path_auto(slot->path,
                                          slot->stack_base,
                                          slot->stack_size,
                                          &image);
    if (rc < 0) {
        KLOGW("usermode: failed to load %s path=%s rc=%d", slot->tag, slot->path, rc);
        return;
    }

    KASSERT(sched_set_current_user_layout(image.image_base,
                                          image.image_size,
                                          slot->stack_base,
                                          slot->stack_size) == 0);
    KASSERT(sched_set_current_task_cmdline(slot->cmdline, slot->cmdline_len) == 0);
    rc = usermode_prepare_child_start_stack(slot, image.stack_top, &entry_esp);
    if (rc < 0) {
        KLOGW("usermode: failed to build argv for %s path=%s rc=%d",
              slot->tag, slot->path, rc);
        return;
    }
    KASSERT(sched_mark_current_task_user_bootstrap(image.entry_eip, entry_esp) == 0);
    KLOGI("usermode: entering ring3 %s path=%s eip=%x esp=%x",
          slot->tag, slot->path, image.entry_eip, entry_esp);
    enter_user_mode(image.entry_eip, entry_esp);
}

int usermode_spawn_demo_task(void) {
    int rc;
    if (g_user_demo_spawned) {
        return -KERR_NOTSUP;
    }
    rc = sched_spawn_kernel_task("user-demo", usermode_demo_task, 0, 0);
    if (rc < 0) {
        return rc;
    }
    g_user_demo_spawned = 1;
    return 0;
}

int usermode_spawn_fault_task(void) {
    int rc;
    if (g_user_fault_spawned) {
        return -KERR_NOTSUP;
    }
    rc = sched_spawn_kernel_task("user-fault", usermode_fault_task, 0, 0);
    if (rc < 0) {
        return rc;
    }
    g_user_fault_spawned = 1;
    return 0;
}

int usermode_spawn_elf_demo_task_a(void) {
    int rc;
    if (g_user_elf_demo_a_spawned) {
        return -KERR_NOTSUP;
    }
    rc = sched_spawn_kernel_task("user-elf-a", usermode_elf_demo_task, (void *)&g_user_elf_demo_a_cfg, 0);
    if (rc < 0) {
        return rc;
    }
    g_user_elf_demo_a_spawned = 1;
    return 0;
}

int usermode_spawn_elf_demo_task_b(void) {
    int rc;
    if (g_user_elf_demo_b_spawned) {
        return -KERR_NOTSUP;
    }
    rc = sched_spawn_kernel_task("user-elf-b", usermode_elf_demo_task, (void *)&g_user_elf_demo_b_cfg, 0);
    if (rc < 0) {
        return rc;
    }
    g_user_elf_demo_b_spawned = 1;
    return 0;
}

int usermode_spawn_shell_task(void) {
    int rc;
    if (g_user_shell_spawned) {
        return -KERR_NOTSUP;
    }
    rc = sched_spawn_kernel_task("user-shell",
                                 usermode_elf_demo_task,
                                 (void *)&g_user_shell_cfg,
                                 0);
    if (rc < 0) {
        return rc;
    }
    g_user_shell_spawned = 1;
    return 0;
}

int usermode_spawn_path_task(const char *path) {
    return usermode_spawn_path_task_ex(path, 0, 0U, 0U);
}

int usermode_spawn_path_task_ex(const char *path,
                                const char *cmdline,
                                uint32_t cmdline_len,
                                uint32_t spawn_flags) {
    struct elf32_user_layout layout;
    struct usermode_child_slot *slot;
    int child_pid;
    int rc;

    if (!path || path[0] != '/') {
        return -KERR_INVAL;
    }
    if (!cmdline && cmdline_len != 0U) {
        return -KERR_INVAL;
    }
    if (cmdline_len >= USERMODE_CMDLINE_MAX) {
        return -KERR_INVAL;
    }

    rc = elf32_inspect_user_static_path(path, &layout);
    if (rc < 0) {
        return rc;
    }
    if (!sched_user_image_range_available(layout.image_base, layout.image_size)) {
        return -KERR_NOTSUP;
    }

    slot = usermode_child_slot_acquire(layout.image_base, layout.image_size);
    if (!slot) {
        return -KERR_NOTSUP;
    }

    rc = usermode_copy_cstr(slot->path,
                            (uint32_t)sizeof(slot->path),
                            path);
    if (rc < 0) {
        return rc;
    }
    slot->cmdline[0] = '\0';
    if (cmdline_len != 0U) {
        memcpy(slot->cmdline, cmdline, cmdline_len);
    }
    slot->cmdline[cmdline_len] = '\0';
    slot->cmdline_len = cmdline_len;
    slot->image_base = layout.image_base;
    slot->image_size = layout.image_size;

    slot->used = 1;
    rc = sched_spawn_user_child_task(slot->task_name,
                                     usermode_child_slot_task,
                                     (void *)slot,
                                     0,
                                     (spawn_flags & SYSCALL_SPAWN_FLAG_INHERIT_FDS) != 0U,
                                     &child_pid);
    if (rc < 0) {
        slot->used = 0;
        slot->path[0] = '\0';
        slot->cmdline[0] = '\0';
        slot->cmdline_len = 0U;
        slot->image_base = 0U;
        slot->image_size = 0U;
        return rc;
    }

    KLOGI("usermode: spawned path task path=%s pid=%d slot=%x..%x",
          slot->path,
          child_pid,
          slot->stack_base,
          slot->stack_base + slot->stack_size);
    return child_pid;
}

void usermode_notify_task_reaped(const char *task_name) {
    struct usermode_child_slot *slot;
    uint32_t image_base;
    uint32_t image_size;

    if (!task_name) {
        return;
    }
    if (usermode_str_eq(task_name, "user-shell")) {
        g_user_shell_spawned = 0;
        vfs_console_set_input_owner_kernel();
        return;
    }
    slot = usermode_child_slot_by_task_name(task_name);
    if (!slot || !slot->used) {
        return;
    }

    image_base = slot->image_base;
    image_size = slot->image_size;
    slot->used = 0;
    slot->path[0] = '\0';
    slot->cmdline[0] = '\0';
    slot->cmdline_len = 0U;
    slot->image_base = 0U;
    slot->image_size = 0U;
    KLOGI("usermode: released spawn slot task=%s image=%x..%x",
          task_name,
          image_base,
          image_base + image_size);
}
