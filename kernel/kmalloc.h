#ifndef KMALLOC_H
#define KMALLOC_H

#include <stddef.h>
#include <stdint.h>

struct kmalloc_stats {
    uintptr_t heap_base;
    uintptr_t heap_end;
    uintptr_t small_cursor;
    uintptr_t large_cursor;
    size_t small_bytes_used;
    size_t large_bytes_used;
    uint32_t small_alloc_count;
    uint32_t large_alloc_count;
};

/* Initialise the kernel heap.  heap_start and heap_size specify
 * where the heap lives in virtual memory. */
void kmalloc_init(void *heap_start, size_t heap_size);

/* Allocate SIZE bytes from the kernel heap. The heap is not
 * thread-safe. Small allocations are bump-only; large allocations
 * support kfree() reuse. Returns NULL on exhaustion.
 */
void *kmalloc(size_t size);

/* Free a large/page-granularity allocation previously returned by
 * kmalloc(). Small allocations remain bump-only in this bootstrap
 * allocator.
 */
void kfree(void *ptr);

/* Copy allocator state into OUT for diagnostics. */
void kmalloc_get_stats(struct kmalloc_stats *out);

#endif /* KMALLOC_H */
