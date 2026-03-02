#ifndef MEMMAP_H
#define MEMMAP_H

#include <stdint.h>

struct boot_framebuffer_info {
    uint64_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t type;
    uint8_t red_field_position;
    uint8_t red_mask_size;
    uint8_t green_field_position;
    uint8_t green_mask_size;
    uint8_t blue_field_position;
    uint8_t blue_mask_size;
};

/* Parse the Multiboot2 memory map.  mb_magic and mb_info are passed
 * through from the boot loader.  This routine finds the highest
 * available physical address, captures optional framebuffer metadata,
 * and passes the memory ceiling to the PMM.
 */
void memmap_parse(uint32_t mb_magic, uint32_t mb_info);

/* Copy out the Multiboot2 framebuffer handoff info if the bootloader
 * provided it.  Returns 0 on success, -1 if no framebuffer tag was
 * present.
 */
int memmap_get_framebuffer_info(struct boot_framebuffer_info *out);

#endif /* MEMMAP_H */
