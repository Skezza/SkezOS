#include "interrupts.h"

#include "idt.h"
#include "serial.h"
#include "vga.h"
#include "panic.h"
#include "pic.h"
#include "utils.h"

/* A minimal interrupt frame structure used by the GCC interrupt
 * attribute to pass CPU state.  Only the fields required for us to
 * inspect the faulting address (EIP) are defined here. */
struct interrupt_frame {
    uint32_t eip;
    uint16_t cs;
    uint16_t _pad;
    uint32_t eflags;
};

/* Default handler for unhandled interrupts.  It simply prints a
 * message and halts. */
__attribute__((interrupt)) void isr_default(struct interrupt_frame *frame) {
    (void)frame;
    serial_writestr("Unhandled interrupt\n");
    vga_puts("Unhandled interrupt\n");
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

/* Page fault handler.  The second argument provided by the compiler
 * contains the error code.  We read CR2 to find the faulting address
 * and then print it.  Finally we panic. */
__attribute__((interrupt)) void isr_page_fault(struct interrupt_frame *frame, uint32_t error_code) {
    (void)frame;
    (void)error_code;
    uint32_t fault_addr;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(fault_addr));
    serial_writestr("Page fault at 0x");
    /* Convert the faulting address to hex. */
    char buf[9];
    const char *hex = "0123456789ABCDEF";
    buf[8] = '\0';
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[fault_addr & 0xF];
        fault_addr >>= 4;
    }
    serial_writestr(buf);
    serial_writestr("\n");
    vga_puts("Page fault\n");
    panic("Page fault");
}

/* Install the ISRs into the IDT and remap the PIC.
 * Hardware IRQ lines remain masked until the IRQ subsystem and drivers
 * explicitly unmask the lines they need. */
void interrupts_install(void) {
    /* Set up default handlers for the first 32 vectors. */
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (uint32_t)isr_default);
    }
    /* Override page fault (#14) */
    idt_set_gate(14, (uint32_t)isr_page_fault);
    /* Load the IDT */
    idt_install();
    /* Remap the PIC so that IRQs start at vector 32 (0x20) */
    pic_remap(0x20, 0x28);
    /* Keep all IRQ lines masked until irq_init()/driver setup runs. */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}
