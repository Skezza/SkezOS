#include "ata_pio.h"

#include <stdint.h>

#include "kerrno.h"
#include "klog.h"
#include "utils.h"

#define ATA_IO_BASE_PRIMARY 0x1F0U
#define ATA_CTL_BASE_PRIMARY 0x3F6U

#define ATA_REG_DATA      0x00U
#define ATA_REG_ERROR     0x01U
#define ATA_REG_FEATURES  0x01U
#define ATA_REG_SECCOUNT0 0x02U
#define ATA_REG_LBA0      0x03U
#define ATA_REG_LBA1      0x04U
#define ATA_REG_LBA2      0x05U
#define ATA_REG_HDDEVSEL  0x06U
#define ATA_REG_COMMAND   0x07U
#define ATA_REG_STATUS    0x07U

#define ATA_REG_ALTSTATUS 0x00U
#define ATA_REG_CONTROL   0x00U

#define ATA_CMD_READ_SECTORS   0x20U
#define ATA_CMD_WRITE_SECTORS  0x30U
#define ATA_CMD_CACHE_FLUSH    0xE7U
#define ATA_CMD_IDENTIFY       0xECU

#define ATA_SR_ERR  0x01U
#define ATA_SR_DRQ  0x08U
#define ATA_SR_DF   0x20U
#define ATA_SR_DRDY 0x40U
#define ATA_SR_BSY  0x80U

#define ATA_TIMEOUT_SPINS 1000000U

struct ata_pio_device {
    uint16_t io_base;
    uint16_t ctl_base;
    uint32_t sector_count;
    int present;
};

static int ata_read_sector_impl(struct block_device *dev, uint32_t lba, uint8_t *out_sector);
static int ata_write_sector_impl(struct block_device *dev,
                                 uint32_t lba,
                                 const uint8_t *sector_data);
static int ata_flush_impl(struct block_device *dev);

static const block_device_ops_t g_ata_ops = {
    .read_sector = ata_read_sector_impl,
    .write_sector = ata_write_sector_impl,
    .flush = ata_flush_impl,
};

static struct ata_pio_device g_ata_primary_master = {
    .io_base = ATA_IO_BASE_PRIMARY,
    .ctl_base = ATA_CTL_BASE_PRIMARY,
    .sector_count = 0U,
    .present = 0,
};

static struct block_device g_ata_primary_master_block = {
    .name = "ata0-master",
    .ops = &g_ata_ops,
    .driver_private = &g_ata_primary_master,
    .sector_count = 0U,
};

static uint8_t ata_status(struct ata_pio_device *dev) {
    return inb((uint16_t)(dev->io_base + ATA_REG_STATUS));
}

static uint8_t ata_alt_status(struct ata_pio_device *dev) {
    return inb((uint16_t)(dev->ctl_base + ATA_REG_ALTSTATUS));
}

static void ata_400ns_delay(struct ata_pio_device *dev) {
    (void)ata_alt_status(dev);
    (void)ata_alt_status(dev);
    (void)ata_alt_status(dev);
    (void)ata_alt_status(dev);
}

static int ata_wait_not_bsy(struct ata_pio_device *dev) {
    uint32_t spins = ATA_TIMEOUT_SPINS;

    while (spins-- > 0U) {
        if ((ata_alt_status(dev) & ATA_SR_BSY) == 0U) {
            return 0;
        }
    }
    return -KERR_FAULT;
}

static int ata_wait_drq_or_error(struct ata_pio_device *dev) {
    uint32_t spins = ATA_TIMEOUT_SPINS;

    while (spins-- > 0U) {
        uint8_t status = ata_alt_status(dev);
        if ((status & ATA_SR_BSY) != 0U) {
            continue;
        }
        if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0U) {
            return -KERR_FAULT;
        }
        if ((status & ATA_SR_DRQ) != 0U) {
            return 0;
        }
    }
    return -KERR_FAULT;
}

static void ata_select_drive_lba28(struct ata_pio_device *dev, uint32_t lba) {
    outb((uint16_t)(dev->io_base + ATA_REG_HDDEVSEL),
         (uint8_t)(0xE0U | ((lba >> 24U) & 0x0FU)));
    ata_400ns_delay(dev);
}

static int ata_issue_lba28(struct ata_pio_device *dev, uint8_t command, uint32_t lba) {
    int rc;

    rc = ata_wait_not_bsy(dev);
    if (rc < 0) {
        return rc;
    }

    ata_select_drive_lba28(dev, lba);

    outb((uint16_t)(dev->io_base + ATA_REG_FEATURES), 0U);
    outb((uint16_t)(dev->io_base + ATA_REG_SECCOUNT0), 1U);
    outb((uint16_t)(dev->io_base + ATA_REG_LBA0), (uint8_t)(lba & 0xFFU));
    outb((uint16_t)(dev->io_base + ATA_REG_LBA1), (uint8_t)((lba >> 8U) & 0xFFU));
    outb((uint16_t)(dev->io_base + ATA_REG_LBA2), (uint8_t)((lba >> 16U) & 0xFFU));
    outb((uint16_t)(dev->io_base + ATA_REG_COMMAND), command);

    return 0;
}

