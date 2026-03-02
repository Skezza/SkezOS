# SkezOS Technical Considerations

## Current sequencing note (2026-02-28)

The post-Phase-6 interaction milestone and the narrow timing follow-up are now delivered:

- `/dev/console` input ownership is PID-based, and the synchronous `waitpid()` path hands stdin to the waited foreground child before restoring it to `user-shell`
- spawned tools now start with a minimal `argc/argv` frame even though `SYS_SPAWN_EX` still accepts a flat cmdline request shape
- `SYS_TASK_SNAPSHOT` provides a bounded kernel task snapshot used by the shell `ps` builtin
- owning-task `/dev/console` reads now block in the kernel, so the shell and `readln` no longer tick-sleep on empty stdin from userland
- the shell and `readln` now support basic erase-in-place input editing for both `BS` and serial `DEL`
- `SYS_TIME_INFO` now exposes a stable monotonic tick snapshot plus PIT frequency, and `/bin/uptime.elf` exercises that ABI from the shell
- `/bin/sleep.elf` now exercises the existing tick scheduler from normal userland and reports requested vs observed elapsed ticks
- `make qemu-smoke-phase6` now validates `readln`, real `ps`, `uptime`, `sleep`, external `echo`/`cat`, unknown-command failure handling, and clean shell exit

The active follow-on is now a display-first creative milestone:

- `Phase 9 - Framebuffer bring-up and visual shell`
- the first landed slice is intentionally small: VGA text mode now reserves fixed chrome rows at the top of the screen and keeps shell/log output in a content region underneath
- the first real groundwork under that milestone is also in place: the kernel now captures the Multiboot framebuffer handoff descriptor when the bootloader provides it
- kernel boot output, `/dev/console`, and WARN/PANIC VGA mirroring now flow through a thin `display` abstraction, so the framebuffer backend swap is localized
- the kernel now also has a dynamic higher-half mapping path plus a reserved framebuffer virtual window, so pixel framebuffer memory can be mapped safely once it is handed over
- the direct boot-time switch is now active again: the kernel requests an optional `1024x768x32` framebuffer, maps direct-RGB pixel framebuffers, and renders a minimal framebuffer text shell there while keeping VGA as fallback
- the framebuffer shell now has a larger framed content region, more deliberate chrome, distinct lowercase glyphs, lightweight line color-coding for prompts plus common `user:` / `elf-` output, a scrolling left gutter rail that reinforces those line types, and a fixed prompt lane under the scrolling transcript that now behaves like a labeled input strip with a reserved entry runway, a compact live mode-plus-seconds badge (`R`/`E`/`C` + elapsed monotonic seconds), and trailing-window prompt clipping
- the current framebuffer font is also denser now: the existing bootstrap glyph table is rasterized onto a 5x7 cell grid instead of the earlier coarse 3x5 presentation, which improves readability without yet adding a full replacement font asset

Guardrails for this visual track:

- keep it keyboard-first and full-screen; do not jump to windows, mouse input, or a compositor yet
- keep the existing shell/runtime flow intact; this is a presentation and display-path upgrade, not a shell rewrite
- preserve the serial-first debug path and the current smoke coverage while display work lands

Queued after the display-first slice:

- broader glyph coverage or a true replacement framebuffer font
- syscall fuzz hooks or other reliability instrumentation
- ATA PIO / block-device bring-up

Historical planning context for the just-completed post-Phase-6 slice lives in `docs/next_phase_handover.md`.

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
