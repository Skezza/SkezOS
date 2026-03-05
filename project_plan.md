# SkezOS Project Plan (Hobby-Realistic, AI-Assisted)

## Current implementation note (2026-03-03)
The original phase labels below are now historical scaffolding. The codebase has already delivered the shell/process milestones through the post-Phase-6 interaction slice, completed the narrow shell input follow-up, landed the first clock/timer slice (`SYS_TIME_INFO` plus `/bin/uptime.elf` and `/bin/sleep.elf`), and effectively completed the display-first milestone (`Phase 9 - Framebuffer bring-up and visual shell`). The active follow-on is now a narrow reliability milestone: `Phase 10 - Reliability hooks and syscall exerciser`. The first concrete slice is deliberately small: a normal shell-launchable `/bin/diag.elf` tool now exercises a few invalid syscall paths and reports pass/fail, so the project has a lightweight operator-facing reliability probe before broader kernel hooks or block-device work land.

## 1) Current baseline
SkezOS currently boots via GRUB/Multiboot2 into a 32-bit kernel and includes:
- IDT/interrupt scaffolding, PIC remap, IRQ handlers
- VGA + serial output
- Keyboard and timer input
- Paging enablement
- Early physical memory parsing/allocation + `kmalloc`

This is an excellent "bring-up" base. The next step is to move from a demo kernel to a **usable tiny system**.

---

## 2) Vision and constraints

### Vision (12-month hobby target)
Build a small but coherent single-user OS with:
- preemptive multitasking
- user mode processes
- a tiny virtual filesystem + initramfs
- a basic syscall ABI
- a text shell and a few user programs

### Constraints (important)
- Part-time hobby cadence (4–10 hrs/week)
- Prefer maintainability over maximum features
- Prioritize debuggability and deterministic behavior
- AI-assisted coding is welcome, but all architecture decisions must be documented

---

## 3) Outcome tiers (what “done” means)

### Tier A (minimum viable OS)
- Stable boot, memory manager, scheduler, syscall entry
- Run at least 2 user-space programs
- Shell supports process launch, `help`, `ps`, `cat`, `echo`
- Read-only filesystem support

### Tier B (recommended target)
- ELF loader
- copy-on-write deferred (optional), but proper process isolation present
- simple IPC primitive (pipe or message queue)
- block device read path + simple write-capable FS or robust tmpfs + initramfs tooling

### Tier C (stretch)
- network driver + minimal TCP/IP stack or loopback sockets
- SMP awareness (not required to support multicore scheduling yet)

---

## 4) Development phases and milestones

## Phase 0: Foundation hardening (2–4 weeks)
**Goal:** make current kernel easier to evolve safely.

Deliverables:
- Centralized kernel logging with levels (DEBUG/INFO/WARN/PANIC)
- Assert + error code conventions
- Early boot self-checks
- CI-like local script (`make check`) for compile + smoke boot in QEMU
- Documentation for architecture and coding rules

Exit criteria:
- Boot and interaction are stable over repeated runs
- Regressions become visible quickly

## Phase 1: Memory subsystem maturation (3–6 weeks)
**Goal:** reliable memory foundation before processes.

Deliverables:
- Harden PMM allocator accounting
- Kernel virtual memory layout document + constants cleanup
- Split `kmalloc` into slab-like small alloc + page alloc fallback (basic version)
- Page fault diagnostics (faulting addr, mode, error bits)

Exit criteria:
- Stress allocation test runs without obvious corruption
- Clear memory ownership model documented

## Phase 2: Process model + scheduler (4–8 weeks)
**Goal:** transition from single loop to tasking kernel.

Deliverables:
- Task struct, kernel stack per task
- Context switch (register save/restore)
- Preemptive round-robin scheduler (timer-driven)
- Idle task + kernel worker tasks

Exit criteria:
- Multiple kernel tasks run and yield/preempt correctly
- No timer-related crashes over long run

## Phase 3: User mode + syscall ABI (4–8 weeks)
**Goal:** secure boundary between kernel and user code.

Deliverables:
- Ring3 transition path (`iret`/trapframe model)
- Syscall entry (`int 0x80` initially; fast syscall later optional)
- Handle table for FDs/resources
- Minimal syscalls: `write`, `read`, `exit`, `spawn`, `yield`, `time`

Exit criteria:
- User-space “hello”, “echo” run reliably
- Faulty user program cannot crash kernel silently

## Phase 4: Executable loading + filesystem baseline (5–10 weeks)
**Goal:** load multiple programs from a real format.

Deliverables:
- ELF32 loader (static binaries first)
- VFS abstraction with one backend to start (initramfs or tarfs)
- Basic path resolution (`/bin`, `/etc`, `/dev` model)
- `/dev/console` and `/dev/null`

Exit criteria:
- Shell can execute binaries from `/bin`
- File reading is stable and repeatable

## Userspace ergonomics stage (4-8 weeks)
**Goal:** become usable, not just bootable.

Deliverables:
- Tiny C runtime/startup for user programs
- Basic shell parser + command execution
- Core tools: `ls`, `cat`, `echo`, `ps`, `sleep`
- Init process and startup script

Exit criteria:
- Boot-to-shell workflow works every run
- Demo scenario reproducible with script

## Phase 6: Device, display, and reliability improvements (ongoing)
**Goal:** reduce fragility and expand capabilities.

Possible items:
- Multiboot framebuffer bring-up + a first visual shell surface
- ATA PIO block driver
- better keyboard input editing/history
- monotonic clock improvements
- kernel test hooks and fuzz-like syscall exerciser

---

## 5) Recommended technical priorities (strict order)
1. Build/release hygiene
2. Memory correctness
3. Scheduler correctness
4. User/kernel isolation
5. Executable loading + FS
6. Userland polish
7. New hardware capabilities

If you violate this order, expect painful rewrites.

---

## 6) Time budgeting (realistic)

Assuming ~6 hrs/week average:
- Foundation + memory + tasking + user mode: 4–6 months
- Filesystem + shell + userland tools: 2–4 months
- Hardening and optional extras: ongoing

**Practical expectation:** a solid Tier A/B SkezOS in ~8–12 months.

---

## 7) Risk register + mitigations
- **Risk:** weak observability in kernel crashes.
  - **Mitigation:** log ring buffer + structured panic dumps early.
- **Risk:** memory bugs block all progress.
  - **Mitigation:** make memory invariants explicit and assert often.
- **Risk:** architecture drift from AI-generated patches.
  - **Mitigation:** require design notes per subsystem change.
- **Risk:** feature creep.
  - **Mitigation:** only one active milestone at a time; backlog everything else.

---

## 8) Definition of Done (per milestone)
A milestone is done only when all are true:
1. Code merged + documented
2. Boot demo works in clean environment
3. Regression checklist passes
4. Known limitations recorded in docs
5. Next milestone explicitly unblocked

---

## 9) Suggested immediate next 2 weeks
1. Request a framebuffer mode in the Multiboot/GRUB path and confirm QEMU handoff
2. Parse the framebuffer info tag into a minimal kernel display descriptor
3. Add a tiny framebuffer text renderer with VGA fallback
4. Keep `make qemu-smoke-shell-core` green while the shell display path evolves

These steps create the platform for a real UI layer without throwing away the current shell/runtime path.
