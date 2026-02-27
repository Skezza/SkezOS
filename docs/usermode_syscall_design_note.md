# SkezOS User Mode + Syscall Design Note (Phase 3)

Status note (2026-02-27): this document captures Phase 3 bootstrap only. Current syscall/lifecycle behavior has advanced in Phase 4/5 (`SYS_READ`, `SYS_OPEN`, `SYS_CLOSE`, `SYS_SPAWN` pid return, and `SYS_WAITPID`); see `docs/phase4_loader_vfs_design_note.md` and `docs/phase5_process_fd_lifecycle_design_note.md`.

## Scope

This note documents the current Phase 3 bring-up:

- Ring 3 entry using `iret`
- `int 0x80` syscall gate + dispatcher
- minimal syscall ABI (`write`, `yield`, `exit`, `time`)
- temporary in-memory user demo task
- user-fault recovery smoke path (faulting Ring 3 task terminates without silent lockup)
- scheduler task metadata split for kernel vs user-bootstrap tasks (diagnostic fields only)

It is intentionally not yet a full process model.

## GDT/TSS model

`kernel/gdt.c` installs a flat GDT with:

- kernel code/data segments
- user code/data segments (DPL=3)
- one TSS descriptor

The scheduler updates `TSS.esp0` on every task switch so user-mode interrupts/syscalls land on the current task's kernel stack.

## Ring 3 transition

`enter_user_mode(user_eip, user_esp)` (assembly, `kernel/gdt_flush.S`) builds an `iret` frame:

1. `SS = user data selector`
2. `ESP = user stack top`
3. `EFLAGS` with `IF=1`
4. `CS = user code selector`
5. `EIP = user entry`

Then executes `iret` to drop to CPL3.

## Syscall path (`int 0x80`)

- IDT vector `0x80` is installed with DPL=3 (`idt_set_gate_user`)
- `kernel/syscall_entry.S` uses `pushal`
- C dispatcher receives a pointer to the saved register image
- Dispatcher writes return value into saved `eax`
- `popal` + `iret` returns to user mode

## Current syscall ABI (bootstrap)

Numbers are defined in `kernel/syscall_abi.h`:

- `1` = `write(fd, buf, len)`
- `2` = `yield()`
- `3` = `exit(code)`
- `4` = `time()`
- `5` = `read(fd, buf, len)` reserved (Phase 3 returns `-KERR_NOTSUP`, implementation deferred)

Argument registers follow an x86 int-ABI style:

- `eax` syscall number
- `ebx`, `ecx`, `edx` arguments
- return value in `eax`

## User memory model (temporary)

- One demo code page at `0x01400000`
- One demo user stack page at `0x01401000`
- Both pages are reserved in PMM init and marked user-accessible in the low identity map
- Higher-half aliases remain supervisor-only because only low PDEs get the user bit

This is a bootstrap arrangement, not per-process virtual memory.

## Validation status

Verified:

- Ring 3 demo enters and runs
- user code issues `int 0x80` syscalls repeatedly
- `sys_exit` returns control to scheduler cleanly
- kernel tasks continue running after user task exit
- dedicated fault smoke (`make qemu-smoke-userfault`) logs user page fault + task termination and shows continued scheduler progress

Not yet verified:

- user `read`/`spawn` functionality

## Known limitations

- Single shared address space (no per-process page directory)
- Syscall pointer validation is currently restricted to the demo user region
- No userspace libc/wrapper layer yet
- No handle table / FD objects yet (only stdout/stderr write path)
- `SYS_READ` is ABI-reserved but intentionally unimplemented until handle-table/VFS groundwork exists
