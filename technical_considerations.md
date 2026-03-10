# SkezOS Technical Considerations

## Current sequencing note (2026-03-09)

The post-Phase-6 interaction milestone and the narrow timing follow-up are now delivered:

- `/dev/console` input ownership is PID-based, and the synchronous `waitpid()` path hands stdin to the waited foreground child before restoring it to `user-shell`
- spawned tools now start with a minimal `argc/argv` frame even though `SYS_SPAWN_EX` still accepts a flat cmdline request shape
- `SYS_TASK_SNAPSHOT` provides a bounded kernel task snapshot used by the shell `ps` builtin
- owning-task `/dev/console` reads now block in the kernel, so the shell and `readln` no longer tick-sleep on empty stdin from userland
- the shell and `readln` now support basic erase-in-place input editing for both `BS` and serial `DEL`
- `SYS_TIME_INFO` now exposes a stable monotonic tick snapshot plus PIT frequency, and `/bin/uptime.elf` exercises that ABI from the shell
- `/bin/sleep.elf` now exercises the existing tick scheduler from normal userland and reports requested vs observed elapsed ticks
- `make qemu-smoke-shell-core` is now frozen as the core shell regression path (`readln`, real `ps`, external `echo`/`cat`, unknown-command failure handling, and clean shell exit)
- `make qemu-smoke-reliability` now carries timing and reliability stress checks (`uptime`, `sleep`, pipe/redirection paths, and `diag`)

The display-first milestone is now effectively complete enough to stop treating it as the active planning branch:

- `Phase 9 - Framebuffer bring-up and visual shell` delivered the visual shell surface, framebuffer mapping, tighter chrome, denser glyph rendering, and the current shell-readability passes
- the shell/runtime path stayed intact while the display path moved behind the `display` abstraction

The reliability branch is now maintenance baseline, not the active planning lane:

- `Phase 10 - Reliability hooks and syscall exerciser` delivered `/bin/diag.elf`, expanded deterministic negative-path coverage, structured JSON contracts, replay hash gating, and CI artifact/report plumbing
- `qemu-smoke-reliability`, `qemu-smoke-reliability-replay`, and `qemu-smoke-reliability-fuzz-lite-matrix` remain the guardrails for runtime regressions

The active follow-on is back on GUI progression:

- `Phase 11 - GUI polish and visual shell operator HUD`
- keep shell/runtime ABI fixed while improving framebuffer readability, chrome, and operator context
- keep reliability work scoped to maintenance and explicit regression gaps, not as the default growth branch
- latest landed slice: footer HUD is now persistent and the bootstrap framebuffer font now verifies printable ASCII coverage at boot
- latest landed creative slice: footer HUD now renders a command timeline rail (running/success/failure capsules) from existing shell lifecycle output without syscall or ABI churn
- latest landed deterministic gate: framebuffer GUI state hash (`fb-shell-v4`, expected `0xD9BFAA54`) is emitted by the kernel and asserted in `qemu-smoke-shell-core`
- latest landed shell-chrome interaction slice: prompt lane state hints now reflect command lifecycle (`INPUT` -> `RUN <tag>` -> `OK/ERR <tag>`) via existing shell output parsing only
- latest landed operator HUD slice: footer legend now includes compact state meanings (`I/R/O/E`) plus last transition cause (`PROM`, `WAIT`, `FAIL`, `EXIT`, `ROLL`, `HOLD`) from parse-only shell/log transitions
- latest landed nightly triage slice: when `check-nightly` fails, CI now attempts a non-gating framebuffer screenshot capture (`build/artifacts/gui-fb-failure-*.ppm`) for GUI debugging
- latest landed Phase 12 candidate slice: user shell input now supports `Tab` command-name completion, bounded-history preview navigation (`Esc`/`Ctrl+P` older, `Ctrl+N` newer/current), and a bounded `history` builtin view, with compact ambiguity hints and no syscall ABI or parser model churn
- latest reliability maintenance follow-up: redirected pipeline smoke now uses a two-stage `cat < readme.txt | cat` probe to keep coverage while avoiding deterministic spawn-slot contention in three-stage shells
- latest Phase 13 candidate hardening slice: framebuffer GUI hash assertions are now enforced across the shell-facing smoke suite (userfault, lifecycle, reliability, replay, fuzz-lite) instead of only `qemu-smoke-shell-core`, still conditional on framebuffer mode
- latest reliability sequencing hardening: fuzz-lite runner settle time is now `2.25s` by default to avoid command-loss races during console ownership transitions in nightly matrix runs
- latest artifact-triage hardening follow-up: framebuffer dump capture now emits validated `.meta` sidecars (timestamp, geometry, size, sha256) for deterministic GUI failure triage artifacts
- latest artifact-triage usability follow-up: capture now updates stable `gui-fb-failure-latest.*` pointers and emits a machine-parseable `GUI_FB_DUMP_META` contract line

Queued after the current GUI slices:

- font-coverage and density polish
- deterministic visual regression strategy (artifact/screenshot or framebuffer hash)
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
