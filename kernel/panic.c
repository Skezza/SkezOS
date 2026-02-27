#include "panic.h"
#include "klog.h"

static void panic_halt(void) __attribute__((noreturn));

static void panic_halt(void) {
    __asm__ __volatile__("cli");
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

void panic(const char *msg) {
    KLOGP("%s", msg);
    panic_halt();
}

void panic_assert_failed(const char *expr, const char *file, uint32_t line) {
    KLOGP("assertion failed: %s (%s:%u)", expr, file, line);
    panic_halt();
}
