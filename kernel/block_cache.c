#include "block_cache.h"

#include <stdint.h>

#include "block_device.h"
#include "kerrno.h"
#include "utils.h"

#define BLOCK_CACHE_ENTRY_COUNT 64U
#define BLOCK_SECTOR_SIZE       512U

struct block_cache_entry {
    uint32_t lba;
    uint8_t data[BLOCK_SECTOR_SIZE];
    int valid;
};

static struct block_device *g_block_cache_device;
static struct block_cache_entry g_block_cache_entries[BLOCK_CACHE_ENTRY_COUNT];
static uint32_t g_block_cache_next_victim;

static uint32_t block_cache_enter_critical(void) {
    uint32_t saved_flags;

    __asm__ __volatile__("pushf\n\t"
                         "pop %0\n\t"
                         "cli"
                         : "=r"(saved_flags)
                         :
                         : "memory");
    return saved_flags;
}

static void block_cache_leave_critical(uint32_t saved_flags) {
    if ((saved_flags & (1U << 9U)) != 0U) {
        __asm__ __volatile__("sti" : : : "memory");
    }
}

static int block_cache_find_entry_locked(uint32_t lba, uint32_t *out_idx) {
    if (!out_idx) {
        return -KERR_INVAL;
    }
    for (uint32_t i = 0U; i < BLOCK_CACHE_ENTRY_COUNT; i++) {
        if (g_block_cache_entries[i].valid && g_block_cache_entries[i].lba == lba) {
            *out_idx = i;
            return 0;
        }
    }
    return -KERR_NOENT;
}

static void block_cache_store_locked(uint32_t lba, const uint8_t *sector_data) {
    uint32_t idx;

    if (block_cache_find_entry_locked(lba, &idx) == 0) {
        memcpy(g_block_cache_entries[idx].data, sector_data, BLOCK_SECTOR_SIZE);
        return;
    }

    idx = g_block_cache_next_victim;
    g_block_cache_next_victim = (g_block_cache_next_victim + 1U) % BLOCK_CACHE_ENTRY_COUNT;

    g_block_cache_entries[idx].lba = lba;
    g_block_cache_entries[idx].valid = 1;
    memcpy(g_block_cache_entries[idx].data, sector_data, BLOCK_SECTOR_SIZE);
}

int block_cache_bind_device(struct block_device *dev) {
    uint32_t saved_flags;

    if (!dev || dev->sector_count == 0U) {
        return -KERR_INVAL;
    }

    saved_flags = block_cache_enter_critical();
    g_block_cache_device = dev;
    g_block_cache_next_victim = 0U;
    memset(g_block_cache_entries, 0, sizeof(g_block_cache_entries));
    block_cache_leave_critical(saved_flags);
    return 0;
}

int block_cache_is_ready(void) {
    return g_block_cache_device != 0;
}

uint32_t block_cache_sector_count(void) {
    if (!g_block_cache_device) {
        return 0U;
    }
    return g_block_cache_device->sector_count;
}

int block_cache_read_sector(uint32_t lba, uint8_t *out_sector) {
    uint32_t saved_flags;
    uint32_t idx;
    int rc;
    uint8_t scratch[BLOCK_SECTOR_SIZE];

    if (!out_sector || !g_block_cache_device) {
        return -KERR_INVAL;
    }
    if (lba >= g_block_cache_device->sector_count) {
        return -KERR_INVAL;
    }

    saved_flags = block_cache_enter_critical();
    rc = block_cache_find_entry_locked(lba, &idx);
    if (rc == 0) {
        memcpy(out_sector, g_block_cache_entries[idx].data, BLOCK_SECTOR_SIZE);
        block_cache_leave_critical(saved_flags);
        return 0;
    }
    block_cache_leave_critical(saved_flags);

    rc = block_device_read_sector(g_block_cache_device, lba, scratch);
    if (rc < 0) {
        return rc;
    }

    saved_flags = block_cache_enter_critical();
    block_cache_store_locked(lba, scratch);
    memcpy(out_sector, scratch, BLOCK_SECTOR_SIZE);
    block_cache_leave_critical(saved_flags);
    return 0;
}

int block_cache_write_sector(uint32_t lba, const uint8_t *sector_data) {
    uint32_t saved_flags;
    int rc;

    if (!sector_data || !g_block_cache_device) {
        return -KERR_INVAL;
    }
    if (lba >= g_block_cache_device->sector_count) {
        return -KERR_INVAL;
    }

    rc = block_device_write_sector(g_block_cache_device, lba, sector_data);
    if (rc < 0) {
        return rc;
    }
    rc = block_device_flush(g_block_cache_device);
    if (rc < 0) {
        return rc;
    }

    saved_flags = block_cache_enter_critical();
    block_cache_store_locked(lba, sector_data);
    block_cache_leave_critical(saved_flags);

    return 0;
}

int block_cache_flush(void) {
    if (!g_block_cache_device) {
        return -KERR_INVAL;
    }
    return block_device_flush(g_block_cache_device);
}
