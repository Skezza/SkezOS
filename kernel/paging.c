#include "paging.h"
#include "kerrno.h"
#include "klog.h"
#include "kmalloc.h"
#include "memory_layout.h"
#include "pmm.h"
#include "utils.h"

#define PAGE_DIRECTORY_ENTRIES 1024
#define PAGE_TABLE_ENTRIES     1024
#define PAGE_TABLE_SPAN_BYTES  (PAGE_TABLE_ENTRIES * PAGE_SIZE_BYTES)
#define KERNEL_EARLY_PAGE_TABLE_COUNT (KERNEL_EARLY_MAP_BYTES / PAGE_TABLE_SPAN_BYTES)

#if (KERNEL_EARLY_MAP_BYTES % PAGE_TABLE_SPAN_BYTES) != 0
#error "KERNEL_EARLY_MAP_BYTES must be a multiple of 4 MiB"
#endif

/* The page directory and first page table will be allocated from
 * physical memory using the PMM.  They must be page aligned. */
static uint32_t *page_directory;
static uint32_t *early_page_tables[KERNEL_EARLY_PAGE_TABLE_COUNT];
static uint32_t *dynamic_page_tables[PAGE_DIRECTORY_ENTRIES];
static int paging_ready_flag;
static int paging_enabled_flag;

static inline void load_page_directory(uint32_t *pd) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(pd));
}

static uint32_t paging_high_half_index(void) {
    return KERNEL_VIRTUAL_BASE >> 22;
}

static uint32_t *paging_lookup_page_table(uint32_t pd_index) {
    uint32_t high_half_index = paging_high_half_index();

    if (pd_index < KERNEL_EARLY_PAGE_TABLE_COUNT) {
        return early_page_tables[pd_index];
    }
    if (pd_index >= high_half_index &&
        pd_index < high_half_index + KERNEL_EARLY_PAGE_TABLE_COUNT) {
        return early_page_tables[pd_index - high_half_index];
    }
    return dynamic_page_tables[pd_index];
}

static uint32_t *paging_alloc_dynamic_page_table(uint32_t pd_index) {
    uint32_t *pt;
    uint32_t pt_virt;
    uint32_t pt_phys;

    if (paging_lookup_page_table(pd_index) != 0) {
        return paging_lookup_page_table(pd_index);
    }

    pt = (uint32_t *)kmalloc(PAGE_SIZE_BYTES);
    if (!pt) {
        KLOGW("paging: page-table allocation failed for pd=%u", pd_index);
        return 0;
    }

    pt_virt = (uint32_t)(uintptr_t)pt;
    if (pt_virt < KERNEL_VIRTUAL_BASE) {
        KLOGW("paging: page-table allocation outside high-half virt=%x", pt_virt);
        return 0;
    }

    pt_phys = pt_virt - KERNEL_VIRTUAL_BASE;
    if (pt_phys >= KERNEL_EARLY_MAP_BYTES) {
        KLOGW("paging: page-table allocation outside early mirror phys=%x", pt_phys);
        return 0;
    }

    memset(pt, 0, PAGE_SIZE_BYTES);
    dynamic_page_tables[pd_index] = pt;
    page_directory[pd_index] = pt_phys | PAGING_PAGE_FLAG_PRESENT | PAGING_PAGE_FLAG_WRITABLE;
    return pt;
}

void paging_init(void) {
    /* Allocate one page for the page directory and early page tables
     * that map low memory both identity and at the higher-half base. */
    page_directory  = (uint32_t *)pmm_alloc_frame();
    if (!page_directory) {
        KLOGW("paging init failed: page directory allocation failed");
        return;
    }
    for (uint32_t t = 0; t < KERNEL_EARLY_PAGE_TABLE_COUNT; t++) {
        early_page_tables[t] = (uint32_t *)pmm_alloc_frame();
        if (!early_page_tables[t]) {
            KLOGW("paging init failed: page table allocation failed (index=%u)", t);
            return;
        }
    }
    /* Clear the directory */
    for (int i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        page_directory[i] = 0x00000002; /* supervisor, writable, not present */
    }

    for (uint32_t t = 0; t < KERNEL_EARLY_PAGE_TABLE_COUNT; t++) {
        uint32_t *pt = early_page_tables[t];
        uint32_t base = t * PAGE_TABLE_SPAN_BYTES;

        /* Map a contiguous 4MiB chunk. Each entry is the physical address
         * plus flags: present (bit 0) and writable (bit 1). */
        for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
            pt[i] = (base + (i * PAGE_SIZE_BYTES)) | 3U;
        }

        /* Identity mapping */
        page_directory[t] = ((uint32_t)pt) | 3U;
        /* Higher-half mirror */
        page_directory[(KERNEL_VIRTUAL_BASE >> 22) + t] = ((uint32_t)pt) | 3U;
    }

    if (KERNEL_EARLY_MAP_BYTES <= PMM_BITMAP_PHYS_BASE) {
        KLOGW("paging init warning: early map does not cover PMM bitmap");
        return;
    }

    paging_ready_flag = 1;
    KLOGI("paging: initialized (mapped=%u bytes, high-half base=%x, tables=%u)",
          KERNEL_EARLY_MAP_BYTES, KERNEL_VIRTUAL_BASE, KERNEL_EARLY_PAGE_TABLE_COUNT);
}

