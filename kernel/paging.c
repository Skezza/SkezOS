#include "paging.h"

#include "kerrno.h"
#include "klog.h"
#include "kmalloc.h"
#include "memory_layout.h"
#include "pmm.h"
#include "utils.h"

#define PAGE_DIRECTORY_ENTRIES 1024U
#define PAGE_TABLE_ENTRIES     1024U
#define PAGE_TABLE_SPAN_BYTES  (PAGE_TABLE_ENTRIES * PAGE_SIZE_BYTES)
#define KERNEL_EARLY_PAGE_TABLE_COUNT (KERNEL_EARLY_MAP_BYTES / PAGE_TABLE_SPAN_BYTES)
#define PAGING_LOW_PD_COUNT    (KERNEL_VIRTUAL_BASE >> 22)
#define PAGING_OWNED_PT_WORDS  ((PAGING_LOW_PD_COUNT + 31U) / 32U)
#define PAGING_COW_META_CAP    2048U
#define PAGING_PAGE_FLAG_OWNED 0x400U

#if (KERNEL_EARLY_MAP_BYTES % PAGE_TABLE_SPAN_BYTES) != 0
#error "KERNEL_EARLY_MAP_BYTES must be a multiple of 4 MiB"
#endif

struct paging_address_space {
    uint32_t *pd_virt;
    uint32_t pd_phys;
    uint32_t owned_pt_words[PAGING_OWNED_PT_WORDS];
    int is_kernel;
};

struct paging_cow_meta {
    uint32_t frame;
    uint32_t refs;
    uint8_t in_use;
    uint8_t owned;
};

static uint32_t g_kernel_pd_phys;
static uint32_t *g_kernel_pd_virt;
static uint32_t *g_early_page_tables[KERNEL_EARLY_PAGE_TABLE_COUNT];
static uint32_t *g_dynamic_page_tables[PAGE_DIRECTORY_ENTRIES];
static struct paging_address_space g_kernel_as;
static struct paging_address_space *g_current_as;
static struct paging_cow_meta g_cow_meta[PAGING_COW_META_CAP];
static int paging_ready_flag;
static int paging_enabled_flag;

static inline void load_page_directory(uint32_t pd_phys) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(pd_phys));
}

static inline void paging_invlpg(uint32_t vaddr) {
    __asm__ __volatile__("invlpg (%0)" : : "r"((void *)(uintptr_t)vaddr) : "memory");
}

