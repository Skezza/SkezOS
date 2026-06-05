#include <stdint.h>
#include <stdbool.h>
#include "kassert.h"
#include "display.h"
#include "klog.h"
#include "memory_layout.h"
#include "serial.h"
#include "panic.h"
#include "gdt.h"
#include "interrupts.h"
#include "irq.h"
#include "memmap.h"
#include "pmm.h"
#include "paging.h"
#include "kmalloc.h"
#include "timer.h"
#include "sched.h"
#include "syscall.h"
#include "ata_pio.h"
#include "block_cache.h"
#include "block_device.h"
#include "persistfs.h"
#include "tarfs.h"
#include "usermode.h"
#include "vfs.h"
#include "keyboard.h"
#include "mouse.h"

#ifndef SKEZOS_GUI_BOOT
#define SKEZOS_GUI_BOOT 0
#endif

#define LOG_BOOT_SERIAL 0
#define BOOT_SPAWN_DEMO_TASKS 0
#define SMOKE_READY_MARKER "SKEZOS_SMOKE_READY\n"
#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289U

struct demo_task_cfg {
    const char *name;
    uint32_t sleep_ticks;
};

static const struct demo_task_cfg g_demo_task_a = { "worker-a", 37U };
static const struct demo_task_cfg g_demo_task_b = { "worker-b", 53U };

static void boot_self_checks_pre_paging(uint32_t magic) {
    struct pmm_stats before;
    struct pmm_stats after_alloc;
    struct pmm_stats after_free;
    uint32_t frame;

    KLOGI("self-check (pre-paging): start");
    KASSERT(magic == MULTIBOOT2_BOOTLOADER_MAGIC);
    KASSERT(pmm_is_ready());

    pmm_get_stats(&before);
    KASSERT(before.total_frames > 0);
    KASSERT(before.free_frames > 0);
    KASSERT(before.used_frames < before.total_frames);

    frame = pmm_alloc_frame();
    KASSERT(frame != 0);
    KASSERT((frame & (PAGE_SIZE_BYTES - 1U)) == 0);

    pmm_get_stats(&after_alloc);
    KASSERT(after_alloc.used_frames == before.used_frames + 1U);
    KASSERT(after_alloc.free_frames + after_alloc.used_frames == after_alloc.total_frames);

    pmm_free_frame(frame);
    pmm_get_stats(&after_free);
    KASSERT(after_free.used_frames == before.used_frames);
    KASSERT(after_free.free_frames == before.free_frames);

    KLOGI("self-check (pre-paging): pass");
}

static void boot_self_checks_post_paging(void) {
    void *a;
    void *b;
    void *c;
    uint32_t frame;
    struct kmalloc_stats before;
    struct kmalloc_stats after;

    KLOGI("self-check (post-paging): start");
    KASSERT(paging_is_ready());
    KASSERT(paging_is_enabled());

    /* PMM bitmap must remain accessible after paging is enabled. */
    frame = pmm_alloc_frame();
    KASSERT(frame != 0);
    pmm_free_frame(frame);

    kmalloc_get_stats(&before);
    a = kmalloc(1);
    b = kmalloc(13);
    c = kmalloc(PAGE_SIZE_BYTES + 1U);
    KASSERT(a != 0);
    KASSERT(b != 0);
    KASSERT(c != 0);
    KASSERT((((uint32_t)a) & 7U) == 0);
    KASSERT((((uint32_t)b) & 7U) == 0);
    KASSERT((((uint32_t)c) & (PAGE_SIZE_BYTES - 1U)) == 0);

    kmalloc_get_stats(&after);
    KASSERT(after.small_alloc_count >= before.small_alloc_count + 2U);
    KASSERT(after.large_alloc_count >= before.large_alloc_count + 1U);
    KASSERT(after.small_cursor <= after.large_cursor);
    KLOGI("kmalloc: stats small_allocs=%u large_allocs=%u small_bytes=%u large_bytes=%u",
          after.small_alloc_count,
          after.large_alloc_count,
          (uint32_t)after.small_bytes_used,
          (uint32_t)after.large_bytes_used);
    kfree(c);

    KLOGI("self-check (post-paging): pass");
}

static void handle_input_char(char c) {
    uint32_t saved_flags;

    if (c == 0x16) {
        bool verbose = !keyboard_is_verbose();
        keyboard_set_verbose(verbose);
        serial_writestr(verbose ? "\nVerbose keyboard logging enabled\n" : "\nVerbose keyboard logging disabled\n");
        return;
    }
    saved_flags = display_console_enter_critical();
    display_putc(c);
    serial_writechar(c);
    display_console_leave_critical(saved_flags);
}

static void console_task(void *arg) {
    (void)arg;
    KLOGI("console task online");
    if (vfs_console_get_input_owner() == VFS_CONSOLE_INPUT_OWNER_KERNEL) {
        display_puts("Ready> ");
    }
    for (;;) {
        int ch;
        if (vfs_console_get_input_owner() != VFS_CONSOLE_INPUT_OWNER_KERNEL) {
            sched_sleep_ticks(1);
            continue;
        }
        ch = vfs_console_poll_input_char();
        if (ch != -1) {
            handle_input_char((char)ch);
            sched_checkpoint();
            continue;
        }
        sched_sleep_ticks(1);
    }
}

