# Current Milestone

## Milestone
Name: Phase 9 - Framebuffer bring-up and visual shell
Target window: 1-2 weeks
Owner: joe + codex

## Objective
Start the first genuinely visual UI track without jumping straight into a desktop rewrite:
- land an immediate visible win by framing the shell with a fixed VGA text-mode chrome layer
- keep the current shell/runtime path intact while lifting it onto a real framebuffer-backed text surface
- preserve VGA fallback so the operator path stays usable when pixel mode is unavailable or unsupported

## In scope
- [x] Re-baseline the active planning docs around a display-first milestone
- [x] Add a visible VGA text-mode shell chrome layer and keep scrolling beneath it
- [x] Request a Multiboot framebuffer mode from the boot path once a framebuffer console exists
- [x] Capture framebuffer handoff metadata when the bootloader provides it
- [x] Route kernel console output through a minimal display abstraction
- [x] Add framebuffer-safe kernel mapping groundwork behind the display layer for future pixel framebuffers
- [x] Add a minimal framebuffer text renderer while keeping VGA fallback
- [x] Preserve `make qemu-smoke-phase6` and `make check` for the first visual slice

## Out of scope
- [x] Window manager or compositor work
- [x] Mouse input or overlapping windows
- [x] Widget/toolkit work beyond framing the shell surface
- [x] ATA or writable filesystem work in this slice
- [x] Rewriting the shell parser or command model

## Tasks
- [x] Re-baseline the docs from the completed timing slice to the new visual-shell milestone (`done`)
- [x] Reserve fixed top-row chrome in VGA text mode and confine console output to the content region (`done`)
- [x] Validate the first visual slice with `make qemu-smoke-phase6` and `make check` (`done`)
- [x] Capture and expose the Multiboot framebuffer handoff descriptor when the tag is present (`done`)
- [x] Add the Multiboot2 framebuffer request tag plus GRUB graphics payload hints (`done`)
- [x] Introduce a minimal display-surface abstraction so kernel console paths stop calling VGA directly (`done`)
- [x] Add a dynamic kernel mapping path for framebuffer memory and claim a reserved framebuffer window when pixel framebuffer metadata is present (`done`)
- [x] Add first pixel fill / text rendering primitives for the framebuffer path (`done`)
- [x] Move the shell-facing console surface onto the new display path while preserving serial-first logs and VGA fallback (`done`)
- [x] Keep the validation path green through the framebuffer text-shell landing (`done`)
- [x] Improve framebuffer layout polish and readability beyond the first bootstrap presentation (`done`)
- [x] Add distinct lowercase framebuffer glyphs so shell output renders in mixed case (`done`)
- [x] Add lightweight framebuffer line classification so prompt and common log prefixes use distinct colors (`done`)
- [x] Add a styled left gutter rail so prompt and common log prefixes read as distinct lanes (`done`)
- [x] Reserve a fixed prompt lane inside the framebuffer panel so input stays pinned while output scrolls above (`done`)
- [x] Turn the fixed prompt lane into a labeled input strip with reserved text runway and status badge (`done`)
- [x] Make the input strip status live and switch prompt overflow from hard clamping to trailing-window clipping (`done`)
- [x] Densify the framebuffer bootstrap glyph raster so the current shell is more readable without replacing the glyph table yet (`done`)
- [x] Make the prompt badge show mode plus monotonic uptime seconds on redraw (`done`)
- [x] Add shadowed framebuffer chrome text so the title/logo/readouts remain legible against richer header bands (`done`)

## Risks
- Risk: the first visible UI lift is still text mode, so it can look more polished without yet being a real graphics stack.
  - Mitigation: document that this is an intentional bridge slice; treat framebuffer request/detection as the next concrete engineering step.
- Risk: Multiboot framebuffer handoff can vary between GRUB/QEMU configurations.
  - Mitigation: keep VGA text as the fallback console path and prove each framebuffer step incrementally under the existing smoke flow.
