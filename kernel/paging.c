#include "paging.h"
#include "pmm.h"
#include "serial.h"
#include "utils.h"

#define PAGE_DIRECTORY_ENTRIES 1024
#define PAGE_TABLE_ENTRIES     1024
#define PAGE_SIZE 4096
#define KERNEL_VIRTUAL_BASE 0xC0000000

/* The page directory and first page table will be allocated from
 * physical memory using the PMM.  They must be page aligned. */
static uint32_t *page_directory;
static uint32_t *first_page_table;

static inline void load_page_directory(uint32_t *pd) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(pd));
}

void paging_init(void) {
    /* Allocate one page for the page directory and one for the first
     * page table.  */
    page_directory  = (uint32_t *)pmm_alloc_frame();
    first_page_table = (uint32_t *)pmm_alloc_frame();
    if (!page_directory || !first_page_table) {
        serial_writestr("paging: allocation failed\n");
        return;
    }
    /* Identity map the first 4MiB.  Each entry is the physical address
     * plus flags: present (bit 0) and writable (bit 1). */
    for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        first_page_table[i] = (i * PAGE_SIZE) | 3;
    }
    /* Clear the directory */
    for (int i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        page_directory[i] = 0x00000002; /* supervisor, writable, not present */
    }
    /* Point the first directory entry at our page table */
    page_directory[0] = ((uint32_t)first_page_table) | 3;
    /* Mirror the first 4MiB into the higher half.  This allows the
     * kernel to access physical addresses through the higher half.  */
    page_directory[KERNEL_VIRTUAL_BASE >> 22] = ((uint32_t)first_page_table) | 3;
    serial_writestr("paging: initialized\n");
}

void paging_enable(void) {
    load_page_directory(page_directory);
    uint32_t cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; /* set paging bit */
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
    serial_writestr("paging: enabled\n");
}
