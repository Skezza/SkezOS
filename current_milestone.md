# Current Milestone

## Milestone
Name: Phase 11 - GUI polish and visual shell operator HUD
Target window: 3-5 days
Owner: joe + codex

## Objective
Move active execution back to the GUI track with a narrow, low-risk framebuffer polish slice:
- keep the shell/runtime model unchanged while improving the framebuffer UI readability and operator context
- keep reliability infrastructure intact as a guardrail, not the main development branch
- define the next GUI-focused backlog in milestone terms so work does not drift back into ad hoc reliability-only increments

## In scope
- [x] Re-baseline active planning docs so GUI is the current milestone
- [x] Land one narrow framebuffer HUD/chrome improvement that is visible at boot
- [x] Preserve the current shell ABI, command model, and syscall contracts
- [x] Keep `make check` and `make check-nightly` green with no timeout inflation

## Out of scope
- [x] New userland app framework, widgets, or window manager work
- [x] Shell parser expansion (quoting/jobs) as part of this GUI slice
- [x] Syscall ABI growth for GUI-only concerns
- [x] Replacing the bootstrap font asset in this slice

## Tasks
- [x] Switch active milestone text from reliability to GUI polish (`done`)
- [x] Add framebuffer footer HUD strip with stable operator-state text (`done`)
- [x] Add command timeline rail in footer HUD using existing shell lifecycle output patterns (`done`)
- [x] Keep framebuffer content viewport/prompt lane intact while reserving footer space (`done`)
- [x] Define the immediate next GUI slices (font coverage pass, line-density tweaks, shell chrome interactions) in this document (`done`)
- [x] Add one deterministic visual regression check strategy note for future CI use (`done`, `docs/gui_visual_regression_strategy.md`)
- [x] Implement first deterministic GUI gate: framebuffer state hash line asserted in smoke (`done`)

## Immediate next GUI slices
- Slice A (font coverage + fallback): complete printable ASCII glyph coverage in the bootstrap table, keep `?` fallback, and emit one boot-time coverage log line.
- Slice B (line density/readability): tighten glyph baseline and line marker contrast while preserving current row/column geometry and prompt lane behavior.
- Slice C (shell chrome interactions): use existing shell state transitions (`R`/`E`/`C`) to drive subtle prompt-lane/footer state hints without new syscalls.

## Visual regression strategy
- Strategy note is tracked in `docs/gui_visual_regression_strategy.md`.
- Initial deterministic gate: continue serial/reliability smokes and add framebuffer-render-state hashing from the display path later, before screenshot-based CI.

## Risks
- Risk: visual tweaks can accidentally reduce text readability or prompt clarity.
  - Mitigation: keep each change narrow and preserve prompt/content geometry invariants.
- Risk: GUI work can become difficult to validate in headless CI.
  - Mitigation: keep smoke chains green and capture visual assertions as structured future tasks, not implicit assumptions.
- Risk: milestone drift back to reliability-only work without explicit planning.
  - Mitigation: keep reliability in maintenance mode and gate new reliability additions behind explicit GUI-scope compatibility needs.

## Exit criteria
- [x] Active planning docs identify GUI polish as the current milestone
- [x] One concrete framebuffer UI polish slice is merged without runtime/ABI churn
- [x] Reliability and nightly smoke chains remain green after GUI changes
- [x] Next two GUI slices are pre-scoped in actionable terms

## Notes / decisions
- 2026-03-09 - Reliability branch is now treated as maintenance baseline; active execution is `Phase 11 - GUI polish and visual shell operator HUD`.
- 2026-03-09 - Landed first Phase 11 slice: framebuffer shell now reserves a footer row and renders a compact operator HUD line while preserving shell/runtime behavior.
- 2026-03-09 - Landed next Phase 11 slice: bootstrap framebuffer font table now covers all printable ASCII glyphs, and boot logs now emit explicit coverage verification (`95/95` expected).
- 2026-03-09 - Landed creative GUI slice: footer HUD now includes a command timeline rail keyed off prompt/launch/wait/failure output transitions, with running and pass/fail capsule colors.
- 2026-03-09 - Landed deterministic GUI gate slice: framebuffer path now emits `display: gui_state_hash=... profile=fb-shell-v2`, and `qemu-smoke-shell-core` asserts the expected hash when framebuffer mode is active.
- 2026-03-09 - Landed line-density/readability pass: framebuffer glyph baseline and chrome contrast were tuned, hot-path HUD redraws were tightened, and reliability smoke sequencing was made deterministic under unchanged smoke timeout values.
- 2026-03-09 - Added deterministic visual-regression strategy note: `docs/gui_visual_regression_strategy.md`.
- 2026-03-09 - Validation for this transition slice:
  - `make check`
  - `make check-nightly`
- 2026-02-27 - Lifecycle hardening milestone completed: wait-driven child synchronization, process-owned FD ownership, and deterministic task-stack/loader-scratch reclamation are in place.
- 2026-02-27 - Lifecycle hardening validation used:
  - `make qemu-smoke-phase4-repeat PHASE4_REPEAT=2`
  - `make qemu-smoke-lifecycle`
  - `make qemu-smoke-userfault`
