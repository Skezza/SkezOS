#include "elf32_loader.h"

#include <stdint.h>

#include "kerrno.h"
#include "kfile.h"
#include "klog.h"
#include "kmalloc.h"
#include "memory_layout.h"
#include "paging.h"
#include "utils.h"
#include "vfs.h"

#define ELF32_LOADER_MAX_FILE_BYTES (64U * 1024U)
#define ELF32_EI_NIDENT             16U

enum {
    ELFCLASS32 = 1,
    ELFDATA2LSB = 1,
    EV_CURRENT = 1,
    ET_EXEC = 2,
    EM_386 = 3,
    PT_LOAD = 1,
};

struct elf32_ehdr {
    uint8_t e_ident[ELF32_EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf32_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed));

static uint32_t elf32_page_floor(uint32_t addr) {
    return addr & ~(PAGE_SIZE_BYTES - 1U);
}

static int elf32_page_ceil(uint32_t addr, uint32_t *out_addr) {
    uint32_t rounded = addr + (PAGE_SIZE_BYTES - 1U);

    if (rounded < addr) {
        return 0;
    }
    rounded &= ~(PAGE_SIZE_BYTES - 1U);
    if (out_addr) {
        *out_addr = rounded;
    }
    return 1;
}

static int elf32_add_u32_ok(uint32_t a, uint32_t b, uint32_t *out_sum) {
    uint32_t sum = a + b;
    if (sum < a) {
        return 0;
    }
    if (out_sum) {
        *out_sum = sum;
    }
    return 1;
}

static int elf32_mul_u32_ok(uint32_t a, uint32_t b, uint32_t *out_prod) {
    uint64_t prod = (uint64_t)a * (uint64_t)b;
    if (prod > 0xFFFFFFFFULL) {
        return 0;
    }
    if (out_prod) {
        *out_prod = (uint32_t)prod;
    }
    return 1;
}

static int elf32_range_contains(uint32_t outer_base,
                                uint32_t outer_size,
                                uint32_t inner_base,
                                uint32_t inner_size) {
    uint32_t outer_end;
    uint32_t inner_end;

    if (!elf32_add_u32_ok(outer_base, outer_size, &outer_end)) {
        return 0;
    }
    if (!elf32_add_u32_ok(inner_base, inner_size, &inner_end)) {
        return 0;
    }
    if (inner_base < outer_base) {
        return 0;
    }
    if (inner_end > outer_end) {
        return 0;
    }
    return 1;
}

static int elf32_read_file(const char *path, uint8_t **out_buf, uint32_t *out_size) {
    struct kfile file;
    uint8_t *buf;
    uint32_t total = 0;
    int rc;

    if (!path || !out_buf || !out_size) {
        return -KERR_INVAL;
    }
    *out_buf = 0;
    *out_size = 0;

    rc = vfs_open(path, 0, &file);
    if (rc < 0) {
        KLOGW("elf32: open failed path=%s rc=%d", path, rc);
        return rc;
    }

    /* Loader scratch uses a bounded large allocation and is released
     * after the image is validated/copied.
     */
    buf = (uint8_t *)kmalloc(ELF32_LOADER_MAX_FILE_BYTES + 1U);
    if (!buf) {
        kfile_close(&file);
        return -KERR_NOMEM;
    }

    for (;;) {
        uint32_t n = 0;
        uint32_t remaining = (ELF32_LOADER_MAX_FILE_BYTES + 1U) - total;

        if (remaining == 0U) {
            rc = -KERR_NOMEM;
            KLOGW("elf32: file too large path=%s (> %u bytes)",
                  path, ELF32_LOADER_MAX_FILE_BYTES);
            break;
        }

        rc = kfile_read(&file, buf + total, remaining, &n);
        if (rc < 0) {
            KLOGW("elf32: read failed path=%s rc=%d", path, rc);
            break;
        }
        total += n;
        if (n == 0U) {
            break;
        }
    }

    if (rc >= 0 && total > ELF32_LOADER_MAX_FILE_BYTES) {
        rc = -KERR_NOMEM;
        KLOGW("elf32: file too large path=%s (> %u bytes)",
              path, ELF32_LOADER_MAX_FILE_BYTES);
    }

    if (kfile_close(&file) < 0 && rc >= 0) {
        rc = -KERR_FAULT;
    }
    if (rc < 0) {
        kfree(buf);
        return rc;
    }

    *out_buf = buf;
    *out_size = total;
    return 0;
}

