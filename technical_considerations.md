# SkezOS Technical Considerations

## Current sequencing note (2026-02-27)

Recommended active milestone after Phase 5 lifecycle hardening:

- Phase 6: userland workflow + shell bootstrap
- focus on shared userspace syscall wrappers, boot-to-shell flow, and first user-facing `/bin` tools

Rationale: process/FD lifecycle behavior is now deterministic enough that shell and command execution can move forward on a stable base.

Phase 5 progress status:
- process-owned FD table wiring and `waitpid` synchronization are in place
- transient task-stack and loader-scratch allocations are reclaimed via large-block `kfree`

Phase 6 bootstrap status:
- shared assembly syscall/runtime includes now back the current `/bin/hello*.elf` demos
- `make` rebuilds the initramfs blob from `userland/` sources automatically and now also builds the first mixed C/assembly userland tools
- the kernel now boots a direct `/bin/sh.elf` fixed-slot shell task, hands `/dev/console` input to the shell, and `make qemu-smoke-phase6` checks real command execution
- the active remaining bottlenecks are argv/exec generalization and moving beyond the fixed-slot spawn table, not shell input handoff

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