- 2026-02-27 - Active follow-up milestone is Phase 6 userland workflow + shell bootstrap.
- 2026-02-27 - Phase 6 design note added: `docs/phase6_userland_workflow_design_note.md`.
- 2026-02-27 - Landed Phase 6 bootstrap foundation: shared assembly syscall/runtime includes now back the existing `/bin/hello*.elf` demos, and `make` regenerates `kernel/initramfs_demo_blob.c` from `userland/` sources automatically.
- 2026-02-27 - Validation for the new Phase 6 foundation slice:
  - `make qemu-smoke-lifecycle`
  - `make qemu-smoke-userfault`
- 2026-02-27 - Chose direct `/bin/sh.elf` bootstrap. The kernel now starts a fixed-slot shell task at boot, and `make qemu-smoke-shell-core` asserts the shell banner/prompt path.
- 2026-02-27 - Phase 6 shell bootstrap is now interactive: `/dev/console` input hands off to `user-shell`, the shell runs a prompt/read/dispatch loop, external `/bin/echo.elf` and `/bin/cat.elf` run through `spawn` + `waitpid`, and `SYS_SPAWN_EX` hands children a flat inherited cmdline.
- 2026-02-27 - Validation for the interactive Phase 6 slice:
  - `make qemu-smoke-lifecycle`
  - `make qemu-smoke-userfault`
  - `make qemu-smoke-shell-core`
- 2026-02-27 - Phase 6 completion landed: child launch now inspects ET_EXEC images directly, `uaccess` is task-aware, `/bin/echo.elf` is external, and `make check` now runs the active userfault + Phase 6 smoke path.
- 2026-02-27 - Remaining Phase 6 limitation: child launch is generic for fixed-address ET_EXEC images, but argument handoff is still flat cmdline-only (no argc/argv), stdin stays with `user-shell`, and `ps` is still a stub.
- 2026-02-28 - Post-Phase-6 handover is captured in `docs/next_phase_handover.md`; the recommended next slice is foreground stdin handoff, a minimal `argc/argv` ABI, and a real `ps` path before broader device work.
- 2026-02-28 - Delivered the post-Phase-6 interaction slice: `/dev/console` input ownership is now PID-based, synchronous `waitpid()` temporarily hands stdin to the foreground child, and ownership returns to `user-shell` on completion.
- 2026-02-28 - Spawned user tools now receive a minimal `argc/argv` startup stack built by the kernel launch path; `/bin/echo.elf` and `/bin/cat.elf` now consume `argc/argv` directly instead of `SYS_GETCMDLINE`.
- 2026-02-28 - `ps` is now backed by `SYS_TASK_SNAPSHOT`, a bounded kernel task snapshot syscall used by the shell builtin.
- 2026-02-28 - Added `/bin/readln.elf` as a dedicated stdin handoff smoke tool; `make qemu-smoke-shell-core` now checks `readln`, real `ps`, `echo`, `cat`, unknown-command failure, and clean shell exit.
- 2026-02-28 - Validation for the foreground I/O slice:
  - `make all`
  - `make qemu-smoke-shell-core`
  - `make check`
- 2026-02-28 - The old post-Phase-6 interaction milestone is complete; the active follow-on is Phase 7, starting with blocking `/dev/console` reads so foreground readers no longer poll from userland.
- 2026-02-28 - Landed the first Phase 7 slice: the owning `/dev/console` task now blocks in the kernel read path, while non-owner reads stay zero-byte and non-blocking; `user-shell` and `/bin/readln.elf` now rely on blocking `read()` directly.
- 2026-02-28 - Validation for the Phase 7 blocking-read slice:
  - `make qemu-smoke-shell-core`
  - `make check`
- 2026-02-28 - `SYS_SLEEP` remains available as a generic primitive, but shell stdin waiting no longer depends on userland tick-sleep polling.
- 2026-02-28 - Landed the planned narrow UX follow-up: the shell and `/bin/readln.elf` now treat both `BS` and serial `DEL` as erase-one-char input and redraw the line in place.
- 2026-02-28 - `qemu-smoke-shell-core` now injects corrected shell and `readln` input that depends on the new backspace handling, so the smoke path covers the edit behavior instead of only the final commands.
- 2026-02-28 - Phase 7 is now effectively complete; the next logical milestone is to pivot to the queued device/reliability track unless more shell polish is chosen deliberately.
- 2026-02-28 - Started the next device/reliability slice as a narrow timing milestone: the kernel now exposes `SYS_TIME_INFO`, a stable monotonic tick snapshot (`ticks_hi` + `ticks_lo`) plus PIT frequency in Hz.
- 2026-02-28 - Added `/bin/uptime.elf` as a regular external tool so the shell can inspect the monotonic clock path without relying on demo-only logging.
- 2026-02-28 - Added `/bin/sleep.elf` as a second timing tool on top of the existing tick scheduler; it accepts a tick count, calls `SYS_SLEEP`, and reports requested vs observed elapsed ticks.
- 2026-02-28 - `make qemu-smoke-shell-core` now runs both `uptime` and `sleep` and asserts their output, keeping the timing ABI and timing utility path on the default shell regression path.
- 2026-02-28 - Validation for the Phase 8 timing slice:
  - `make qemu-smoke-shell-core`
  - `make check`