- Risk: on QEMU the linear framebuffer may live outside the kernel's 32 MiB early identity map, so even a detected framebuffer is not safely writable yet.
  - Mitigation: do not switch the default boot path into framebuffer mode until a mapped framebuffer write path exists.

## Exit criteria
- [x] Boot reaches a visibly framed shell surface even before framebuffer work lands
- [x] Active planning docs identify the visual-shell track as the current milestone
- [x] The first Phase 9 slice keeps `make qemu-smoke-phase6` and `make check` green
- [x] The kernel captures framebuffer handoff metadata when the bootloader provides it
- [x] Kernel console output now flows through a display abstraction instead of direct VGA calls
- [x] The kernel now has a safe virtual framebuffer window mapping path for pixel framebuffers even though VGA remains the active display
- [x] Kernel can request and detect a framebuffer mode on supported boots without regressing the visible shell
- [x] A minimal framebuffer text surface can render shell output while VGA remains a fallback
- [x] `make qemu-smoke-phase6` stays green after the framebuffer text-shell landing
- [x] `make check` stays green after the framebuffer text-shell landing
- [x] The framebuffer shell now has a framed content panel and more intentional chrome instead of raw text on a flat bitmap
- [x] Lowercase shell/log text now renders as lowercase in framebuffer mode instead of being coerced to uppercase
- [x] Prompt and common `user:` / `elf-` lines now render with distinct framebuffer colors for faster visual scanning
- [x] The framebuffer panel now includes a left gutter rail that scrolls with output and reinforces line-type styling
- [x] The framebuffer panel now behaves as a split console: scrolling transcript above, fixed prompt lane below
- [x] The prompt lane now reads as a real input strip with dedicated label, bounded entry area, and `READY` status badge
- [x] The prompt strip now flips between `READY`, `EDIT`, and `CLIP`, and long prompt input keeps the newest text visible instead of pinning every extra character to the last cell
- [x] The framebuffer shell now rasterizes the existing bootstrap glyph set more densely, improving readability before any larger font-table replacement
- [x] The prompt badge now shows edit state plus elapsed monotonic seconds (`R`/`E`/`C`) instead of a static word-only indicator
- [x] The framebuffer header chrome now renders the ASCII logo, header metrics, and console title with a subtle shadow pass for cleaner contrast

## Notes / decisions
- 2026-02-27 - Phase 5 completed: wait-driven child synchronization, process-owned FD ownership, and deterministic task-stack/loader-scratch reclamation are in place.
- 2026-02-27 - Phase 5 validation used:
  - `make qemu-smoke-phase4-repeat PHASE4_REPEAT=2`
  - `make qemu-smoke-phase5`
  - `make qemu-smoke-userfault`
- 2026-02-27 - Active follow-up milestone is Phase 6 userland workflow + shell bootstrap.
- 2026-02-27 - Phase 6 design note added: `docs/phase6_userland_workflow_design_note.md`.
- 2026-02-27 - Landed Phase 6 bootstrap foundation: shared assembly syscall/runtime includes now back the existing `/bin/hello*.elf` demos, and `make` regenerates `kernel/initramfs_demo_blob.c` from `userland/` sources automatically.
- 2026-02-27 - Validation for the new Phase 6 foundation slice:
  - `make qemu-smoke-phase5`
  - `make qemu-smoke-userfault`
- 2026-02-27 - Chose direct `/bin/sh.elf` bootstrap. The kernel now starts a fixed-slot shell task at boot, and `make qemu-smoke-phase6` asserts the shell banner/prompt path.
- 2026-02-27 - Phase 6 shell bootstrap is now interactive: `/dev/console` input hands off to `user-shell`, the shell runs a prompt/read/dispatch loop, external `/bin/echo.elf` and `/bin/cat.elf` run through `spawn` + `waitpid`, and `SYS_SPAWN_EX` hands children a flat inherited cmdline.
- 2026-02-27 - Validation for the interactive Phase 6 slice:
  - `make qemu-smoke-phase5`
  - `make qemu-smoke-userfault`
  - `make qemu-smoke-phase6`
