/*
 * Entry point for the kernel after GRUB jumps into our image.
 *
 * GRUB loads the kernel into 32‑bit protected mode with paging disabled
 * but basic segment descriptors installed.  We set up a small stack and
 * then jump into our C kernel, passing along the Multiboot magic and
 * pointer to the Multiboot information structure.  From this point on
 * everything else happens in C.
 */
	.intel_syntax noprefix
	.global _start
	.extern kmain

	.section .text
_start:
	cli
	/* Set up a trivial stack in the BSS; the linker script reserves
	 * 16KiB at the end of the loader object for us.  Without a valid
	 * stack the call into C would immediately explode.  */
	lea esp, [stack_top]
	xor ebp, ebp

	/* Multiboot2 hands us EAX=multiboot magic and EBX=pointer to the
	 * multiboot information structure.  Preserve those registers on
	 * the stack for kmain()'s arguments.  */
	push ebx
	push eax
	call kmain

1:
	hlt
	jmp 1b

	.section .bss
	.align 16
	/* Reserve 16KiB for the initial stack.  The symbol stack_top is
	 * defined at the top of this reservation so that SP can be set to it.  */
	.space 16384
stack_top:
