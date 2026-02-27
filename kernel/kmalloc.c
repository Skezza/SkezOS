#include "kmalloc.h"
#include <stdint.h>
#include "memory_layout.h"
#include "utils.h"

static uint8_t *heap_base = 0;
static uint8_t *heap_end  = 0;
static uint8_t *small_ptr = 0;
static uint8_t *large_ptr = 0;
static size_t small_bytes_used = 0;
static size_t large_bytes_used = 0;
static uint32_t small_alloc_count = 0;
static uint32_t large_alloc_count = 0;

#define KMALLOC_SMALL_MAX 256U
#define KMALLOC_LARGE_TRACK_MAX 64U

struct kmalloc_large_alloc {
    uint8_t *ptr;
    size_t size;
    int in_use;
};

static struct kmalloc_large_alloc g_large_allocs[KMALLOC_LARGE_TRACK_MAX];

static uintptr_t align_down_uintptr(uintptr_t value, uintptr_t align) {
    return value & ~(align - 1U);
}

static size_t align_up_size(size_t value, size_t align) {
    return (value + (align - 1U)) & ~(align - 1U);
}

static struct kmalloc_large_alloc *kmalloc_find_unused_large_record(void) {
    for (uint32_t i = 0; i < KMALLOC_LARGE_TRACK_MAX; i++) {
        if (g_large_allocs[i].size == 0U) {
            return &g_large_allocs[i];
        }
    }
    return 0;
}

static struct kmalloc_large_alloc *kmalloc_find_large_record(void *ptr, int want_in_use) {
    uint8_t *addr = (uint8_t *)ptr;

    for (uint32_t i = 0; i < KMALLOC_LARGE_TRACK_MAX; i++) {
        struct kmalloc_large_alloc *rec = &g_large_allocs[i];
        if (rec->size == 0U || rec->ptr != addr) {
            continue;
        }
        if (!!rec->in_use != !!want_in_use) {
            continue;
        }
        return rec;
    }
    return 0;
}

static struct kmalloc_large_alloc *kmalloc_find_best_free_large_record(size_t size) {
    struct kmalloc_large_alloc *best = 0;

    for (uint32_t i = 0; i < KMALLOC_LARGE_TRACK_MAX; i++) {
        struct kmalloc_large_alloc *rec = &g_large_allocs[i];
        if (rec->size == 0U || rec->in_use || rec->size < size) {
            continue;
        }
        if (!best || rec->size < best->size) {
            best = rec;
        }
    }
    return best;
}

static void kmalloc_try_reclaim_large_top(void) {
    for (;;) {
        int reclaimed = 0;

        for (uint32_t i = 0; i < KMALLOC_LARGE_TRACK_MAX; i++) {
            struct kmalloc_large_alloc *rec = &g_large_allocs[i];
            if (rec->size == 0U || rec->in_use || rec->ptr != large_ptr) {
                continue;
            }

            large_ptr += rec->size;
            memset(rec, 0, sizeof(*rec));
            reclaimed = 1;
            break;
        }

        if (!reclaimed) {
            return;
        }
    }
}

/* Split allocator:
 * - small allocations (<= KMALLOC_SMALL_MAX) grow upward from heap base
 * - page-granularity allocations grow downward from heap end
 * This is still no-free and single-threaded, but reduces fragmentation
 * for large buffers and makes alignment predictable.
 */
void kmalloc_init(void *heap_start, size_t heap_size) {
    heap_base = (uint8_t *)heap_start;
    heap_end  = heap_base + heap_size;
    small_ptr = heap_base;
    large_ptr = (uint8_t *)align_down_uintptr((uintptr_t)heap_end, PAGE_SIZE_BYTES);
    small_bytes_used = 0;
    large_bytes_used = 0;
    small_alloc_count = 0;
    large_alloc_count = 0;
    memset(g_large_allocs, 0, sizeof(g_large_allocs));
}

void *kmalloc(size_t size) {
    if (!heap_base || !heap_end || size == 0) {
        return 0;
    }

    if (size <= KMALLOC_SMALL_MAX) {
        size_t alloc_size = (size + 7U) & ~7U;
        if (small_ptr + alloc_size > large_ptr) {
            return 0;
        }
        void *ptr = small_ptr;
        small_ptr += alloc_size;
        small_bytes_used += alloc_size;
        small_alloc_count++;
        return ptr;
    }

    size_t alloc_size = align_up_size(size, PAGE_SIZE_BYTES);
    struct kmalloc_large_alloc *rec = kmalloc_find_best_free_large_record(alloc_size);
    if (rec) {
        rec->in_use = 1;
        large_bytes_used += rec->size;
        large_alloc_count++;
        return rec->ptr;
    }

    uintptr_t large_addr = (uintptr_t)large_ptr;
    uintptr_t small_addr = (uintptr_t)small_ptr;
    if (large_addr < small_addr || (large_addr - small_addr) < alloc_size) {
        return 0;
    }

    rec = kmalloc_find_unused_large_record();
    if (!rec) {
        return 0;
    }

    large_ptr -= alloc_size;
    rec->ptr = large_ptr;
    rec->size = alloc_size;
    rec->in_use = 1;
    large_bytes_used += alloc_size;
    large_alloc_count++;
    return large_ptr;
}

void kfree(void *ptr) {
    struct kmalloc_large_alloc *rec;

    if (!ptr) {
        return;
    }
    if ((((uintptr_t)ptr) & (PAGE_SIZE_BYTES - 1U)) != 0U) {
        return;
    }

    rec = kmalloc_find_large_record(ptr, 1);
    if (!rec) {
        return;
    }

    rec->in_use = 0;
    if (large_bytes_used >= rec->size) {
        large_bytes_used -= rec->size;
    } else {
        large_bytes_used = 0;
    }
    kmalloc_try_reclaim_large_top();
}

void kmalloc_get_stats(struct kmalloc_stats *out) {
    if (!out) {
        return;
    }
    out->heap_base = (uintptr_t)heap_base;
    out->heap_end = (uintptr_t)heap_end;
    out->small_cursor = (uintptr_t)small_ptr;
    out->large_cursor = (uintptr_t)large_ptr;
    out->small_bytes_used = small_bytes_used;
    out->large_bytes_used = large_bytes_used;
    out->small_alloc_count = small_alloc_count;
    out->large_alloc_count = large_alloc_count;
}
