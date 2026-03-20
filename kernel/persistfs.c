#include "persistfs.h"

#include <stdint.h>

#include "block_cache.h"
#include "kerrno.h"
#include "kfile.h"
#include "klog.h"
#include "kmalloc.h"
#include "syscall_abi.h"
#include "utils.h"
#include "vfs.h"

#define PERSISTFS_MAGIC   0x50524653U
#define PERSISTFS_VERSION 1U

#define PERSISTFS_BLOCK_SIZE         512U
#define PERSISTFS_FILE_TABLE_ENTRIES 64U
#define PERSISTFS_FILE_TABLE_LBA     1U
#define PERSISTFS_FILE_TABLE_SECTORS 16U
#define PERSISTFS_BITMAP_LBA         17U
#define PERSISTFS_BITMAP_SECTORS     8U
#define PERSISTFS_DATA_START_LBA     25U
#define PERSISTFS_MAX_FILENAME       32U
#define PERSISTFS_DIRECT_BLOCKS      20U
#define PERSISTFS_MAX_FILE_SIZE      (PERSISTFS_DIRECT_BLOCKS * PERSISTFS_BLOCK_SIZE)
#define PERSISTFS_BITMAP_BYTES       (PERSISTFS_BITMAP_SECTORS * PERSISTFS_BLOCK_SIZE)
#define PERSISTFS_BITMAP_BITS        (PERSISTFS_BITMAP_BYTES * 8U)

struct persistfs_superblock_disk {
    uint32_t magic;
    uint32_t version;
    uint32_t clean;
    uint32_t total_sectors;
    uint32_t file_table_lba;
    uint32_t file_table_sectors;
    uint32_t bitmap_lba;
    uint32_t bitmap_sectors;
    uint32_t data_start_lba;
    uint32_t max_files;
    uint32_t reserved[(PERSISTFS_BLOCK_SIZE - 40U) / 4U];
};

struct persistfs_file_entry_disk {
    uint32_t in_use;
    uint32_t generation;
    uint32_t size_bytes;
    uint32_t block_count;
    char name[PERSISTFS_MAX_FILENAME];
    uint32_t direct_blocks[PERSISTFS_DIRECT_BLOCKS];
};

struct persistfs_file_slot {
    struct persistfs_file_entry_disk disk;
    struct vfs_node node;
};

struct persistfs_open_file {
    uint32_t slot_index;
    uint32_t generation;
};

static int persistfs_dir_lookup(struct vfs_node *dir,
                                const char *name,
                                uint32_t name_len,
                                struct vfs_node **out_node);
static int persistfs_dir_list(struct vfs_node *dir,
                              struct vfs_dir_entry *entries,
                              uint32_t entry_cap,
                              uint32_t *out_count);
static int persistfs_dir_create_open(struct vfs_node *dir,
                                     const char *name,
                                     uint32_t name_len,
                                     uint32_t open_flags,
                                     struct kfile *out_file);
static int persistfs_dir_unlink(struct vfs_node *dir, const char *name, uint32_t name_len);
static int persistfs_file_open(struct vfs_node *node, uint32_t open_flags, struct kfile *out_file);
static int persistfs_file_read(struct kfile *file, void *buf, uint32_t len, uint32_t *out_read);
static int persistfs_file_write(struct kfile *file,
                                const void *buf,
                                uint32_t len,
                                uint32_t *out_written);
static int persistfs_file_close(struct kfile *file);

static const struct vfs_node_ops g_persistfs_dir_ops = {
    .lookup = persistfs_dir_lookup,
    .list = persistfs_dir_list,
    .open = 0,
    .create_open = persistfs_dir_create_open,
    .unlink = persistfs_dir_unlink,
};

static const struct vfs_node_ops g_persistfs_file_node_ops = {
    .lookup = 0,
    .list = 0,
    .open = persistfs_file_open,
    .create_open = 0,
    .unlink = 0,
};

static const struct kfile_ops g_persistfs_kfile_ops = {
    .read = persistfs_file_read,
    .write = persistfs_file_write,
    .retain = 0,
    .close = persistfs_file_close,
};

static struct persistfs_superblock_disk g_persistfs_super;
static struct persistfs_file_slot g_persistfs_slots[PERSISTFS_FILE_TABLE_ENTRIES];
static uint8_t g_persistfs_bitmap[PERSISTFS_BITMAP_BYTES];
static struct vfs_node g_persistfs_dir = {
    .name = "persist",
    .type = VFS_NODE_DIR,
    .ops = &g_persistfs_dir_ops,
    .backend_private = 0,
};

