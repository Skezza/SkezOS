# Current Milestone

## Milestone
Name: Foreground process I/O and userland ergonomics
Target window: 1-2 weeks
Owner: joe + codex

## Objective
Close the main interaction/runtime gaps left after Phase 6 while keeping the shell intentionally narrow:
- hand `/dev/console` stdin to synchronous foreground children and restore it after `waitpid()`
- replace flat cmdline-only child startup with a minimal `argc/argv` ABI for spawned tools
- make `ps` report a real bounded task snapshot instead of a stub message

## In scope
- [x] PID-based `/dev/console` input ownership for one user task at a time
- [x] Foreground stdin handoff for the synchronous `spawn` + `waitpid` path
- [x] Minimal `argc/argv` startup stack for spawned user binaries
- [x] Shell builtin `ps` backed by a bounded kernel task snapshot
- [x] Dedicated smoke coverage for stdin handoff + real `ps`
- [x] Preserve Phase 6 regressions under `make check`

## Out of scope
- [x] Full POSIX shell parsing / quoting / expansion
- [x] Pipes, redirection, background jobs, job control
- [x] Writable filesystem support
- [x] PIE / relocatable user binaries
- [x] Dynamic linking

## Tasks
- [x] Replace shell-name-based console input ownership with PID-based ownership (`done`)
- [x] Hand stdin to a waited foreground child and restore it to the shell after `waitpid()` (`done`)
- [x] Build a minimal `argc/argv` startup frame from `SYS_SPAWN_EX` cmdline data (`done`)
- [x] Move `/bin/echo.elf` and `/bin/cat.elf` over to `argc/argv` (`done`)
- [x] Replace the stub `ps` builtin with a bounded kernel snapshot syscall (`done`)
- [x] Add one tiny stdin smoke tool and extend `qemu-smoke-phase6` (`done`: `/bin/readln.elf`)
- [x] Keep `make check` green after the transition (`done`)

## Risks
- Risk: console reads are still non-blocking, so simple foreground readers can spin hard while waiting for input.
  - Mitigation: the immediate stopgap is now one-tick sleep-based polling in the shell and `readln`; revisit true blocking reads next.
- Risk: userland ergonomics work can easily sprawl into a much larger shell feature push.
  - Mitigation: keep whitespace-only parsing and synchronous foreground execution explicitly out of scope.

## Exit criteria
- [x] One foreground child can read from `/dev/console` and return control to the shell cleanly
- [x] Console ownership returns to `user-shell` after the child exits
- [x] `echo` preserves multi-word arguments through the new `argc/argv` startup path
- [x] `ps` shows a deterministic-enough task snapshot for smoke coverage
- [x] `make qemu-smoke-phase6` and `make check` pass with the new behavior

## Notes / decisions
- 2026-02-27 - Phase 5 completed: wait-driven child synchronization, process-owned FD ownership, and deterministic task-stack/loader-scratch reclamation are in place.
- 2026-02-27 - Phase 5 validation used:
  - `make qemu-smoke-phase4-repeat PHASE4_REPEAT=2`
  - `make qemu-smoke-phase5`
  - `make qemu-smoke-userfault`
- 2026-02-27 - Active follow-up milestone is Phase 6 userland workflow + shell bootstrap.
- 2026-02-27 - Phase 6 design note added: `docs/phase6_userland_workflow_design_note.md`.
- 2026-02-27 - Landed Phase 6 bootstrap foundation: shared assembly syscall/runtime includes now back the existing `/bin/hello*.elf` demos, and `make` regenerates `kernel/initramfs_demo_blob.c` from `userland/` sources automatically.
- 2026-02-27 - Validation for the new Phase 6 foundation slice:
  - `make qemu-smoke-phase5`
  - `make qemu-smoke-userfault`
- 2026-02-27 - Chose direct `/bin/sh.elf` bootstrap. The kernel now starts a fixed-slot shell task at boot, and `make qemu-smoke-phase6` asserts the shell banner/prompt path.
- 2026-02-27 - Phase 6 shell bootstrap is now interactive: `/dev/console` input hands off to `user-shell`, the shell runs a prompt/read/dispatch loop, external `/bin/echo.elf` and `/bin/cat.elf` run through `spawn` + `waitpid`, and `SYS_SPAWN_EX` hands children a flat inherited cmdline.
- 2026-02-27 - Validation for the interactive Phase 6 slice:
  - `make qemu-smoke-phase5`
  - `make qemu-smoke-userfault`
  - `make qemu-smoke-phase6`
- 2026-02-27 - Phase 6 completion landed: child launch now inspects ET_EXEC images directly, `uaccess` is task-aware, `/bin/echo.elf` is external, and `make check` now runs the active userfault + Phase 6 smoke path.
- 2026-02-27 - Remaining Phase 6 limitation: child launch is generic for fixed-address ET_EXEC images, but argument handoff is still flat cmdline-only (no argc/argv), stdin stays with `user-shell`, and `ps` is still a stub.
- 2026-02-28 - Post-Phase-6 handover is captured in `docs/next_phase_handover.md`; the recommended next slice is foreground stdin handoff, a minimal `argc/argv` ABI, and a real `ps` path before broader device work.
- 2026-02-28 - Delivered the post-Phase-6 interaction slice: `/dev/console` input ownership is now PID-based, synchronous `waitpid()` temporarily hands stdin to the foreground child, and ownership returns to `user-shell` on completion.
- 2026-02-28 - Spawned user tools now receive a minimal `argc/argv` startup stack built by the kernel launch path; `/bin/echo.elf` and `/bin/cat.elf` now consume `argc/argv` directly instead of `SYS_GETCMDLINE`.
- 2026-02-28 - `ps` is now backed by `SYS_TASK_SNAPSHOT`, a bounded kernel task snapshot syscall used by the shell builtin.
- 2026-02-28 - Added `/bin/readln.elf` as a dedicated stdin handoff smoke tool; `make qemu-smoke-phase6` now checks `readln`, real `ps`, `echo`, `cat`, unknown-command failure, and clean shell exit.
- 2026-02-28 - Validation for the foreground I/O slice:
  - `make all`
  - `make qemu-smoke-phase6`
  - `make check`
- 2026-02-28 - Next decision point is no longer shell-process correctness; it is whether to keep polishing the shell/input path (blocking reads, history/editing, maybe `sleep`) or move to broader device/reliability work.
- 2026-02-28 - Began the first post-milestone shell-input polish step: added `SYS_SLEEP`, and the shell plus `/bin/readln.elf` now sleep for one tick on empty stdin instead of hot-spinning with pure `yield`.
