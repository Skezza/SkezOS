# Lifecycle Smoke Marker Contract

## Purpose

`make qemu-smoke-lifecycle` relies on stable `SMOKE_LIFECYCLE_*` markers rather than human-readable prose logs. This keeps lifecycle regression checks resilient to wording changes.

## Marker contract

- `SMOKE_LIFECYCLE_SPAWN_HELLO3_OK`
  - Meaning: primary child spawn (`/bin/hello3.elf`) succeeded in the lifecycle demo flow.
  - Produced by: `userland/hello_slot1.S`.
  - Asserted by: `Makefile` `qemu-smoke-lifecycle`.

- `SMOKE_LIFECYCLE_SPAWN_HELLO4_OK`
  - Meaning: secondary child spawn (`/bin/hello4.elf`) succeeded.
  - Produced by: `userland/hello_slot1.S`.
  - Asserted by: `Makefile` `qemu-smoke-lifecycle`.

- `SMOKE_LIFECYCLE_SPAWN_HELLO3_REUSE_OK`
  - Meaning: spawn-slot reuse path succeeded after explicit wait/reap.
  - Produced by: `userland/hello_slot1.S`.
  - Asserted by: `Makefile` `qemu-smoke-lifecycle`.

- `SMOKE_LIFECYCLE_WAIT_HELLO3_OK`
  - Meaning: wait/reap of `/bin/hello3.elf` succeeded.
  - Produced by: `userland/hello_slot1.S`.
  - Asserted by: `Makefile` `qemu-smoke-lifecycle`.

- `SMOKE_LIFECYCLE_WAIT_HELLO4_OK`
  - Meaning: wait/reap of `/bin/hello4.elf` succeeded.
  - Produced by: `userland/hello_slot1.S`.
  - Asserted by: `Makefile` `qemu-smoke-lifecycle`.

- `SMOKE_LIFECYCLE_WAIT_HELLO3_REUSE_OK`
  - Meaning: wait/reap succeeded for the respawned child.
  - Produced by: `userland/hello_slot1.S`.
  - Asserted by: `Makefile` `qemu-smoke-lifecycle`.

- `SMOKE_LIFECYCLE_FD_OPEN_OK`
  - Meaning: open on `/bin/readme.txt` succeeded in spawned FD flow.
  - Produced by: `userland/hello_slot3.S`.
  - Asserted by: `Makefile` `qemu-smoke-lifecycle`.

- `SMOKE_LIFECYCLE_FD_READ_OK`
  - Meaning: file read succeeded in spawned FD flow.
  - Produced by: `userland/hello_slot3.S`.
  - Asserted by: `Makefile` `qemu-smoke-lifecycle`.

- `SMOKE_LIFECYCLE_FD_CLOSE_OK`
  - Meaning: close succeeded in spawned FD flow.
  - Produced by: `userland/hello_slot3.S`.
  - Asserted by: `Makefile` `qemu-smoke-lifecycle`.

- `SMOKE_LIFECYCLE_WAIT_REAP`
  - Meaning: kernel waitpid completed and returned a specific waited child + exit code.
  - Produced by: `kernel/syscall.c` (`sys_waitpid`).
  - Asserted by: `Makefile` `qemu-smoke-lifecycle` (count >= 3).

- `SMOKE_LIFECYCLE_ELF_SCRATCH_RECLAIM`
  - Meaning: loader scratch allocation was reclaimed after ELF inspect/load operation.
  - Produced by: `kernel/elf32_loader.c`.
  - Asserted by: `Makefile` `qemu-smoke-lifecycle` (count >= 5).

- `SMOKE_LIFECYCLE_STACK_RECLAIM`
  - Meaning: waited child stack memory was reclaimed.
  - Produced by: `kernel/sched.c`.
  - Asserted by: `Makefile` `qemu-smoke-lifecycle`.

- `SMOKE_LIFECYCLE_STACK_DEFERRED`
  - Meaning: deferred stack reclaim path executed at shutdown.
  - Produced by: `kernel/sched.c`.
  - Asserted by: `Makefile` `qemu-smoke-lifecycle`.

## Update rules

- If lifecycle behavior changes, update marker names only when semantics change.
- If marker strings are renamed, update producers and Makefile assertions in the same change.
- Keep marker strings ASCII and unique to avoid accidental matches.
