# Agentic Worker Handover (2026-03-09)

## Snapshot
- Repo: `SkezOS`
- Branch: `main`
- Handover commit: `43afce5` (`Add prompt lifecycle hints and bump GUI hash profile`)
- Active milestone: `Phase 11 - GUI polish and visual shell operator HUD`

## What is landed
- Framebuffer GUI profile is now `fb-shell-v3` with deterministic hash gate:
  - expected hash: `0x378E9E02`
  - asserted by `qemu-smoke-shell-core`
- Prompt-lane lifecycle hints are implemented in framebuffer mode without ABI/syscall changes:
  - `INPUT` -> `RUN <tag>` -> `OK <tag>` or `ERR <tag>`
  - driven from existing shell/log output parsing in `kernel/display.c`
- Footer command timeline rail (running/success/failure capsules) is active and gated.
- Reliability runner + smoke matrix is stable with deterministic sequencing:
  - replay/fuzz targets now use runner settle delays to avoid dropped foreground commands.

## Critical files and ownership map
- GUI runtime and HUD behavior:
  - `kernel/display.c`
- GUI hash + smoke gates + smoke pacing:
  - `Makefile`
- Reliability scenario runner JSON contract:
  - `userland/reliability_runner_slot15.c`
- Planning/status docs:
  - `current_milestone.md`
  - `technical_considerations.md`
  - `docs/gui_visual_regression_strategy.md`

## Current guardrails (do not violate)
- No syscall ABI churn for GUI polish slices.
- No kernel-only debug hooks for reliability checks.
- Keep shell parser scope narrow (no quoting/jobs expansion bundled into GUI slices).
- Keep `qemu-smoke-shell-core` as frozen core-shell baseline; reliability growth belongs in reliability smokes.

## Validation baseline (must stay green)
Run these before pushing:
1. `make qemu-smoke-shell-core`
2. `make qemu-smoke-reliability`
3. `make qemu-smoke-reliability-replay`
4. `make qemu-smoke-reliability-fuzz-lite-matrix`
5. `make check`
6. `make check-nightly`

Optional release gate:
1. `make check-release`

## Known pitfalls and recent fixes
- Replay/fuzz reliability runs can fail if next command is sent before prior `reliability_runner` exits.
  - Mitigation is already in `Makefile`:
    - `RELIABILITY_REPLAY_RUNNER_SETTLE_SECS`
    - `RELIABILITY_FUZZ_RUNNER_SETTLE_SECS`
- Reliability base smoke similarly requires deterministic sequencing around `diag`.
  - Controlled by:
    - `RELIABILITY_SMOKE_RUNNER_SETTLE_SECS`
    - `RELIABILITY_SMOKE_DIAG_SETTLE_SECS`

## Next recommended execution queue
1. Add a compact interaction legend in the HUD/footer (state meanings + last transition cause), still parse-only, no ABI work.
2. Add framebuffer dump artifact-on-failure for GUI triage (non-gating, nightly only).
3. Start Phase 12 candidate scope: userland-visible input UX (history preview / inline completion hints) behind strict smoke preservation.

## Definition of done for next worker
- One narrow GUI/operator slice landed.
- `fb-shell-*` profile/hash gate updated and asserted.
- `make check` + `make check-nightly` green.
- `current_milestone.md` + `technical_considerations.md` updated with concrete landed notes.
