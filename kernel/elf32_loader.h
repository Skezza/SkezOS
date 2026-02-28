#ifndef ELF32_LOADER_H
#define ELF32_LOADER_H

#include <stdint.h>

struct elf32_user_layout {
    uint32_t entry_eip;
    uint32_t image_base;
    uint32_t image_size;
};

struct elf32_user_image {
    uint32_t entry_eip;
    uint32_t stack_top;
    uint32_t image_base;
    uint32_t image_size;
};

/* Read and validate a small static ET_EXEC i386 ELF, then derive the
 * fixed virtual image window encoded in its PT_LOAD segments.
 */
int elf32_inspect_user_static_path(const char *path, struct elf32_user_layout *out_layout);

/* Load a small static ET_EXEC i386 ELF from the VFS into a fixed
 * user image region and prepare a fixed user stack. Returns 0 on
 * success or a negative -KERR_* code.
 */
int elf32_load_user_static_path(const char *path,
                                uint32_t image_base,
                                uint32_t image_size,
                                uint32_t stack_base,
                                uint32_t stack_size,
                                struct elf32_user_image *out_image);

/* Inspect a static ET_EXEC ELF to derive its fixed image range, then
 * load it into that encoded image range using the provided stack.
 */
int elf32_load_user_static_path_auto(const char *path,
                                     uint32_t stack_base,
                                     uint32_t stack_size,
                                     struct elf32_user_image *out_image);

#endif /* ELF32_LOADER_H */
