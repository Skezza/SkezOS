	.global sched_context_switch
/* void sched_context_switch(uint32_t *old_esp, uint32_t new_esp)
 *
 * Save callee-saved registers on the current stack, store ESP into
 * *old_esp, load ESP=new_esp, then restore callee-saved registers and
 * return into the resumed context.
 */
sched_context_switch:
	push %ebp
	push %ebx
	push %esi
	push %edi

	mov 20(%esp), %eax   /* old_esp pointer */
	mov 24(%esp), %edx   /* new_esp value */
	mov %esp, (%eax)
	mov %edx, %esp

	pop %edi
	pop %esi
	pop %ebx
	pop %ebp
	ret
