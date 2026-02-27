#ifndef PMM_H
#define PMM_H

#include <stdint.h>

struct pmm_stats {
    uint32_t total_frames;
    uint32_t used_frames;
    uint32_t free_frames;
    uint32_t bitmap_bytes;
};

/* Initialise the physical memory manager.  mem_upper specifies the
 * highest addressable physical byte.  The memory below 1MiB is
 * automatically reserved. */
void pmm_init(uint32_t mem_upper);

/* Allocate a 4KiB frame.  Returns the physical address of the
 * beginning of the frame or zero if none are available. */
uint32_t pmm_alloc_frame(void);

/* Free a frame previously returned by pmm_alloc_frame().  The
 * address must be 4KiB aligned. */
void pmm_free_frame(uint32_t frame);

/* Copy current allocator accounting counters into OUT. */
void pmm_get_stats(struct pmm_stats *out);

/* Returns non-zero once the PMM bitmap has been initialised. */
int pmm_is_ready(void);

/* Mark a physical byte range as available for frame allocation.
 * Only fully-covered pages are freed.
 */
void pmm_mark_region_available(uint32_t base, uint32_t length);

/* Mark a physical byte range as reserved. Any page touched by the
 * range is marked used.
 */
void pmm_reserve_region(uint32_t base, uint32_t length);

#endif /* PMM_H */
