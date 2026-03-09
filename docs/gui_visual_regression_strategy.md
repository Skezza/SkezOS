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

2. Artifact snapshot gate (optional follow-on)
   - Export one compact framebuffer dump artifact for failed GUI runs only.
   - Keep this as triage aid, not pass/fail source.

3. Screenshot diff gate (later)
   - Introduce pixel diffs only after state hashing proves stable and false positives are low.

## Acceptance criteria for phase 1
- One deterministic GUI hash emitted per boot profile in framebuffer mode.
- Hash remains stable across repeated local and CI runs for identical inputs.
- `make check` and `make check-nightly` continue to pass without timeout changes.

## Current status (2026-03-09)
- Implemented for profile `fb-shell-v2`:
  - kernel emits `display: gui_state_hash=... profile=fb-shell-v2`
  - `qemu-smoke-shell-core` asserts expected hash when framebuffer mode is active
