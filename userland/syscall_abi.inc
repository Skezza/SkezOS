/* Shared userspace syscall numbers and fd constants for assembly programs. */

	.set SYS_WRITE, 1
	.set SYS_YIELD, 2
	.set SYS_EXIT, 3
	.set SYS_TIME, 4
	.set SYS_READ, 5
	.set SYS_SPAWN, 6
	.set SYS_OPEN, 7
	.set SYS_CLOSE, 8
	.set SYS_WAITPID, 9

	.set SYSCALL_FD_STDIN, 0
	.set SYSCALL_FD_STDOUT, 1
	.set SYSCALL_FD_STDERR, 2
