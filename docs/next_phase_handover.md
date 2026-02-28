# Post-Phase-6 Handover

Date: 2026-02-28
Scope: handoff for the next agentic worker after Phase 6 completion

## Current baseline

Phase 6 is complete and the system now has a stable boot-to-shell workflow:

- `/bin/sh.elf` boots directly and owns `/dev/console` input
- external `/bin/echo.elf` and `/bin/cat.elf` run through the generic spawn/wait path
- child launch now inspects fixed-address ET_EXEC images directly instead of relying on bespoke per-path kernel slots
- user pointer validation is task-aware
- `make check` covers the active regression path (`qemu-smoke-userfault` + `qemu-smoke-phase6`)
- the console path was just hardened so VGA scrolls instead of wrapping, full console writes are serialized, and worker demo log spam is suppressed while the shell owns the console

Latest known branch at this handoff: `fix/console-output-phase6`
Latest known commit at this handoff: `9afb3f2` (`Fix console output scrolling and shell readability`)

## Known limits that still matter

These are the highest-value remaining gaps in the current shell/runtime model:

- child argument handoff is still one flat inherited cmdline string, not a real `argc/argv` ABI
- stdin stays with `user-shell`, so foreground children still cannot own console input
- `ps` is still a stub message, not a real task/process view
- shell parsing remains intentionally narrow: no quoting, pipes, redirection, or background jobs
- serial output is much cleaner now, but aggressively piped scripted input can still look awkward because the shell still echoes input while children do not consume stdin

## Recommended next milestone

Start with a narrow interaction/runtime milestone before taking on larger hardware work:

Name: Foreground process I/O and userland ergonomics

Objective:
Make shell-launched tools behave like real foreground programs, then close the most obvious shell usability gap (`ps`) on top of that.

Why this should come before ATA or larger device work:

- it removes the most visible remaining UX limitation in the current boot-to-shell path
- it finishes the shell/process model while the Phase 6 code is still fresh
- it gives future device or storage work a cleaner operator workflow for testing and debugging

## Suggested implementation order

### 1. Foreground stdin handoff

Make the shell temporarily hand `/dev/console` input ownership to the foreground child, then restore ownership after `waitpid()` completes.

Expected scope:

- keep stdout/stderr behavior unchanged
- only hand off stdin for the synchronous foreground child path
- do not add job control or background process ownership yet

Likely touch points:

- `kernel/vfs.c`
- `kernel/syscall.c`
- `kernel/sched.c`
- `userland/sh_slot4.c`

### 2. Replace flat cmdline-only handoff with a minimal `argc/argv` startup ABI

The current `SYS_GETCMDLINE` path was the right Phase 6 shortcut, but it is the next thing that will become annoying as soon as tools grow.

Recommended direction:

- keep `SYS_SPAWN_EX` for the spawn request shape
- build `argc/argv` in the kernel launch path (or a shared user startup layer)
- preserve the current narrow shell parser; tokenization can stay simple and whitespace-only for now

Likely touch points:

- `kernel/usermode.c`
- `kernel/syscall_abi.h`
- `userland/syscall_abi.inc`
- shared user startup/runtime includes under `userland/`
- `userland/sh_slot4.c`
- `/bin/echo.elf` and `/bin/cat.elf`

### 3. Make `ps` real

Keep scope narrow: a minimal task snapshot is enough. Do not build a full `/proc` model.

Recommended direction:

- add one syscall that copies out a bounded task snapshot table or one task at a time
- either keep `ps` as a shell builtin backed by that syscall, or move it to `/bin/ps.elf` if the ABI work above is already done

Success criteria:

- `ps` shows enough to confirm that shell, workers, and foreground children are alive or have exited
- the output is deterministic enough for smoke coverage

### 4. Only after that, decide between shell input polish or device work

Once foreground stdin and `ps` are real, the next worker can make a clean decision:

- continue userland polish (`history`, backspace/editing, maybe `sleep`)
- or move to the broader project-plan bucket of device/reliability work (ATA PIO, clock improvements, syscall fuzz hooks)

## Validation expectations

Existing commands that should stay green throughout:

- `make qemu-smoke-userfault`
- `make qemu-smoke-phase6`
- `make check`

Recommended new smoke coverage for the next milestone:

- one foreground child can read from stdin and returns control to the shell cleanly
- `echo` still works with multi-word arguments after the `argc/argv` transition
- `ps` returns a non-stub task snapshot
- console ownership returns to `user-shell` after the child exits

The easiest way to validate stdin handoff may be adding one tiny dedicated user tool for the smoke path (for example a line reader that echoes one line back), rather than overloading `cat`.

## Guardrails

- Keep the shell intentionally narrow. Do not bundle quoting, pipes, redirection, and job control into this slice.
- Do not mix in writable filesystem work yet.
- Do not attempt PIE/relocatable user binaries in this slice.
- Keep test evidence attached to the work (`make qemu-smoke-phase6` at minimum, ideally `make check`).

## Docs to keep aligned when this starts

- `current_milestone.md`
- `technical_considerations.md`
- `docs/phase6_userland_workflow_design_note.md`
- `README.md` if any user-visible commands or limitations change
