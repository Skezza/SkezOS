# SkezOS Phase 6 Userland Workflow + Shell Bootstrap Design Note

## Scope

This phase turns the now-stable process/FD baseline into a usable boot-to-command workflow. It focuses on:

- a minimal userspace syscall wrapper/runtime layer
- a first interactive shell (or init-launched command loop)
- a small set of core user-facing tools loaded from `/bin`

The goal is usability, not POSIX completeness.

## Why this phase now

Phase 5 removed the main lifecycle blockers:

- child processes can be spawned and waited deterministically
- FD ownership is process-local
- transient task stacks and loader scratch allocations are reclaimed

That means shell and command-execution work can now build on stable process behavior instead of bootstrap shortcuts.

## Current status (2026-02-27)

The interactive Phase 6 bootstrap slice is now in place:

- shared userspace syscall/runtime helpers still live in `userland/syscall_abi.inc` and `userland/runtime.inc`, and the first C-side wrapper layer now lives in `userland/userlib.h`
- the existing `/bin/hello*.elf` demo programs still use the shared assembly layer
- `make` now builds mixed assembly/C userland programs, stages `/bin`, rebuilds the tar initramfs, and regenerates `kernel/initramfs_demo_blob.c`
- the kernel directly boots a fixed-slot `/bin/sh.elf` task and hands `/dev/console` input ownership to `user-shell`
- the shell now runs a real prompt/read/dispatch loop, keeps `help`, `wait`, `ps`, and `exit` as builtins, and launches external commands with `spawn` + `waitpid`
- `SYS_SPAWN_EX` and `SYS_GETCMDLINE` now provide a narrow flat-cmdline handoff, so `/bin/echo.elf` and `/bin/cat.elf` run as real external tools
- child launch now inspects fixed-address ET_EXEC images directly instead of relying on a per-path kernel whitelist, while boot shell startup remains fixed-slot
- `make qemu-smoke-phase6` now drives the shell through serial input, asserts external `echo`/`cat`, and checks unknown-command failure handling
- VGA text output now scrolls instead of wrapping over the boot banner, and background worker demo logs stay quiet after the shell takes console ownership

This closes the console handoff blocker and proves the first end-to-end userland workflow. The phase still intentionally keeps the runtime narrow: there is no argv support, no relocatable/dynamic loader, and stdin remains pinned to the shell rather than the foreground child.

## Follow-on status (2026-02-28)

The immediate post-Phase-6 ergonomics slice has now landed on top of this baseline:

- `/dev/console` input ownership is now PID-based rather than hard-coded to the task name `user-shell`
- the synchronous `waitpid()` path temporarily hands stdin to the waited foreground child, then restores ownership to the shell when the child exits
- `SYS_SPAWN_EX` still takes a narrow flat-cmdline request, but the kernel launch path now builds a minimal `argc/argv` startup frame for spawned children
- `/bin/echo.elf` and `/bin/cat.elf` now consume `argc/argv` directly instead of depending on `SYS_GETCMDLINE`
- `ps` is no longer a stub: `SYS_TASK_SNAPSHOT` returns a bounded task snapshot used by the shell builtin
- a small `SYS_SLEEP` syscall is now available, and the shell plus `/bin/readln.elf` use it to avoid hot-spinning when stdin is temporarily empty
- `make qemu-smoke-phase6` now also validates a tiny `/bin/readln.elf` stdin handoff path alongside `ps`, `echo`, `cat`, and unknown-command failure handling

This keeps the shell deliberately narrow while removing the most visible interaction/runtime limitations that remained immediately after Phase 6 proper.

## Target user-visible outcome

On every boot, the system should reliably reach a simple userspace command loop that can:

- print help
- launch binaries from `/bin`
- wait for child completion
- run basic tools like `echo` and `cat`
- hand stdin to a synchronous foreground child when needed
- inspect a bounded task snapshot through `ps`

## Implementation order

1. Add a tiny userspace syscall wrapper layer (assembly stubs or inline wrappers) so user programs stop hardcoding raw syscall numbers.
2. Introduce a minimal userspace runtime/startup convention shared by all `/bin` programs.
3. Choose one bootstrap entry:
   - spawn `/bin/sh.elf` directly from the kernel, or
   - spawn `/bin/init.elf`, which then launches the shell
4. Implement a tiny shell focused on deterministic command execution, not rich parsing.
5. Add first user-facing tools and replace the scripted shell path with the same real dispatch path used interactively.

## Shell constraints (initial)

Keep the first shell intentionally narrow:

- single-line command input
- whitespace tokenization only
- builtins currently include `help`, `wait`, `ps`, and `exit`
- external command execution resolves `/bin/<name>.elf` and still uses one flat cmdline string as the spawn request shape
- synchronous foreground execution only
- foreground stdin handoff only; no background ownership or job control

Do not add pipes, redirection, or job control in the initial shell.

## Runtime/library baseline

Minimum shared userspace support should provide:

- stable syscall wrappers for `write`, `read`, `exit`, `spawn`, `spawn_ex`, `getcmdline`, `waitpid`, `open`, `close`, `yield`, `time`, `task_snapshot`
- string helpers needed by the first shell/tools
- a small `_start` convention that can support a C-like `main(argc, argv)` entry when needed

The ABI boundary now stops being copied ad hoc into each program. The legacy `spawn` ABI remains path-only for compatibility, while `spawn_ex` adds the narrow cmdline handoff used by the shell.

## Validation

Add a dedicated smoke target for the userland workflow, for example:

- `make qemu-smoke-phase6`

Minimum assertions:

- boot reaches shell/init prompt deterministically
- shell can launch at least one external `/bin` command and wait for it
- builtins and external commands both produce expected console output
- lifecycle regressions from Phase 5 remain covered by the existing Phase 4/5 smoke targets

Current validation evidence for the interactive slice:

- `make qemu-smoke-phase5`
- `make qemu-smoke-userfault`
- `make qemu-smoke-phase6`
- `make check`

## Non-goals

- full POSIX shell parsing
- pipelines, redirection, background jobs
- writable filesystem semantics
- dynamic linking

## Exit criteria

- system reaches a repeatable boot-to-shell (or boot-to-init-to-shell) flow
- command execution uses the stabilized spawn/wait process model
- first user-facing tools run from `/bin` without bespoke demo-only kernel hooks
- retained shell/runtime limitations are documented for the next milestone

## Remaining limitations

- `SYS_SPAWN_EX` now inspects fixed-address ET_EXEC images directly and uses a reusable child stack pool, so new `/bin` tools no longer need kernel path-whitelist wiring
- the shell is intentionally foreground-only and does not implement pipes, redirection, quoting, escaping, or job control
- shell parsing is still whitespace-only; there is still no quoting or escape handling
- console reads remain non-blocking, so simple foreground readers still poll once per tick via `SYS_SLEEP` rather than blocking in the kernel
- the task snapshot exposed to `ps` is intentionally bounded and minimal; there is no `/proc` model or richer introspection interface yet
- boot shell startup remains fixed-slot and user binaries are still fixed-address ET_EXEC images; this slice does not attempt PIE or relocatable loading
