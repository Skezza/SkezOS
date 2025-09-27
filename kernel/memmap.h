#ifndef MEMMAP_H
#define MEMMAP_H

#include <stdint.h>

/* Parse the Multiboot2 memory map.  mb_magic and mb_info are passed
 * through from the boot loader.  This routine finds the highest
 * available physical address and passes it to the PMM. */
void memmap_parse(uint32_t mb_magic, uint32_t mb_info);

#endif /* MEMMAP_H */
