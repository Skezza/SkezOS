#include "pmm.h"
#include "serial.h"
#include "utils.h"

#define FRAME_SIZE 4096
#define MAX_MEMORY (1024*1024*1024) /* 1GiB cap for demonstration */

static uint32_t nframes;
static uint32_t *frames;

static void set_frame(uint32_t frame) { frames[frame / 32] |= (1U << (frame % 32)); }
static void clear_frame(uint32_t frame) { frames[frame / 32] &= ~(1U << (frame % 32)); }
static int test_frame(uint32_t frame) { return frames[frame / 32] & (1U << (frame % 32)); }

/* Find the first free frame in the bitmap.  Returns an index or
 * 0xFFFFFFFF if none is available. */
static uint32_t first_free_frame(void) {
    for (uint32_t i = 0; i < nframes / 32; i++) {
        if (frames[i] != 0xFFFFFFFF) {
            for (int j = 0; j < 32; j++) {
                if (!(frames[i] & (1U << j)))
                    return i * 32 + j;
            }
        }
    }
    return 0xFFFFFFFF;
}

void pmm_init(uint32_t mem_upper) {
    if (mem_upper > MAX_MEMORY)
        mem_upper = MAX_MEMORY;
    nframes = mem_upper / FRAME_SIZE;
    /* Compute how many 32‑bit integers are needed for the bitmap. */
    uint32_t bitmap_size_bytes = nframes / 8;
    /* For simplicity we place the bitmap at 1MiB.  This will overwrite
     * part of the kernel in a real system, so a real PMM should call
     * pmm_alloc_frame() to allocate space for the bitmap itself.
     * However for demonstration purposes this suffices. */
    /* Place the bitmap well above the kernel image to avoid overwriting code.
       0x01000000 (16 MiB) is safely within the 1 GiB limit and typically free. */
    frames = (uint32_t *)0x01000000;
    uint32_t words = bitmap_size_bytes / sizeof(uint32_t);
    for (uint32_t i = 0; i < words; i++)
        frames[i] = 0xFFFFFFFF; /* mark all frames used */
    /* Free frames above the bitmap start address */
    for (uint32_t i = 0x01000000 / FRAME_SIZE; i < nframes; i++)
        clear_frame(i);
    serial_writestr("pmm: ready\n");
}

uint32_t pmm_alloc_frame(void) {
    uint32_t frame = first_free_frame();
    if (frame == 0xFFFFFFFF)
        return 0;
    set_frame(frame);
    return frame * FRAME_SIZE;
}

void pmm_free_frame(uint32_t addr) {
    uint32_t frame = addr / FRAME_SIZE;
    clear_frame(frame);
}
