#include "irq.h"
#include "kerrno.h"
#include "utils.h"

struct irq_entry {
    irq_fn_t handler;
    void *ctx;
};

static struct irq_entry irq_handlers[16];

int irq_register(int irq, irq_fn_t handler, void *ctx) {
    if (irq < 0 || irq >= 16) {
        return -KERR_INVAL;
    }
    irq_handlers[irq].handler = handler;
    irq_handlers[irq].ctx = ctx;
    return 0;
}

void irq_unregister(int irq) {
    if (irq < 0 || irq >= 16) {
        return;
    }
    irq_handlers[irq].handler = 0;
    irq_handlers[irq].ctx = 0;
}

void irq_dispatch(int irq) {
    if (irq < 0 || irq >= 16) {
        return;
    }
    irq_fn_t fn = irq_handlers[irq].handler;
    void *ctx = irq_handlers[irq].ctx;
    if (fn) {
        fn(ctx);
    }
}

static inline uint8_t pic_read_data(int pic) {
    return inb(pic ? 0xA1 : 0x21);
}

static inline void pic_write_data(int pic, uint8_t value) {
    outb(pic ? 0xA1 : 0x21, value);
}

void irq_mask(int irq, int masked) {
    if (irq < 0 || irq >= 16) {
        return;
    }
    int pic = (irq < 8) ? 0 : 1;
    int irq_line = irq & 7;
    uint8_t mask = pic_read_data(pic);
    if (masked) {
        mask |= (1 << irq_line);
    } else {
        mask &= ~(1 << irq_line);
    }
    pic_write_data(pic, mask);
}
