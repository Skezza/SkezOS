# SkezOS Process Lifecycle + FD Ownership Hardening Design Note

## Scope

This milestone hardens the bootstrap process model from Phase 4 so user-space flows are deterministic and resources are reclaimable. It focuses on:

- process-owned file descriptor tables
- deterministic parent/child synchronization
- deterministic cleanup on task exit/reap

It intentionally does not add shell polish or broader POSIX behavior yet.

## Why this milestone now

Phase 4 delivered loader/VFS/syscall bring-up, but it retained shortcuts that block reliable iteration:

- FD state is owned inside scheduler internals
- child completion polling in demos is retry/yield based
- task stacks and loader scratch allocations are not reclaimed

Lifecycle hardening removes these constraints so later userland work does not build on unstable lifecycle semantics.

## Process and FD model

### Process ownership boundary

Move FD ownership out of scheduler-private state and into process/task-owned structures with explicit lifecycle:

- each process has an FD table mapping integer fd -> open `kfile` handle + fd flags
- fd `0`, `1`, `2` are initialized to console-compatible handles during process setup
- scheduler only sees runnable/blocking/zombie state, not FD internals

### FD lifecycle rules

- `open` allocates the lowest free fd and attaches a `kfile`
- `close` detaches the fd entry and drops the handle reference
- process exit closes all remaining fds
- after exit, fd table remains immutable until reap, then process metadata can be reclaimed

## Parent/child lifecycle model

### Spawn semantics

- `spawn(path, ...)` returns child pid on success, negative `-KERR_*` on failure
- parent-child linkage is recorded at spawn time
- pid allocation and reuse stay deterministic and bounded

### Wait semantics

Add a minimal blocking wait syscall (`waitpid`-style):

- `waitpid(pid, status_ptr, options)` where initial support is `pid > 0` and `pid == -1`
- parent blocks until matching child reaches zombie state
- syscall returns child pid and publishes exit status
- unsupported options return `-KERR_NOTSUP`

This replaces user-space retry/yield polling loops for child completion.

### Exit and reap semantics

- child `exit(code)` transitions task to zombie and stores exit status
- zombie is visible to parent via wait
- successful wait reaps the zombie and releases reclaimable resources

If parent is gone before child reap, preserve current bootstrap fallback behavior (no full reparenting model in this milestone).

## Resource reclamation targets

Lifecycle hardening must establish deterministic ownership and release points for:

- kernel task stacks
- loader scratch allocations used during ELF load
- process metadata tied to zombie tasks after reap

If `kfree` is still partial, track allocations explicitly and prove bounded growth with repeat smoke runs.

## Implementation staging

1. Introduce process-owned FD table API without changing syscall behavior.
2. Switch `SYS_OPEN`/`SYS_CLOSE`/`SYS_READ`/`SYS_WRITE` to that API.
3. Add minimal `SYS_WAITPID` and parent/child zombie bookkeeping.
4. Replace spawn retry/yield demo flow with wait-driven flow.
5. Add reclamation hooks for exit/reap, then validate bounded resource usage.

## Current status (2026-02-27)

Lifecycle hardening is complete.

Implemented:

- `kernel/proc_fd.[ch]` owns per-process FD-table operations
- scheduler task state uses waitable parent/child metadata and wait-child blocking state
- `SYS_OPEN`/`SYS_CLOSE`/`SYS_READ`/`SYS_WRITE` flow through process FD helpers
- `SYS_SPAWN` returns child pid on success
- `SYS_WAITPID` (minimal blocking, `pid > 0` or `pid == -1`, `options == 0`) is wired
- spawned-slot demo userland uses explicit `waitpid` synchronization
- large-block `kfree` support reclaims task stacks and ELF loader scratch allocations
- `make qemu-smoke-lifecycle` now checks wait-driven lifecycle behavior plus stack/scratch reclamation using stable `SMOKE_LIFECYCLE_*` markers

Validation used:

- `make qemu-smoke-phase4-repeat PHASE4_REPEAT=2`
- `make qemu-smoke-lifecycle`
- `make qemu-smoke-userfault`

Observed bounded-liveness signal:

- Lifecycle smoke now requires the final `SMOKE_LIFECYCLE_STACK_DEFERRED` marker, proving transient task-stack and loader-scratch allocations are reclaimed back toward steady state for always-on kernel tasks

## Validation

Smoke marker contract: `docs/lifecycle_smoke_marker_contract.md`

Add `make qemu-smoke-lifecycle` with checks for:

- parent spawns child and waits deterministically
- child exit status observed correctly by parent
- open/read/close behavior still works through the new FD layer
- repeated runs do not show unbounded task/loader allocation growth

Keep `qemu-smoke-phase4-repeat` as a regression gate while introducing Lifecycle hardening changes.

## Non-goals

- writable filesystem semantics
- per-process CR3 isolation overhaul
- full signals/process groups/tty job control
- dynamic linking or relocation support

## Exit criteria

- wait-driven child synchronization replaces retry/yield polling
- FD ownership is process-local and no longer scheduler-internal
- exited task resources are reclaimed on deterministic lifecycle boundaries
- docs clearly capture retained limitations for the next milestone handoff

## Handoff

With lifecycle and FD ownership stabilized, the next milestone should move back up the stack:

- minimal userspace syscall wrapper/runtime support
- boot-to-shell (or init-to-shell) workflow
- core user-facing command execution from `/bin`

Phase 6 update (2026-02-27):

- shared assembly syscall/runtime includes are now in place
- `make` regenerates the initramfs blob from `userland/` automatically
- the kernel boots a direct `/bin/sh.elf` bootstrap task
- remaining work is interactive console ownership and shell-driven external command execution