static void demo_worker_task(void *arg) {
    const struct demo_task_cfg *cfg = (const struct demo_task_cfg *)arg;
    uint32_t iter = 0;
    for (;;) {
        iter++;
        if (vfs_console_get_input_owner() == VFS_CONSOLE_INPUT_OWNER_KERNEL &&
            (iter % 8U) == 1U) {
            KLOGI("sched demo: %s iter=%u tick=%u",
                  cfg->name, iter, (uint32_t)timer_ticks);
        }
        sched_sleep_ticks(cfg->sleep_ticks);
    }
}

void kmain(uint32_t magic, uint32_t mb2_addr) {
    // Initialize serial and display outputs
    serial_init();
    display_init();
    klog_set_level(KLOG_LEVEL_INFO);
    gdt_init();
    // ASCII logo for SkezOS
    const char *logo =
        " ad88888ba   88                                 ,ad8888ba,     ad88888ba  \n"
        "d8\"     \"8b  88                                d8\'    `8b   d8\"     \"8b \n"
        "Y8,          88                               d8\'        `8b  Y8,         \n"
        "`Y8aaaaa,    88   ,d8   ,adPPYba,  888888888  88          88  `Y8aaaaa,   \n"
        "  `\"\"\"\"\"8b,  88 ,a8\"   a8P_____88       a8P\"  88          88    `\"\"\"\"\"8b, \n"
        "        `8b  8888[     8PP\"\"\"\"\"    ,d8P'    Y8,        ,8P          `8b \n"
        "Y8a     a8P  88`\"Yba,  \"8b,   ,aa  ,d8\"        Y8a.    .a8P   Y8a     a8P \n"
        " `Y88888P\"   88   `Y8a  `\"Ybbd8\'\"  888888888    `\"Y8888Y\'\"     \"Y88888P\"\n";
    #if LOG_BOOT_SERIAL
    serial_writestr(logo);
    #endif
    display_puts(logo);
    #if LOG_BOOT_SERIAL
    serial_writestr("SkezOS booting...\n");
    #endif
    display_puts("SkezOS booting...\n");

    // Parse memory map and initialize memory management
    memmap_parse(magic, mb2_addr);
    boot_self_checks_pre_paging(magic);

    // Setup paging and enable it
    paging_init();
    paging_enable();

    // Initialize a simple kernel heap in the higher-half direct map window.
    kmalloc_init((void *)KERNEL_HEAP_START, KERNEL_HEAP_SIZE_BYTES);
    boot_self_checks_post_paging();
    display_late_init();
    vfs_init();
    KASSERT(tarfs_mount_demo_archive() == 0);
    if (ata_pio_init() == 0) {
        struct block_device *storage_dev = ata_pio_primary_master_device();
        if (storage_dev && block_cache_bind_device(storage_dev) == 0) {
            if (persistfs_mount() < 0) {
                KLOGW("storage: persistfs mount failed; continuing without /persist");
            }
        } else {
            KLOGW("storage: block cache bind failed; continuing without /persist");
        }
    } else {
        KLOGW("storage: ATA not available; continuing without /persist");
    }

    // Install default interrupt handlers and remap the PIC
    interrupts_install();

    // Disable interrupts while we set up IRQ stubs and hardware IRQ handlers
    __asm__ __volatile__("cli");

    // Initialize IRQ stubs in the IDT and mask all hardware IRQs
    irq_init();
    syscall_init();

    // Set up timer (100Hz) and keyboard
    timer_init(100);
    keyboard_init();
#if SKEZOS_GUI_BOOT
    if (display_framebuffer_ready()) {
        display_gui_enable();
        mouse_init();
    }
#endif

    // Initialize the kernel scheduler. The scheduler enables interrupts
    // when the first task starts.
    sched_init();
#if SKEZOS_GUI_BOOT
    if (display_gui_mode_active()) {
        KASSERT(usermode_spawn_gui_session_task() == 0);
        KLOGI("sched: boot tasks created (idle + gui-session)");
    } else
#endif
    {
        KASSERT(sched_spawn_kernel_task("console", console_task, 0, 0) == 0);
        KASSERT(usermode_spawn_shell_task() == 0);

#if BOOT_SPAWN_DEMO_TASKS
        KASSERT(sched_spawn_kernel_task("worker-a", demo_worker_task, (void *)&g_demo_task_a, 0) == 0);
        KASSERT(sched_spawn_kernel_task("worker-b", demo_worker_task, (void *)&g_demo_task_b, 0) == 0);
        KASSERT(usermode_spawn_demo_task() == 0);
        KASSERT(usermode_spawn_fault_task() == 0);
        KASSERT(usermode_spawn_elf_demo_task_a() == 0);
        KASSERT(usermode_spawn_elf_demo_task_b() == 0);
        KLOGI("sched: demo tasks created (idle + console + 2 workers + user-demo + user-fault + 2 user-elf + shell)");
#else
        KLOGI("sched: boot tasks created (idle + console + shell)");
#endif
    }

    #if LOG_BOOT_SERIAL
    serial_writestr("kernel initialised\n");
    #endif
    klog_serial_raw(SMOKE_READY_MARKER);
    display_puts("kernel initialised\n");
    KLOGI("sched: starting");
    sched_start();
}
