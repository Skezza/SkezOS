	.global user_demo_blob_start
	.global user_demo_blob_end

/* Temporary in-memory user-space demo program.
 * Syscall ABI (duplicated from syscall_abi.h for assembly):
 *   1 = write, 2 = yield, 3 = exit, 4 = time
 */
user_demo_blob_start:
	/* Establish a base register for position-independent references. */
	call 1f
1:
	pop %esi
	sub $(1b - user_demo_blob_start), %esi

	xor %edi, %edi              /* loop counter */

2:	/* write(1, msg, msg_len) */
	mov $1, %eax                /* SYS_WRITE */
	mov $1, %ebx                /* fd=stdout */
	lea msg-user_demo_blob_start(%esi), %ecx
	mov $msg_len, %edx
	int $0x80

	/* time() and occasionally print a second marker line. */
	mov $4, %eax                /* SYS_TIME */
	int $0x80
	and $0x3, %eax
	cmp $0, %eax
	jne 3f

	mov $1, %eax                /* SYS_WRITE */
	mov $1, %ebx
	lea tickmsg-user_demo_blob_start(%esi), %ecx
	mov $tickmsg_len, %edx
	int $0x80

3:	/* yield() */
	mov $2, %eax                /* SYS_YIELD */
	int $0x80

	inc %edi
	cmp $10, %edi
	jl 2b

	/* exit(0) */
	mov $3, %eax                /* SYS_EXIT */
	xor %ebx, %ebx
	int $0x80

4:	jmp 4b

msg:
	.ascii "user: hello via int 0x80\n"
msg_end:
	.set msg_len, msg_end - msg

tickmsg:
	.ascii "user: tick sample\n"
tickmsg_end:
	.set tickmsg_len, tickmsg_end - tickmsg

user_demo_blob_end:
