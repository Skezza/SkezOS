#include "memmap.h"
#include "pmm.h"
#include "serial.h"
#include "utils.h"

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

/* Parse the Multiboot2 memory map and initialise the PMM.  We find
 * the highest address of any available memory region and cap it at
 * 1GiB. */
void memmap_parse(uint32_t mb_magic, uint32_t mb_info) {
    (void)mb_magic;
    struct multiboot_tag *tag = (struct multiboot_tag *)((uint32_t)mb_info + 8);
    uint32_t mem_upper = 0;
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
                    if (end > mem_upper)
                        mem_upper = (uint32_t)end;
                }
                entry = (struct multiboot_mmap_entry *)((uint8_t *)entry + entry_size);
            }
        }
        /* Tags are 8‑byte aligned. */
        tag = (struct multiboot_tag *)(((uint32_t)tag + tag->size + 7) & ~7);
    }
    pmm_init(mem_upper);
}
