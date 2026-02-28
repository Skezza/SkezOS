#include "klog.h"

#include <stdarg.h>
#include <stdint.h>

#include "serial.h"
#include "vga.h"

static klog_level_t g_klog_min_level = KLOG_LEVEL_INFO;

static const char *klog_level_name(klog_level_t level) {
    switch (level) {
        case KLOG_LEVEL_DEBUG: return "DEBUG";
        case KLOG_LEVEL_INFO:  return "INFO";
        case KLOG_LEVEL_WARN:  return "WARN";
        case KLOG_LEVEL_PANIC: return "PANIC";
        default:               return "UNK";
    }
}

static void klog_write_char(klog_level_t level, char c) {
    if (c == '\n') {
        serial_writechar('\r');
    }
    serial_writechar(c);
    if (level >= KLOG_LEVEL_WARN) {
        vga_putc(c);
    }
}

static void klog_write_str(klog_level_t level, const char *s) {
    while (*s) {
        klog_write_char(level, *s++);
    }
}

static void klog_write_u32_dec(klog_level_t level, uint32_t value) {
    char buf[10];
    int i = 0;
    if (value == 0) {
        klog_write_char(level, '0');
        return;
    }
    while (value > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (i > 0) {
        klog_write_char(level, buf[--i]);
    }
}

static void klog_write_i32_dec(klog_level_t level, int32_t value) {
    if (value < 0) {
        klog_write_char(level, '-');
        /* Avoid UB for INT32_MIN by widening before negation. */
        uint32_t mag = (uint32_t)(-(int64_t)value);
        klog_write_u32_dec(level, mag);
        return;
    }
    klog_write_u32_dec(level, (uint32_t)value);
}

static void klog_write_u32_hex(klog_level_t level, uint32_t value, int width) {
    static const char *hex = "0123456789ABCDEF";
    if (width < 1) {
        width = 1;
    }
    for (int shift = (width - 1) * 4; shift >= 0; shift -= 4) {
        klog_write_char(level, hex[(value >> shift) & 0xF]);
    }
}

void klog_set_level(klog_level_t level) {
    g_klog_min_level = level;
}

klog_level_t klog_get_level(void) {
    return g_klog_min_level;
}

void klog_serial_raw(const char *s) {
    serial_writestr(s);
}

void klogf(klog_level_t level, const char *fmt, ...) {
    uint32_t saved_flags;

    if (level < g_klog_min_level) {
        return;
    }

    saved_flags = vga_console_enter_critical();
    klog_write_char(level, '[');
    klog_write_str(level, klog_level_name(level));
    klog_write_str(level, "] ");

    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            klog_write_char(level, *fmt++);
            continue;
        }
        fmt++;
        switch (*fmt) {
            case '%':
                klog_write_char(level, '%');
                break;
            case 'c':
                klog_write_char(level, (char)va_arg(ap, int));
                break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                klog_write_str(level, s ? s : "(null)");
                break;
            }
            case 'u':
                klog_write_u32_dec(level, va_arg(ap, uint32_t));
                break;
            case 'd':
                klog_write_i32_dec(level, va_arg(ap, int32_t));
                break;
            case 'x':
                klog_write_str(level, "0x");
                klog_write_u32_hex(level, va_arg(ap, uint32_t), 8);
                break;
            case 'p':
                klog_write_str(level, "0x");
                klog_write_u32_hex(level, (uint32_t)(uintptr_t)va_arg(ap, void *), 8);
                break;
            case '\0':
                /* Treat trailing '%' as literal to avoid reading past end. */
                klog_write_char(level, '%');
                fmt--;
                break;
            default:
                klog_write_char(level, '%');
                klog_write_char(level, *fmt);
                break;
        }
        if (*fmt) {
            fmt++;
        }
    }
    va_end(ap);

    klog_write_char(level, '\n');
    vga_console_leave_critical(saved_flags);
}