static inline uint32_t paging_irq_save(void) {
    uint32_t flags;
    __asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void paging_irq_restore(uint32_t flags) {
    if ((flags & (1U << 9)) != 0U) {
        __asm__ __volatile__("sti" ::: "memory");
    }
}

static int paging_phys_to_virt(uint32_t phys, uint32_t **out_virt) {
    if (!out_virt || phys >= KERNEL_EARLY_MAP_BYTES) {
        return -KERR_INVAL;
    }
    *out_virt = (uint32_t *)(uintptr_t)(KERNEL_VIRTUAL_BASE + phys);
    return 0;
}

static int paging_virt_to_phys(const void *virt, uint32_t *out_phys) {
    uint32_t va = (uint32_t)(uintptr_t)virt;

    if (!out_phys) {
        return -KERR_INVAL;
    }
    if (va < KERNEL_VIRTUAL_BASE || va >= (KERNEL_VIRTUAL_BASE + KERNEL_EARLY_MAP_BYTES)) {
        return -KERR_INVAL;
    }
    *out_phys = va - KERNEL_VIRTUAL_BASE;
    return 0;
}

static int paging_pd_index_is_low(uint32_t pd_index) {
    return pd_index < PAGING_LOW_PD_COUNT;
}

static void paging_mark_owned_pt(struct paging_address_space *as, uint32_t pd_index) {
    uint32_t word;
    uint32_t bit;

    if (!as || !paging_pd_index_is_low(pd_index)) {
        return;
    }
    word = pd_index / 32U;
    bit = pd_index % 32U;
    as->owned_pt_words[word] |= (1U << bit);
}

static int paging_is_owned_pt(const struct paging_address_space *as, uint32_t pd_index) {
    uint32_t word;
    uint32_t bit;

    if (!as || !paging_pd_index_is_low(pd_index)) {
        return 0;
    }
    word = pd_index / 32U;
    bit = pd_index % 32U;
    return (as->owned_pt_words[word] & (1U << bit)) != 0U;
}

static uint32_t paging_high_half_index(void) {
    return KERNEL_VIRTUAL_BASE >> 22;
}

static uint32_t *paging_lookup_kernel_page_table(uint32_t pd_index) {
    uint32_t high_half_index = paging_high_half_index();

    if (pd_index < KERNEL_EARLY_PAGE_TABLE_COUNT) {
        return g_early_page_tables[pd_index];
    }
    if (pd_index >= high_half_index &&
        pd_index < high_half_index + KERNEL_EARLY_PAGE_TABLE_COUNT) {
        return g_early_page_tables[pd_index - high_half_index];
    }
    return g_dynamic_page_tables[pd_index];
}

static uint32_t *paging_alloc_kernel_dynamic_page_table(uint32_t pd_index) {
    uint32_t *pt;
    uint32_t pt_phys;

    if (paging_lookup_kernel_page_table(pd_index) != 0) {
        return paging_lookup_kernel_page_table(pd_index);
    }

    pt = (uint32_t *)kmalloc(PAGE_SIZE_BYTES);
    if (!pt) {
        KLOGW("paging: page-table allocation failed for pd=%u", pd_index);
        return 0;
    }
    if (paging_virt_to_phys(pt, &pt_phys) < 0) {
        KLOGW("paging: page-table allocation outside early mirror virt=%x",
              (uint32_t)(uintptr_t)pt);
        kfree(pt);
        return 0;
    }

    memset(pt, 0, PAGE_SIZE_BYTES);
    g_dynamic_page_tables[pd_index] = pt;
    g_kernel_pd_virt[pd_index] = pt_phys | PAGING_PAGE_FLAG_PRESENT | PAGING_PAGE_FLAG_WRITABLE;
    return pt;
}

static int paging_aspace_lookup_pte(struct paging_address_space *as,
                                    uint32_t vaddr,
                                    uint32_t **out_pte,
                                    uint32_t **out_pt,
                                    uint32_t *out_pd_index) {
    uint32_t pd_index;
    uint32_t pt_index;
    uint32_t pde;
    uint32_t pt_phys;
    uint32_t *pt_virt;

    if (!as || !out_pte) {
        return -KERR_INVAL;
    }

    pd_index = vaddr >> 22;
    pt_index = (vaddr >> 12) & 0x3FFU;
    pde = as->pd_virt[pd_index];
    if ((pde & PAGING_PAGE_FLAG_PRESENT) == 0U) {
        return -KERR_NOENT;
    }

    pt_phys = pde & ~(PAGE_SIZE_BYTES - 1U);
    if (paging_phys_to_virt(pt_phys, &pt_virt) < 0) {
        return -KERR_FAULT;
    }

    *out_pte = &pt_virt[pt_index];
    if (out_pt) {
        *out_pt = pt_virt;
    }
    if (out_pd_index) {
        *out_pd_index = pd_index;
    }
    return 0;
}

static int paging_aspace_ensure_private_pt(struct paging_address_space *as,
                                           uint32_t pd_index,
                                           uint32_t **out_pt) {
    uint32_t pde;
    uint32_t src_phys;
    uint32_t *src_pt;
    uint32_t *new_pt;
    uint32_t new_phys;

    if (!as || !paging_pd_index_is_low(pd_index)) {
        return -KERR_INVAL;
    }

    pde = as->pd_virt[pd_index];
    if ((pde & PAGING_PAGE_FLAG_PRESENT) == 0U) {
        return -KERR_NOENT;
    }

    if (paging_is_owned_pt(as, pd_index)) {
        src_phys = pde & ~(PAGE_SIZE_BYTES - 1U);
        if (out_pt && paging_phys_to_virt(src_phys, out_pt) < 0) {
            return -KERR_FAULT;
        }
        return 0;
    }

    src_phys = pde & ~(PAGE_SIZE_BYTES - 1U);
    if (paging_phys_to_virt(src_phys, &src_pt) < 0) {
        return -KERR_FAULT;
    }

    new_pt = (uint32_t *)kmalloc(PAGE_SIZE_BYTES);
    if (!new_pt) {
        return -KERR_NOMEM;
    }
    if (paging_virt_to_phys(new_pt, &new_phys) < 0) {
        return -KERR_FAULT;
    }

    memcpy(new_pt, src_pt, PAGE_SIZE_BYTES);
    as->pd_virt[pd_index] = new_phys | (pde & 0xFFFU);
    paging_mark_owned_pt(as, pd_index);
    if (out_pt) {
        *out_pt = new_pt;
    }
    return 0;
}

static struct paging_address_space *paging_alloc_aspace(void) {
    struct paging_address_space *as;
    uint32_t *pd;

    as = (struct paging_address_space *)kmalloc(sizeof(*as));
    if (!as) {
        return 0;
    }
    memset(as, 0, sizeof(*as));

    pd = (uint32_t *)kmalloc(PAGE_SIZE_BYTES);
    if (!pd) {
        kfree(as);
        return 0;
    }
    memset(pd, 0, PAGE_SIZE_BYTES);

    as->pd_virt = pd;
    if (paging_virt_to_phys(pd, &as->pd_phys) < 0) {
        kfree(pd);
        kfree(as);
        return 0;
    }
    return as;
}

static void paging_free_aspace_storage(struct paging_address_space *as) {
    if (!as || as->is_kernel) {
        return;
    }
    if (as->pd_virt) {
        kfree(as->pd_virt);
    }
}

static uint32_t paging_cow_hash(uint32_t frame) {
    return (frame * 2654435761U) % PAGING_COW_META_CAP;
}

static int paging_cow_find_slot(uint32_t frame, int *out_slot) {
    uint32_t idx;
    int free_slot = -1;

    if (!out_slot) {
        return -KERR_INVAL;
    }

    idx = paging_cow_hash(frame);
    for (uint32_t probe = 0; probe < PAGING_COW_META_CAP; probe++) {
        uint32_t slot = (idx + probe) % PAGING_COW_META_CAP;
        struct paging_cow_meta *meta = &g_cow_meta[slot];

        if (meta->in_use) {
            if (meta->frame == frame) {
                *out_slot = (int)slot;
                return 0;
            }
            continue;
        }

        if (free_slot < 0) {
            free_slot = (int)slot;
        }
        break;
    }

    if (free_slot >= 0) {
        *out_slot = free_slot;
        return -KERR_NOENT;
    }
    return -KERR_NOMEM;
}

static int paging_cow_set_refs(uint32_t frame, uint32_t refs, int owned) {
    uint32_t flags;
    int slot;
    int rc;

    flags = paging_irq_save();
    rc = paging_cow_find_slot(frame, &slot);
    if (rc == -KERR_NOENT) {
        struct paging_cow_meta *meta = &g_cow_meta[slot];
        meta->frame = frame;
        meta->refs = refs;
        meta->owned = owned ? 1U : 0U;
        meta->in_use = 1U;
        paging_irq_restore(flags);
        return 0;
    }
    if (rc == 0) {
        struct paging_cow_meta *meta = &g_cow_meta[slot];
        meta->refs = refs;
        meta->owned = owned ? 1U : 0U;
        paging_irq_restore(flags);
        return 0;
    }
    paging_irq_restore(flags);
    return rc;
}

static int paging_cow_inc_ref(uint32_t frame) {
    uint32_t flags;
    int slot;
    int rc;

    flags = paging_irq_save();
    rc = paging_cow_find_slot(frame, &slot);
    if (rc == 0) {
        g_cow_meta[slot].refs++;
        paging_irq_restore(flags);
        return 0;
    }
    paging_irq_restore(flags);
    return rc;
}

static int paging_cow_get(uint32_t frame, uint32_t *out_refs, int *out_owned) {
    uint32_t flags;
    int slot;
    int rc;

    if (!out_refs || !out_owned) {
        return -KERR_INVAL;
    }

    flags = paging_irq_save();
    rc = paging_cow_find_slot(frame, &slot);
    if (rc == 0) {
        *out_refs = g_cow_meta[slot].refs;
        *out_owned = g_cow_meta[slot].owned ? 1 : 0;
        paging_irq_restore(flags);
        return 0;
    }
    paging_irq_restore(flags);
    return rc;
}

static int paging_cow_dec_ref(uint32_t frame, uint32_t *out_refs, int *out_owned) {
    uint32_t flags;
    int slot;
    int rc;

    if (!out_refs || !out_owned) {
        return -KERR_INVAL;
    }

    flags = paging_irq_save();
    rc = paging_cow_find_slot(frame, &slot);
    if (rc != 0) {
        paging_irq_restore(flags);
        return rc;
    }

    if (g_cow_meta[slot].refs == 0U) {
        *out_refs = 0U;
        *out_owned = g_cow_meta[slot].owned ? 1 : 0;
        paging_irq_restore(flags);
        return 0;
    }

    g_cow_meta[slot].refs--;
    *out_refs = g_cow_meta[slot].refs;
    *out_owned = g_cow_meta[slot].owned ? 1 : 0;
    if (g_cow_meta[slot].refs == 0U) {
        memset(&g_cow_meta[slot], 0, sizeof(g_cow_meta[slot]));
    }
    paging_irq_restore(flags);
    return 0;
}

static void paging_cow_remove(uint32_t frame) {
    uint32_t flags;
    int slot;

    flags = paging_irq_save();
    if (paging_cow_find_slot(frame, &slot) == 0) {
        memset(&g_cow_meta[slot], 0, sizeof(g_cow_meta[slot]));
    }
    paging_irq_restore(flags);
}

static void paging_release_user_mapping(uint32_t pte) {
    uint32_t frame;

    if ((pte & PAGING_PAGE_FLAG_PRESENT) == 0U) {
        return;
    }
    frame = pte & ~(PAGE_SIZE_BYTES - 1U);

    if ((pte & PAGING_PAGE_FLAG_COW) != 0U) {
        uint32_t refs = 0U;
        int owned = 0;

        if (paging_cow_dec_ref(frame, &refs, &owned) == 0 && refs == 0U && owned) {
            uint32_t *frame_virt = 0;
            if (paging_phys_to_virt(frame, &frame_virt) == 0) {
                kfree(frame_virt);
            }
        }
        return;
    }

    if ((pte & PAGING_PAGE_FLAG_OWNED) != 0U) {
        uint32_t *frame_virt = 0;
        if (paging_phys_to_virt(frame, &frame_virt) == 0) {
            kfree(frame_virt);
        }
    }
}

void paging_init(void) {
    uint32_t pd_phys;
    uint32_t *pd_low;

    pd_phys = pmm_alloc_frame();
    if (!pd_phys) {
        KLOGW("paging init failed: page directory allocation failed");
        return;
    }
    pd_low = (uint32_t *)(uintptr_t)pd_phys;

    for (uint32_t t = 0; t < KERNEL_EARLY_PAGE_TABLE_COUNT; t++) {
        uint32_t pt_phys = pmm_alloc_frame();
        uint32_t *pt_low;

        if (!pt_phys) {
            KLOGW("paging init failed: page table allocation failed (index=%u)", t);
            return;
        }

        pt_low = (uint32_t *)(uintptr_t)pt_phys;
        for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
            pt_low[i] = (t * PAGE_TABLE_SPAN_BYTES + (i * PAGE_SIZE_BYTES)) |
                        PAGING_PAGE_FLAG_PRESENT |
                        PAGING_PAGE_FLAG_WRITABLE;
        }

        g_early_page_tables[t] =
            (uint32_t *)(uintptr_t)(KERNEL_VIRTUAL_BASE + pt_phys);
    }

    for (uint32_t i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        pd_low[i] = PAGING_PAGE_FLAG_WRITABLE;
    }

    for (uint32_t t = 0; t < KERNEL_EARLY_PAGE_TABLE_COUNT; t++) {
        uint32_t pt_phys;

        if (paging_virt_to_phys(g_early_page_tables[t], &pt_phys) < 0) {
            KLOGW("paging init failed: invalid early page table alias");
            return;
        }

        pd_low[t] = pt_phys | PAGING_PAGE_FLAG_PRESENT | PAGING_PAGE_FLAG_WRITABLE;
        pd_low[(KERNEL_VIRTUAL_BASE >> 22) + t] =
            pt_phys | PAGING_PAGE_FLAG_PRESENT | PAGING_PAGE_FLAG_WRITABLE;
    }

    if (KERNEL_EARLY_MAP_BYTES <= PMM_BITMAP_PHYS_BASE) {
        KLOGW("paging init warning: early map does not cover PMM bitmap");
        return;
    }

    g_kernel_pd_phys = pd_phys;
    g_kernel_pd_virt = (uint32_t *)(uintptr_t)(KERNEL_VIRTUAL_BASE + pd_phys);
    memset(g_dynamic_page_tables, 0, sizeof(g_dynamic_page_tables));
    memset(&g_kernel_as, 0, sizeof(g_kernel_as));
    g_kernel_as.pd_phys = g_kernel_pd_phys;
    g_kernel_as.pd_virt = g_kernel_pd_virt;
    g_kernel_as.is_kernel = 1;
    g_current_as = &g_kernel_as;
    memset(g_cow_meta, 0, sizeof(g_cow_meta));

    paging_ready_flag = 1;
    KLOGI("paging: initialized (mapped=%u bytes, high-half base=%x, tables=%u)",
          KERNEL_EARLY_MAP_BYTES, KERNEL_VIRTUAL_BASE, KERNEL_EARLY_PAGE_TABLE_COUNT);
}

