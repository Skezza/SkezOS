#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGING_PAGE_FLAG_PRESENT       0x001U
#define PAGING_PAGE_FLAG_WRITABLE      0x002U
#define PAGING_PAGE_FLAG_USER          0x004U
#define PAGING_PAGE_FLAG_WRITE_THROUGH 0x008U
#define PAGING_PAGE_FLAG_CACHE_DISABLE 0x010U
#define PAGING_PAGE_FLAG_COW           0x200U

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

struct paging_address_space;

struct paging_user_range {
    uint32_t base;
    uint32_t size;
};

/* Kernel template address space used by kernel-only tasks. */
struct paging_address_space *paging_kernel_address_space(void);
struct paging_address_space *paging_current_address_space(void);

/* Switch to AS (or kernel template when AS is null). */
int paging_activate_address_space(struct paging_address_space *as);

/* Build a user task address space by cloning the current mappings and
 * privatizing page tables that cover user ranges.
 */
int paging_create_user_address_space(const struct paging_user_range *ranges,
                                     uint32_t range_count,
                                     struct paging_address_space **out_as);

/* Fork helper: clone parent address space and set writable user pages
 * in ranges to shared COW mappings in parent+child.
 */
int paging_clone_address_space_cow(struct paging_address_space *parent_as,
                                   const struct paging_user_range *ranges,
                                   uint32_t range_count,
                                   struct paging_address_space **out_child_as);

/* Release a previously created non-kernel address space. */
void paging_destroy_address_space(struct paging_address_space *as);

/* Resolve a user-mode write fault on a COW page for AS.
 * Returns 0 when handled, negative -KERR_* when not handled/failed.
 */
int paging_handle_cow_fault(struct paging_address_space *as,
                            uint32_t fault_addr,
                            uint32_t error_code);

#endif /* PAGING_H */
