#ifndef UACCESS_H
#define UACCESS_H

#include <stdint.h>

/* Bootstrap user-memory validation against fixed demo/user ELF regions.
 * Replace with per-process address-space checks in a later phase.
 */
int uaccess_user_range_ok(uint32_t addr, uint32_t len);

/* Copy helpers return 0 on success or negative -KERR_* on failure. */
int uaccess_copy_from_user(void *dst, uint32_t src_addr, uint32_t len);
int uaccess_copy_to_user(uint32_t dst_addr, const void *src, uint32_t len);

#endif /* UACCESS_H */
