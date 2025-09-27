#ifndef PMM_H
#define PMM_H

#include <stdint.h>

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

#endif /* PMM_H */
