#include "memmap.h"
#include "klog.h"
#include "memory_layout.h"
#include "pmm.h"

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289U

/* Multiboot2 tag types. */
#define MULTIBOOT2_TAG_TYPE_END  0
#define MULTIBOOT2_TAG_TYPE_MMAP 6

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
};

extern uint8_t __kernel_start;
extern uint8_t __kernel_end;

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
    KLOGI("pmm: ready (total_frames=%u used=%u free=%u bitmap=%u bytes)",
          stats.total_frames, stats.used_frames, stats.free_frames, stats.bitmap_bytes);
}
