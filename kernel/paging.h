#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

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

#endif /* PAGING_H */
