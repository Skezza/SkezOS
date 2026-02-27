	.include "runtime.inc"

/* Linked for USER_ELF_SLOT3_IMAGE_BASE in kernel/memory_layout.h.
 * Second spawn target for bootstrap SYS_SPAWN slot-table testing.
 */
	user_entry
	user_write $msg0, $msg0_len
	user_yield

	/* fd = open("/bin/readme.txt", 0) */
	user_syscall3 $SYS_OPEN, $path_readme, $path_readme_len, $0
	test %eax, %eax
	js open_fail
	mov %eax, %edi

	user_write $msg_open_ok, $msg_open_ok_len

	/* n = read(fd, readbuf, sizeof(readbuf)) */
	user_syscall3 $SYS_READ, %edi, $readbuf, $readbuf_len
	test %eax, %eax
	js read_fail
	mov %eax, %esi
	jz read_done

	user_write $msg_read_prefix, $msg_read_prefix_len

	user_write $readbuf, %esi

read_done:
	/* close(fd) */
	user_syscall1 $SYS_CLOSE, %edi
	test %eax, %eax
	js close_fail

	user_write $msg_close_ok, $msg_close_ok_len
	user_write $msg1, $msg1_len
	user_exit $0

open_fail:
	user_write $msg_open_fail, $msg_open_fail_len
	jmp exit_fail

read_fail:
	user_write $msg_read_fail, $msg_read_fail_len
	jmp exit_fail

close_fail:
	user_write $msg_close_fail, $msg_close_fail_len

exit_fail:
	user_exit $1

	user_hang

msg0:
	.ascii "elf-d: spawned via sys_spawn\n"
msg0_end:
	.set msg0_len, msg0_end - msg0

msg_open_ok:
	.ascii "elf-d: open /bin/readme.txt ok\n"
msg_open_ok_end:
	.set msg_open_ok_len, msg_open_ok_end - msg_open_ok

msg_open_fail:
	.ascii "elf-d: open fail\n"
msg_open_fail_end:
	.set msg_open_fail_len, msg_open_fail_end - msg_open_fail

msg_read_prefix:
	.ascii "elf-d: readme: "
msg_read_prefix_end:
	.set msg_read_prefix_len, msg_read_prefix_end - msg_read_prefix

msg_read_fail:
	.ascii "elf-d: read fail\n"
msg_read_fail_end:
	.set msg_read_fail_len, msg_read_fail_end - msg_read_fail

msg_close_ok:
	.ascii "elf-d: close fd ok\n"
msg_close_ok_end:
	.set msg_close_ok_len, msg_close_ok_end - msg_close_ok

msg_close_fail:
	.ascii "elf-d: close fail\n"
msg_close_fail_end:
	.set msg_close_fail_len, msg_close_fail_end - msg_close_fail

msg1:
	.ascii "elf-d: exit\n"
msg1_end:
	.set msg1_len, msg1_end - msg1

path_readme:
	.ascii "/bin/readme.txt"
path_readme_end:
	.set path_readme_len, path_readme_end - path_readme

	.set readbuf_len, 32
readbuf:
	.space readbuf_len
