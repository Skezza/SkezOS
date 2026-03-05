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
- `make qemu-smoke-shell-core` now drives the shell through serial input, asserts external `echo`/`cat`, and checks unknown-command failure handling
- VGA text output now scrolls instead of wrapping over the boot banner, and background worker demo logs stay quiet after the shell takes console ownership

This closes the console handoff blocker and proves the first end-to-end userland workflow. The phase still intentionally keeps the runtime narrow: there is no argv support, no relocatable/dynamic loader, and stdin remains pinned to the shell rather than the foreground child.

## Follow-on status (2026-02-28)

The immediate post-Phase-6 ergonomics slice has now landed on top of this baseline:

- `/dev/console` input ownership is now PID-based rather than hard-coded to the task name `user-shell`
- the synchronous `waitpid()` path temporarily hands stdin to the waited foreground child, then restores ownership to the shell when the child exits
- `SYS_SPAWN_EX` still takes a narrow flat-cmdline request, but the kernel launch path now builds a minimal `argc/argv` startup frame for spawned children
- `/bin/echo.elf` and `/bin/cat.elf` now consume `argc/argv` directly instead of depending on `SYS_GETCMDLINE`
- `ps` is no longer a stub: `SYS_TASK_SNAPSHOT` returns a bounded task snapshot used by the shell builtin
- owning-task `/dev/console` reads now block in-kernel, so the shell plus `/bin/readln.elf` no longer tick-sleep on empty stdin from userland
- the shell plus `/bin/readln.elf` now support basic erase-in-place editing for `BS` and serial `DEL`
- `SYS_TIME_INFO` now returns a stable monotonic tick snapshot plus PIT frequency, and a tiny `/bin/uptime.elf` tool exposes that path from the shell
- a tiny `/bin/sleep.elf` tool now exercises `SYS_SLEEP` from normal userland and reports requested vs observed elapsed ticks
- `make qemu-smoke-shell-core` now also validates `/bin/readln.elf`, real `ps`, `uptime`, `sleep`, `echo`, `cat`, and unknown-command failure handling

This keeps the shell deliberately narrow while removing the most visible interaction/runtime limitations that remained immediately after Phase 6 proper.

## Current follow-on (2026-02-28)

This shell/runtime baseline is now feeding the next creative milestone:

- `Phase 9 - Framebuffer bring-up and visual shell`
- the first landed slice is intentionally conservative: the existing shell still runs on the same code path, but VGA text mode now reserves fixed chrome rows at the top of the screen so the system has an immediate visual frame
- the groundwork under that milestone now also captures the Multiboot framebuffer handoff descriptor, routes console output through a display abstraction, and safely maps a framebuffer window when a direct-RGB surface is provided
- the current landing now includes a minimal framebuffer-backed text shell with VGA fallback; the next planned slices are font coverage and layout polish

The important constraint has not changed: build a more intentional presentation layer without blowing up the shell ABI or bundling in a window manager before the display substrate exists.

## Target user-visible outcome

On every boot, the system should reliably reach a simple userspace command loop that can:

- print help
- launch binaries from `/bin`
- wait for child completion
- run basic tools like `echo` and `cat`
- inspect basic monotonic uptime through `uptime`
- use a minimal tick-based `sleep` command for testing simple timing behavior
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

- stable syscall wrappers for `write`, `read`, `exit`, `spawn`, `spawn_ex`, `getcmdline`, `waitpid`, `open`, `close`, `yield`, `time`, `time_info`, `sleep`, `task_snapshot`
- string helpers needed by the first shell/tools
- a small `_start` convention that can support a C-like `main(argc, argv)` entry when needed

The ABI boundary now stops being copied ad hoc into each program. The legacy `spawn` ABI remains path-only for compatibility, while `spawn_ex` adds the narrow cmdline handoff used by the shell.

## Validation

Add a dedicated smoke target for the userland workflow, for example:

- `make qemu-smoke-shell-core`

Minimum assertions:

- boot reaches shell/init prompt deterministically
- shell can launch at least one external `/bin` command and wait for it
- builtins and external commands both produce expected console output
- lifecycle regressions from Phase 5 remain covered by the existing Phase 4/5 smoke targets

Current validation evidence for the interactive slice:

- `make qemu-smoke-phase5`
- `make qemu-smoke-userfault`
- `make qemu-smoke-shell-core`
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
- the timer path exposed to userspace is still a PIT-backed monotonic tick counter; there is still no RTC or wall-clock/date model
- the task snapshot exposed to `ps` is intentionally bounded and minimal; there is no `/proc` model or richer introspection interface yet
- boot shell startup remains fixed-slot and user binaries are still fixed-address ET_EXEC images; this slice does not attempt PIE or relocatable loading