void paging_enable(void) {
    if (!g_kernel_pd_phys) {
        KLOGW("paging_enable called before paging_init");
        return;
    }
    load_page_directory(g_kernel_pd_phys);
    {
        uint32_t cr0;
        __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= 0x80000000U;
        __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
    }
    g_current_as = &g_kernel_as;
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

    if (length == 0U) {
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
        pt = g_early_page_tables[pd_index];
        if (!pt) {
            return -KERR_FAULT;
        }

        g_kernel_pd_virt[pd_index] |= PAGING_PAGE_FLAG_USER;
        pt[pt_index] |= PAGING_PAGE_FLAG_USER;
    }

    if (paging_enabled_flag && g_current_as) {
        load_page_directory(g_current_as->pd_phys);
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
        uint32_t *pt = paging_lookup_kernel_page_table(pd_index);

        if (!pt) {
            pt = paging_alloc_kernel_dynamic_page_table(pd_index);
            if (!pt) {
                return -KERR_NOMEM;
            }
        }
        pt[pt_index] = pa | page_flags;
    }

    if (paging_enabled_flag && g_current_as) {
        load_page_directory(g_current_as->pd_phys);
    }
    return 0;
}

struct paging_address_space *paging_kernel_address_space(void) {
    return &g_kernel_as;
}

