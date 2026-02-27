	.global syscall_entry_stub
	.extern syscall_dispatch

/* int 0x80 syscall entry (Phase 3 bootstrap ABI)
 *
 * CPU state on entry (same CPL): [eip][cs][eflags]
 * We save general-purpose registers with PUSHA, pass a pointer to the
 * saved register block into C, write the return value back to saved
 * EAX, and return via IRET.
 */
syscall_entry_stub:
	cld
	push %ds
	push %es
	push %fs
	push %gs
	pusha
	mov $0x10, %eax        /* GDT_SEL_KERNEL_DATA */
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	pushl %esp
	call syscall_dispatch
	addl $4, %esp
	movl %eax, 28(%esp)    /* saved eax slot inside pusha frame */
	popa
	pop %gs
	pop %fs
	pop %es
	pop %ds
	iret
