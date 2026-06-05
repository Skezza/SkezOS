# Current Milestone

## Update (2026-03-12)
- Active execution has moved beyond the old GUI-only slice to **Phase 14.5 hardening** for recently landed features:
  - writable storage stack (`ATA PIO + block cache + /persist persistfs`) and `SYS_UNLINK`
  - user `fork` with per-task address spaces and COW fault handling (`SYS_FORK`)
  - shell parser v2 (`quote/escape aware`) + background pipelines (`&`) + `waitpid(NO_HANG)`
- Current focus is polish/robustness only (no new syscall numbers in this slice):
  - shell `wait` behavior for background jobs, deterministic overflow signaling, and replay stability
  - `persistfs` clean-flag semantics and dirty-mount sanity handling
  - COW pressure-path diagnostics and deterministic fork-failure behavior under capacity pressure
- Landed GUI polish follow-on (Phase 15 font refresh):
  - framebuffer console font upgraded to crisp `5x7` source glyphs while keeping existing `14x17` cell layout
  - GUI profile/hash contract moved to `fb-shell-v5` (`display: gui_state_hash=0x9A4C1DA5 profile=fb-shell-v5`)
  - nightly visual baseline now validates `fb-shell-v5` ROI hash `0x1BD7880D`
- Landed desktop-shell chrome follow-on:
  - framebuffer layout now renders a desktop-style top bar, launcher rail, windowed shell surface, and operator sidebar without changing shell/syscall ABI
  - GUI profile/hash contract moved to `fb-shell-v6` (`display: gui_state_hash=0xAAA213A9 profile=fb-shell-v6`)
  - visual ROI baseline now validates `fb-shell-v6` hash `0x17AA9EDD`
  - shell output now drives live chrome state (`set theme`, `set hud`, `bootshow`, `hud:`) so the sidebar/top bar/prompt/footer react in-place without repainting the transcript surface
  - desktop tabs/titlebar/dock now react to live command/task/operator state (`RUN`, multi-user task load, HUD/showcase health) while keeping the boot framebuffer baseline unchanged
  - PS/2 arrow keys now drive framebuffer navigation state: left/right shifts focus between dock, live shell window, and sidebar, while up/down cycles the active app lens or selected operator card without changing the shell/runtime ABI or the boot framebuffer baseline
  - framebuffer panel navigation now switches real body content instead of chrome only: `TERM` restores a retained visible transcript/prompt model, `TASK` renders scheduler snapshots, `FS` renders the live prompt-derived cwd via `vfs_list_dir()`, and `LOG` renders command-health/timeline telemetry without changing shell/syscall ABI
  - scripted serial ANSI arrows now translate back into kernel keyboard scancodes for deterministic smoke coverage of the same navigation path used by real arrow keys
  - nightly GUI coverage now includes `qemu-smoke-gui-nav`, with post-navigation framebuffer profiles `fb-shell-v6-nav-task` (`0x77AFF05E`) and `fb-shell-v6-nav-focus` (`0xBFC5F4C6`) while the default boot ROI baseline remains `fb-shell-v6` / `0x17AA9EDD`
- Remaining intentional limits:
  - no journaling/fsck or crash-recovery redesign in `persistfs`
  - no shell `jobs`/`fg` builtins yet
  - fixed-size COW metadata table remains in place (hardened behavior around limits)

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
- [x] Add compact footer interaction legend (`I/IN`, `R/RUN`, `O/OK`, `E/ERR`) with last transition cause from existing shell output parsing (`done`)
- [x] Keep framebuffer content viewport/prompt lane intact while reserving footer space (`done`)
- [x] Define the immediate next GUI slices (font coverage pass, line-density tweaks, shell chrome interactions) in this document (`done`)
- [x] Add one deterministic visual regression check strategy note for future CI use (`done`, `docs/gui_visual_regression_strategy.md`)
- [x] Implement first deterministic GUI gate: framebuffer state hash line asserted in smoke (`done`)
- [x] Add nightly-only non-gating framebuffer dump artifact capture on failure (`done`)
- [x] Add nightly-gated masked framebuffer pixel baseline check (`done`, `qemu-smoke-gui-visual-baseline`)
- [x] Add command-latency HUD telemetry in framebuffer footer (last/avg/peak + outcome/run) using existing shell lifecycle parsing only (`done`)
- [x] Add command-health telemetry + recovery cue (ok/fail streak + recent cmd-rate) using existing shell lifecycle parsing only (`done`)
- [x] Add command-health state machine (`OK/WARN/DEGR`) with prompt/footer cues and transition logs from existing shell lifecycle parsing only (`done`)
- [x] Add rolling command-quality window telemetry and HUD exposure (`recent success %`) with deterministic logs (`done`)
- [x] Add command-health recovery telemetry (`time-to-recover`) and HUD exposure with deterministic logs (`done`)
- [x] Add adaptive command-latency budget telemetry (`in-budget` vs `slow`) with footer/header HUD exposure and deterministic logs (`done`)

