	.include "runtime.inc"

/* Linked for USER_ELF_SLOT1_IMAGE_BASE in kernel/memory_layout.h. */
	user_entry
	user_write $msg0, $msg0_len
	user_yield

	/* read(0, readbuf, 8) -> nonblocking /dev/console in bootstrap */
	user_syscall3 $SYS_READ, $SYSCALL_FD_STDIN, $readbuf, $8
	test %eax, %eax
	jne 1f

	user_write $msg_read0, $msg_read0_len
	jmp 2f

1:
	user_write $msg_readnz, $msg_readnz_len

2:
	/* spawn("/bin/hello3.elf", len) */
	user_syscall2 $SYS_SPAWN, $spawn_path, $spawn_path_len
	test %eax, %eax
	jle spawn1_fail
	mov %eax, %edi

	user_write $msg_spawn_ok, $msg_spawn_ok_len
	user_write $smoke_spawn1_ok, $smoke_spawn1_ok_len

	/* spawn("/bin/hello4.elf", len) */
	user_syscall2 $SYS_SPAWN, $spawn_path2, $spawn_path2_len
	test %eax, %eax
	jle spawn2_fail
	mov %eax, %ebp

	user_write $msg_spawn2_ok, $msg_spawn2_ok_len
	user_write $smoke_spawn2_ok, $smoke_spawn2_ok_len

	/* waitpid(child1, &status1, 0) */
	user_syscall3 $SYS_WAITPID, %edi, $wait_status1, $0
	cmp %edi, %eax
	jne wait1_fail

	user_write $msg_wait1_ok, $msg_wait1_ok_len
	user_write $smoke_wait1_ok, $smoke_wait1_ok_len

	/* waitpid(child2, &status2, 0) */
	user_syscall3 $SYS_WAITPID, %ebp, $wait_status2, $0
	cmp %ebp, %eax
	jne wait2_fail

	user_write $msg_wait2_ok, $msg_wait2_ok_len
	user_write $smoke_wait2_ok, $smoke_wait2_ok_len

	/* Spawn hello3 again after explicit wait/reap. */
	user_syscall2 $SYS_SPAWN, $spawn_path, $spawn_path_len
	test %eax, %eax
	jle spawn3_fail
	mov %eax, %esi

	user_write $msg_spawn3_ok, $msg_spawn3_ok_len
	user_write $smoke_spawn3_ok, $smoke_spawn3_ok_len

	/* waitpid(child3, &status3, 0) */
	user_syscall3 $SYS_WAITPID, %esi, $wait_status3, $0
	cmp %esi, %eax
	jne wait3_fail

	user_write $msg_wait3_ok, $msg_wait3_ok_len
	user_write $smoke_wait3_ok, $smoke_wait3_ok_len
	jmp done

spawn1_fail:
	user_write $msg_spawn_fail, $msg_spawn_fail_len
	jmp fail

spawn2_fail:
	user_write $msg_spawn2_fail, $msg_spawn2_fail_len
	jmp fail

wait1_fail:
	user_write $msg_wait1_fail, $msg_wait1_fail_len
	jmp fail

wait2_fail:
	user_write $msg_wait2_fail, $msg_wait2_fail_len
	jmp fail

spawn3_fail:
	user_write $msg_spawn3_fail, $msg_spawn3_fail_len
	jmp fail

wait3_fail:
	user_write $msg_wait3_fail, $msg_wait3_fail_len
	jmp fail

done:
	user_write $msg1, $msg1_len
	user_yield
	mov $0, %ebx
	jmp exit

fail:
	mov $1, %ebx

exit:
	user_exit %ebx

	user_hang

msg0:
	.ascii "elf-b: hello from /bin/hello2.elf\n"
msg0_end:
	.set msg0_len, msg0_end - msg0

msg_read0:
	.ascii "elf-b: sys_read stdin -> 0 bytes\n"
msg_read0_end:
	.set msg_read0_len, msg_read0_end - msg_read0

msg_readnz:
	.ascii "elf-b: sys_read stdin -> nonzero/error\n"
msg_readnz_end:
	.set msg_readnz_len, msg_readnz_end - msg_readnz

msg_spawn_ok:
	.ascii "elf-b: sys_spawn /bin/hello3.elf ok\n"
msg_spawn_ok_end:
	.set msg_spawn_ok_len, msg_spawn_ok_end - msg_spawn_ok

msg_spawn_fail:
	.ascii "elf-b: sys_spawn /bin/hello3.elf fail\n"
msg_spawn_fail_end:
	.set msg_spawn_fail_len, msg_spawn_fail_end - msg_spawn_fail

