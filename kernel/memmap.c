#include "memmap.h"
#include "klog.h"
#include "memory_layout.h"
#include "pmm.h"

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289U

/* Multiboot2 tag types. */
#define MULTIBOOT2_TAG_TYPE_END         0
#define MULTIBOOT2_TAG_TYPE_MMAP        6
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER 8

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

/* Memory map entry.  See the Multiboot2 specification. */
struct multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed));

struct multiboot_tag_framebuffer_common {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
} __attribute__((packed));

struct multiboot_tag_framebuffer_rgb {
    struct multiboot_tag_framebuffer_common common;
    uint8_t framebuffer_red_field_position;
    uint8_t framebuffer_red_mask_size;
    uint8_t framebuffer_green_field_position;
    uint8_t framebuffer_green_mask_size;
    uint8_t framebuffer_blue_field_position;
    uint8_t framebuffer_blue_mask_size;
} __attribute__((packed));

extern uint8_t __kernel_start;
extern uint8_t __kernel_end;

static struct boot_framebuffer_info g_framebuffer_info;
static uint8_t g_framebuffer_info_present = 0U;

static struct multiboot_tag *mb2_first_tag(uint32_t mb_info) {
    return (struct multiboot_tag *)((uint32_t)mb_info + 8U);
}

static struct multiboot_tag *mb2_next_tag(struct multiboot_tag *tag) {
    return (struct multiboot_tag *)(((uint32_t)tag + tag->size + 7U) & ~7U);
}

static uint32_t u64_range_to_u32_len(uint64_t start, uint64_t end) {
    if (end <= start) {
        return 0;
    }
    if (end > 0x100000000ULL) {
        end = 0x100000000ULL;
    }
    if (start >= 0x100000000ULL) {
        return 0;
    }
    if ((end - start) > 0xFFFFFFFFULL) {
        return 0xFFFFFFFFU;
    }
    return (uint32_t)(end - start);
}

/* Parse the Multiboot2 memory map and initialise the PMM.  We find
 * the highest address of any available memory region, initialise the
 * PMM bitmap, then free exact available ranges and reserve boot
 * critical regions explicitly. */
