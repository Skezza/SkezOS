# SkezOS Phase 4 Loader + VFS Baseline Design Note

## Scope

This note defines the Phase 4 bootstrap architecture for:

- a read-only filesystem backend
- a minimal VFS/file-object layer
- ELF32 static executable loading
- syscall integration points for `read` and later `spawn`

The goal is to unblock repeatable `/bin/...` user program loading without overbuilding process or filesystem semantics.

## Decisions (Phase 4 bootstrap)

### 1) Backend choice: read-only tarfs-style archive in memory

Chosen first backend: a read-only in-memory archive parser (`tarfs`-style layout).

Rationale:

- simpler than writable FS bring-up
- deterministic in QEMU smoke tests
- easy to embed as a kernel-linked blob first, then later move to a Multiboot module/initramfs without changing VFS call sites

Phase 4 implementation may name the backend `initramfs`, but the on-disk/on-blob format should stay simple and streaming-friendly.

## Layering and boundaries

Recommended source split (can all live under `kernel/` initially):

- `vfs.[ch]`
  - path lookup entry points
  - vnode/file object abstractions
  - device-node registration hooks
- `kfile.[ch]` (or fold into `vfs.[ch]` if keeping it tiny)
  - open file object + file ops table
- `tarfs.[ch]` (or `initramfs.[ch]`)
  - read-only archive parsing and vnode population
- `elf32_loader.[ch]`
  - ELF header/program-header validation and segment mapping/copy
- `devfs.[ch]` or simple registration inside `vfs.c`
  - `/dev/console`, `/dev/null` bootstrap nodes

Keep syscall code (`kernel/syscall.c`) as a consumer of file/process services, not the owner of VFS state.

## Minimal object model

### Vnode (filesystem object)

Represents a path-resolved object:

- type: regular file / directory / char device
- backend-private pointer
- ops table for object-level operations (lookup/readdir/open)

### File object (open handle)

Represents one open instance:

- pointer to vnode
- current offset
- open flags (read-only for now)
- file ops table (`read`, `write`, `close`)
- bootstrap implementation may cache only stdio handles per task first (before a full general FD table)

This separation avoids baking per-open offsets into vnodes and matches the later syscall FD-table model.

## Path model (Phase 4 subset)

Supported initially:

- absolute paths only (`/bin/foo`, `/dev/console`)
- exact component matching
- root directory `/`
- no symlinks, no `.`/`..` normalization requirements beyond basic rejection/simplification

Return `-KERR_NOTSUP` / `-KERR_INVAL` for unsupported path features instead of silently guessing.

## Device nodes

Bootstrap device nodes should be VFS-visible even before a full devfs:

- `/dev/console`
  - `write`: serial + VGA console path (reuse existing console output behavior)
  - `read`: optional deferred (can return `-KERR_NOTSUP` initially if Phase 4 scope needs to stay tight)
- `/dev/null`
  - `read`: returns `0`
  - `write`: consumes bytes and returns `len`

If `read` for `/dev/console` is deferred, document that clearly in the milestone notes and keep `SYS_READ` returning `-KERR_NOTSUP` for unsupported handles.

## ELF32 loader subset (initial)

Accept only:

- ELF32
- little-endian
- `ET_EXEC` (static executable)
- `EM_386`
- `PT_LOAD` segments only (ignore unsupported segment types unless critical)

Initial loader behavior:

- validate ELF + program headers
- copy loadable segments into a predefined user memory region
- zero BSS (`memsz > filesz`)
- choose entrypoint from ELF header
- provide initial user stack at fixed bootstrap location
- spawn task + enter user mode using existing scheduler/GDT/TSS path

Defer:

- `ET_DYN`, relocations, shared libs
- argument/env stack layout richness
- per-process address spaces

## Syscall integration staging

Phase 4 order:

1. VFS + file objects + tarfs backend
2. `/dev/null` + `/dev/console` nodes
3. `SYS_READ` implementation on top of file objects (and/or device nodes)
4. ELF loader
5. `spawn` syscall hook (or kernel demo path calling loader first)

Keep syscall numbers centralized in `kernel/syscall_abi.h`.

Bootstrap status note (current implementation):

- `SYS_READ`, `SYS_WRITE`, bootstrap `SYS_SPAWN`, and bootstrap `SYS_OPEN`/`SYS_CLOSE` are wired
- stdio currently uses a scheduler-owned per-task cache of open `kfile` handles (lazy-open `/dev/console`)
- non-stdio `SYS_OPEN` installs VFS-backed `kfile` handles into the same per-task bootstrap FD table (read-only focus)
- general FD operations (`open/close/dup`, arbitrary file descriptors, inheritance) remain deferred

## Error/ownership rules

- VFS/path lookup returns negative `-KERR_*` on failure
- File objects have explicit open/close ownership
- Loader must log why ELF validation failed (magic/class/machine/segment bounds)
- Never trust file-provided sizes without bounds checks against archive blob length

## Validation plan

Minimum Phase 4 smoke milestones:

- load a file from `/bin` and print bytes to console/serial
- load one static ELF and reach user-mode entry
- run two user binaries sequentially from archive-backed paths
- verify `/dev/null` write and `/dev/console` write semantics

Bootstrap automation note (current implementation):

- `make qemu-smoke-phase4` checks tarfs self-check logs, `/bin` ELF execution, `SYS_READ` smoke output, and bootstrap `SYS_SPAWN` child execution logs
- current `qemu-smoke-phase4` also checks bootstrap `SYS_OPEN`/`SYS_CLOSE` via a spawned child reading `/bin/readme.txt`
- current `qemu-smoke-phase4` also proves bootstrap spawn-slot reuse by requiring a respawned `/bin/hello3.elf` child (`elf-c`) and repeated slot-release logs
- `make qemu-smoke-phase4-repeat PHASE4_REPEAT=N` reruns the Phase 4 smoke checks to detect regressions in deterministic bootstrap behavior

## Non-goals (still deferred)

- writable filesystem
- process isolation via per-process CR3
- full FD semantics / dup / pipes
- shell integration polish (Phase 6+)

## Carried-Forward Bootstrap Limitations (At Phase 4 Handoff)

These were retained at the end of Phase 4 and were expected to change in lifecycle hardening:

- user binaries are `ET_EXEC` and linked for fixed addresses (no relocation or dynamic placement)
- runtime `SYS_SPAWN` uses a small fixed path-to-slot table for demo binaries (not general process loading)
- runtime spawn-slot reuse is demonstrated via retry/yield in the demo program (no `wait`/`waitpid` syscall yet)
- `uaccess` validation is region-based against known bootstrap user ranges (not page-table/process-aware)
- per-task FD caching is stdio-focused and scheduler-owned (not a general `open/close/dup` FD table yet)
- bootstrap `SYS_OPEN`/`SYS_CLOSE` exist, but path/flag semantics remain intentionally narrow and read-only oriented
- scheduler reuses zombie task slots, but kernel stacks and loader scratch allocations are not reclaimed (`kmalloc` has no `kfree`)

## Phase 4 Handoff (2026-02-27)

Phase 4 is considered complete for loader/VFS/bootstrap syscall goals. The immediate follow-up milestone is lifecycle hardening:

- process-owned FD tables
- wait-driven parent/child synchronization
- deterministic reclaim on exit/reap

See `docs/process_fd_lifecycle_design_note.md` for the active design baseline.

Lifecycle hardening completion note: wait-driven spawn synchronization, process-owned FD-table wiring, and transient task-stack/loader-scratch reclamation are now implemented. Active follow-up work is Phase 6 userland workflow + shell bootstrap.