static uint32_t g_persistfs_data_block_count;
static int g_persistfs_mounted;

static int persistfs_name_eq(const char *lhs, const char *rhs, uint32_t rhs_len) {
    uint32_t i;

    if (!lhs || !rhs) {
        return 0;
    }
    for (i = 0U; i < rhs_len; i++) {
        if (lhs[i] == '\0' || lhs[i] != rhs[i]) {
            return 0;
        }
    }
    return lhs[rhs_len] == '\0';
}

static void persistfs_copy_name(char *dst, const char *src, uint32_t len) {
    if (!dst || !src || len == 0U) {
        return;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static int persistfs_name_valid(const char *name, uint32_t name_len) {
    if (!name || name_len == 0U || name_len >= PERSISTFS_MAX_FILENAME) {
        return 0;
    }
    for (uint32_t i = 0U; i < name_len; i++) {
        if (name[i] == '/' || name[i] == '\0') {
            return 0;
        }
    }
    return 1;
}

static void persistfs_setup_slot_node(uint32_t slot_index) {
    struct persistfs_file_slot *slot = &g_persistfs_slots[slot_index];

    slot->node.name = slot->disk.name;
    slot->node.type = VFS_NODE_FILE;
    slot->node.ops = &g_persistfs_file_node_ops;
    slot->node.backend_private = (void *)(uintptr_t)slot_index;
}

static uint32_t persistfs_next_generation(uint32_t current) {
    uint32_t next = current + 1U;
    if (next == 0U) {
        next = 1U;
    }
    return next;
}

static int persistfs_bitmap_test(uint32_t bit) {
    uint32_t byte_idx;
    uint32_t bit_mask;

    if (bit >= g_persistfs_data_block_count) {
        return 1;
    }
    byte_idx = bit >> 3U;
    bit_mask = 1U << (bit & 7U);
    return (g_persistfs_bitmap[byte_idx] & bit_mask) != 0U;
}

static void persistfs_bitmap_set(uint32_t bit) {
    uint32_t byte_idx = bit >> 3U;
    uint8_t bit_mask = (uint8_t)(1U << (bit & 7U));
    g_persistfs_bitmap[byte_idx] = (uint8_t)(g_persistfs_bitmap[byte_idx] | bit_mask);
}

static void persistfs_bitmap_clear(uint32_t bit) {
    uint32_t byte_idx = bit >> 3U;
    uint8_t bit_mask = (uint8_t)(1U << (bit & 7U));
    g_persistfs_bitmap[byte_idx] = (uint8_t)(g_persistfs_bitmap[byte_idx] & (uint8_t)(~bit_mask));
}

static int persistfs_sync_superblock(void) {
    uint8_t sector[PERSISTFS_BLOCK_SIZE];

    memset(sector, 0, sizeof(sector));
    memcpy(sector, &g_persistfs_super, sizeof(g_persistfs_super));
    return block_cache_write_sector(0U, sector);
}

static int persistfs_sync_file_table(void) {
    uint8_t sector[PERSISTFS_BLOCK_SIZE];

    for (uint32_t i = 0U; i < PERSISTFS_FILE_TABLE_SECTORS; i++) {
        uint32_t base_index = i * 4U;

        memcpy(sector, &g_persistfs_slots[base_index].disk, sizeof(sector));
        if (block_cache_write_sector(PERSISTFS_FILE_TABLE_LBA + i, sector) < 0) {
            return -KERR_FAULT;
        }
    }
    return 0;
}

static int persistfs_sync_bitmap(void) {
    for (uint32_t i = 0U; i < PERSISTFS_BITMAP_SECTORS; i++) {
        uint8_t *sector_ptr = &g_persistfs_bitmap[i * PERSISTFS_BLOCK_SIZE];
        if (block_cache_write_sector(PERSISTFS_BITMAP_LBA + i, sector_ptr) < 0) {
            return -KERR_FAULT;
        }
    }
    return 0;
}

static int persistfs_sync_metadata(void) {
    int rc;

    g_persistfs_super.clean = 0U;
    rc = persistfs_sync_superblock();
    if (rc < 0) {
        return rc;
    }
    rc = block_cache_flush();
    if (rc < 0) {
        return rc;
    }

    rc = persistfs_sync_file_table();
    if (rc < 0) {
        return rc;
    }
    rc = persistfs_sync_bitmap();
    if (rc < 0) {
        return rc;
    }
    rc = block_cache_flush();
    if (rc < 0) {
        return rc;
    }

    g_persistfs_super.clean = 1U;
    rc = persistfs_sync_superblock();
    if (rc < 0) {
        return rc;
    }
    rc = block_cache_flush();
    if (rc < 0) {
        g_persistfs_super.clean = 0U;
        (void)persistfs_sync_superblock();
        return rc;
    }
    return 0;
}

static int persistfs_mark_clean(void) {
    int rc;

    if (g_persistfs_super.clean != 0U) {
        return 0;
    }

    g_persistfs_super.clean = 1U;
    rc = persistfs_sync_superblock();
    if (rc < 0) {
        g_persistfs_super.clean = 0U;
        return rc;
    }
    rc = block_cache_flush();
    if (rc < 0) {
        g_persistfs_super.clean = 0U;
        (void)persistfs_sync_superblock();
        return rc;
    }
    return 0;
}

static int persistfs_load_file_table(void) {
    uint8_t sector[PERSISTFS_BLOCK_SIZE];

    for (uint32_t i = 0U; i < PERSISTFS_FILE_TABLE_SECTORS; i++) {
        uint32_t base_index = i * 4U;

        if (block_cache_read_sector(PERSISTFS_FILE_TABLE_LBA + i, sector) < 0) {
            return -KERR_FAULT;
        }
        memcpy(&g_persistfs_slots[base_index].disk, sector, sizeof(sector));
    }

    for (uint32_t i = 0U; i < PERSISTFS_FILE_TABLE_ENTRIES; i++) {
        struct persistfs_file_slot *slot = &g_persistfs_slots[i];

        if (!slot->disk.in_use) {
            slot->disk.name[0] = '\0';
            if (slot->disk.generation == 0U) {
                slot->disk.generation = 1U;
            }
        } else {
            slot->disk.name[PERSISTFS_MAX_FILENAME - 1U] = '\0';
            if (slot->disk.generation == 0U) {
                slot->disk.generation = 1U;
            }
            if (slot->disk.block_count > PERSISTFS_DIRECT_BLOCKS ||
                slot->disk.size_bytes > PERSISTFS_MAX_FILE_SIZE) {
                return -KERR_FAULT;
            }
            for (uint32_t j = 0U; j < slot->disk.block_count; j++) {
                if (slot->disk.direct_blocks[j] >= g_persistfs_data_block_count) {
                    return -KERR_FAULT;
                }
            }
        }

        persistfs_setup_slot_node(i);
    }

    return 0;
}

static int persistfs_load_bitmap(void) {
    for (uint32_t i = 0U; i < PERSISTFS_BITMAP_SECTORS; i++) {
        uint8_t *sector_ptr = &g_persistfs_bitmap[i * PERSISTFS_BLOCK_SIZE];
        if (block_cache_read_sector(PERSISTFS_BITMAP_LBA + i, sector_ptr) < 0) {
            return -KERR_FAULT;
        }
    }
    return 0;
}

static int persistfs_run_metadata_sanity_pass(void) {
    uint8_t expected_bitmap[PERSISTFS_BITMAP_BYTES];
    uint32_t repaired_set = 0U;
    uint32_t repaired_clear = 0U;

    memset(expected_bitmap, 0, sizeof(expected_bitmap));

    for (uint32_t i = 0U; i < PERSISTFS_FILE_TABLE_ENTRIES; i++) {
        const struct persistfs_file_slot *slot = &g_persistfs_slots[i];
        uint32_t refs = slot->disk.block_count;

        if (!slot->disk.in_use) {
            continue;
        }
        if (refs > PERSISTFS_DIRECT_BLOCKS ||
            slot->disk.size_bytes > PERSISTFS_MAX_FILE_SIZE) {
            return -KERR_FAULT;
        }

        for (uint32_t j = 0U; j < refs; j++) {
            uint32_t block = slot->disk.direct_blocks[j];
            uint32_t byte_idx;
            uint8_t bit_mask;

            if (block >= g_persistfs_data_block_count) {
                return -KERR_FAULT;
            }
            byte_idx = block >> 3U;
            bit_mask = (uint8_t)(1U << (block & 7U));
            if ((expected_bitmap[byte_idx] & bit_mask) != 0U) {
                return -KERR_FAULT;
            }
            expected_bitmap[byte_idx] = (uint8_t)(expected_bitmap[byte_idx] | bit_mask);
        }
    }

    for (uint32_t bit = 0U; bit < g_persistfs_data_block_count; bit++) {
        uint32_t byte_idx = bit >> 3U;
        uint8_t bit_mask = (uint8_t)(1U << (bit & 7U));
        int expected_set = (expected_bitmap[byte_idx] & bit_mask) != 0U;
        int current_set = (g_persistfs_bitmap[byte_idx] & bit_mask) != 0U;

        if (expected_set && !current_set) {
            g_persistfs_bitmap[byte_idx] = (uint8_t)(g_persistfs_bitmap[byte_idx] | bit_mask);
            repaired_set++;
        } else if (!expected_set && current_set) {
            g_persistfs_bitmap[byte_idx] = (uint8_t)(g_persistfs_bitmap[byte_idx] & (uint8_t)(~bit_mask));
            repaired_clear++;
        }
    }

    if (repaired_set != 0U || repaired_clear != 0U) {
        KLOGW("persistfs: sanity repaired bitmap set=%u clear=%u",
              repaired_set,
              repaired_clear);
        return persistfs_sync_metadata();
    }
    KLOGI("persistfs: sanity pass ok");
    return persistfs_mark_clean();
}

static int persistfs_validate_superblock(const struct persistfs_superblock_disk *sb,
                                         uint32_t sector_count) {
    if (!sb) {
        return -KERR_INVAL;
    }
    if (sb->magic != PERSISTFS_MAGIC || sb->version != PERSISTFS_VERSION) {
        return -KERR_NOENT;
    }
    if (sb->total_sectors != sector_count ||
        sb->file_table_lba != PERSISTFS_FILE_TABLE_LBA ||
        sb->file_table_sectors != PERSISTFS_FILE_TABLE_SECTORS ||
        sb->bitmap_lba != PERSISTFS_BITMAP_LBA ||
        sb->bitmap_sectors != PERSISTFS_BITMAP_SECTORS ||
        sb->data_start_lba != PERSISTFS_DATA_START_LBA ||
        sb->max_files != PERSISTFS_FILE_TABLE_ENTRIES) {
        return -KERR_FAULT;
    }
    if (sb->data_start_lba >= sb->total_sectors) {
        return -KERR_FAULT;
    }
    return 0;
}

static int persistfs_format(uint32_t sector_count) {
    memset(&g_persistfs_super, 0, sizeof(g_persistfs_super));
    memset(g_persistfs_slots, 0, sizeof(g_persistfs_slots));
    memset(g_persistfs_bitmap, 0, sizeof(g_persistfs_bitmap));

    for (uint32_t i = 0U; i < PERSISTFS_FILE_TABLE_ENTRIES; i++) {
        g_persistfs_slots[i].disk.generation = 1U;
        persistfs_setup_slot_node(i);
    }

    g_persistfs_super.magic = PERSISTFS_MAGIC;
    g_persistfs_super.version = PERSISTFS_VERSION;
    g_persistfs_super.clean = 1U;
    g_persistfs_super.total_sectors = sector_count;
    g_persistfs_super.file_table_lba = PERSISTFS_FILE_TABLE_LBA;
    g_persistfs_super.file_table_sectors = PERSISTFS_FILE_TABLE_SECTORS;
    g_persistfs_super.bitmap_lba = PERSISTFS_BITMAP_LBA;
    g_persistfs_super.bitmap_sectors = PERSISTFS_BITMAP_SECTORS;
    g_persistfs_super.data_start_lba = PERSISTFS_DATA_START_LBA;
    g_persistfs_super.max_files = PERSISTFS_FILE_TABLE_ENTRIES;

    return persistfs_sync_metadata();
}

static int persistfs_find_slot_by_name(const char *name, uint32_t name_len, uint32_t *out_slot) {
    if (!name || !out_slot) {
        return -KERR_INVAL;
    }
    for (uint32_t i = 0U; i < PERSISTFS_FILE_TABLE_ENTRIES; i++) {
        if (!g_persistfs_slots[i].disk.in_use) {
            continue;
        }
        if (persistfs_name_eq(g_persistfs_slots[i].disk.name, name, name_len)) {
            *out_slot = i;
            return 0;
        }
    }
    return -KERR_NOENT;
}

static int persistfs_find_free_slot(uint32_t *out_slot) {
    if (!out_slot) {
        return -KERR_INVAL;
    }
    for (uint32_t i = 0U; i < PERSISTFS_FILE_TABLE_ENTRIES; i++) {
        if (!g_persistfs_slots[i].disk.in_use) {
            *out_slot = i;
            return 0;
        }
    }
    return -KERR_NOMEM;
}

static int persistfs_alloc_data_block(uint32_t *out_block_index) {
    if (!out_block_index) {
        return -KERR_INVAL;
    }

    for (uint32_t bit = 0U; bit < g_persistfs_data_block_count; bit++) {
        if (!persistfs_bitmap_test(bit)) {
            persistfs_bitmap_set(bit);
            *out_block_index = bit;
            return 0;
        }
    }
    return -KERR_NOMEM;
}

static void persistfs_free_data_block(uint32_t block_index) {
    if (block_index >= g_persistfs_data_block_count) {
        return;
    }
    persistfs_bitmap_clear(block_index);
}

static void persistfs_release_file_blocks(struct persistfs_file_slot *slot) {
    if (!slot) {
        return;
    }

    for (uint32_t i = 0U; i < slot->disk.block_count && i < PERSISTFS_DIRECT_BLOCKS; i++) {
        persistfs_free_data_block(slot->disk.direct_blocks[i]);
        slot->disk.direct_blocks[i] = 0U;
    }
    slot->disk.block_count = 0U;
    slot->disk.size_bytes = 0U;
}

static int persistfs_ensure_file_blocks(struct persistfs_file_slot *slot, uint32_t required_blocks) {
    uint8_t zero_sector[PERSISTFS_BLOCK_SIZE];
    uint32_t old_count;

    if (!slot) {
        return -KERR_INVAL;
    }
    if (required_blocks > PERSISTFS_DIRECT_BLOCKS) {
        return -KERR_NOMEM;
    }
    if (slot->disk.block_count >= required_blocks) {
        return 0;
    }

    old_count = slot->disk.block_count;
    memset(zero_sector, 0, sizeof(zero_sector));

    while (slot->disk.block_count < required_blocks) {
        uint32_t block_index;
        uint32_t lba;

        if (persistfs_alloc_data_block(&block_index) < 0) {
            goto rollback;
        }
        lba = g_persistfs_super.data_start_lba + block_index;
        if (block_cache_write_sector(lba, zero_sector) < 0) {
            persistfs_free_data_block(block_index);
            goto rollback;
        }

        slot->disk.direct_blocks[slot->disk.block_count] = block_index;
        slot->disk.block_count++;
    }

    return 0;

rollback:
    while (slot->disk.block_count > old_count) {
        uint32_t idx = slot->disk.block_count - 1U;
        persistfs_free_data_block(slot->disk.direct_blocks[idx]);
        slot->disk.direct_blocks[idx] = 0U;
        slot->disk.block_count--;
    }
    return -KERR_NOMEM;
}

static int persistfs_handle_get_slot(struct kfile *file,
                                     struct persistfs_file_slot **out_slot,
                                     struct persistfs_open_file **out_handle) {
    struct persistfs_open_file *handle;

    if (!file || !out_slot) {
        return -KERR_INVAL;
    }
    *out_slot = 0;
    if (out_handle) {
        *out_handle = 0;
    }

    handle = (struct persistfs_open_file *)file->backend_private;
    if (!handle || handle->slot_index >= PERSISTFS_FILE_TABLE_ENTRIES) {
        return -KERR_FAULT;
    }

    if (!g_persistfs_slots[handle->slot_index].disk.in_use ||
        g_persistfs_slots[handle->slot_index].disk.generation != handle->generation) {
        return -KERR_NOENT;
    }

    *out_slot = &g_persistfs_slots[handle->slot_index];
    if (out_handle) {
        *out_handle = handle;
    }
    return 0;
}

static int persistfs_dir_lookup(struct vfs_node *dir,
                                const char *name,
                                uint32_t name_len,
                                struct vfs_node **out_node) {
    uint32_t slot_index;

    if (!dir || !name || !out_node || name_len == 0U) {
        return -KERR_INVAL;
    }
    *out_node = 0;

    if (dir != &g_persistfs_dir) {
        return -KERR_INVAL;
    }

    if (persistfs_find_slot_by_name(name, name_len, &slot_index) < 0) {
        return -KERR_NOENT;
    }

    *out_node = &g_persistfs_slots[slot_index].node;
    return 0;
}

static int persistfs_dir_list(struct vfs_node *dir,
                              struct vfs_dir_entry *entries,
                              uint32_t entry_cap,
                              uint32_t *out_count) {
    uint32_t count = 0U;

    if (!dir || !out_count) {
        return -KERR_INVAL;
    }
    *out_count = 0U;

    if (dir != &g_persistfs_dir) {
        return -KERR_INVAL;
    }
    if (entry_cap != 0U && !entries) {
        return -KERR_INVAL;
    }

    for (uint32_t i = 0U; i < PERSISTFS_FILE_TABLE_ENTRIES; i++) {
        if (!g_persistfs_slots[i].disk.in_use) {
            continue;
        }
        if (count >= entry_cap) {
            break;
        }

        entries[count].type = (uint32_t)VFS_NODE_FILE;
        memcpy(entries[count].name,
               g_persistfs_slots[i].disk.name,
               sizeof(entries[count].name));
        entries[count].name[sizeof(entries[count].name) - 1U] = '\0';
        count++;
    }

    *out_count = count;
    return 0;
}

static int persistfs_file_open(struct vfs_node *node, uint32_t open_flags, struct kfile *out_file) {
    uint32_t slot_index;
    struct persistfs_file_slot *slot;
    struct persistfs_open_file *handle;
    int rc;

    if (!node || !out_file) {
        return -KERR_INVAL;
    }

    slot_index = (uint32_t)(uintptr_t)node->backend_private;
    if (slot_index >= PERSISTFS_FILE_TABLE_ENTRIES) {
        return -KERR_FAULT;
    }

    slot = &g_persistfs_slots[slot_index];
    if (!slot->disk.in_use) {
        return -KERR_NOENT;
    }

    if ((open_flags & SYSCALL_OPEN_FLAG_TRUNC) != 0U &&
        (open_flags & SYSCALL_OPEN_FLAG_WRITE) == 0U) {
        return -KERR_INVAL;
    }

    if ((open_flags & SYSCALL_OPEN_FLAG_TRUNC) != 0U) {
        persistfs_release_file_blocks(slot);
        rc = persistfs_sync_metadata();
        if (rc < 0) {
            return rc;
        }
    }

    handle = (struct persistfs_open_file *)kmalloc(sizeof(*handle));
    if (!handle) {
        return -KERR_NOMEM;
    }
    handle->slot_index = slot_index;
    handle->generation = slot->disk.generation;

    kfile_init(out_file, node, &g_persistfs_kfile_ops, handle, open_flags);
    if ((open_flags & SYSCALL_OPEN_FLAG_APPEND) != 0U) {
        out_file->offset = slot->disk.size_bytes;
    }

    return 0;
}

static int persistfs_dir_create_open(struct vfs_node *dir,
                                     const char *name,
                                     uint32_t name_len,
                                     uint32_t open_flags,
                                     struct kfile *out_file) {
    uint32_t slot_index;
    struct persistfs_file_slot *slot;
    uint32_t prev_generation;
    int rc;

    if (!dir || !name || !out_file) {
        return -KERR_INVAL;
    }
    if (dir != &g_persistfs_dir) {
        return -KERR_INVAL;
    }
    if (!persistfs_name_valid(name, name_len)) {
        return -KERR_INVAL;
    }
    if ((open_flags & SYSCALL_OPEN_FLAG_TRUNC) != 0U &&
        (open_flags & SYSCALL_OPEN_FLAG_WRITE) == 0U) {
        return -KERR_INVAL;
    }

    rc = persistfs_find_slot_by_name(name, name_len, &slot_index);
    if (rc == 0) {
        return persistfs_file_open(&g_persistfs_slots[slot_index].node, open_flags, out_file);
    }

    rc = persistfs_find_free_slot(&slot_index);
    if (rc < 0) {
        return rc;
    }

    slot = &g_persistfs_slots[slot_index];
    prev_generation = slot->disk.generation;
    memset(&slot->disk, 0, sizeof(slot->disk));
    slot->disk.in_use = 1U;
    slot->disk.generation = persistfs_next_generation(prev_generation);
    persistfs_copy_name(slot->disk.name, name, name_len);
    persistfs_setup_slot_node(slot_index);

    rc = persistfs_sync_metadata();
    if (rc < 0) {
        memset(&slot->disk, 0, sizeof(slot->disk));
        slot->disk.generation = 1U;
        slot->disk.name[0] = '\0';
        return rc;
    }

    return persistfs_file_open(&slot->node, open_flags, out_file);
}

static int persistfs_dir_unlink(struct vfs_node *dir, const char *name, uint32_t name_len) {
    uint32_t slot_index;
    struct persistfs_file_slot *slot;

    if (!dir || !name || name_len == 0U) {
        return -KERR_INVAL;
    }
    if (dir != &g_persistfs_dir) {
        return -KERR_INVAL;
    }

    if (persistfs_find_slot_by_name(name, name_len, &slot_index) < 0) {
        return -KERR_NOENT;
    }

    slot = &g_persistfs_slots[slot_index];
    persistfs_release_file_blocks(slot);
    slot->disk.in_use = 0U;
    slot->disk.generation = persistfs_next_generation(slot->disk.generation);
    slot->disk.name[0] = '\0';

    return persistfs_sync_metadata();
}

static int persistfs_file_read(struct kfile *file, void *buf, uint32_t len, uint32_t *out_read) {
    struct persistfs_file_slot *slot;
    uint8_t *dst = (uint8_t *)buf;
    uint8_t sector[PERSISTFS_BLOCK_SIZE];
    uint32_t total = 0U;
    int rc;

    if (!file || (len != 0U && !buf)) {
        return -KERR_INVAL;
    }
    if (out_read) {
        *out_read = 0U;
    }

    rc = persistfs_handle_get_slot(file, &slot, 0);
    if (rc < 0) {
        return rc;
    }

    if (len == 0U || file->offset >= slot->disk.size_bytes) {
        return 0;
    }

    while (len > 0U && file->offset < slot->disk.size_bytes) {
        uint32_t block_idx = file->offset / PERSISTFS_BLOCK_SIZE;
        uint32_t block_off = file->offset % PERSISTFS_BLOCK_SIZE;
        uint32_t remaining_in_file = slot->disk.size_bytes - file->offset;
        uint32_t chunk = PERSISTFS_BLOCK_SIZE - block_off;
        uint32_t data_lba;

        if (block_idx >= slot->disk.block_count) {
            break;
        }
        if (chunk > len) {
            chunk = len;
        }
        if (chunk > remaining_in_file) {
            chunk = remaining_in_file;
        }

        data_lba = g_persistfs_super.data_start_lba + slot->disk.direct_blocks[block_idx];
        rc = block_cache_read_sector(data_lba, sector);
        if (rc < 0) {
            return rc;
        }

        memcpy(dst + total, sector + block_off, chunk);
        total += chunk;
        file->offset += chunk;
        len -= chunk;
    }

    if (out_read) {
        *out_read = total;
    }
    return 0;
}

static int persistfs_file_write(struct kfile *file,
                                const void *buf,
                                uint32_t len,
                                uint32_t *out_written) {
    struct persistfs_file_slot *slot;
    struct persistfs_open_file *handle;
    const uint8_t *src = (const uint8_t *)buf;
    uint8_t sector[PERSISTFS_BLOCK_SIZE];
    uint32_t total = 0U;
    uint32_t write_start;
    uint32_t write_end;
    uint32_t required_blocks;
    int rc;

    if (!file || (len != 0U && !buf)) {
        return -KERR_INVAL;
    }
    if ((file->flags & SYSCALL_OPEN_FLAG_WRITE) == 0U) {
        return -KERR_NOTSUP;
    }
    if (out_written) {
        *out_written = 0U;
    }

    rc = persistfs_handle_get_slot(file, &slot, &handle);
    if (rc < 0) {
        return rc;
    }

    if ((file->flags & SYSCALL_OPEN_FLAG_APPEND) != 0U) {
        file->offset = slot->disk.size_bytes;
    }
    if (file->offset > slot->disk.size_bytes) {
        return -KERR_INVAL;
    }

    if (len == 0U) {
        return 0;
    }

    write_start = file->offset;
    write_end = write_start + len;
    if (write_end < write_start || write_end > PERSISTFS_MAX_FILE_SIZE) {
        return -KERR_NOMEM;
    }

    required_blocks = (write_end + (PERSISTFS_BLOCK_SIZE - 1U)) / PERSISTFS_BLOCK_SIZE;
    rc = persistfs_ensure_file_blocks(slot, required_blocks);
    if (rc < 0) {
        return rc;
    }

    while (len > 0U) {
        uint32_t block_idx = file->offset / PERSISTFS_BLOCK_SIZE;
        uint32_t block_off = file->offset % PERSISTFS_BLOCK_SIZE;
        uint32_t chunk = PERSISTFS_BLOCK_SIZE - block_off;
        uint32_t data_lba;

        if (chunk > len) {
            chunk = len;
        }
        data_lba = g_persistfs_super.data_start_lba + slot->disk.direct_blocks[block_idx];

        if (chunk != PERSISTFS_BLOCK_SIZE) {
            rc = block_cache_read_sector(data_lba, sector);
            if (rc < 0) {
                break;
            }
        } else {
            memset(sector, 0, sizeof(sector));
        }

        memcpy(sector + block_off, src + total, chunk);
        rc = block_cache_write_sector(data_lba, sector);
        if (rc < 0) {
            break;
        }

        total += chunk;
        file->offset += chunk;
        len -= chunk;
    }

    if (file->offset > slot->disk.size_bytes) {
        slot->disk.size_bytes = file->offset;
    }

    rc = persistfs_sync_metadata();
    if (rc < 0 && total == 0U) {
        return rc;
    }

    if (out_written) {
        *out_written = total;
    }
    if (len != 0U) {
        return -KERR_FAULT;
    }
    (void)handle;
    return 0;
}

static int persistfs_file_close(struct kfile *file) {
    struct persistfs_open_file *handle;

    if (!file) {
        return -KERR_INVAL;
    }

    handle = (struct persistfs_open_file *)file->backend_private;
    if (handle) {
        kfree(handle);
    }
    return 0;
}

int persistfs_mount(void) {
    uint8_t super_sector[PERSISTFS_BLOCK_SIZE];
    uint32_t sector_count;
    int rc;

    if (g_persistfs_mounted) {
        return 0;
    }
    if (!block_cache_is_ready()) {
        return -KERR_NOTSUP;
    }

    if (sizeof(struct persistfs_superblock_disk) != PERSISTFS_BLOCK_SIZE) {
        return -KERR_FAULT;
    }
    if (sizeof(struct persistfs_file_entry_disk) != 128U) {
        return -KERR_FAULT;
    }

    sector_count = block_cache_sector_count();
    if (sector_count <= PERSISTFS_DATA_START_LBA) {
        return -KERR_NOTSUP;
    }

    g_persistfs_data_block_count = sector_count - PERSISTFS_DATA_START_LBA;
    if (g_persistfs_data_block_count > PERSISTFS_BITMAP_BITS) {
        g_persistfs_data_block_count = PERSISTFS_BITMAP_BITS;
    }

    rc = block_cache_read_sector(0U, super_sector);
    if (rc < 0) {
        return rc;
    }
    memcpy(&g_persistfs_super, super_sector, sizeof(g_persistfs_super));

    rc = persistfs_validate_superblock(&g_persistfs_super, sector_count);
    if (rc < 0) {
        rc = persistfs_format(sector_count);
        if (rc < 0) {
            return rc;
        }
        KLOGI("persistfs: formatted sectors=%u", sector_count);
    } else {
        rc = persistfs_load_file_table();
        if (rc < 0) {
            return rc;
        }
        rc = persistfs_load_bitmap();
        if (rc < 0) {
            return rc;
        }
        if (g_persistfs_super.clean == 0U) {
            KLOGW("persistfs: dirty flag detected; running metadata sanity pass");
            rc = persistfs_run_metadata_sanity_pass();
            if (rc < 0) {
                return rc;
            }
        }
    }

    rc = vfs_register_root_child("persist", &g_persistfs_dir);
    if (rc < 0) {
        return rc;
    }

    g_persistfs_mounted = 1;
    KLOGI("persistfs: mounted /persist sectors=%u data_blocks=%u",
          sector_count,
          g_persistfs_data_block_count);
    return 0;
}