struct paging_address_space *paging_current_address_space(void) {
    return g_current_as ? g_current_as : &g_kernel_as;
}

int paging_activate_address_space(struct paging_address_space *as) {
    if (!as) {
        as = &g_kernel_as;
    }
    if (!paging_ready_flag) {
        return -KERR_NOTSUP;
    }

    if (g_current_as == as) {
        return 0;
    }

    g_current_as = as;
    if (paging_enabled_flag) {
        load_page_directory(as->pd_phys);
    }
    return 0;
}

int paging_create_user_address_space(const struct paging_user_range *ranges,
                                     uint32_t range_count,
                                     struct paging_address_space **out_as) {
    struct paging_address_space *as;
    struct paging_address_space *src_as;

    if (!ranges || range_count == 0U || !out_as) {
        return -KERR_INVAL;
    }
    *out_as = 0;

    src_as = paging_current_address_space();
    as = paging_alloc_aspace();
    if (!as) {
        return -KERR_NOMEM;
    }

    memcpy(as->pd_virt, src_as->pd_virt, PAGE_SIZE_BYTES);

    for (uint32_t r = 0; r < range_count; r++) {
        uint32_t base = ranges[r].base;
        uint32_t size = ranges[r].size;
        uint32_t end;

        if (size == 0U) {
            continue;
        }
        if (base + size < base) {
            paging_destroy_address_space(as);
            return -KERR_INVAL;
        }

        end = (base + size + PAGE_SIZE_BYTES - 1U) & ~(PAGE_SIZE_BYTES - 1U);
        base &= ~(PAGE_SIZE_BYTES - 1U);
        for (uint32_t va = base; va < end; va += PAGE_SIZE_BYTES) {
            uint32_t pd_index = va >> 22;
            int rc;

            if (!paging_pd_index_is_low(pd_index)) {
                paging_destroy_address_space(as);
                return -KERR_INVAL;
            }
            rc = paging_aspace_ensure_private_pt(as, pd_index, 0);
            if (rc < 0) {
                paging_destroy_address_space(as);
                return rc;
            }
        }
    }

    *out_as = as;
    return 0;
}

