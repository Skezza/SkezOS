#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

/* Display a panic message on both the serial console and the VGA
 * display, then halt the processor.  This function does not return. */
void panic(const char *msg) __attribute__((noreturn));

/* Panic helper for assertion failures. */
void panic_assert_failed(const char *expr, const char *file, uint32_t line)
    __attribute__((noreturn));

#endif /* PANIC_H */
