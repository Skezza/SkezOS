#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#include "syscall_abi.h"

#define SYSCALL_VECTOR 0x80

enum {
    SYSCALL_FD_STDIN  = 0,
    SYSCALL_FD_STDOUT = 1,
    SYSCALL_FD_STDERR = 2,
};

/* Register image saved by the int 0x80 entry stub via PUSHA.
 * The dispatcher reads arguments from these fields and writes the
 * return value back to eax before POPA/IRET.
 */
struct syscall_saved_regs {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_pusha;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
};

/* Install the int 0x80 syscall gate (DPL=3) into the IDT. */
void syscall_init(void);

/* Called from syscall_entry.S. Returns the value to place in EAX. */
uint32_t syscall_dispatch(struct syscall_saved_regs *regs);

#endif /* SYSCALL_H */
