#include "block_device.h"

#include <stdint.h>

#include "kerrno.h"

int block_device_read_sector(struct block_device *dev, uint32_t lba, uint8_t *out_sector) {
    if (!dev || !out_sector || !dev->ops || !dev->ops->read_sector) {
        return -KERR_INVAL;
    }
    if (lba >= dev->sector_count) {
        return -KERR_INVAL;
    }
    return dev->ops->read_sector(dev, lba, out_sector);
}

int block_device_write_sector(struct block_device *dev, uint32_t lba, const uint8_t *sector_data) {
    if (!dev || !sector_data || !dev->ops || !dev->ops->write_sector) {
        return -KERR_INVAL;
    }
    if (lba >= dev->sector_count) {
        return -KERR_INVAL;
    }
    return dev->ops->write_sector(dev, lba, sector_data);
}

int block_device_flush(struct block_device *dev) {
    if (!dev || !dev->ops || !dev->ops->flush) {
        return -KERR_INVAL;
    }
    return dev->ops->flush(dev);
}
