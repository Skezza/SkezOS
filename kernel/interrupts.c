#include "interrupts.h"

#include "idt.h"
#include "klog.h"
#include "panic.h"
#include "pic.h"
#include "sched.h"
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

static int frame_from_user_mode(const struct interrupt_frame *frame) {
    if (!frame) {
        return 0;
    }
    return (((uint32_t)frame->cs) & 0x3U) == 0x3U;
}

static void terminate_faulting_user_task(const char *kind) __attribute__((noreturn));

static void terminate_faulting_user_task(const char *kind) {
    KLOGW("fault recovery: terminating user task pid=%d name=%s after %s",
          sched_current_task_pid(),
          sched_current_task_name(),
          kind ? kind : "exception");
    sched_exit_current();
}

/* Default handler for unhandled interrupts.  It simply prints a
 * message and halts. */
__attribute__((interrupt)) void isr_default(struct interrupt_frame *frame) {
    int user_mode = frame_from_user_mode(frame);
    KLOGW("%s unhandled exception/interrupt: eip=%x cs=%x eflags=%x",
          user_mode ? "user" : "kernel",
          frame->eip, (uint32_t)frame->cs, frame->eflags);
    if (user_mode) {
        terminate_faulting_user_task("unhandled exception");
    }
    panic("Unhandled interrupt");
}

/* Generic handler for exceptions that push an error code but do not
 * yet have dedicated decoding (e.g. #GP, #SS, #NP, #DF, #AC). */
__attribute__((interrupt)) void isr_error_code_default(struct interrupt_frame *frame, uint32_t error_code) {
    int user_mode = frame_from_user_mode(frame);
    KLOGW("%s exception (error-code): eip=%x cs=%x eflags=%x err=%x",
          user_mode ? "user" : "kernel",
          frame->eip, (uint32_t)frame->cs, frame->eflags, error_code);
    if (user_mode) {
        terminate_faulting_user_task("error-code exception");
    }
    panic("Unhandled exception (error code)");
}

static const char *pf_reason(uint32_t error_code) {
    return (error_code & (1U << 0)) ? "protection" : "not-present";
}

static const char *pf_access(uint32_t error_code) {
    return (error_code & (1U << 1)) ? "write" : "read";
}

static const char *pf_mode(uint32_t error_code) {
    return (error_code & (1U << 2)) ? "user" : "kernel";
}

static const char *pf_exec(uint32_t error_code) {
    return (error_code & (1U << 4)) ? "exec" : "data";
}

/* Page fault handler. The error code bits are decoded for diagnostics:
 * P, W/R, U/S, RSVD, I/D. */
__attribute__((interrupt)) void isr_page_fault(struct interrupt_frame *frame, uint32_t error_code) {
    uint32_t fault_addr;
    int user_mode = frame_from_user_mode(frame);
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(fault_addr));
    if (user_mode) {
        sched_note_current_user_fault(frame->eip, fault_addr, error_code);
        KLOGW("user page fault: pid=%d name=%s cr2=%x eip=%x cs=%x eflags=%x err=%x",
              sched_current_task_pid(),
              sched_current_task_name(),
              fault_addr, frame->eip, (uint32_t)frame->cs, frame->eflags, error_code);
        KLOGW("user page fault decode: kind=%s access=%s mode=%s op=%s rsvd=%u",
              pf_reason(error_code),
              pf_access(error_code),
              pf_mode(error_code),
              pf_exec(error_code),
              (error_code >> 3) & 1U);
        terminate_faulting_user_task("page fault");
    }

    KLOGP("page fault: cr2=%x eip=%x cs=%x eflags=%x err=%x",
          fault_addr, frame->eip, (uint32_t)frame->cs, frame->eflags, error_code);
    KLOGP("page fault decode: kind=%s access=%s mode=%s op=%s rsvd=%u",
          pf_reason(error_code),
          pf_access(error_code),
          pf_mode(error_code),
          pf_exec(error_code),
          (error_code >> 3) & 1U);
    panic("Page fault");
}

/* Install the ISRs into the IDT and remap the PIC.
 * Hardware IRQ lines remain masked until the IRQ subsystem and drivers
 * explicitly unmask the lines they need. */
void interrupts_install(void) {
    static const uint8_t error_code_vectors[] = { 8, 10, 11, 12, 13, 14, 17 };
    /* Set up default handlers for the first 32 vectors. */
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (uint32_t)isr_default);
    }
    for (uint32_t i = 0; i < sizeof(error_code_vectors) / sizeof(error_code_vectors[0]); i++) {
        idt_set_gate(error_code_vectors[i], (uint32_t)isr_error_code_default);
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
