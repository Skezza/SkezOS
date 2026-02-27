/* Minimal shared assembly runtime helpers for userland ELF programs. */

	.include "syscall_abi.inc"

	.macro user_entry
	.global _start
_start:
	.endm

	.macro user_syscall0 nr
	mov \nr, %eax
	int $0x80
	.endm

	.macro user_syscall1 nr, arg0
	mov \nr, %eax
	mov \arg0, %ebx
	int $0x80
	.endm

	.macro user_syscall2 nr, arg0, arg1
	mov \nr, %eax
	mov \arg0, %ebx
	mov \arg1, %ecx
	int $0x80
	.endm

	.macro user_syscall3 nr, arg0, arg1, arg2
	mov \nr, %eax
	mov \arg0, %ebx
	mov \arg1, %ecx
	mov \arg2, %edx
	int $0x80
	.endm

	.macro user_write ptr, len
	user_syscall3 $SYS_WRITE, $SYSCALL_FD_STDOUT, \ptr, \len
	.endm

	.macro user_yield
	user_syscall0 $SYS_YIELD
	.endm

	.macro user_time
	user_syscall0 $SYS_TIME
	.endm

	.macro user_exit code
	user_syscall1 $SYS_EXIT, \code
	.endm

	.macro user_hang
1:
	jmp 1b
	.endm
