#include "pmm.h"
#include "klog.h"
#include "memory_layout.h"

#define MAX_MEMORY (1024*1024*1024) /* 1GiB cap for demonstration */

static uint32_t nframes;
static uint32_t *frames;
static uint32_t bitmap_bytes;
static uint32_t used_frames;

static uint32_t align_down_u32(uint32_t value, uint32_t align) {
    return value & ~(align - 1U);
}

static uint32_t align_up_u32(uint32_t value, uint32_t align) {
    return (value + align - 1U) & ~(align - 1U);
}

static int frame_index_valid(uint32_t frame) {
    return frame < nframes;
}

static int test_frame(uint32_t frame) {
    if (!frames || !frame_index_valid(frame)) {
        return 1;
    }
    return (frames[frame / 32] & (1U << (frame % 32))) != 0;
}

static void set_frame(uint32_t frame) {
    if (!frames || !frame_index_valid(frame)) {
        return;
    }
    uint32_t mask = 1U << (frame % 32);
    uint32_t *word = &frames[frame / 32];
    if ((*word & mask) == 0) {
        *word |= mask;
        used_frames++;
    }
}

static void clear_frame(uint32_t frame) {
    if (!frames || !frame_index_valid(frame)) {
        return;
    }
    uint32_t mask = 1U << (frame % 32);
    uint32_t *word = &frames[frame / 32];
    if (*word & mask) {
        *word &= ~mask;
        if (used_frames > 0) {
            used_frames--;
        }
    }
}

/* Find the first free frame in the bitmap.  Returns an index or
 * 0xFFFFFFFF if none is available. */
static uint32_t first_free_frame(void) {
    uint32_t words = (nframes + 31U) / 32U;
    for (uint32_t i = 0; i < words; i++) {
        if (frames[i] != 0xFFFFFFFF) {
            for (int j = 0; j < 32; j++) {
                uint32_t frame = i * 32U + (uint32_t)j;
                if (frame >= nframes) {
                    break;
                }
                if (!(frames[i] & (1U << j))) {
                    return frame;
                }
            }
        }
    }
    return 0xFFFFFFFF;
}

void pmm_init(uint32_t mem_upper) {
    if (mem_upper > MAX_MEMORY) {
        mem_upper = MAX_MEMORY;
    }
    nframes = mem_upper / PAGE_SIZE_BYTES;
    used_frames = 0;
    bitmap_bytes = ((nframes + 31U) / 32U) * sizeof(uint32_t);

    /* Place the bitmap well above the kernel image to avoid overwriting code.
     * PMM_BITMAP_PHYS_BASE (16 MiB) is usually free on QEMU's default PC layout.
     */
    frames = (uint32_t *)PMM_BITMAP_PHYS_BASE;
    uint32_t words = (nframes + 31U) / 32U;
    for (uint32_t i = 0; i < words; i++) {
        frames[i] = 0xFFFFFFFF; /* mark all frames used */
    }
    used_frames = nframes;
    KLOGI("pmm: bitmap initialized (total_frames=%u bitmap=%u bytes)",
          nframes, bitmap_bytes);
}

uint32_t pmm_alloc_frame(void) {
    if (!frames || nframes == 0) {
        KLOGW("pmm_alloc_frame called before pmm_init");
        return 0;
    }
    uint32_t frame = first_free_frame();
    if (frame == 0xFFFFFFFF) {
        return 0;
    }
    set_frame(frame);
    return frame * PAGE_SIZE_BYTES;
}

void pmm_free_frame(uint32_t addr) {
    if (!frames || nframes == 0) {
        KLOGW("pmm_free_frame called before pmm_init");
        return;
    }
    if ((addr & (PAGE_SIZE_BYTES - 1U)) != 0) {
        KLOGW("pmm_free_frame ignored unaligned addr=%x", addr);
        return;
    }
    uint32_t frame = addr / PAGE_SIZE_BYTES;
    if (!frame_index_valid(frame)) {
        KLOGW("pmm_free_frame ignored out-of-range addr=%x", addr);
        return;
    }
    if (!test_frame(frame)) {
        KLOGW("pmm_free_frame ignored double free addr=%x", addr);
        return;
    }
    clear_frame(frame);
}

void pmm_get_stats(struct pmm_stats *out) {
    if (!out) {
        return;
    }
    out->total_frames = nframes;
    out->used_frames = used_frames;
    out->free_frames = (nframes >= used_frames) ? (nframes - used_frames) : 0;
    out->bitmap_bytes = bitmap_bytes;
}

int pmm_is_ready(void) {
    return frames != 0 && nframes != 0;
}

void pmm_mark_region_available(uint32_t base, uint32_t length) {
    if (!frames || nframes == 0 || length == 0) {
        return;
    }

    uint64_t end64 = (uint64_t)base + (uint64_t)length;
    uint64_t max64 = (uint64_t)nframes * (uint64_t)PAGE_SIZE_BYTES;
    if (base >= max64) {
        return;
    }
    if (end64 > max64) {
        end64 = max64;
    }

    uint32_t start_aligned = align_up_u32(base, PAGE_SIZE_BYTES);
    uint32_t end_aligned = align_down_u32((uint32_t)end64, PAGE_SIZE_BYTES);
    if (end_aligned <= start_aligned) {
        return;
    }

    uint32_t start_frame = start_aligned / PAGE_SIZE_BYTES;
    uint32_t end_frame = end_aligned / PAGE_SIZE_BYTES;
    for (uint32_t frame = start_frame; frame < end_frame; frame++) {
        clear_frame(frame);
    }
}

void pmm_reserve_region(uint32_t base, uint32_t length) {
    if (!frames || nframes == 0 || length == 0) {
        return;
    }

    uint64_t end64 = (uint64_t)base + (uint64_t)length;
    uint64_t max64 = (uint64_t)nframes * (uint64_t)PAGE_SIZE_BYTES;
    if (base >= max64) {
        return;
    }
    if (end64 > max64) {
        end64 = max64;
    }

    uint32_t start_aligned = align_down_u32(base, PAGE_SIZE_BYTES);
    uint32_t end_aligned = align_up_u32((uint32_t)end64, PAGE_SIZE_BYTES);
    if (end_aligned <= start_aligned) {
        return;
    }

    uint32_t start_frame = start_aligned / PAGE_SIZE_BYTES;
    uint32_t end_frame = end_aligned / PAGE_SIZE_BYTES;
    for (uint32_t frame = start_frame; frame < end_frame; frame++) {
        set_frame(frame);
    }
}