static int elf32_validate_ehdr(const char *path,
                               const uint8_t *file_buf,
                               uint32_t file_size,
                               const struct elf32_ehdr **out_ehdr) {
    const struct elf32_ehdr *ehdr;

    if (!file_buf || !out_ehdr) {
        return -KERR_INVAL;
    }
    if (file_size < sizeof(struct elf32_ehdr)) {
        KLOGW("elf32: file too small path=%s size=%u", path ? path : "(null)", file_size);
        return -KERR_INVAL;
    }

    ehdr = (const struct elf32_ehdr *)(const void *)file_buf;
    if (ehdr->e_ident[0] != 0x7FU ||
        ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F') {
        KLOGW("elf32: bad magic path=%s", path ? path : "(null)");
        return -KERR_INVAL;
    }
    if (ehdr->e_ident[4] != ELFCLASS32 || ehdr->e_ident[5] != ELFDATA2LSB) {
        KLOGW("elf32: unsupported class/data path=%s class=%u data=%u",
              path ? path : "(null)",
              (uint32_t)ehdr->e_ident[4],
              (uint32_t)ehdr->e_ident[5]);
        return -KERR_NOTSUP;
    }
    if (ehdr->e_ident[6] != EV_CURRENT || ehdr->e_version != EV_CURRENT) {
        KLOGW("elf32: unsupported version path=%s ident_ver=%u ver=%u",
              path ? path : "(null)",
              (uint32_t)ehdr->e_ident[6],
              ehdr->e_version);
        return -KERR_NOTSUP;
    }
    if (ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_386) {
        KLOGW("elf32: unsupported type/machine path=%s type=%u machine=%u",
              path ? path : "(null)", ehdr->e_type, ehdr->e_machine);
        return -KERR_NOTSUP;
    }
    if (ehdr->e_ehsize != sizeof(struct elf32_ehdr)) {
        KLOGW("elf32: bad ehdr size path=%s ehsize=%u", path ? path : "(null)", ehdr->e_ehsize);
        return -KERR_INVAL;
    }
    if (ehdr->e_phnum == 0U || ehdr->e_phentsize != sizeof(struct elf32_phdr)) {
        KLOGW("elf32: bad phdr table path=%s phnum=%u phentsz=%u",
              path ? path : "(null)", ehdr->e_phnum, ehdr->e_phentsize);
        return -KERR_INVAL;
    }

    *out_ehdr = ehdr;
    return 0;
}

static int elf32_collect_user_layout(const char *path,
                                     const struct elf32_ehdr *ehdr,
                                     uint32_t file_size,
                                     const uint8_t *file_buf,
                                     struct elf32_user_layout *out_layout) {
    uint32_t ph_table_bytes;
    uint32_t ph_table_end;
    const struct elf32_phdr *phdrs;
    uint32_t load_segments = 0;
    uint32_t image_base = 0U;
    uint32_t image_end = 0U;

    if (!ehdr || !file_buf || !out_layout) {
        return -KERR_INVAL;
    }

    if (!elf32_mul_u32_ok((uint32_t)ehdr->e_phnum, (uint32_t)ehdr->e_phentsize, &ph_table_bytes) ||
        !elf32_add_u32_ok(ehdr->e_phoff, ph_table_bytes, &ph_table_end)) {
        KLOGW("elf32: phdr overflow path=%s", path);
        return -KERR_INVAL;
    }
    if (ehdr->e_phoff > file_size || ph_table_bytes > (file_size - ehdr->e_phoff)) {
        KLOGW("elf32: phdr table out of range path=%s phoff=%u size=%u file=%u",
              path, ehdr->e_phoff, ph_table_bytes, file_size);
        return -KERR_INVAL;
    }
    phdrs = (const struct elf32_phdr *)(const void *)(file_buf + ehdr->e_phoff);

    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        const struct elf32_phdr *ph = &phdrs[i];
        uint32_t seg_end;
        uint32_t file_end;
        uint32_t seg_base_aligned;
        uint32_t seg_end_aligned;

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0U) {
            continue;
        }
        if (ph->p_filesz > ph->p_memsz) {
            KLOGW("elf32: bad segment sizes path=%s idx=%u filesz=%u memsz=%u",
                  path, i, ph->p_filesz, ph->p_memsz);
            return -KERR_INVAL;
        }
        if (!elf32_add_u32_ok(ph->p_offset, ph->p_filesz, &file_end) || file_end > file_size) {
            KLOGW("elf32: segment file range invalid path=%s idx=%u off=%u filesz=%u file=%u",
                  path, i, ph->p_offset, ph->p_filesz, file_size);
            return -KERR_INVAL;
        }
        if (!elf32_add_u32_ok(ph->p_vaddr, ph->p_memsz, &seg_end)) {
            KLOGW("elf32: segment address overflow path=%s idx=%u vaddr=%x memsz=%u",
                  path, i, ph->p_vaddr, ph->p_memsz);
            return -KERR_INVAL;
        }

        seg_base_aligned = elf32_page_floor(ph->p_vaddr);
        if (!elf32_page_ceil(seg_end, &seg_end_aligned)) {
            KLOGW("elf32: segment page align overflow path=%s idx=%u end=%x",
                  path, i, seg_end);
            return -KERR_INVAL;
        }
        if (seg_end_aligned > KERNEL_EARLY_MAP_BYTES) {
            KLOGW("elf32: segment outside low map path=%s idx=%u seg=%x..%x",
                  path, i, seg_base_aligned, seg_end_aligned);
            return -KERR_INVAL;
        }

        if (load_segments == 0U || seg_base_aligned < image_base) {
            image_base = seg_base_aligned;
        }
        if (load_segments == 0U || seg_end_aligned > image_end) {
            image_end = seg_end_aligned;
        }
        load_segments++;
    }

    if (load_segments == 0U) {
        KLOGW("elf32: no loadable segments path=%s", path);
        return -KERR_INVAL;
    }
    if (image_end <= image_base) {
        KLOGW("elf32: invalid image window path=%s image=%x..%x",
              path, image_base, image_end);
        return -KERR_INVAL;
    }
    if (!elf32_range_contains(image_base, image_end - image_base, ehdr->e_entry, 1U)) {
        KLOGW("elf32: entry outside image path=%s entry=%x image=%x..%x",
              path, ehdr->e_entry, image_base, image_end);
        return -KERR_INVAL;
    }

    out_layout->entry_eip = ehdr->e_entry;
    out_layout->image_base = image_base;
    out_layout->image_size = image_end - image_base;
    return 0;
}