static int paging_clone_user_pts_for_ranges(struct paging_address_space *as,
                                            const struct paging_user_range *ranges,
                                            uint32_t range_count) {
    for (uint32_t r = 0; r < range_count; r++) {
        uint32_t base = ranges[r].base;
        uint32_t size = ranges[r].size;
        uint32_t end;

        if (size == 0U) {
            continue;
        }
        if (base + size < base) {
            return -KERR_INVAL;
        }

        end = (base + size + PAGE_SIZE_BYTES - 1U) & ~(PAGE_SIZE_BYTES - 1U);
        base &= ~(PAGE_SIZE_BYTES - 1U);
        for (uint32_t va = base; va < end; va += PAGE_SIZE_BYTES) {
            uint32_t pd_index = va >> 22;
            int rc;

            if (!paging_pd_index_is_low(pd_index)) {
                return -KERR_INVAL;
            }
            rc = paging_aspace_ensure_private_pt(as, pd_index, 0);
            if (rc < 0) {
                return rc;
            }
        }
    }
    return 0;
}

static uint32_t paging_cow_count_free_slots_locked(void) {
    uint32_t free_slots = 0U;

    for (uint32_t i = 0U; i < PAGING_COW_META_CAP; i++) {
        if (!g_cow_meta[i].in_use) {
            free_slots++;
        }
    }
    return free_slots;
}

