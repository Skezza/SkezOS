# SkezOS Scheduler Design Note (Phase 2)

## Scope

This note documents the first kernel-task scheduler implementation used to complete Phase 2.
It intentionally targets a single-core kernel with kernel-mode tasks only.

## Current model

- Fixed-size task table (`SCHED_MAX_TASKS`)
- Per-task kernel stacks allocated from `kmalloc`
- Round-robin scheduling with fixed timeslice (`SCHED_DEFAULT_TIMESLICE`)
- Task states: `RUNNABLE`, `RUNNING`, `SLEEPING`, `ZOMBIE`
- Idle task created during `sched_init()`

## Context switch ABI

`sched_context_switch(uint32_t *old_esp, uint32_t new_esp)` is an assembly helper that:

1. pushes callee-saved registers (`ebp`, `ebx`, `esi`, `edi`)
2. stores current `esp` into `*old_esp`
3. loads `esp = new_esp`
4. restores callee-saved registers
5. `ret`urns into the resumed context

This supports switching between:

- normal kernel code paths (`sched_yield`, `sched_sleep_ticks`)
- timer IRQ call paths (preemption from `sched_on_timer_tick_irq`)
- fresh task stacks (which return into `sched_task_trampoline`)

## Timer-driven preemption detail

Timer preemption occurs inside IRQ0 handling (`timer_handler` -> `sched_on_timer_tick_irq`).

Important implementation detail:

- IRQ0 is acknowledged (PIC EOI) **before** dispatch in `kernel/irq_stubs.c`

Reason:

- the scheduler may suspend the interrupted kernel stack while still inside the timer IRQ call chain
- if EOI were delayed until after dispatch, future timer interrupts could be blocked while that suspended IRQ frame remains unresolved

Only IRQ0 uses this early-ack behavior. Other IRQs keep ack-after-dispatch.

## Sleep/wake behavior

- `sched_sleep_ticks(n)` marks the current task `SLEEPING` until `timer_ticks + n`
- sleeping tasks are woken from the timer IRQ path
- idle task runs whenever no non-idle task is runnable

## Known limitations

- No separate scheduler lock abstraction yet (interrupt masking is used directly)
- No per-task accounting export (`ps`) yet
- No kernel stack guard pages
- Preemption currently depends on the existing C interrupt-wrapper strategy and IRQ0 early-EOI ordering
- No user/kernel address space switching (kernel tasks only)

## Validation used

- `make check` (compile + headless QEMU smoke)
- extended headless run (`make qemu-smoke SMOKE_TIMEOUT_SECS=12`) showing:
  - worker task iteration logs over time
  - idle heartbeat log (`idle_ticks`)