int elf32_inspect_user_static_path(const char *path, struct elf32_user_layout *out_layout) {
    uint8_t *file_buf = 0;
    uint32_t file_size = 0;
    const struct elf32_ehdr *ehdr = 0;
    struct kmalloc_stats stats;
    int rc;

    if (!path || !out_layout) {
        return -KERR_INVAL;
    }

    rc = elf32_read_file(path, &file_buf, &file_size);
    if (rc < 0) {
        return rc;
    }

    rc = elf32_validate_ehdr(path, file_buf, file_size, &ehdr);
    if (rc >= 0) {
        rc = elf32_collect_user_layout(path, ehdr, file_size, file_buf, out_layout);
    }

    if (file_buf) {
        kfree(file_buf);
        kmalloc_get_stats(&stats);
        KLOGI("elf32: scratch reclaimed path=%s live_large=%u",
              path, (uint32_t)stats.large_bytes_used);
        KLOGI("SMOKE_LIFECYCLE_ELF_SCRATCH_RECLAIM path=%s live_large=%u",
              path, (uint32_t)stats.large_bytes_used);
    }
    return rc;
}

int elf32_load_user_static_path(const char *path,
                                uint32_t image_base,
                                uint32_t image_size,
                                uint32_t stack_base,
                                uint32_t stack_size,
                                struct elf32_user_image *out_image) {
    uint8_t *file_buf = 0;
    uint32_t file_size = 0;
    const struct elf32_ehdr *ehdr;
    struct elf32_user_layout layout;
    uint32_t image_end;
    uint32_t stack_end;
    const struct elf32_phdr *phdrs;
    uint32_t load_segments = 0;
    struct kmalloc_stats stats;
    int rc;

    if (!path || image_size == 0U || stack_size == 0U) {
        return -KERR_INVAL;
    }
    if (!elf32_add_u32_ok(image_base, image_size, &image_end) ||
        !elf32_add_u32_ok(stack_base, stack_size, &stack_end)) {
        return -KERR_INVAL;
    }
    if (image_end > KERNEL_EARLY_MAP_BYTES || stack_end > KERNEL_EARLY_MAP_BYTES) {
        return -KERR_INVAL;
    }
    if (!((stack_base >= image_end) || (image_base >= stack_end))) {
        return -KERR_INVAL;
    }

    rc = elf32_read_file(path, &file_buf, &file_size);
    if (rc < 0) {
        return rc;
    }

    rc = elf32_validate_ehdr(path, file_buf, file_size, &ehdr);
    if (rc < 0) {
        goto cleanup;
    }

    rc = elf32_collect_user_layout(path, ehdr, file_size, file_buf, &layout);
    if (rc < 0) {
        goto cleanup;
    }
    if (!elf32_range_contains(image_base, image_size, layout.image_base, layout.image_size)) {
        KLOGW("elf32: image outside reserved window path=%s window=%x..%x actual=%x..%x",
              path,
              image_base,
              image_end,
              layout.image_base,
              layout.image_base + layout.image_size);
        rc = -KERR_INVAL;
        goto cleanup;
    }
    phdrs = (const struct elf32_phdr *)(const void *)(file_buf + ehdr->e_phoff);

    memset((void *)(uintptr_t)image_base, 0x00, image_size);

    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        const struct elf32_phdr *ph = &phdrs[i];
        uint32_t seg_end;
        uint32_t file_end;

        if (ph->p_type != PT_LOAD) {
            continue;
        }
        if (ph->p_memsz == 0U) {
            continue;
        }
        if (ph->p_filesz > ph->p_memsz) {
            KLOGW("elf32: bad segment sizes path=%s idx=%u filesz=%u memsz=%u",
                  path, i, ph->p_filesz, ph->p_memsz);
            rc = -KERR_INVAL;
            goto cleanup;
        }
        if (!elf32_add_u32_ok(ph->p_offset, ph->p_filesz, &file_end) || file_end > file_size) {
            KLOGW("elf32: segment file range invalid path=%s idx=%u off=%u filesz=%u file=%u",
                  path, i, ph->p_offset, ph->p_filesz, file_size);
            rc = -KERR_INVAL;
            goto cleanup;
        }
        if (!elf32_add_u32_ok(ph->p_vaddr, ph->p_memsz, &seg_end)) {
            KLOGW("elf32: segment address overflow path=%s idx=%u vaddr=%x memsz=%u",
                  path, i, ph->p_vaddr, ph->p_memsz);
            rc = -KERR_INVAL;
            goto cleanup;
        }
        if (ph->p_vaddr < image_base || seg_end > image_end) {
            KLOGW("elf32: segment outside image path=%s idx=%u seg=%x..%x image=%x..%x",
                  path, i, ph->p_vaddr, seg_end, image_base, image_end);
            rc = -KERR_INVAL;
            goto cleanup;
        }

        if (ph->p_filesz != 0U) {
            memcpy((void *)(uintptr_t)ph->p_vaddr, file_buf + ph->p_offset, ph->p_filesz);
        }
        if (ph->p_memsz > ph->p_filesz) {
            memset((void *)(uintptr_t)(ph->p_vaddr + ph->p_filesz),
                   0x00,
                   ph->p_memsz - ph->p_filesz);
        }
        load_segments++;
    }

    if (load_segments == 0U) {
        KLOGW("elf32: no loadable segments path=%s", path);
        rc = -KERR_INVAL;
        goto cleanup;
    }

    memset((void *)(uintptr_t)stack_base, 0x00, stack_size);

    rc = paging_mark_user_region(image_base, image_size);
    if (rc < 0) {
        KLOGW("elf32: failed to mark image user path=%s rc=%d", path, rc);
        goto cleanup;
    }
    rc = paging_mark_user_region(stack_base, stack_size);
    if (rc < 0) {
        KLOGW("elf32: failed to mark stack user path=%s rc=%d", path, rc);
        goto cleanup;
    }

    if (out_image) {
        out_image->entry_eip = ehdr->e_entry;
        out_image->stack_top = stack_end;
        out_image->image_base = layout.image_base;
        out_image->image_size = layout.image_size;
    }

    KLOGI("elf32: loaded path=%s entry=%x ph=%u loads=%u image=%x..%x stack=%x..%x",
          path,
          ehdr->e_entry,
          (uint32_t)ehdr->e_phnum,
          load_segments,
          image_base,
          image_end,
          stack_base,
          stack_end);
    rc = 0;

cleanup:
    if (file_buf) {
        kfree(file_buf);
        kmalloc_get_stats(&stats);
        KLOGI("elf32: scratch reclaimed path=%s live_large=%u",
              path, (uint32_t)stats.large_bytes_used);
        KLOGI("SMOKE_LIFECYCLE_ELF_SCRATCH_RECLAIM path=%s live_large=%u",
              path, (uint32_t)stats.large_bytes_used);
    }
    return rc;
}

int elf32_load_user_static_path_auto(const char *path,
                                     uint32_t stack_base,
                                     uint32_t stack_size,
                                     struct elf32_user_image *out_image) {
    struct elf32_user_layout layout;
    int rc;

    rc = elf32_inspect_user_static_path(path, &layout);
    if (rc < 0) {
        return rc;
    }
    return elf32_load_user_static_path(path,
                                       layout.image_base,
                                       layout.image_size,
                                       stack_base,
                                       stack_size,
                                       out_image);
}
