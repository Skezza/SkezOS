# GUI Visual Regression Strategy (Phase 11)

## Goal
Add deterministic GUI-regression signals without requiring fragile screenshot comparison as the first step.

## Constraints
- Keep shell/runtime/syscall behavior unchanged.
- Keep CI headless-friendly and stable on GitHub-hosted runners.
- Avoid introducing heavyweight image-processing dependencies in the first pass.

## Phased approach
1. State-hash gate (first implementation target)
   - Compute a deterministic framebuffer render-state hash from display-layer events/layout state.
   - Emit hash lines in a stable text contract so smoke targets can assert known profiles.
   - Keep hash inputs explicit (resolution, panel geometry, style transitions) and exclude high-variance data.

2. Artifact snapshot + masked pixel baseline gate (active follow-on)
   - Export compact framebuffer dump artifacts for GUI baseline checks.
   - Compute a deterministic masked-ROI pixel hash for static chrome regions.
   - Gate this baseline in nightly only (`check-nightly`) to catch visual drift without affecting `check-pr`.

3. Screenshot diff gate (later)
   - Introduce pixel diffs only after state hashing proves stable and false positives are low.

## Acceptance criteria for phase 1
- One deterministic GUI hash emitted per boot profile in framebuffer mode.
- Hash remains stable across repeated local and CI runs for identical inputs.
- `make check` and `make check-nightly` continue to pass without timeout changes.

## Current status (2026-03-09)
- Implemented for profile `fb-shell-v4`:
  - kernel emits `display: gui_state_hash=... profile=fb-shell-v4`
  - expected hash is `0xD9BFAA54`
  - shell-facing smokes now assert expected hash when framebuffer mode is active:
    - `qemu-smoke-userfault`
    - `qemu-smoke-shell-core`
    - `qemu-smoke-lifecycle`
    - `qemu-smoke-reliability`
    - `qemu-smoke-reliability-replay`
    - `qemu-smoke-reliability-fuzz-lite-matrix`
- Implemented nightly failure triage hook:
  - `check-nightly` now attempts `qemu-smoke-gui-fb-dump` when nightly fails
  - artifact output is `build/artifacts/gui-fb-failure-*.ppm`
  - capture now emits sidecar metadata `build/artifacts/gui-fb-failure-*.meta` with timestamp, geometry, size, and `sha256`
  - capture now updates stable latest pointers:
    - `build/artifacts/gui-fb-failure-latest.ppm`
    - `build/artifacts/gui-fb-failure-latest.meta`
    - `build/artifacts/gui-fb-failure-latest.qemu.log`
  - capture now emits a parseable summary line:
    - `GUI_FB_DUMP_META path=... sha256=... width=... height=... size_bytes=...`
  - capture is explicitly non-gating (nightly still fails/passes based on smoke assertions)

## Current status (2026-03-11)
- Added masked pixel-baseline helper:
  - `scripts/gui_visual_baseline.py`
  - profile: `fb-shell-v4`
  - ROI geometry constants are read from `kernel/display.c` to avoid script/kernel drift
  - ROI hash excludes dynamic regions (uptime/timeline text) and hashes static chrome regions
- Added new make targets:
  - `qemu-smoke-gui-visual-baseline-refresh` (capture + print candidate hash)
  - `qemu-smoke-gui-visual-baseline` (capture + assert expected ROI hash)
- Nightly integration:
  - `check-nightly` now runs `qemu-smoke-gui-visual-baseline` after fork/COW smokes
  - expected ROI hash: `0x1BD7880D`
- Artifact pointer split:
  - baseline capture now updates `gui-fb-baseline-latest.*`
  - failure triage capture keeps `gui-fb-failure-latest.*`
  - this avoids baseline verification runs overwriting failure triage pointers
- Dynamic HUD telemetry note:
  - masked ROI intentionally excludes dynamic footer content
  - dynamic HUD telemetry regressions are guarded by shell smoke assertions on emitted `display: cmd_latency ...`, `display: cmd_latency_budget ...`, `display: cmd_health ...`, `display: cmd_health_state ...`, `display: cmd_health_window ...`, and `display: cmd_health_recovery ...` log lines

## Current status (2026-03-12)
- Landed framebuffer font refresh slice:
  - switched framebuffer text rendering from sampled `3x5` source to crisp native `5x7` source glyphs
  - preserved existing cell geometry (`14x17`) and shell layout density
- Visual contract was versioned to `fb-shell-v5`:
  - kernel now emits `display: gui_state_hash=... profile=fb-shell-v5`
  - expected state hash: `0x9A4C1DA5`
- Baseline gate now targets `fb-shell-v5`:
  - expected ROI hash: `0x1BD7880D`
  - `qemu-smoke-gui-visual-baseline-refresh` and `qemu-smoke-gui-visual-baseline` both use profile `fb-shell-v5`
- Desktop-shell chrome follow-on is now versioned to `fb-shell-v6`:
  - kernel now emits `display: gui_state_hash=0xAAA213A9 profile=fb-shell-v6`
  - expected ROI hash: `0x17AA9EDD`
  - the masked ROI now covers the desktop chrome set: top bar, launcher rail, window frame/titlebar, and operator-sidebar headers
  - subsequent runtime-only navigation work (PS/2 arrow-key focus/view switching inside the framebuffer shell) intentionally preserves the same boot capture and ROI hash; the existing baseline remains the guardrail for default chrome layout
  - runtime panel bodies are now gated separately:
    - `fb-shell-v6-nav-task` hashes the task-view chrome plus upper body content after a scripted `down` arrow sequence and expects `0x77AFF05E`
    - `fb-shell-v6-nav-focus` hashes focus/card-sensitive chrome after a scripted `down,right,down,left,up` sequence and expects `0xBFC5F4C6`
  - the navigation smoke currently injects ANSI escape arrows over the serial smoke path and translates them back into keyboard extended scancodes inside the guest, because QMP `send-key` was accepted by QEMU but produced unchanged framebuffer captures under `-display none`