static int paging_clone_preflight_cow(struct paging_address_space *parent_as,
                                      const struct paging_user_range *ranges,
                                      uint32_t range_count) {
    uint32_t needed_slots = 0U;
    uint32_t free_slots;

    if (!parent_as || !ranges || range_count == 0U) {
        return -KERR_INVAL;
    }

    for (uint32_t r = 0U; r < range_count; r++) {
        uint32_t base = ranges[r].base;
        uint32_t size = ranges[r].size;
        uint32_t end;

        if (size == 0U) {
            continue;
        }
        if (base + size < base) {
            return -KERR_INVAL;
        }
        end = (base + size + PAGE_SIZE_BYTES - 1U) & ~(PAGE_SIZE_BYTES - 1U);
        base &= ~(PAGE_SIZE_BYTES - 1U);

        for (uint32_t va = base; va < end; va += PAGE_SIZE_BYTES) {
            uint32_t *parent_pte = 0;
            uint32_t pte;
            uint32_t frame;
            int slot_rc;
            int slot;

            if (paging_aspace_lookup_pte(parent_as, va, &parent_pte, 0, 0) < 0 || !parent_pte) {
                continue;
            }
            pte = *parent_pte;
            if ((pte & PAGING_PAGE_FLAG_PRESENT) == 0U ||
                (pte & PAGING_PAGE_FLAG_USER) == 0U) {
                continue;
            }

            frame = pte & ~(PAGE_SIZE_BYTES - 1U);
            slot_rc = paging_cow_find_slot(frame, &slot);
            if ((pte & PAGING_PAGE_FLAG_COW) != 0U) {
                if (slot_rc != 0) {
                    KLOGW("paging: cow metadata missing frame=%x", frame);
                    return -KERR_FAULT;
                }
                continue;
            }
            if ((pte & PAGING_PAGE_FLAG_WRITABLE) == 0U) {
                continue;
            }
            if (slot_rc == -KERR_NOENT) {
                needed_slots++;
            } else if (slot_rc < 0) {
                return slot_rc;
            }
        }
    }

    free_slots = paging_cow_count_free_slots_locked();
    if (needed_slots > free_slots) {
        KLOGW("paging: cow capacity exceeded needed=%u free=%u",
              needed_slots,
              free_slots);
        return -KERR_NOMEM;
    }
    return 0;
}

