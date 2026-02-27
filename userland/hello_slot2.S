	.include "runtime.inc"

/* Linked for USER_ELF_SLOT2_IMAGE_BASE in kernel/memory_layout.h.
 * Spawn target for the bootstrap SYS_SPAWN demo.
 */
	user_entry
	user_write $msg0, $msg0_len
	user_yield
	user_write $msg1, $msg1_len
	user_exit $0
	user_hang

msg0:
	.ascii "elf-c: spawned via sys_spawn\n"
msg0_end:
	.set msg0_len, msg0_end - msg0

msg1:
	.ascii "elf-c: exit\n"
msg1_end:
	.set msg1_len, msg1_end - msg1