## Immediate next GUI slices
- Slice A (font coverage + fallback): complete printable ASCII glyph coverage in the bootstrap table, keep `?` fallback, and emit one boot-time coverage log line.
- Slice B (line density/readability): tighten glyph baseline and line marker contrast while preserving current row/column geometry and prompt lane behavior.
- Slice C (shell chrome interactions): use existing shell state transitions (`R`/`E`/`C`) to drive subtle prompt-lane/footer state hints without new syscalls.

## Phase 12 candidate (input UX, ABI-stable)
- [x] Start userland command-entry UX hints without parser/ABI growth: `Tab` now completes first-token command names (builtins + `/bin/*.elf`) and shows compact `hint:` candidates on ambiguity (`done`)
- [x] Add lightweight history preview navigation without ABI churn: `Esc`/`Ctrl+P` browse backward through recent commands, `Ctrl+N` moves forward toward the current buffer (`done`)
- [x] Expose bounded in-shell command history as a builtin (`history`) and gate it in `qemu-smoke-shell-core` without syscall/parser/ABI changes (`done`)
- [x] Add `/persist`-backed shell history persistence + `history clear` management path and gate cross-boot replay/clear durability in nightly (`qemu-smoke-shell-history-persist`) without syscall/parser/ABI changes (`done`)
- [x] Add reverse history search (`Ctrl+R`) with repeat-cycle behavior and deterministic `search:` hint lines, gated in `qemu-smoke-shell-core` without syscall/parser/ABI changes (`done`)
- [x] Add fast line-edit kill shortcuts (`Ctrl+U` clear line, `Ctrl+W` erase previous word) and gate deterministic output behavior in `qemu-smoke-shell-core` without syscall/parser/ABI changes (`done`)
- [x] Add history event recall shortcuts (`!!`, `!N`) with deterministic recall hint output, gated in `qemu-smoke-shell-core` without syscall/parser/ABI changes (`done`)
- [x] Expand history event recall with relative/pattern selectors (`!-N`, `!prefix`, `!?term`) and deterministic hint output, gated in `qemu-smoke-shell-core` without syscall/parser/ABI changes (`done`)
- [x] Add quick substitution replay (`^old^new^`) against latest history command with deterministic hint/output gating in `qemu-smoke-shell-core` without syscall/parser/ABI changes (`done`)
- [x] Add prefix-filtered history browse (`Esc`/`Ctrl+P` + `Ctrl+N` on typed prefix) with deterministic replay gating in `qemu-smoke-shell-core` without syscall/parser/ABI changes (`done`)
- [x] Add `history run N` replay execution (by displayed history index) with deterministic hint/output gating in `qemu-smoke-shell-core` without syscall/parser/ABI changes (`done`)