int paging_clone_address_space_cow(struct paging_address_space *parent_as,
                                   const struct paging_user_range *ranges,
                                   uint32_t range_count,
                                   struct paging_address_space **out_child_as) {
    struct paging_address_space *child_as;
    uint32_t irq_flags = 0U;
    int rc;

    if (!parent_as || !ranges || range_count == 0U || !out_child_as) {
        return -KERR_INVAL;
    }
    *out_child_as = 0;

    child_as = paging_alloc_aspace();
    if (!child_as) {
        return -KERR_NOMEM;
    }

    memcpy(child_as->pd_virt, parent_as->pd_virt, PAGE_SIZE_BYTES);

    if (paging_clone_user_pts_for_ranges(parent_as, ranges, range_count) < 0 ||
        paging_clone_user_pts_for_ranges(child_as, ranges, range_count) < 0) {
        paging_destroy_address_space(child_as);
        return -KERR_NOMEM;
    }

    irq_flags = paging_irq_save();
    rc = paging_clone_preflight_cow(parent_as, ranges, range_count);
    if (rc < 0) {
        paging_irq_restore(irq_flags);
        paging_destroy_address_space(child_as);
        return rc;
    }

    for (uint32_t r = 0; r < range_count; r++) {
        uint32_t base = ranges[r].base;
        uint32_t size = ranges[r].size;
        uint32_t end;

        if (size == 0U) {
            continue;
        }
        end = (base + size + PAGE_SIZE_BYTES - 1U) & ~(PAGE_SIZE_BYTES - 1U);
        base &= ~(PAGE_SIZE_BYTES - 1U);

        for (uint32_t va = base; va < end; va += PAGE_SIZE_BYTES) {
            uint32_t *parent_pte = 0;
            uint32_t *child_pte = 0;
            uint32_t pte;
            uint32_t frame;
            uint32_t new_flags;
            int owned;

            if (paging_aspace_lookup_pte(parent_as, va, &parent_pte, 0, 0) < 0 ||
                paging_aspace_lookup_pte(child_as, va, &child_pte, 0, 0) < 0) {
                continue;
            }

            pte = *parent_pte;
            if ((pte & PAGING_PAGE_FLAG_PRESENT) == 0U ||
                (pte & PAGING_PAGE_FLAG_USER) == 0U) {
                continue;
            }

            frame = pte & ~(PAGE_SIZE_BYTES - 1U);
            owned = (pte & PAGING_PAGE_FLAG_OWNED) != 0U;

            if ((pte & PAGING_PAGE_FLAG_COW) != 0U) {
                if (paging_cow_inc_ref(frame) < 0) {
                    KLOGW("paging: cow clone ref-inc failed frame=%x", frame);
                    paging_irq_restore(irq_flags);
                    paging_destroy_address_space(child_as);
                    return -KERR_NOMEM;
                }
            } else if ((pte & PAGING_PAGE_FLAG_WRITABLE) != 0U) {
                if (paging_cow_set_refs(frame, 2U, owned) < 0) {
                    KLOGW("paging: cow clone set-refs failed frame=%x", frame);
                    paging_irq_restore(irq_flags);
                    paging_destroy_address_space(child_as);
                    return -KERR_NOMEM;
                }
            } else {
                *child_pte = pte;
                continue;
            }

            new_flags = (pte & 0xFFFU) & ~PAGING_PAGE_FLAG_WRITABLE;
            new_flags |= PAGING_PAGE_FLAG_COW;
            if (owned) {
                new_flags |= PAGING_PAGE_FLAG_OWNED;
            }
            *parent_pte = frame | new_flags;
            *child_pte = frame | new_flags;
        }
    }

    if (paging_current_address_space() == parent_as && paging_enabled_flag) {
        load_page_directory(parent_as->pd_phys);
    }
    paging_irq_restore(irq_flags);

    *out_child_as = child_as;
    return 0;
}

