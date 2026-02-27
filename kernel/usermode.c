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

struct usermode_elf_demo_cfg {
    const char *path;
    const char *tag;
    uint32_t image_base;
    uint32_t image_size;
    uint32_t stack_base;
    uint32_t stack_size;
};

struct usermode_spawn_req {
    const char *allowed_path;
    char path[USERMODE_SPAWN_PATH_MAX];
    const char *tag;
    const char *task_name;
    uint32_t image_base;
    uint32_t image_size;
    uint32_t stack_base;
    uint32_t stack_size;
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

static struct usermode_spawn_req g_user_spawn_req_slot2 = {
    .allowed_path = "/bin/hello3.elf",
    .path = { 0 },
    .tag = "elf-spawn-c",
    .task_name = "user-spawn-c",
    .image_base = USER_ELF_SLOT2_IMAGE_BASE,
    .image_size = USER_ELF_SLOT2_IMAGE_SIZE_BYTES,
    .stack_base = USER_ELF_SLOT2_STACK_BASE,
    .stack_size = USER_ELF_SLOT2_STACK_SIZE_BYTES,
    .used = 0,
};

static struct usermode_spawn_req g_user_spawn_req_slot3 = {
    .allowed_path = "/bin/hello4.elf",
    .path = { 0 },
    .tag = "elf-spawn-d",
    .task_name = "user-spawn-d",
    .image_base = USER_ELF_SLOT3_IMAGE_BASE,
    .image_size = USER_ELF_SLOT3_IMAGE_SIZE_BYTES,
    .stack_base = USER_ELF_SLOT3_STACK_BASE,
    .stack_size = USER_ELF_SLOT3_STACK_SIZE_BYTES,
    .used = 0,
};

static struct usermode_spawn_req g_user_spawn_req_slot5 = {
    .allowed_path = "/bin/cat.elf",
    .path = { 0 },
    .tag = "tool-cat",
    .task_name = "user-cat",
    .image_base = USER_ELF_SLOT5_IMAGE_BASE,
    .image_size = USER_ELF_SLOT5_IMAGE_SIZE_BYTES,
    .stack_base = USER_ELF_SLOT5_STACK_BASE,
    .stack_size = USER_ELF_SLOT5_STACK_SIZE_BYTES,
    .used = 0,
};

static struct usermode_spawn_req *g_user_spawn_reqs[] = {
    &g_user_spawn_req_slot2,
    &g_user_spawn_req_slot3,
    &g_user_spawn_req_slot5,
};

#define USERMODE_SPAWN_REQ_COUNT \
    ((uint32_t)(sizeof(g_user_spawn_reqs) / sizeof(g_user_spawn_reqs[0])))

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

static struct usermode_spawn_req *usermode_spawn_req_by_path(const char *path) {
    for (uint32_t i = 0; i < USERMODE_SPAWN_REQ_COUNT; i++) {
        struct usermode_spawn_req *req = g_user_spawn_reqs[i];
        if (req && req->allowed_path && usermode_str_eq(path, req->allowed_path)) {
            return req;
        }
    }
    return 0;
}

static struct usermode_spawn_req *usermode_spawn_req_by_task_name(const char *task_name) {
    for (uint32_t i = 0; i < USERMODE_SPAWN_REQ_COUNT; i++) {
        struct usermode_spawn_req *req = g_user_spawn_reqs[i];
        if (req && req->task_name && usermode_str_eq(task_name, req->task_name)) {
            return req;
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
    KASSERT(sched_mark_current_task_user_bootstrap(USER_DEMO_CODE_BASE, USER_DEMO_STACK_TOP) == 0);
    KLOGI("usermode: entering ring3 demo eip=%x esp=%x",
          USER_DEMO_CODE_BASE, USER_DEMO_STACK_TOP);
    enter_user_mode(USER_DEMO_CODE_BASE, USER_DEMO_STACK_TOP);
}

static void usermode_fault_task(void *arg) {
    (void)arg;
    usermode_fault_prepare_pages();
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

    KASSERT(sched_mark_current_task_user_bootstrap(image.entry_eip, image.stack_top) == 0);
    KLOGI("usermode: entering ring3 %s path=%s eip=%x esp=%x",
          cfg->tag, cfg->path, image.entry_eip, image.stack_top);
    enter_user_mode(image.entry_eip, image.stack_top);
}

static void usermode_spawn_req_task(void *arg) {
    struct usermode_spawn_req *req = (struct usermode_spawn_req *)arg;
    struct elf32_user_image image;
    int rc;

    if (!req) {
        KLOGW("usermode: null spawn req");
        return;
    }

    rc = elf32_load_user_static_path(req->path,
                                     req->image_base,
                                     req->image_size,
                                     req->stack_base,
                                     req->stack_size,
                                     &image);
    if (rc < 0) {
        req->used = 0; /* allow retry if load fails before entering user mode */
        KLOGW("usermode: failed to load %s path=%s rc=%d", req->tag, req->path, rc);
        return;
    }

    KASSERT(sched_mark_current_task_user_bootstrap(image.entry_eip, image.stack_top) == 0);
    KLOGI("usermode: entering ring3 %s path=%s eip=%x esp=%x",
          req->tag, req->path, image.entry_eip, image.stack_top);
    enter_user_mode(image.entry_eip, image.stack_top);
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
    vfs_console_set_input_owner(VFS_CONSOLE_INPUT_OWNER_USER_SHELL);
    g_user_shell_spawned = 1;
    return 0;
}

int usermode_spawn_path_task(const char *path) {
    struct usermode_spawn_req *req;
    int child_pid;
    int rc;

    if (!path || path[0] != '/') {
        return -KERR_INVAL;
    }

    /* Bootstrap constraint: ET_EXEC binaries are linked for fixed
     * addresses. Runtime spawn supports a tiny fixed slot table keyed
     * by known /bin ELF demo paths.
     */
    req = usermode_spawn_req_by_path(path);
    if (!req) {
        return -KERR_NOTSUP;
    }
    if (req->used) {
        return -KERR_NOTSUP;
    }

    rc = usermode_copy_cstr(req->path,
                            (uint32_t)sizeof(req->path),
                            path);
    if (rc < 0) {
        return rc;
    }

    req->used = 1;
    rc = sched_spawn_user_child_task(req->task_name,
                                     usermode_spawn_req_task,
                                     (void *)req,
                                     0,
                                     &child_pid);
    if (rc < 0) {
        req->used = 0;
        return rc;
    }

    KLOGI("usermode: spawned path task path=%s pid=%d slot=%x..%x",
          req->path,
          child_pid,
          req->image_base,
          req->stack_base + req->stack_size);
    return child_pid;
}

void usermode_notify_task_reaped(const char *task_name) {
    struct usermode_spawn_req *req;

    if (!task_name) {
        return;
    }
    if (usermode_str_eq(task_name, "user-shell")) {
        g_user_shell_spawned = 0;
        vfs_console_set_input_owner(VFS_CONSOLE_INPUT_OWNER_KERNEL);
        return;
    }
    req = usermode_spawn_req_by_task_name(task_name);
    if (!req || !req->used) {
        return;
    }

    req->used = 0;
    req->path[0] = '\0';
    KLOGI("usermode: released spawn slot task=%s image=%x..%x",
          task_name,
          req->image_base,
          req->stack_base + req->stack_size);
}
