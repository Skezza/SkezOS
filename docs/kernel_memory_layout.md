# SkezOS Kernel Memory Layout (Phase 1)

## Current constants (`kernel/memory_layout.h`)

- `PAGE_SIZE_BYTES = 4096`
- `KERNEL_VIRTUAL_BASE = 0xC0000000`
- `KERNEL_EARLY_MAP_BYTES = 32 MiB`
- `KERNEL_IDENTITY_MAP_BYTES = 32 MiB`
- `KERNEL_HEAP_START = 0xC0800000`
- `KERNEL_HEAP_SIZE_BYTES = 4 MiB`
- `PMM_BITMAP_PHYS_BASE = 0x01000000` (16 MiB)

## Effective layout (today)

- Physical `0 .. 32 MiB` is identity-mapped
- Physical `0 .. 32 MiB` is also mirrored at `0xC0000000 .. 0xC1FFFFFF`
- Kernel heap starts at `0xC0800000` and uses physical `0x00800000 .. 0x00BFFFFF`
- PMM bitmap lives at physical `0x01000000` (16 MiB) and is covered by the early map

## PMM initialization model (Phase 1 complete)

- PMM bitmap is initialized with all frames marked used
- `memmap_parse()` frees only exact Multiboot "available" ranges
- Boot-critical regions are then re-reserved explicitly:
  - low memory (`0 .. 1 MiB`)
  - kernel image (`__kernel_start .. __kernel_end`)
  - Multiboot2 info block (`mb_info .. mb_info + total_size`)
  - heap backing physical range
  - PMM bitmap storage

## Important invariants

- `KERNEL_EARLY_MAP_BYTES` must continue to cover the PMM bitmap region.
- Heap allocations must stay within the higher-half mapped window.
- Small `kmalloc` allocations grow upward; page-granularity allocations grow downward.
- All frame addresses are page-aligned (`4 KiB`).

## Memory follow-up work (post-Phase 1)

- Replace fixed PMM bitmap placement with a proper reserved allocation from the PMM/VMM bring-up path
- Add a real kernel page allocator/VMM mapping path for large allocations beyond the fixed early map
- Add allocator stress/test hooks callable from a kernel shell/monitor once command infrastructure exists
- Extend page fault reporting with task context when scheduling is implemented