void paging_destroy_address_space(struct paging_address_space *as) {
    if (!as || as->is_kernel) {
        return;
    }

    for (uint32_t pd_index = 0; pd_index < PAGING_LOW_PD_COUNT; pd_index++) {
        uint32_t pde;
        uint32_t *pt;

        if (!paging_is_owned_pt(as, pd_index)) {
            continue;
        }

        pde = as->pd_virt[pd_index];
        if ((pde & PAGING_PAGE_FLAG_PRESENT) == 0U) {
            continue;
        }

        if (paging_phys_to_virt(pde & ~(PAGE_SIZE_BYTES - 1U), &pt) < 0) {
            continue;
        }

        for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
            paging_release_user_mapping(pt[i]);
            pt[i] = 0U;
        }

        kfree(pt);
        as->pd_virt[pd_index] = 0U;
    }

    paging_free_aspace_storage(as);
}

int paging_handle_cow_fault(struct paging_address_space *as,
                            uint32_t fault_addr,
                            uint32_t error_code) {
    uint32_t va;
    uint32_t *pte = 0;
    uint32_t old_pte;
    uint32_t old_frame;
    uint32_t refs = 0;
    int owned = 0;

    if (!as) {
        return -KERR_INVAL;
    }
    if ((error_code & ((1U << 0) | (1U << 1) | (1U << 2))) !=
        ((1U << 0) | (1U << 1) | (1U << 2))) {
        return -KERR_NOTSUP;
    }

    va = fault_addr & ~(PAGE_SIZE_BYTES - 1U);
    if (paging_aspace_lookup_pte(as, va, &pte, 0, 0) < 0 || !pte) {
        return -KERR_FAULT;
    }

    old_pte = *pte;
    if ((old_pte & PAGING_PAGE_FLAG_PRESENT) == 0U ||
        (old_pte & PAGING_PAGE_FLAG_COW) == 0U) {
        return -KERR_NOTSUP;
    }

    old_frame = old_pte & ~(PAGE_SIZE_BYTES - 1U);
    if (paging_cow_get(old_frame, &refs, &owned) < 0) {
        refs = 1U;
        owned = (old_pte & PAGING_PAGE_FLAG_OWNED) != 0U;
    }

    if (refs <= 1U) {
        uint32_t new_flags = (old_pte & 0xFFFU) | PAGING_PAGE_FLAG_WRITABLE;

        new_flags &= ~PAGING_PAGE_FLAG_COW;
        if (owned) {
            new_flags |= PAGING_PAGE_FLAG_OWNED;
        } else {
            new_flags &= ~PAGING_PAGE_FLAG_OWNED;
        }

        paging_cow_remove(old_frame);
        *pte = old_frame | new_flags;
        paging_invlpg(va);
        return 0;
    }

    {
        uint32_t *new_page = (uint32_t *)kmalloc(PAGE_SIZE_BYTES);
        uint32_t new_phys;
        uint32_t *old_page_virt = 0;
        uint32_t new_flags;
        uint32_t rem_refs = 0U;
        int rem_owned = 0;

        if (!new_page) {
            return -KERR_NOMEM;
        }
        if (paging_virt_to_phys(new_page, &new_phys) < 0 ||
            paging_phys_to_virt(old_frame, &old_page_virt) < 0) {
            kfree(new_page);
            return -KERR_FAULT;
        }

        memcpy(new_page, old_page_virt, PAGE_SIZE_BYTES);
        if (paging_cow_dec_ref(old_frame, &rem_refs, &rem_owned) < 0) {
            kfree(new_page);
            return -KERR_FAULT;
        }
        if (rem_refs == 0U && rem_owned) {
            kfree(old_page_virt);
        }

        new_flags = (old_pte & 0xFFFU) | PAGING_PAGE_FLAG_WRITABLE | PAGING_PAGE_FLAG_OWNED;
        new_flags &= ~PAGING_PAGE_FLAG_COW;
        *pte = new_phys | new_flags;
        paging_invlpg(va);
    }

    return 0;
}
