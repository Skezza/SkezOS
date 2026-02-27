#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* An IDT entry is made up of a 32‑bit handler split into two 16‑bit
 * halves, a 16‑bit selector and a type/attributes byte.  The zero
 * field is unused. */
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

/* The IDT pointer used by lidt.  Consists of a 16‑bit limit and a
 * 32‑bit base address. */
typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

/* Set an entry in the IDT to point at HANDLER.  This helper wraps
 * construction of the descriptor fields. */
void idt_set_gate(uint8_t n, uint32_t handler);

/* Same as idt_set_gate but allows user-mode invocation (DPL=3). */
void idt_set_gate_user(uint8_t n, uint32_t handler);

/* Load the IDT register with our table.  Should be called after
 * setting up your gates. */
void idt_install(void);

#endif /* IDT_H */
