# SkezOS Kernel Architecture and Coding Rules

## Purpose

This document defines the current subsystem boundaries and coding rules used for kernel changes.
It is the implementation-facing companion to `technical_considerations.md`.

## Subsystem boundaries (current ownership)

- `arch/x86` (currently spread across `kernel/`): IDT/ISR entry, PIC, IRQ stubs, paging enable, CPU control registers
- `mm`: memory map parsing, PMM frame allocator, paging structures, kernel heap (`kmalloc`)
- `sched`: kernel tasks, context switch, sleep/wake, timer-driven round-robin
- `sys`: syscall gate/dispatcher (`int 0x80`) and kernel-side ABI handling
- `dev`: serial, VGA console, keyboard, PIT timer
- `fs`: not implemented yet (reserve namespace and avoid leaking concerns into existing files)

## Boot sequencing contract

1. `serial_init()` and `vga_clear()`
2. `gdt_init()` (kernel/user segments + TSS)
3. Parse multiboot memory map (`memmap_parse`)
4. PMM self-checks (pre-paging)
5. Paging init + enable
6. Heap init (`kmalloc_init`)
7. Post-paging self-checks
8. Interrupts/IRQ/timer/keyboard setup
9. `syscall_init()` (`int 0x80` gate)
10. Scheduler init + task creation
11. Scheduler start (enters kernel tasks)

The current paging setup maps the first `32 MiB`, which intentionally keeps the
PMM bitmap (`16 MiB`) accessible after paging enable. If either value changes,
re-validate PMM accessibility before merging.

## Logging rules

- Use `KLOGD/KLOGI/KLOGW/KLOGP` from `kernel/klog.h` for kernel diagnostics.
- Logs are serial-first and level-prefixed.
- `WARN` and `PANIC` logs are mirrored to VGA for visibility during failures.
- Reserve raw serial writes (`klog_serial_raw`) for deterministic protocol markers (e.g. smoke tests).

## Assertions and panics

- Use `KASSERT(expr)` for kernel invariants.
- Assertion failures route through `panic_assert_failed()` and halt.
- Fault handlers should log structured context before calling `panic()`.

## Error code conventions

- Integer-returning kernel APIs should return `0` on success.
- Failures should return negative errno-style values (e.g. `-KERR_INVAL`) from `kernel/kerrno.h`.
- Do not mix `-1` sentinel errors and `-KERR_*` in the same API family.

## Change scope rules

- One subsystem per patch when possible.
- Do not mix refactor + behavior change + formatting churn.
- Any memory/interrupt change must include `make check` evidence.
- Scheduler/timer IRQ changes must include headless runtime evidence beyond boot (e.g. worker/idle logs).
- User/kernel boundary changes must include explicit serial evidence of Ring 3 entry and syscall activity.

## Known current limitations

- PMM bitmap placement is still a fixed physical address (`16 MiB`) rather than a proper reserved allocation.
- `kmalloc` is split and safer for large buffers, but still a no-free allocator.
- Panic reports do not yet include scheduler task context.
- User-mode support is bootstrap-only (shared address space, fixed demo pages, minimal syscall set).
