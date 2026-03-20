	.global gdt_flush
	.global tss_flush
	.global enter_user_mode
	.global enter_user_mode_full

/* void gdt_flush(uint32_t *gdt_ptr) */
gdt_flush:
	mov 4(%esp), %eax
	lgdt (%eax)

	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss

	ljmp $0x08, $gdt_flush_done
gdt_flush_done:
	ret

/* void tss_flush(void) */
tss_flush:
	mov $0x28, %ax
	ltr %ax
	ret

/* void enter_user_mode(uint32_t user_eip, uint32_t user_esp) */
enter_user_mode:
	mov 4(%esp), %ecx   /* user_eip */
	mov 8(%esp), %edx   /* user_esp */

	mov $0x23, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs

	pushl $0x23         /* user SS */
	push %edx           /* user ESP */
	pushf
	pop %eax
	orl $0x200, %eax    /* ensure IF set in user mode */
	push %eax
	pushl $0x1B         /* user CS */
	push %ecx           /* user EIP */
	iret

/* void enter_user_mode_full(const struct user_mode_resume_ctx *ctx) */
enter_user_mode_full:
	mov 4(%esp), %eax   /* ctx */

	mov $0x23, %dx
	mov %dx, %ds
	mov %dx, %es
	mov %dx, %fs
	mov %dx, %gs

	pushl $0x23         /* user SS */
	pushl 32(%eax)      /* user ESP */
	pushl 36(%eax)      /* EFLAGS */
	pushl $0x1B         /* user CS */
	pushl 28(%eax)      /* user EIP */

	mov 20(%eax), %edi
	mov 16(%eax), %esi
	mov 24(%eax), %ebp
	mov 4(%eax), %ebx
	mov 8(%eax), %ecx
	mov 12(%eax), %edx
	mov 0(%eax), %eax
	iret
