# SkezOS User Mode + Syscall Bootstrap Design Note (Phase 3)

Status note (2026-02-27): this document is intentionally Phase 3 bootstrap context. The active syscall/process-lifecycle baseline now lives in `docs/phase4_loader_vfs_design_note.md` and `docs/phase5_process_fd_lifecycle_design_note.md` (`SYS_WAITPID`, process FD ownership, wait-driven spawn flow).

## Scope

This note documents the initial Phase 3 implementation that brings up:

- Ring 3 entry using an `iret` transition
- a DPL=3 `int 0x80` syscall gate
- a minimal syscall ABI (`write`, `yield`, `exit`, `time`)
- a temporary in-memory user demo program

It is intentionally a bootstrap design, not the final process model.

## Current architecture (bootstrap)

### GDT / segments / TSS

- `kernel/gdt.c` installs a flat GDT with:
  - kernel code/data segments
  - user code/data segments (DPL=3)
  - one 32-bit available TSS
- `kernel/gdt_flush.S` loads the GDT, reloads segment registers, and loads TR (`ltr`)
- `tss_set_kernel_stack()` updates `TSS.esp0`

### Scheduler interaction with TSS

- `kernel/sched.c` updates `TSS.esp0` on every context switch
- The kernel stack top for the scheduled task is used as the Ring 0 stack for privilege transitions (syscalls/faults from user mode)

This is the minimum required to avoid privilege-transition stack corruption when user code enters the kernel.

### Ring 3 entry path (`iret`)

- `enter_user_mode(user_eip, user_esp)` in `kernel/gdt_flush.S`:
  - loads user data segments (`ds/es/fs/gs = 0x23`)
  - pushes a user `SS:ESP`, `EFLAGS` (with IF set), `CS`, and `EIP`
  - executes `iret`
- `kernel/usermode.c` uses this helper from a scheduler task after preparing pages for the user demo blob

## Syscall ABI (Phase 3 bootstrap)

Stable numeric IDs live in `kernel/syscall_abi.h`:

- `SYS_WRITE = 1`
- `SYS_YIELD = 2`
- `SYS_EXIT = 3`
- `SYS_TIME = 4`

Register calling convention (i386, `int 0x80`):

- `eax` = syscall number
- `ebx`, `ecx`, `edx`, `esi`, `edi` = arguments (as needed)
- return value in `eax`
- errors are negative errno-style values (e.g. `-KERR_INVAL`, `-KERR_FAULT`)

## Syscall entry/trapframe representation

`kernel/syscall_entry.S` saves general-purpose registers with `pusha` and passes a pointer to the saved register block into `kernel/syscall.c`.

The C-visible register image is `struct syscall_saved_regs` in `kernel/syscall.h`, laid out to match `pusha`:

- `edi`, `esi`, `ebp`, `esp_pusha`, `ebx`, `edx`, `ecx`, `eax`

The dispatcher writes the syscall return value back to the saved `eax` slot before `popa` + `iret`.

## Current syscall implementation details

- `SYS_WRITE`
  - supports `stdout`/`stderr` only
  - mirrors output to serial and VGA
  - temporary pointer validation only accepts the demo user region (`USER_DEMO_REGION_*`)
- `SYS_YIELD`
  - calls `sched_yield()`
- `SYS_EXIT`
  - logs exit code and terminates the current task via `sched_exit_current()`
- `SYS_TIME`
  - returns low 32 bits of `timer_ticks`

## Temporary user demo path

- `kernel/user_demo_blob.S` is an in-memory user-mode test program
- `kernel/usermode.c` copies it into a mapped low-memory page, marks code+stack pages user-accessible, and enters Ring 3
- The demo exercises `write`, `time`, `yield`, and `exit` via `int 0x80`
- A second fault-demo task intentionally enters Ring 3 at an unmapped address to validate user page-fault handling/recovery

## Known limitations / next steps

- No per-task user trapframe/context persistence yet (bootstrap demo path only)
- No per-process address spaces or CR3 switching
- No general `copy_from_user` / `copy_to_user` helpers
- Pointer validation is demo-region-specific, not process-aware
- User page faults and other covered exceptions now terminate the current user task (bootstrap recovery path); broader per-process fault policy is still pending
- No ELF loader/VFS/userspace runtime yet

## Validation used

- `make all`
- `make qemu-smoke SMOKE_TIMEOUT_SECS=8`
- Smoke log confirms:
  - Ring 3 entry (`usermode: entering ring3 demo`)
  - repeated `user: hello via int 0x80`
  - `sys_exit: code=0`
  - intentional Ring 3 fault produces `user page fault` log + task termination
  - scheduler continues running afterward
