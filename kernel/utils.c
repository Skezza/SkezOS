#include "utils.h"

/* Fill a region of memory with a constant byte.  This is used to
 * initialise structures and clear BSS sections. */
void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

/* Copy a block of memory from SRC to DEST.  The regions must not
 * overlap.  Returns the destination pointer. */
void *memcpy(void *dest, const void *src, size_t n) {
    const unsigned char *s = src;
    unsigned char *d = dest;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/* Compute the length of a NUL‑terminated string. */
size_t strlen(const char *s) {
    size_t l = 0;
    while (s[l])
        l++;
    return l;
}
