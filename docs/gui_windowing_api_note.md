# GUI Windowing API Note

Status: Phase 16 GUI robustness slice, 2026-06-05.

## ABI Additions
- `SYS_GUI_POLL_BATCH` (`27`) copies out up to `SYSCALL_GUI_POLL_BATCH_MAX` queued events for the owning window.
- `SYS_GUI_INFO` (`28`) copies global compositor capability and pressure counters.
- `SYS_GUI_WINDOW_INFO` (`29`) copies owner-visible state for one window.
- `SYSCALL_GUI_INFO_VERSION` is `1`.

## Ownership Contract
- A task may own at most one GUI window.
- `flush`, `poll`, `poll_batch`, `destroy`, and `window_info` require the caller to own the target window.
- Cross-owner access returns `-KERR_NOTSUP` and emits a deterministic `display: gui_owner_denied ...` log for smoke coverage.

## Event Queue Pressure
- Each window has a bounded event queue.
- Mouse move events are lossy under pressure; when the queue is full, a new mouse move is dropped and the per-window/global mouse-drop counters advance.
- Non-mouse events may evict one queued mouse move. If no mouse move can be evicted, the event is dropped as overflow and the per-window/global overflow counters advance.
- Queue counters are exposed through `SYS_GUI_INFO` and `SYS_GUI_WINDOW_INFO`.

## Smoke Coverage
- `/bin/gui_probe.elf` validates the ABI from user mode during GUI boot.
- `qemu-smoke-gui-session` requires the probe self-test marker.
- `qemu-smoke-gui-chaos` drives a focused pressure window, checks owner isolation, checks queue-drop accounting, and verifies the `fb-shell-v6-chaos` framebuffer ROI hash.