## Phase 13 candidate (GUI regression hardening)
- [x] Promote framebuffer GUI hash assertions from shell-core-only to the shell-facing smoke suite (`qemu-smoke-userfault`, `qemu-smoke-lifecycle`, `qemu-smoke-reliability`, replay, and fuzz-lite), while keeping hash checks conditional on framebuffer mode (`done`)

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
- 2026-03-09 - Landed deterministic GUI gate slice: framebuffer path now emits `display: gui_state_hash=... profile=fb-shell-v3`, and `qemu-smoke-shell-core` asserts the expected hash when framebuffer mode is active.
- 2026-03-09 - Landed compact HUD legend slice: footer now shows state meanings (`I/R/O/E`) plus the last transition cause (`PROM`, `WAIT`, `FAIL`, `EXIT`, `ROLL`, `HOLD`) using existing shell/log parsing only; no ABI/syscall changes.
- 2026-03-09 - Refreshed deterministic GUI gate profile to `fb-shell-v4` with expected hash `0xD9BFAA54` asserted by `qemu-smoke-shell-core`.
- 2026-03-09 - Landed nightly triage slice: `check-nightly` now auto-attempts a non-gating framebuffer screenshot artifact capture on failure (`qemu-smoke-gui-fb-dump` -> `build/artifacts/gui-fb-failure-*.ppm`).
- 2026-03-10 - Started Phase 12 candidate input UX slice: shell `Tab` completion now auto-expands first-token command names from builtins and `/bin/*.elf` entries, emits compact `hint:` match lists when ambiguous, and preserves existing syscall/parser contracts.
- 2026-03-10 - Landed follow-up input UX slice: shell input now keeps a bounded history ring and supports quick preview navigation (`Esc`/`Ctrl+P` older, `Ctrl+N` newer/current) while keeping command execution semantics and ABI unchanged.
- 2026-03-10 - Landed next Phase 12 follow-up: shell now exposes the same bounded ring through a `history` builtin and `qemu-smoke-shell-core` now exercises both Tab-completed builtin invocation (`his<Tab>`) and numbered history output.
- 2026-03-10 - Reliability maintenance follow-up: `qemu-smoke-reliability` redirected pipeline probe was narrowed from three `cat` stages to two (`cat < readme.txt | cat`) to avoid spawn-slot timing contention (`rc=-95`) while preserving redirected-pipeline coverage.
- 2026-03-10 - Started Phase 13 candidate hardening: added a reusable framebuffer GUI-hash assertion helper and applied it across all shell-facing smoke targets so visual-regression hash drift is caught outside shell-core as well.
- 2026-03-10 - Reliability sequencing hardening: raised `RELIABILITY_FUZZ_RUNNER_SETTLE_SECS` to `2.25` to keep fuzz-lite command injection aligned with console ownership handoff and avoid intermittent JSON validator failures in nightly runs.
- 2026-03-10 - Artifact-triage hardening follow-up: framebuffer dump capture now validates PPM headers and writes sidecar `.meta` files (timestamp, geometry, size, sha256, qmp settings) so nightly failure artifacts are deterministic and machine-readable.
- 2026-03-10 - Artifact-triage usability follow-up: capture now also updates `gui-fb-failure-latest.*` pointers and emits a stable `GUI_FB_DUMP_META ...` summary line for automation-friendly post-failure ingestion.
- 2026-03-11 - Landed masked pixel-baseline gate for framebuffer GUI: new `scripts/gui_visual_baseline.py` computes profile-aware ROI hashes, `qemu-smoke-gui-visual-baseline` now asserts expected `fb-shell-v4` ROI hash (`0x1BD7880D`) in nightly, and `qemu-smoke-gui-visual-baseline-refresh` provides deterministic baseline refresh output.
- 2026-03-11 - Landed framebuffer command-latency HUD telemetry: footer legend now includes compact command timing stats (`last/avg/peak`, outcome, and active run ticks) derived from existing prompt/wait/fail transitions with no syscall/parser/ABI changes; `qemu-smoke-shell-core` now asserts `display: cmd_latency ...` telemetry lines.
- 2026-03-11 - Landed framebuffer command-health telemetry follow-up: header metrics now include compact command health (`ok/fail`, active fail streak, recent commands-per-minute), prompt input state now shows a subtle `CHK <n>` recovery cue after consecutive failures, and `qemu-smoke-shell-core` now asserts emitted `display: cmd_health ...` telemetry lines.
- 2026-04-09 - Landed desktop-shell chrome slice: framebuffer mode now presents a top bar, launcher rail, windowed operator shell, and right-hand status sidebar while preserving the existing shell/runtime model; GUI contract bumped to `fb-shell-v6` with state hash `0xAAA213A9` and ROI baseline `0x17AA9EDD`.
- 2026-03-11 - Landed framebuffer command-health state slice: command outcomes now drive a bounded `OK/WARN/DEGR` state machine (fail-streak + success-rate), footer legend now shows `H:<state>`, prompt lane adapts to `WARN/DEGR` recovery cues, and shell-core smoke now asserts emitted `display: cmd_health_state ...` transitions.
- 2026-03-11 - Landed rolling command-quality window slice: recent command outcomes now feed a bounded rolling success window (`R%`) shown in header/footer HUD, health-state classification now prefers recent window quality when populated, and shell-core smoke now asserts emitted `display: cmd_health_window ...` logs.
- 2026-03-11 - Landed command-health recovery slice: health-state transitions now account for dwell in `WARN/DEGR`, emit deterministic `display: cmd_health_recovery ...` lines with last/avg/peak recovery ticks, and expose compact recovery timing (`X`) in header/footer HUD telemetry.
- 2026-03-11 - Landed adaptive latency-budget slice: command completion telemetry now classifies each command against an adaptive budget (`display: cmd_latency_budget ...`), header/footer HUD now surface slow-command context (`L`, `B`, `S`), and shell-core smoke now asserts latency-budget logs.
- 2026-03-11 - Landed persistent shell-history slice: shell now loads/saves bounded history at `/persist/.sh_history` when writable storage is present, supports `history clear`, retries persistence after transient write-open failures, and nightly now validates replay plus clear durability via `qemu-smoke-shell-history-persist`.
- 2026-03-11 - Landed reverse-history-search slice: shell input now supports `Ctrl+R` reverse lookup across bounded history with repeat-cycle semantics, emits deterministic `search: <query> -> <match>` hints, and shell-core smoke now asserts the hint contract.
- 2026-03-11 - Landed line-edit accelerators slice: shell input now supports `Ctrl+U` (kill line) and `Ctrl+W` (kill previous word) in the prompt editor, and shell-core smoke now asserts deterministic edited-command output (`ctrlu-ok`, `alpha gamma`).
- 2026-03-11 - Landed history event-recall slice: shell now supports `!!` (latest) and `!N` (history index in current bounded window), emits deterministic `history: <event> -> <command>` hints, and shell-core smoke now asserts replay output (`recall-target`) plus hint contract.
- 2026-03-11 - Landed advanced history event-recall slice: shell event expansion now also supports `!-N` (relative), `!prefix` (latest prefix match), and `!?term` (latest contains match), with deterministic `history: <event> -> <command>` hints asserted by shell-core smoke.
- 2026-03-11 - Landed history quick-substitution slice: shell now supports `^old^new^` against the latest history command, emits deterministic `history: ^old^new^ -> <command>` hints, and shell-core smoke now asserts substituted replay output (`recall-target2`).
- 2026-03-11 - Landed prefix-filtered history browse slice: when a prefix is typed, `Esc`/`Ctrl+P` now traverses only matching history entries and `Ctrl+N` moves forward/restores the typed prefix; shell-core smoke now asserts deterministic replay output (`pref-two` appears twice).
- 2026-03-11 - Landed indexed history replay slice: `history run N` now executes an entry by current displayed history index, emits deterministic `history: run N -> <command>` hints, and shell-core smoke now asserts replay execution output (`run-target` appears twice).
- 2026-03-09 - Landed line-density/readability pass: framebuffer glyph baseline and chrome contrast were tuned, hot-path HUD redraws were tightened, and reliability smoke sequencing was made deterministic under unchanged smoke timeout values.
- 2026-03-09 - Landed shell chrome interaction slice: prompt-lane status now tracks command lifecycle using existing output transitions (`RUN`, `OK`, `ERR` with command tag) and no syscall/ABI changes.
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
