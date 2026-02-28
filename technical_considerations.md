# SkezOS Technical Considerations

## Current sequencing note (2026-02-28)

The post-Phase-6 interaction milestone is now delivered:

- `/dev/console` input ownership is PID-based, and the synchronous `waitpid()` path hands stdin to the waited foreground child before restoring it to `user-shell`
- spawned tools now start with a minimal `argc/argv` frame even though `SYS_SPAWN_EX` still accepts a flat cmdline request shape
- `SYS_TASK_SNAPSHOT` provides a bounded kernel task snapshot used by the shell `ps` builtin
- `SYS_SLEEP` is now available as a small userland-facing stopgap, and the shell plus `readln` use it to avoid hot-spinning on empty stdin
- `make qemu-smoke-phase6` now validates `readln`, real `ps`, external `echo`/`cat`, unknown-command failure handling, and clean shell exit

Recommended next decision point:

- continue userland ergonomics if operator workflow still matters most
- otherwise move to broader device/reliability work with the shell/process model now in a usable state

If continuing userland polish, the narrow follow-up targets are:

- make console reads block cleanly so foreground readers stop tick-sleep polling altogether
- add small shell input quality-of-life improvements such as history or line editing
- add one or two simple utility commands only if they directly help testing/debugging

If switching to lower-level work, the cleanly unblocked candidates are:

- ATA PIO / block-device bring-up
- clock/timer improvements
- syscall fuzz hooks or other reliability instrumentation

Historical planning context for the just-completed slice lives in `docs/next_phase_handover.md`.

## 1) Architecture boundaries

Define strict subsystem boundaries now:
- `arch/x86` (interrupt entry, GDT/IDT/TSS, paging primitives)
- `mm` (PMM, VMM, heap allocators)
- `sched` (tasks, context switch, timer integration)
- `sys` (syscall dispatcher + ABI)
- `fs` (VFS + filesystem drivers)
- `dev` (keyboard, timer, serial, block)

Even if directories are not reorganized immediately, document ownership and interfaces first.

## 2) Memory model choices

Recommended for hobby scope:
- Keep non-PAE 32-bit initially
- Use higher-half kernel mapping and fixed kernel virtual layout
- Reserve clear regions: kernel text/data, heap, per-task stacks, user space
- Avoid over-optimizing allocator too early

Key invariants:
- Kernel never trusts user pointers without validation
- Every mapping operation has explicit ownership/lifetime
- Page faults should classify: not-present/protection, user/kernel, read/write/exec intent

## 3) Scheduler design

Start simple:
- preemptive round-robin with fixed timeslice
- run queue as intrusive linked list or array ring
- one global scheduler lock (fine for single-core)

Do not add priorities or load balancing until baseline is stable.

## 4) Process + syscall ABI

Minimum process representation:
- PID, state, address space pointer, kernel stack, trapframe, FD table

Syscall recommendations:
- Stable numeric IDs in one header
- Userspace wrapper library to isolate ABI changes
- Return negative errno-style values consistently

Security fundamentals:
- Distinguish user vs kernel memory access paths
- Validate buffer length and pointer range in every syscall

## 5) Filesystem strategy

Recommended order:
1. initramfs (read-only) for bootstrapping
2. VFS layer (inode-like abstraction, file ops table)
3. optional writable FS later

A read-only initramfs first avoids block-device complexity while enabling real user-space workflows.

## 6) Driver model

For hobby scope, use simple registration model:
- boot-time device init order
- singleton drivers where acceptable
- explicit `init`, `read`, `write`, `ioctl`-style interfaces

Prioritize reliability drivers first: timer, keyboard, serial, console, block read.

## 7) Observability and debugging

Must-have diagnostics:
- serial-first logging (always)
- panic report includes registers, EIP, CR2, error code, current task
- optional ring buffer export command in shell
- QEMU debug options documented (`-d int,cpu_reset` etc.)

If debugging is weak, velocity collapses.

## 8) Build and reproducibility

Recommended improvements:
- Pin toolchain versions in docs
- Add `make toolchain-check`
- Add `make qemu-smoke` non-interactive boot test target
- Store generated artifacts under predictable paths

## 9) API and coding consistency

Rules to enforce:
- No hidden global mutable state without subsystem owner
- Header files expose minimal surface
- Error codes and naming conventions standardized
- Keep interrupt-context-safe code paths clearly marked

## 10) AI-assisted development guardrails

When using agents:
- Require a short design note before non-trivial subsystem changes
- Require test evidence for each PR (boot log or smoke run)
- Limit PR scope to one subsystem whenever possible
- Reject patches that mix refactor + feature + formatting churn

## 11) Performance posture

Don’t optimize early, but track these metrics:
- boot time to shell prompt
- context switch latency (rough)
- syscall throughput (microbenchmark)
- allocation success/failure counts

Use metrics to detect regressions, not to chase premature speed.

## 12) Compatibility roadmap

Stage compatibility by layers:
- QEMU-first (reference platform)
- Bochs/VirtualBox sanity checks
- Real hardware only after serial/debug confidence is high

Avoid hardware bring-up until kernel behavior is deterministic in emulators.
