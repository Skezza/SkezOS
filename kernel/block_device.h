#ifndef BLOCK_DEVICE_H
#define BLOCK_DEVICE_H

#include <stdint.h>

struct block_device;

typedef int (*block_read_sector_fn_t)(struct block_device *dev, uint32_t lba, uint8_t *out_sector);
typedef int (*block_write_sector_fn_t)(struct block_device *dev,
                                       uint32_t lba,
                                       const uint8_t *sector_data);
typedef int (*block_flush_fn_t)(struct block_device *dev);

typedef struct block_device_ops {
    block_read_sector_fn_t read_sector;
    block_write_sector_fn_t write_sector;
    block_flush_fn_t flush;
} block_device_ops_t;

struct block_device {
    const char *name;
    const block_device_ops_t *ops;
    void *driver_private;
    uint32_t sector_count;
};

int block_device_read_sector(struct block_device *dev, uint32_t lba, uint8_t *out_sector);
int block_device_write_sector(struct block_device *dev, uint32_t lba, const uint8_t *sector_data);
int block_device_flush(struct block_device *dev);

#endif /* BLOCK_DEVICE_H */
