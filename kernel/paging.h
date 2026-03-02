#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGING_PAGE_FLAG_PRESENT       0x001U
#define PAGING_PAGE_FLAG_WRITABLE      0x002U
#define PAGING_PAGE_FLAG_USER          0x004U
#define PAGING_PAGE_FLAG_WRITE_THROUGH 0x008U
#define PAGING_PAGE_FLAG_CACHE_DISABLE 0x010U

/* Initialise the paging structures.  Creates a page directory and a
 * single page table mapping the first 4MiB of memory and mirrors it
 * into the higher half at 0xC0000000.  Does not enable paging. */
void paging_init(void);

/* Load CR3 and enable paging by setting the PG bit in CR0. */
void paging_enable(void);

/* Diagnostic helpers for boot self-checks and fault reporting. */
int paging_is_ready(void);
int paging_is_enabled(void);

/* Mark an already-mapped identity region as user-accessible (sets U/S
 * on the low-half PDE/PTEs). The region must lie inside the early map.
 * Returns 0 on success or negative -KERR_*.
 */
int paging_mark_user_region(uint32_t vaddr, uint32_t length);

/* Map a kernel virtual range to an arbitrary physical range.  The
 * virtual base must be page-aligned and live in kernel space.  The
 * supplied page flags are standard x86 PTE/PDE low bits.
 */
int paging_map_kernel_region(uint32_t vaddr, uint32_t paddr, uint32_t length, uint32_t page_flags);

#endif /* PAGING_H */
