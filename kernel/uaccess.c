#include "uaccess.h"

#include <stdint.h>

#include "kerrno.h"
#include "sched.h"
#include "utils.h"

int uaccess_user_range_ok(uint32_t addr, uint32_t len) {
    return sched_current_user_range_ok(addr, len);
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
