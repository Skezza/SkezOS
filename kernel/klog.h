#ifndef KLOG_H
#define KLOG_H

#include <stdint.h>

typedef enum {
    KLOG_LEVEL_DEBUG = 0,
    KLOG_LEVEL_INFO  = 1,
    KLOG_LEVEL_WARN  = 2,
    KLOG_LEVEL_PANIC = 3,
} klog_level_t;

/* Configure the minimum emitted log level. */
void klog_set_level(klog_level_t level);

/* Return the current minimum emitted log level. */
klog_level_t klog_get_level(void);

/* Emit a formatted kernel log line. Supported format verbs:
 * %s %c %u %d %x %p and %%.
 */
void klogf(klog_level_t level, const char *fmt, ...);

/* Write a raw string to the serial console without prefixes/newlines.
 * Use sparingly (e.g. deterministic smoke-test markers).
 */
void klog_serial_raw(const char *s);

#define KLOGD(...) klogf(KLOG_LEVEL_DEBUG, __VA_ARGS__)
#define KLOGI(...) klogf(KLOG_LEVEL_INFO, __VA_ARGS__)
#define KLOGW(...) klogf(KLOG_LEVEL_WARN, __VA_ARGS__)
#define KLOGP(...) klogf(KLOG_LEVEL_PANIC, __VA_ARGS__)

#endif /* KLOG_H */
