#include "idt.h"
#include "utils.h"

/* The actual IDT and its descriptor.  We define them static so
 * outside code cannot access them directly. */
static idt_entry_t idt[256];
static idt_ptr_t idt_desc;

/* The assembly routine to load the IDT, provided in idt_load.S. */
extern void idt_load(uint32_t);

static void idt_set_entry(int i, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[i].offset_low = base & 0xFFFF;
    idt[i].selector   = sel;
    idt[i].zero       = 0;
    idt[i].type_attr  = flags;
    idt[i].offset_high = (base >> 16) & 0xFFFF;
}

void idt_set_gate(uint8_t n, uint32_t handler) {
    /* 0x8E = present | ring 0 | 32‑bit interrupt gate */
    idt_set_entry(n, handler, 0x08, 0x8E);
}

void idt_install(void) {
    idt_desc.limit = sizeof(idt) - 1;
    idt_desc.base  = (uint32_t)&idt;
    /* Zero the table before loading.  Without this some entries may
     * contain garbage from BSS initialisation. */
    for (int i = 0; i < 256; i++) {
        idt_set_entry(i, 0, 0, 0);
    }
    idt_load((uint32_t)&idt_desc);
}
