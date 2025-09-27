#ifndef KMALLOC_H
#define KMALLOC_H

#include <stddef.h>

/* Initialise the kernel heap.  heap_start and heap_size specify
 * where the heap lives in virtual memory. */
void kmalloc_init(void *heap_start, size_t heap_size);

/* Allocate SIZE bytes from the kernel heap.  The heap is not
 * thread-safe and does not implement freeing.  Returns NULL on
 * exhaustion. */
void *kmalloc(size_t size);

#endif /* KMALLOC_H */
