#ifndef ATA_PIO_H
#define ATA_PIO_H

#include "block_device.h"

int ata_pio_init(void);
struct block_device *ata_pio_primary_master_device(void);

#endif /* ATA_PIO_H */
