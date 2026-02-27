#include "uaccess.h"

#include <stdint.h>

#include "kerrno.h"
#include "memory_layout.h"
#include "utils.h"

static int uaccess_range_in_region(uint32_t addr, uint32_t len, uint32_t base, uint32_t limit) {
    uint32_t range_end;

    if (len == 0U) {
        return 1;
    }
    if (addr < base) {
        return 0;
    }
    range_end = addr + len;
    if (range_end < addr) {
        return 0;
    }
    if (range_end > limit) {
        return 0;
    }
    return 1;
}

int uaccess_user_range_ok(uint32_t addr, uint32_t len) {
    if (uaccess_range_in_region(addr, len, USER_DEMO_REGION_BASE, USER_DEMO_REGION_END)) {
        return 1;
    }
    if (uaccess_range_in_region(addr, len, USER_ELF_SLOT0_REGION_BASE, USER_ELF_SLOT0_REGION_END)) {
        return 1;
    }
    if (uaccess_range_in_region(addr, len, USER_ELF_SLOT1_REGION_BASE, USER_ELF_SLOT1_REGION_END)) {
        return 1;
    }
    if (uaccess_range_in_region(addr, len, USER_ELF_SLOT2_REGION_BASE, USER_ELF_SLOT2_REGION_END)) {
        return 1;
    }
    if (uaccess_range_in_region(addr, len, USER_ELF_SLOT3_REGION_BASE, USER_ELF_SLOT3_REGION_END)) {
        return 1;
    }
    if (uaccess_range_in_region(addr, len, USER_ELF_SLOT4_REGION_BASE, USER_ELF_SLOT4_REGION_END)) {
        return 1;
    }
    if (uaccess_range_in_region(addr, len, USER_ELF_SLOT5_REGION_BASE, USER_ELF_SLOT5_REGION_END)) {
        return 1;
    }
    return 0;
}

int uaccess_copy_from_user(void *dst, uint32_t src_addr, uint32_t len) {
    if (len == 0U) {
        return 0;
    }
    if (!dst) {
        return -KERR_INVAL;
    }
    if (!uaccess_user_range_ok(src_addr, len)) {
        return -KERR_FAULT;
    }
    memcpy(dst, (const void *)(uintptr_t)src_addr, len);
    return 0;
}

int uaccess_copy_to_user(uint32_t dst_addr, const void *src, uint32_t len) {
    if (len == 0U) {
        return 0;
    }
    if (!src) {
        return -KERR_INVAL;
    }
    if (!uaccess_user_range_ok(dst_addr, len)) {
        return -KERR_FAULT;
    }
    memcpy((void *)(uintptr_t)dst_addr, src, len);
    return 0;
}
