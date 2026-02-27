#include "idt.h"
#include "utils.h"

/* The actual IDT and its descriptor.  We define them static so
 * outside code cannot access them directly. */
static idt_entry_t idt[256];
static idt_ptr_t idt_desc;

/* The assembly routine to load the IDT, provided in idt_load.S. */
extern void idt_load(uint32_t);

static uint16_t idt_code_selector(void) {
    uint16_t cs;
    __asm__ __volatile__("mov %%cs, %0" : "=r"(cs));
    return cs;
}

static void idt_set_entry(int i, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[i].offset_low = base & 0xFFFF;
    idt[i].selector   = sel;
    idt[i].zero       = 0;
    idt[i].type_attr  = flags;
    idt[i].offset_high = (base >> 16) & 0xFFFF;
}

void idt_set_gate(uint8_t n, uint32_t handler) {
    /* 0x8E = present | ring 0 | 32‑bit interrupt gate */
    idt_set_entry(n, handler, idt_code_selector(), 0x8E);
}

void idt_set_gate_user(uint8_t n, uint32_t handler) {
    /* 0xEE = present | ring 3 | 32-bit interrupt gate */
    idt_set_entry(n, handler, idt_code_selector(), 0xEE);
}

void idt_install(void) {
    idt_desc.limit = sizeof(idt) - 1;
    idt_desc.base  = (uint32_t)&idt;
    /* The IDT lives in BSS and starts zeroed. Callers populate entries
     * before loading; do not wipe them here.
     */
    idt_load((uint32_t)&idt_desc);
}
