#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdint.h>

/* Low level port I/O primitives.  These wrappers hide the inline
 * assembly used to read and write from hardware ports.  They are
 * marked inline so that calls are inlined into the caller. */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Some devices (notably the PIC) require a small delay between
 * successive port accesses.  Writing to port 0x80 is a commonly
 * accepted way of achieving this. */
static inline void io_wait(void) {
    __asm__ __volatile__("outb %%al, $0x80" : : "a"(0));
}

/* Simple memory and string primitives.  These mirror the standard
 * libc functions but avoid pulling in the entire standard library. */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
size_t strlen(const char *s);

#endif /* UTILS_H */
