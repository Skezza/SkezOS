# Agentic Execution Guide for SkezOS

## Purpose
This guide defines how autonomous contributors (human+AI or AI-only) should execute work safely and predictably.

## 1) Work protocol (mandatory)
1. Read current milestone docs and open issues.
2. Write a short implementation plan (3–8 bullets).
3. Make smallest viable patch.
4. Run required checks.
5. Update docs/changelog for behavioral changes.
6. Submit PR with explicit risks and rollback notes.

## 2) Branch and PR strategy
- One branch per milestone task.
- Keep PRs under ~500 changed lines when possible.
- Separate refactors from behavior changes.
- Include “How tested” section with exact commands and observed output summary.

## 3) Required PR template
Use this structure:
- **What changed**
- **Why it changed**
- **Design notes**
- **Testing evidence**
- **Known limitations**
- **Follow-up tasks**

## 4) Required checks per change
For code changes, run at least:
- `make clean && make`
- boot smoke in QEMU (if toolchain available)
- targeted scenario test (e.g., input, timer, allocator)

If environment lacks toolchain/QEMU, explicitly record limitation.

## 5) Milestone-oriented backlog discipline
- Keep a single `current_milestone.md` owner document.
- Mark tasks as:
  - `ready`
  - `in_progress`
  - `blocked`
  - `done`
- Never start stretch goals before all milestone `ready` tasks are closed or deferred.

## 6) Design documentation expectations
Any non-trivial subsystem change must include one of:
- update existing design doc section, or
- add a short ADR (`docs/adr/YYYYMMDD-<topic>.md`)

ADR should cover:
- context
- decision
- alternatives considered
- consequences

## 7) Debugging workflow standard
When bug appears:
1. Reproduce with deterministic steps.
2. Capture serial logs.
3. Identify suspected subsystem.
4. Add temporary instrumentation.
5. Propose minimal fix.
6. Remove temporary noisy debug output unless retained intentionally.

## 8) Quality bar
Reject or revise contributions that:
- introduce unexplained magic constants
- bypass pointer validation in syscall paths
- change ABI without doc updates
- reduce logging quality for fault paths
- mix unrelated features in one PR

## 9) Suggested issue labels
- `milestone:<name>`
- `subsystem:mm|sched|sys|fs|dev|arch`
- `kind:feature|bug|refactor|docs`
- `risk:low|medium|high`

## 10) Minimal milestone starter
Create and maintain `current_milestone.md` with:
- objective
- scope in/out
- tasks + owners
- blockers
- exit criteria

Without this file, agentic work tends to drift.