static int ata_identify_primary_master(struct ata_pio_device *dev) {
    uint16_t identify_words[256];
    uint8_t status;

    if (!dev) {
        return -KERR_INVAL;
    }

    outb((uint16_t)(dev->io_base + ATA_REG_HDDEVSEL), 0xA0U);
    ata_400ns_delay(dev);

    outb((uint16_t)(dev->io_base + ATA_REG_SECCOUNT0), 0U);
    outb((uint16_t)(dev->io_base + ATA_REG_LBA0), 0U);
    outb((uint16_t)(dev->io_base + ATA_REG_LBA1), 0U);
    outb((uint16_t)(dev->io_base + ATA_REG_LBA2), 0U);
    outb((uint16_t)(dev->io_base + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);

    status = ata_status(dev);
    if (status == 0U) {
        return -KERR_NOENT;
    }

    if (ata_wait_not_bsy(dev) < 0) {
        return -KERR_FAULT;
    }

    if (inb((uint16_t)(dev->io_base + ATA_REG_LBA1)) != 0U ||
        inb((uint16_t)(dev->io_base + ATA_REG_LBA2)) != 0U) {
        return -KERR_NOTSUP;
    }

    if (ata_wait_drq_or_error(dev) < 0) {
        return -KERR_FAULT;
    }

    for (uint32_t i = 0U; i < 256U; i++) {
        identify_words[i] = inw((uint16_t)(dev->io_base + ATA_REG_DATA));
    }

    dev->sector_count = ((uint32_t)identify_words[61] << 16U) | identify_words[60];
    if (dev->sector_count == 0U) {
        return -KERR_FAULT;
    }

    dev->present = 1;
    return 0;
}

static int ata_read_sector_impl(struct block_device *dev, uint32_t lba, uint8_t *out_sector) {
    struct ata_pio_device *ata;

    if (!dev || !out_sector) {
        return -KERR_INVAL;
    }

    ata = (struct ata_pio_device *)dev->driver_private;
    if (!ata || !ata->present) {
        return -KERR_NOENT;
    }

    if (ata_issue_lba28(ata, ATA_CMD_READ_SECTORS, lba) < 0) {
        return -KERR_FAULT;
    }
    if (ata_wait_drq_or_error(ata) < 0) {
        return -KERR_FAULT;
    }

    for (uint32_t i = 0U; i < 256U; i++) {
        uint16_t word = inw((uint16_t)(ata->io_base + ATA_REG_DATA));
        out_sector[(i * 2U) + 0U] = (uint8_t)(word & 0xFFU);
        out_sector[(i * 2U) + 1U] = (uint8_t)(word >> 8U);
    }

    return 0;
}

static int ata_write_sector_impl(struct block_device *dev,
                                 uint32_t lba,
                                 const uint8_t *sector_data) {
    struct ata_pio_device *ata;

    if (!dev || !sector_data) {
        return -KERR_INVAL;
    }

    ata = (struct ata_pio_device *)dev->driver_private;
    if (!ata || !ata->present) {
        return -KERR_NOENT;
    }

    if (ata_issue_lba28(ata, ATA_CMD_WRITE_SECTORS, lba) < 0) {
        return -KERR_FAULT;
    }
    if (ata_wait_drq_or_error(ata) < 0) {
        return -KERR_FAULT;
    }

    for (uint32_t i = 0U; i < 256U; i++) {
        uint16_t word = (uint16_t)sector_data[(i * 2U) + 0U] |
                        (uint16_t)((uint16_t)sector_data[(i * 2U) + 1U] << 8U);
        outw((uint16_t)(ata->io_base + ATA_REG_DATA), word);
    }

    if (ata_wait_not_bsy(ata) < 0) {
        return -KERR_FAULT;
    }
    if ((ata_alt_status(ata) & (ATA_SR_ERR | ATA_SR_DF)) != 0U) {
        return -KERR_FAULT;
    }

    return 0;
}

static int ata_flush_impl(struct block_device *dev) {
    struct ata_pio_device *ata;

    if (!dev) {
        return -KERR_INVAL;
    }

    ata = (struct ata_pio_device *)dev->driver_private;
    if (!ata || !ata->present) {
        return -KERR_NOENT;
    }

    if (ata_wait_not_bsy(ata) < 0) {
        return -KERR_FAULT;
    }
    outb((uint16_t)(ata->io_base + ATA_REG_COMMAND), ATA_CMD_CACHE_FLUSH);
    if (ata_wait_not_bsy(ata) < 0) {
        return -KERR_FAULT;
    }
    if ((ata_alt_status(ata) & (ATA_SR_ERR | ATA_SR_DF)) != 0U) {
        return -KERR_FAULT;
    }

    return 0;
}

int ata_pio_init(void) {
    int rc;

    rc = ata_identify_primary_master(&g_ata_primary_master);
    if (rc < 0) {
        if (rc == -KERR_NOENT) {
            KLOGW("ata: primary master not present");
        } else {
            KLOGW("ata: identify failed rc=%d", rc);
        }
        return rc;
    }

    g_ata_primary_master_block.sector_count = g_ata_primary_master.sector_count;

    KLOGI("ata: identify ok model=primary-master sectors=%u", g_ata_primary_master.sector_count);
    return 0;
}

struct block_device *ata_pio_primary_master_device(void) {
    if (!g_ata_primary_master.present) {
        return 0;
    }
    return &g_ata_primary_master_block;
}