msg_spawn2_ok:
	.ascii "elf-b: sys_spawn /bin/hello4.elf ok\n"
msg_spawn2_ok_end:
	.set msg_spawn2_ok_len, msg_spawn2_ok_end - msg_spawn2_ok

msg_spawn2_fail:
	.ascii "elf-b: sys_spawn /bin/hello4.elf fail\n"
msg_spawn2_fail_end:
	.set msg_spawn2_fail_len, msg_spawn2_fail_end - msg_spawn2_fail

msg_spawn3_ok:
	.ascii "elf-b: sys_spawn /bin/hello3.elf reuse ok\n"
msg_spawn3_ok_end:
	.set msg_spawn3_ok_len, msg_spawn3_ok_end - msg_spawn3_ok

msg_spawn3_fail:
	.ascii "elf-b: sys_spawn /bin/hello3.elf reuse fail\n"
msg_spawn3_fail_end:
	.set msg_spawn3_fail_len, msg_spawn3_fail_end - msg_spawn3_fail

msg_wait1_ok:
	.ascii "elf-b: sys_waitpid /bin/hello3.elf ok\n"
msg_wait1_ok_end:
	.set msg_wait1_ok_len, msg_wait1_ok_end - msg_wait1_ok

msg_wait1_fail:
	.ascii "elf-b: sys_waitpid /bin/hello3.elf fail\n"
msg_wait1_fail_end:
	.set msg_wait1_fail_len, msg_wait1_fail_end - msg_wait1_fail

msg_wait2_ok:
	.ascii "elf-b: sys_waitpid /bin/hello4.elf ok\n"
msg_wait2_ok_end:
	.set msg_wait2_ok_len, msg_wait2_ok_end - msg_wait2_ok

msg_wait2_fail:
	.ascii "elf-b: sys_waitpid /bin/hello4.elf fail\n"
msg_wait2_fail_end:
	.set msg_wait2_fail_len, msg_wait2_fail_end - msg_wait2_fail

msg_wait3_ok:
	.ascii "elf-b: sys_waitpid /bin/hello3.elf reuse ok\n"
msg_wait3_ok_end:
	.set msg_wait3_ok_len, msg_wait3_ok_end - msg_wait3_ok

msg_wait3_fail:
	.ascii "elf-b: sys_waitpid /bin/hello3.elf reuse fail\n"
msg_wait3_fail_end:
	.set msg_wait3_fail_len, msg_wait3_fail_end - msg_wait3_fail

smoke_spawn1_ok:
	.ascii "SMOKE_LIFECYCLE_SPAWN_HELLO3_OK\n"
smoke_spawn1_ok_end:
	.set smoke_spawn1_ok_len, smoke_spawn1_ok_end - smoke_spawn1_ok

smoke_spawn2_ok:
	.ascii "SMOKE_LIFECYCLE_SPAWN_HELLO4_OK\n"
smoke_spawn2_ok_end:
	.set smoke_spawn2_ok_len, smoke_spawn2_ok_end - smoke_spawn2_ok

smoke_spawn3_ok:
	.ascii "SMOKE_LIFECYCLE_SPAWN_HELLO3_REUSE_OK\n"
smoke_spawn3_ok_end:
	.set smoke_spawn3_ok_len, smoke_spawn3_ok_end - smoke_spawn3_ok

smoke_wait1_ok:
	.ascii "SMOKE_LIFECYCLE_WAIT_HELLO3_OK\n"
smoke_wait1_ok_end:
	.set smoke_wait1_ok_len, smoke_wait1_ok_end - smoke_wait1_ok

smoke_wait2_ok:
	.ascii "SMOKE_LIFECYCLE_WAIT_HELLO4_OK\n"
smoke_wait2_ok_end:
	.set smoke_wait2_ok_len, smoke_wait2_ok_end - smoke_wait2_ok

smoke_wait3_ok:
	.ascii "SMOKE_LIFECYCLE_WAIT_HELLO3_REUSE_OK\n"
smoke_wait3_ok_end:
	.set smoke_wait3_ok_len, smoke_wait3_ok_end - smoke_wait3_ok

msg1:
	.ascii "elf-b: done\n"
msg1_end:
	.set msg1_len, msg1_end - msg1

spawn_path:
	.ascii "/bin/hello3.elf"
spawn_path_end:
	.set spawn_path_len, spawn_path_end - spawn_path

spawn_path2:
	.ascii "/bin/hello4.elf"
spawn_path2_end:
	.set spawn_path2_len, spawn_path2_end - spawn_path2

readbuf:
	.space 8

	.balign 4
wait_status1:
	.long 0
wait_status2:
	.long 0
wait_status3:
	.long 0
