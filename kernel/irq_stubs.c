#include <stdint.h>
#include "irq.h"
#include "utils.h"
#include "idt.h"

/* Interrupt frame passed to interrupt handlers */
struct interrupt_frame {
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
};

static inline void pic_send_eoi(int irq) {
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

/* Forward declarations for irq_dispatch and irq_mask from irq.c */
extern void irq_dispatch(int irq);
extern void irq_mask(int irq, int masked);

/* IRQ0 is acknowledged before dispatch so the timer handler can
 * safely preempt/switch tasks while the interrupted kernel stack is
 * suspended. Other IRQs keep the usual ack-after-dispatch behavior.
 */
__attribute__((interrupt)) void irq0_stub(struct interrupt_frame *frame) { (void)frame; pic_send_eoi(0); irq_dispatch(0); }
__attribute__((interrupt)) void irq1_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(1); pic_send_eoi(1); }
__attribute__((interrupt)) void irq2_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(2); pic_send_eoi(2); }
__attribute__((interrupt)) void irq3_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(3); pic_send_eoi(3); }
__attribute__((interrupt)) void irq4_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(4); pic_send_eoi(4); }
__attribute__((interrupt)) void irq5_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(5); pic_send_eoi(5); }
__attribute__((interrupt)) void irq6_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(6); pic_send_eoi(6); }
__attribute__((interrupt)) void irq7_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(7); pic_send_eoi(7); }
__attribute__((interrupt)) void irq8_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(8); pic_send_eoi(8); }
__attribute__((interrupt)) void irq9_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(9); pic_send_eoi(9); }
__attribute__((interrupt)) void irq10_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(10); pic_send_eoi(10); }
__attribute__((interrupt)) void irq11_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(11); pic_send_eoi(11); }
__attribute__((interrupt)) void irq12_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(12); pic_send_eoi(12); }
__attribute__((interrupt)) void irq13_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(13); pic_send_eoi(13); }
__attribute__((interrupt)) void irq14_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(14); pic_send_eoi(14); }
__attribute__((interrupt)) void irq15_stub(struct interrupt_frame *frame) { (void)frame; irq_dispatch(15); pic_send_eoi(15); }

/* Table of stub pointers for convenience */
static void (*irq_stub_table[16])(struct interrupt_frame *) = {
    irq0_stub, irq1_stub, irq2_stub, irq3_stub,
    irq4_stub, irq5_stub, irq6_stub, irq7_stub,
    irq8_stub, irq9_stub, irq10_stub, irq11_stub,
    irq12_stub, irq13_stub, irq14_stub, irq15_stub
};

void irq_init(void) {
    /* Mask all IRQ lines and register stubs into the IDT */
    for (int i = 0; i < 16; i++) {
        irq_mask(i, 1);
        idt_set_gate(32 + i, (uint32_t)irq_stub_table[i]);
    }
}
