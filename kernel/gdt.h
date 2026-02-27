#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define GDT_SEL_KERNEL_CODE 0x08
#define GDT_SEL_KERNEL_DATA 0x10
#define GDT_SEL_USER_CODE   0x18
#define GDT_SEL_USER_DATA   0x20
#define GDT_SEL_TSS         0x28

#define GDT_SEL_USER_CODE_R3 (GDT_SEL_USER_CODE | 0x3)
#define GDT_SEL_USER_DATA_R3 (GDT_SEL_USER_DATA | 0x3)

/* Install a flat GDT with kernel/user segments and a TSS. */
void gdt_init(void);

/* Update TSS.esp0 for privilege transitions into the current task's
 * kernel stack.
 */
void tss_set_kernel_stack(uint32_t esp0);

/* Enter ring3 by constructing an iret frame on the current kernel stack.
 * Does not return.
 */
void enter_user_mode(uint32_t user_eip, uint32_t user_esp) __attribute__((noreturn));

#endif /* GDT_H */
