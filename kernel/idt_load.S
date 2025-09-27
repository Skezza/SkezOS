.global idt_load
/* Load the IDT.  The address of the IDT descriptor is passed in
 * EAX.  This function is called from C with a single argument and
 * therefore expects the pointer at 4(%esp). */
idt_load:
	mov 4(%esp), %eax
	lidt (%eax)
	ret
