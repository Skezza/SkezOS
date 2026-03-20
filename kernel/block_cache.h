#ifndef BLOCK_CACHE_H
#define BLOCK_CACHE_H

#include <stdint.h>

struct block_device;

int block_cache_bind_device(struct block_device *dev);
int block_cache_is_ready(void);
uint32_t block_cache_sector_count(void);

int block_cache_read_sector(uint32_t lba, uint8_t *out_sector);
int block_cache_write_sector(uint32_t lba, const uint8_t *sector_data);
int block_cache_flush(void);

#endif /* BLOCK_CACHE_H */
