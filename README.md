# SkezOS

## Overview

Tiny 32-bit x86 kernel. Boots through GRUB’s Multiboot2 entry, lives mostly in C, and was assembled with a mix of AI prompts, curiosity, and a bit of stubbornness. The goal? Show that you can cobble together something runnable without needing to write a dissertation.

### Highlights

- **Build**: `make` (you need `gcc`/`as`/`ld` with `-m32` support, plus `grub-mkrescue`, `xorriso`, `qemu-system-i386`, `tar`, and `od`).
- **Userland packaging**: `make` now builds mixed `userland/` assembly/C programs into `/bin/*.elf` and regenerates the built-in initramfs blob automatically.
- **Console behavior**: when GRUB can provide a direct-RGB framebuffer, the shell now renders on a minimal framebuffer text surface; otherwise it falls back to the styled VGA text shell. Background worker demo logs still stay quiet once the shell owns the console.
- **Quick validation**: `make check` (toolchain check + clean build + userfault recovery + Phase 6 shell smoke)
- **User fault smoke**: `make qemu-smoke-userfault` (asserts user-page-fault recovery and continued scheduling)
- **Phase 5 smoke**: `make qemu-smoke-phase5` (asserts wait-driven spawn/reap plus FD open/read/close flow)
- **Phase 6 smoke**: `make qemu-smoke-phase6` (asserts interactive `/bin/sh.elf` command dispatch, foreground stdin handoff via `readln`, real `ps`, external `uptime`/`sleep`/`echo`/`cat`, unknown-command failure handling, and `waitpid` completion)
- **Run**: `make run` or `qemu-system-i386 -cdrom skezos.iso` if you want to keep control of the command line.
- **What’s inside**: serial console, keyboard, VGA output, interrupts/IDT setup, paging, physical allocator, `kmalloc`, panic logging, and the obligatory page fault to show the fault handler works.
- **Layout**: `boot/` holds the Multiboot header/loader, `kernel/` contains the sources, `iso/boot/grub/grub.cfg` houses the menu entry, and `Makefile` ties it all together into `skezos.iso`.

### Build & run (again, because clarity matters)

1. Install `gcc`, `as`, and `ld` with 32-bit output support, plus `grub-mkrescue`, `xorriso`, `qemu-system-i386`, `tar`, and `od`.
2. `make` – assembles the `userland/` ELFs, regenerates `kernel/initramfs_demo_blob.c`, compiles the kernel, links `kernel.elf`, copies it into `iso/boot`, and packages `skezos.iso`.
3. `make check` to run the default validation chain (toolchain check, clean rebuild, `qemu-smoke-userfault`, then `qemu-smoke-phase6`).
4. `make qemu-smoke-userfault` to validate the Phase 3 user-fault recovery path.
5. `make qemu-smoke-phase5` to validate Phase 5 lifecycle + FD ownership behavior.
6. `make qemu-smoke-phase6` to validate the interactive bootstrap shell flow (`help`, `readln`, `ps`, `uptime`, `sleep`, external `echo`, external `cat`, unknown-command failure, `exit`).
7. `make run` or `qemu-system-i386 -cdrom skezos.iso` to boot it interactively.

<img width="1124" height="858" alt="Screenshot from 2026-02-13 00-15-09" src="https://github.com/user-attachments/assets/a12490a1-f83a-4b03-827b-6973b0909c65" />

### Keep tinkering

It’s intentionally minimal. The current shell is still bootstrap-grade: whitespace parsing only, foreground-only execution, real `argc/argv` startup for spawned tools, foreground stdin handoff only for synchronous children, a bounded kernel-backed `ps` snapshot, basic backspace erase/editing, and monotonic `uptime`/`sleep` commands backed by `SYS_TIME_INFO` plus the existing tick scheduler. Owning-task `/dev/console` reads now block in-kernel, so shell tools no longer retry on empty stdin from userland. The display work has moved past scaffolding: the active console path runs through a thin display abstraction, the kernel can map a higher-half framebuffer window safely, and the default boot path now uses a minimal framebuffer text shell when a direct-RGB pixel surface is available while keeping styled VGA fallback. The framebuffer shell now has a framed content panel, stronger chrome, distinct lowercase rendering, lightweight color-coding for prompts plus common `user:` / `elf-` lines, a scrolling left gutter rail that reinforces those line types, and a fixed prompt lane below the scrolling transcript that now reads like a labeled input strip with a bounded entry runway, a compact live mode-plus-seconds badge (`R`/`E`/`C` + elapsed monotonic seconds), and trailing-window prompt clipping. The current font is still a bootstrap font, but it now rasterizes onto a denser 5x7 grid instead of the earlier coarse 3x5 presentation, so it reads materially better while a larger replacement font remains future work. It still has no quoting, pipes, redirection, or background jobs. Add a better shell, disk drivers, a filesystem, or whatever keeps you wired.

### Project docs

- `technical_considerations.md`
- `project_plan.md`
- `current_milestone.md`
- `docs/kernel_architecture_and_coding_rules.md`
- `docs/kernel_memory_layout.md`
- `docs/phase4_loader_vfs_design_note.md`
- `docs/phase5_process_fd_lifecycle_design_note.md`
- `docs/phase6_userland_workflow_design_note.md`

### License

MIT License. Fork it, remix it, flame it, or teach your cat to compile it. See [`LICENSE`](LICENSE:1) for the full text.