- 2026-02-27 - Phase 6 completion landed: child launch now inspects ET_EXEC images directly, `uaccess` is task-aware, `/bin/echo.elf` is external, and `make check` now runs the active userfault + Phase 6 smoke path.
- 2026-02-27 - Remaining Phase 6 limitation: child launch is generic for fixed-address ET_EXEC images, but argument handoff is still flat cmdline-only (no argc/argv), stdin stays with `user-shell`, and `ps` is still a stub.
- 2026-02-28 - Post-Phase-6 handover is captured in `docs/next_phase_handover.md`; the recommended next slice is foreground stdin handoff, a minimal `argc/argv` ABI, and a real `ps` path before broader device work.
- 2026-02-28 - Delivered the post-Phase-6 interaction slice: `/dev/console` input ownership is now PID-based, synchronous `waitpid()` temporarily hands stdin to the foreground child, and ownership returns to `user-shell` on completion.
- 2026-02-28 - Spawned user tools now receive a minimal `argc/argv` startup stack built by the kernel launch path; `/bin/echo.elf` and `/bin/cat.elf` now consume `argc/argv` directly instead of `SYS_GETCMDLINE`.
- 2026-02-28 - `ps` is now backed by `SYS_TASK_SNAPSHOT`, a bounded kernel task snapshot syscall used by the shell builtin.
- 2026-02-28 - Added `/bin/readln.elf` as a dedicated stdin handoff smoke tool; `make qemu-smoke-phase6` now checks `readln`, real `ps`, `echo`, `cat`, unknown-command failure, and clean shell exit.
- 2026-02-28 - Validation for the foreground I/O slice:
  - `make all`
  - `make qemu-smoke-phase6`
  - `make check`
- 2026-02-28 - The old post-Phase-6 interaction milestone is complete; the active follow-on is Phase 7, starting with blocking `/dev/console` reads so foreground readers no longer poll from userland.
- 2026-02-28 - Landed the first Phase 7 slice: the owning `/dev/console` task now blocks in the kernel read path, while non-owner reads stay zero-byte and non-blocking; `user-shell` and `/bin/readln.elf` now rely on blocking `read()` directly.
- 2026-02-28 - Validation for the Phase 7 blocking-read slice:
  - `make qemu-smoke-phase6`
  - `make check`
- 2026-02-28 - `SYS_SLEEP` remains available as a generic primitive, but shell stdin waiting no longer depends on userland tick-sleep polling.
- 2026-02-28 - Landed the planned narrow UX follow-up: the shell and `/bin/readln.elf` now treat both `BS` and serial `DEL` as erase-one-char input and redraw the line in place.
- 2026-02-28 - `qemu-smoke-phase6` now injects corrected shell and `readln` input that depends on the new backspace handling, so the smoke path covers the edit behavior instead of only the final commands.
- 2026-02-28 - Phase 7 is now effectively complete; the next logical milestone is to pivot to the queued device/reliability track unless more shell polish is chosen deliberately.
- 2026-02-28 - Started the next device/reliability slice as a narrow timing milestone: the kernel now exposes `SYS_TIME_INFO`, a stable monotonic tick snapshot (`ticks_hi` + `ticks_lo`) plus PIT frequency in Hz.
- 2026-02-28 - Added `/bin/uptime.elf` as a regular external tool so the shell can inspect the monotonic clock path without relying on demo-only logging.
- 2026-02-28 - Added `/bin/sleep.elf` as a second timing tool on top of the existing tick scheduler; it accepts a tick count, calls `SYS_SLEEP`, and reports requested vs observed elapsed ticks.
- 2026-02-28 - `make qemu-smoke-phase6` now runs both `uptime` and `sleep` and asserts their output, keeping the timing ABI and timing utility path on the default shell regression path.
- 2026-02-28 - Validation for the Phase 8 timing slice:
  - `make qemu-smoke-phase6`
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
  - `make qemu-smoke-phase6`
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
