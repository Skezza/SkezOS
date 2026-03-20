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
	.set SYS_SPAWN_EX, 10
	.set SYS_GETCMDLINE, 11
	.set SYS_TASK_SNAPSHOT, 12
	.set SYS_SLEEP, 13
	.set SYS_TIME_INFO, 14
	.set SYS_LIST_DIR, 15
	.set SYS_CHDIR, 16
	.set SYS_GETCWD, 17
	.set SYS_PIPE, 18
	.set SYS_DUP, 19
	.set SYS_DUP2, 20
	.set SYS_UNLINK, 21
	.set SYS_FORK, 22

	.set SYSCALL_WAITPID_FLAG_NOHANG, (1 << 0)

	.set SYSCALL_FD_STDIN, 0
	.set SYSCALL_FD_STDOUT, 1
	.set SYSCALL_FD_STDERR, 2