void memmap_parse(uint32_t mb_magic, uint32_t mb_info) {
    struct multiboot_tag *tag;
    uint32_t mem_upper = 0;
    struct pmm_stats stats;
    uint32_t mmap_regions = 0;
    uint32_t heap_phys_start = KERNEL_HEAP_START - KERNEL_VIRTUAL_BASE;
    uint32_t mb_info_total_size = *(uint32_t *)(uintptr_t)mb_info;

    if (mb_magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        KLOGW("memmap: unexpected multiboot magic=%x", mb_magic);
    }

    g_framebuffer_info_present = 0U;

    tag = mb2_first_tag(mb_info);
    while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
            /* The memory map tag layout is:
             * u32 type, u32 size, u32 entry_size, u32 entry_version,
             * followed by one or more entries. */
            uint32_t entry_size = *(uint32_t *)((uint8_t *)tag + 8);
            struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)((uint8_t *)tag + 16);
            while ((uint8_t *)entry < (uint8_t *)tag + tag->size) {
                if (entry->type == 1) {
                    uint64_t end = entry->addr + entry->len;
                    if (end > 0xFFFFFFFFULL) {
                        end = 0xFFFFFFFFULL;
                    }
                    if (end > mem_upper) {
                        mem_upper = (uint32_t)end;
                    }
                }
                entry = (struct multiboot_mmap_entry *)((uint8_t *)entry + entry_size);
            }
        } else if (tag->type == MULTIBOOT2_TAG_TYPE_FRAMEBUFFER &&
                   tag->size >= sizeof(struct multiboot_tag_framebuffer_common) &&
                   g_framebuffer_info_present == 0U) {
            const struct multiboot_tag_framebuffer_common *fb =
                (const struct multiboot_tag_framebuffer_common *)tag;
            const struct multiboot_tag_framebuffer_rgb *fb_rgb =
                (const struct multiboot_tag_framebuffer_rgb *)tag;

            g_framebuffer_info.address = fb->framebuffer_addr;
            g_framebuffer_info.pitch = fb->framebuffer_pitch;
            g_framebuffer_info.width = fb->framebuffer_width;
            g_framebuffer_info.height = fb->framebuffer_height;
            g_framebuffer_info.bpp = fb->framebuffer_bpp;
            g_framebuffer_info.type = fb->framebuffer_type;
            g_framebuffer_info.red_field_position = 0U;
            g_framebuffer_info.red_mask_size = 0U;
            g_framebuffer_info.green_field_position = 0U;
            g_framebuffer_info.green_mask_size = 0U;
            g_framebuffer_info.blue_field_position = 0U;
            g_framebuffer_info.blue_mask_size = 0U;
            if (fb->framebuffer_type == 1U &&
                tag->size >= sizeof(struct multiboot_tag_framebuffer_rgb)) {
                g_framebuffer_info.red_field_position = fb_rgb->framebuffer_red_field_position;
                g_framebuffer_info.red_mask_size = fb_rgb->framebuffer_red_mask_size;
                g_framebuffer_info.green_field_position = fb_rgb->framebuffer_green_field_position;
                g_framebuffer_info.green_mask_size = fb_rgb->framebuffer_green_mask_size;
                g_framebuffer_info.blue_field_position = fb_rgb->framebuffer_blue_field_position;
                g_framebuffer_info.blue_mask_size = fb_rgb->framebuffer_blue_mask_size;
            }
            g_framebuffer_info_present = 1U;
        }
        tag = mb2_next_tag(tag);
    }

    pmm_init(mem_upper);

    tag = mb2_first_tag(mb_info);
    while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
            uint32_t entry_size = *(uint32_t *)((uint8_t *)tag + 8);
            struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)((uint8_t *)tag + 16);
            while ((uint8_t *)entry < (uint8_t *)tag + tag->size) {
                if (entry->type == 1) {
                    uint64_t start = entry->addr;
                    uint64_t end = entry->addr + entry->len;
                    uint32_t len32 = u64_range_to_u32_len(start, end);
                    if (len32 != 0) {
                        pmm_mark_region_available((uint32_t)start, len32);
                        mmap_regions++;
                    }
                }
                entry = (struct multiboot_mmap_entry *)((uint8_t *)entry + entry_size);
            }
        }
        tag = mb2_next_tag(tag);
    }

    /* Reserve low memory and all boot critical regions that may live in
     * otherwise "available" Multiboot ranges. */
    pmm_reserve_region(0U, 0x00100000U); /* BIOS/real-mode + VGA area */
    pmm_reserve_region((uint32_t)(uintptr_t)&__kernel_start,
                       (uint32_t)((uintptr_t)&__kernel_end - (uintptr_t)&__kernel_start));
    pmm_reserve_region(mb_info, mb_info_total_size);
    pmm_reserve_region(heap_phys_start, KERNEL_HEAP_SIZE_BYTES);
    pmm_reserve_region(USER_DEMO_CODE_BASE, USER_DEMO_CODE_SIZE_BYTES);
    pmm_reserve_region(USER_DEMO_STACK_BASE, USER_DEMO_STACK_SIZE_BYTES);
    pmm_reserve_region(USER_FAULT_STACK_BASE, USER_FAULT_STACK_SIZE_BYTES);

    pmm_get_stats(&stats);
    pmm_reserve_region(PMM_BITMAP_PHYS_BASE, stats.bitmap_bytes);

    pmm_get_stats(&stats);
    KLOGI("memmap: parsed %u available regions (mb2 info=%u bytes)", mmap_regions, mb_info_total_size);
    if (g_framebuffer_info_present != 0U) {
        KLOGI("mb2: framebuffer addr_hi=%x addr_lo=%x %ux%u pitch=%u bpp=%u type=%u",
              (uint32_t)(g_framebuffer_info.address >> 32),
              (uint32_t)g_framebuffer_info.address,
              g_framebuffer_info.width,
              g_framebuffer_info.height,
              g_framebuffer_info.pitch,
              (uint32_t)g_framebuffer_info.bpp,
              (uint32_t)g_framebuffer_info.type);
    } else {
        KLOGI("mb2: framebuffer handoff not present");
    }
    KLOGI("pmm: ready (total_frames=%u used=%u free=%u bitmap=%u bytes)",
          stats.total_frames, stats.used_frames, stats.free_frames, stats.bitmap_bytes);
}

int memmap_get_framebuffer_info(struct boot_framebuffer_info *out) {
    if (out == 0 || g_framebuffer_info_present == 0U) {
        return -1;
    }

    *out = g_framebuffer_info;
    return 0;
}