- 2026-02-28 - Phase 8 is now complete enough to move on; the active follow-on is Phase 9, a display-first UI milestone.
- 2026-02-28 - Chose `Phase 9 - Framebuffer bring-up and visual shell` as the next active milestone. The first landed slice keeps the existing shell/runtime intact but adds a fixed VGA text-mode chrome header so the system immediately looks more intentional.
- 2026-02-28 - Phase 9 groundwork now also captures the Multiboot framebuffer handoff descriptor when the bootloader provides it, so display metadata has a stable kernel-side home before the renderer exists.
- 2026-02-28 - A direct boot-time switch into framebuffer mode initially blanked the visible console; that blocker is now closed by the display abstraction, safe framebuffer mapping, and a minimal framebuffer text renderer.
- 2026-02-28 - Added a thin `kernel/display.c` abstraction and moved kernel boot output, `/dev/console`, and high-priority klog mirroring behind it. VGA remains the only backend today, but the framebuffer swap is now localized.
- 2026-02-28 - Added a dynamic paging path for kernel-mapped device memory plus a reserved framebuffer window (`0xC2000000` base, `16 MiB`). When a bootloader provides framebuffer metadata, `display_late_init()` can now map the window safely without switching the visible console yet.
- 2026-02-28 - The kernel now requests an optional `1024x768x32` framebuffer by default, maps it when a direct-RGB pixel surface is provided, and renders a minimal uppercase-biased framebuffer text shell there while keeping serial output and VGA fallback intact.
- 2026-02-28 - Next implementation slices for Phase 9 are framebuffer font coverage/polish, richer layout treatment, and only after that any larger GUI/widget ambitions.
- 2026-02-28 - Validation for the first Phase 9 visual slice:
  - `make qemu-smoke-shell-core`
  - `make check`
- 2026-03-01 - The framebuffer shell now rasterizes the existing bootstrap glyph table onto a denser 5x7 cell grid, which materially improves readability without yet introducing a brand-new font asset.
- 2026-03-01 - Validation for the framebuffer font-density slice:
  - `make check`
- 2026-03-01 - The prompt-lane badge now renders a compact live status (`R`/`E`/`C`) plus elapsed monotonic seconds, so the split console shows runtime movement without needing a larger widget.
- 2026-03-01 - Validation for the prompt-badge runtime slice:
  - `make check`
- 2026-03-03 - Landed a narrow Phase 9 chrome-readability slice: framebuffer header/logo text now uses a shadowed glyph pass, improving contrast without replacing the bootstrap font table yet.
- 2026-03-03 - Validation for the chrome-readability slice:
  - `make check`
- 2026-03-03 - Phase 9 is complete enough to re-baseline the active work. The new active milestone is `Phase 10 - Reliability hooks and syscall exerciser`.
- 2026-03-03 - Added `/bin/diag.elf` as the first reliability slice: it intentionally probes a few invalid syscall paths (`SYS_WRITE`, `SYS_OPEN`, `SYS_SPAWN_EX`, and an unknown syscall number) and reports pass/fail through the normal shell workflow.
- 2026-03-03 - `qemu-smoke-shell-core` is being re-baselined to assert the current shell strings, updated tool output, and the new diagnostics tool on the default interactive operator path.
- 2026-03-03 - Added `/bin/fault.elf` as a shell-launched user-fault probe so `qemu-smoke-userfault` no longer depends on the old boot-time `user-fault` demo task.
- 2026-03-03 - Validation for the first Phase 10 reliability slice:
  - `make qemu-smoke-userfault`
  - `make qemu-smoke-shell-core`
  - `make check`
- 2026-03-05 - Closed the smoke de-crowding decision: `qemu-smoke-shell-core` is now a strict frozen core-shell baseline, and reliability/timing/stress checks moved to `make qemu-smoke-reliability`.
- 2026-03-05 - `make check` now runs the full smoke chain: `qemu-smoke-userfault` + `qemu-smoke-shell-core` + `qemu-smoke-reliability`.
- 2026-03-05 - Landed reliability slice 2: `/bin/diag.elf` now covers `SYS_PIPE`, `SYS_DUP`, and `SYS_DUP2` negative paths plus pipe EOF/broken-pipe behavior with stable `diag: ... ok` output lines.
- 2026-03-05 - `make qemu-smoke-reliability` now asserts root ramfile redirect create/append behavior (`/reliability.txt`) and checks each new `diag` reliability line, not just `diag: PASS`.
- 2026-03-05 - Explicit follow-on boundary: reliability growth now goes to broader syscall probe coverage or structured kernel-side hooks, not further expansion of `qemu-smoke-shell-core`.
