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
- the shell now runs a real prompt/read/dispatch loop, supports `help`, `echo`, `wait`, `ps`, and `exit`, and launches fixed-slot external commands with `spawn` + `waitpid`
- `/bin/cat.elf` is the first dedicated shell-driven external tool and prints `/bin/readme.txt`
- `make qemu-smoke-phase6` now drives the shell through serial input and asserts real command execution instead of only the banner/prompt path

This closes the console handoff blocker and proves the first end-to-end userland workflow. The phase still intentionally keeps the runtime narrow: there is no argv support, no general dynamic loader, and external tools still depend on the fixed-slot spawn table.

## Target user-visible outcome

On every boot, the system should reliably reach a simple userspace command loop that can:

- print help
- launch binaries from `/bin`
- wait for child completion
- run basic tools like `echo` and `cat` (with `ps` still a stub until process-listing has a real kernel interface)

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
- builtins currently include `help`, `echo`, `wait`, `ps`, and `exit`
- external command execution still focuses on fixed-slot `/bin/...` paths already wired into the loader
- synchronous foreground execution only
- no argv handoff to child processes yet (external `cat` ignores extra tokens and always prints `/bin/readme.txt`)

Do not add pipes, redirection, or job control in the initial shell.

## Runtime/library baseline

Minimum shared userspace support should provide:

- stable syscall wrappers for `write`, `read`, `exit`, `spawn`, `waitpid`, `open`, `close`, `yield`, `time`
- string helpers needed by the first shell/tools
- a small `_start` convention that hands control to a C-like `main` entry later, if desired

The ABI boundary now stops being copied ad hoc into each program, but the spawn ABI is still intentionally path-only.

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

- `SYS_SPAWN` still maps a small fixed path table to fixed ET_EXEC slots; every new runnable tool still needs coordinated slot wiring in `memory_layout.h`, `usermode.c`, and `uaccess.c`
- console read ownership is currently pinned to `user-shell` itself; foreground children are spawned and waited synchronously, but they are not handed stdin yet
- the shell is intentionally foreground-only and does not implement pipes, redirection, quoting, escaping, or job control
- `echo` is still a shell builtin because there is no argv/argc handoff yet
- `ps` is intentionally a stub until a real process-introspection kernel interface exists