void paging_enable(void) {
    if (!page_directory) {
        KLOGW("paging_enable called before paging_init");
        return;
    }
    load_page_directory(page_directory);
    uint32_t cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; /* set paging bit */
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
    paging_enabled_flag = 1;
    KLOGI("paging: enabled");
}

int paging_is_ready(void) {
    return paging_ready_flag;
}

int paging_is_enabled(void) {
    return paging_enabled_flag;
}

int paging_mark_user_region(uint32_t vaddr, uint32_t length) {
    uint32_t start;
    uint32_t end;

    if (length == 0) {
        return -KERR_INVAL;
    }
    if (vaddr >= KERNEL_EARLY_MAP_BYTES) {
        return -KERR_INVAL;
    }
    if (vaddr + length < vaddr) {
        return -KERR_INVAL;
    }
    end = vaddr + length;
    if (end > KERNEL_EARLY_MAP_BYTES) {
        return -KERR_INVAL;
    }

    start = vaddr & ~(PAGE_SIZE_BYTES - 1U);
    end = (end + PAGE_SIZE_BYTES - 1U) & ~(PAGE_SIZE_BYTES - 1U);

    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE_BYTES) {
        uint32_t pd_index = addr >> 22;
        uint32_t pt_index = (addr >> 12) & 0x3FFU;
        uint32_t *pt;

        if (pd_index >= KERNEL_EARLY_PAGE_TABLE_COUNT) {
            return -KERR_INVAL;
        }
        pt = early_page_tables[pd_index];
        if (!pt) {
            return -KERR_FAULT;
        }
        /* Mark only the low-half PDE as user. Keep higher-half PDEs supervisor. */
        page_directory[pd_index] |= 0x4U;
        pt[pt_index] |= 0x4U;
    }

    /* Flush TLB by reloading CR3. */
    if (paging_enabled_flag) {
        load_page_directory(page_directory);
    }
    return 0;
}

int paging_map_kernel_region(uint32_t vaddr, uint32_t paddr, uint32_t length, uint32_t page_flags) {
    uint32_t phys_base;
    uint32_t phys_offset;
    uint32_t map_length;

    if (length == 0) {
        return -KERR_INVAL;
    }
    if (vaddr < KERNEL_VIRTUAL_BASE) {
        return -KERR_INVAL;
    }
    if ((vaddr & (PAGE_SIZE_BYTES - 1U)) != 0U) {
        return -KERR_INVAL;
    }

    phys_base = paddr & ~(PAGE_SIZE_BYTES - 1U);
    phys_offset = paddr - phys_base;
    if (length > 0xFFFFFFFFU - phys_offset) {
        return -KERR_INVAL;
    }
    map_length = length + phys_offset;
    if (map_length > 0xFFFFFFFFU - (PAGE_SIZE_BYTES - 1U)) {
        return -KERR_INVAL;
    }
    map_length = (map_length + PAGE_SIZE_BYTES - 1U) & ~(PAGE_SIZE_BYTES - 1U);

    if (vaddr > 0xFFFFFFFFU - map_length) {
        return -KERR_INVAL;
    }

    page_flags |= PAGING_PAGE_FLAG_PRESENT;
    for (uint32_t offset = 0; offset < map_length; offset += PAGE_SIZE_BYTES) {
        uint32_t va = vaddr + offset;
        uint32_t pa = phys_base + offset;
        uint32_t pd_index = va >> 22;
        uint32_t pt_index = (va >> 12) & 0x3FFU;
        uint32_t *pt = paging_lookup_page_table(pd_index);

        if (!pt) {
            pt = paging_alloc_dynamic_page_table(pd_index);
            if (!pt) {
                return -KERR_NOMEM;
            }
        }
        pt[pt_index] = pa | page_flags;
    }

    if (paging_enabled_flag) {
        load_page_directory(page_directory);
    }
    return 0;
}
