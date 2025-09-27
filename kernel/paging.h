#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

/* Initialise the paging structures.  Creates a page directory and a
 * single page table mapping the first 4MiB of memory and mirrors it
 * into the higher half at 0xC0000000.  Does not enable paging. */
void paging_init(void);

/* Load CR3 and enable paging by setting the PG bit in CR0. */
void paging_enable(void);

#endif /* PAGING_H */
