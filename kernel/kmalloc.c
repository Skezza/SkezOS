#include "kmalloc.h"
#include <stdint.h>

static uint8_t *heap_base = 0;
static uint8_t *heap_end  = 0;
static uint8_t *heap_ptr  = 0;

/* Simple bump allocator.  It never returns memory to the heap and
 * aligns allocations to 8 bytes. */
void kmalloc_init(void *heap_start, size_t heap_size) {
    heap_base = (uint8_t *)heap_start;
    heap_end  = heap_base + heap_size;
    heap_ptr  = heap_base;
}

void *kmalloc(size_t size) {
    /* Align size to 8 bytes */
    size = (size + 7) & ~7;
    if (heap_ptr + size > heap_end)
        return 0;
    void *ptr = heap_ptr;
    heap_ptr += size;
    return ptr;
}
