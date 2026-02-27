# SkezOS

## Overview

Tiny 32-bit x86 kernel. Boots through GRUB’s Multiboot2 entry, lives mostly in C, and was assembled with a mix of AI prompts, curiosity, and a bit of stubbornness. The goal? Show that you can cobble together something runnable without needing to write a dissertation.

### Highlights

- **Build**: `make` (you need `gcc`/`as`/`ld` with `-m32` support, plus `grub-mkrescue`, `xorriso`, `qemu-system-i386`, `tar`, and `od`).
- **Userland packaging**: `make` now builds mixed `userland/` assembly/C programs into `/bin/*.elf` and regenerates the built-in initramfs blob automatically.
- **Quick validation**: `make check` (toolchain check + build + headless QEMU smoke boot)
- **User fault smoke**: `make qemu-smoke-userfault` (asserts user-page-fault recovery and continued scheduling)
- **Phase 5 smoke**: `make qemu-smoke-phase5` (asserts wait-driven spawn/reap plus FD open/read/close flow)
- **Phase 6 smoke**: `make qemu-smoke-phase6` (asserts interactive `/bin/sh.elf` command dispatch, builtin `echo`, external `cat`, and `waitpid` completion)
- **Run**: `make run` or `qemu-system-i386 -cdrom skezos.iso` if you want to keep control of the command line.
- **What’s inside**: serial console, keyboard, VGA output, interrupts/IDT setup, paging, physical allocator, `kmalloc`, panic logging, and the obligatory page fault to show the fault handler works.
- **Layout**: `boot/` holds the Multiboot header/loader, `kernel/` contains the sources, `iso/boot/grub/grub.cfg` houses the menu entry, and `Makefile` ties it all together into `skezos.iso`.

### Build & run (again, because clarity matters)

1. Install `gcc`, `as`, and `ld` with 32-bit output support, plus `grub-mkrescue`, `xorriso`, `qemu-system-i386`, `tar`, and `od`.
2. `make` – assembles the `userland/` ELFs, regenerates `kernel/initramfs_demo_blob.c`, compiles the kernel, links `kernel.elf`, copies it into `iso/boot`, and packages `skezos.iso`.
3. `make check` to run a non-interactive boot smoke test (serial marker based).
4. `make qemu-smoke-userfault` to validate the Phase 3 user-fault recovery path.
5. `make qemu-smoke-phase5` to validate Phase 5 lifecycle + FD ownership behavior.
6. `make qemu-smoke-phase6` to validate the interactive bootstrap shell flow (`help`, builtin `echo`, external `cat`, `exit`).
7. `make run` or `qemu-system-i386 -cdrom skezos.iso` to boot it interactively.

<img width="1124" height="858" alt="Screenshot from 2026-02-13 00-15-09" src="https://github.com/user-attachments/assets/a12490a1-f83a-4b03-827b-6973b0909c65" />

### Keep tinkering

It’s intentionally minimal. The current shell is deliberately bootstrap-grade: whitespace parsing only, foreground-only execution, fixed-slot external tools, builtin `echo`, and a stub `ps`. Add argv support, a real process listing interface, a better shell, disk drivers, a filesystem, or whatever keeps you wired.

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
