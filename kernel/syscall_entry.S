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
	pusha
	pushl %esp
	call syscall_dispatch
	addl $4, %esp
	movl %eax, 28(%esp)    /* saved eax slot inside pusha frame */
	popa
	iret
