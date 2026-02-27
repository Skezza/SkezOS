# Current Milestone

## Milestone
Name: Phase 6 - Userland workflow + shell bootstrap
Target window: 4-8 weeks
Owner: joe + codex

## Objective
Turn the stabilized Phase 5 process/runtime base into a usable command workflow:
- boot into a shell (or init-launched shell) deterministically
- stop hardcoding raw syscall ABI in each user program
- execute first user-facing tools from `/bin`

## In scope
- [x] Phase 5 complete: process-owned FDs, wait-driven lifecycle, deterministic transient allocation reclamation
- [x] Minimal userspace syscall wrapper/runtime layer
- [x] Bootstrap shell or init-to-shell execution path
- [x] First user-facing tools (`echo`, `cat`, `ps`, equivalent minimal set)
- [x] Dedicated Phase 6 smoke coverage for boot-to-shell command execution

## Out of scope
- [x] Full POSIX shell parsing / quoting / expansion
- [x] Pipes, redirection, background jobs
- [x] Writable filesystem support
- [x] Dynamic linking

## Tasks
- [x] Write Phase 6 design note (`docs/phase6_userland_workflow_design_note.md`) (`done`)
- [x] Add shared userspace syscall wrapper layer (`done`)
- [x] Define a minimal common userspace startup/runtime convention (`done`, assembly-first via shared include macros)
- [x] Decide bootstrap entry (`/bin/sh.elf` direct) and wire kernel launch path (`done`)
- [x] Implement first shell command loop (`done`: console handoff + prompt/read/dispatch loop + foreground wait path)
- [x] Add first user-facing `/bin` tools using shared wrappers (`done`: fixed-slot `/bin/cat.elf`; `echo` is currently a shell builtin because spawn still has no argv)
- [x] Add preliminary `qemu-smoke-phase6` target (boot -> shell banner/prompt) (`done`)
- [x] Extend `qemu-smoke-phase6` to assert shell-driven command execution (`done`)

## Risks
- Risk: shell work introduces broad churn across userland, syscall wrappers, and boot/demo flow.
  - Mitigation: keep the first shell narrow and stage under a dedicated smoke target.
- Risk: ad hoc per-program assembly grows into another bootstrap dead end.
  - Mitigation: require one shared wrapper/runtime layer before adding more user tools.

## Exit criteria
- [x] System reaches a repeatable boot-to-shell (or boot-to-init-to-shell) flow
- [ ] First user-facing tools run from `/bin` without bespoke demo-only kernel hooks
- [x] Process launch uses the Phase 5 spawn/wait path end-to-end
- [x] Shell/runtime limitations are explicitly documented for the next milestone handoff

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
- 2026-02-27 - Phase 6 shell bootstrap is now interactive: `/dev/console` input hands off to `user-shell`, the shell runs a prompt/read/dispatch loop, `echo` works as a builtin, and fixed-slot `/bin/cat.elf` runs through `spawn` + `waitpid`.
- 2026-02-27 - Validation for the interactive Phase 6 slice:
  - `make qemu-smoke-phase5`
  - `make qemu-smoke-userfault`
  - `make qemu-smoke-phase6`
- 2026-02-27 - Remaining Phase 6 limitation: runtime `SYS_SPAWN` is still fixed-slot and path-only, so `echo` remains builtin and external tools still require explicit slot wiring.
