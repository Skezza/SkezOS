# SkezOS

## Overview

Small 32-bit x86 kernel that boots via GRUB Multiboot2. It is mostly C, with a little assembly, and exists to prove you can stitch together something runnable without turning it into a thesis.

### Highlights

- **Build**: `make`  
  Requires `gcc`/`as`/`ld` with `-m32` support, plus `grub-mkrescue`, `xorriso`, `qemu-system-i386`, `tar`, and `od`.
- **Userland**: `make` builds mixed `userland/` assembly/C programs into `/bin/*.elf` and regenerates the built-in initramfs blob.
- **Console**: uses a minimal framebuffer text UI when GRUB provides a direct-RGB framebuffer; otherwise falls back to styled VGA text.
- **Validation**:
  - `make check` — toolchain check, clean build, userfault smoke, shell-core smoke, reliability smoke
  - `make check-release` — `make check` plus lifecycle smoke
  - `make qemu-smoke-userfault` — verifies `/bin/fault.elf` triggers user page-fault recovery and returns to the shell
  - `make qemu-smoke-lifecycle` — verifies wait/spawn/reap and FD open/read/close flow
  - `make qemu-smoke-shell-core` — verifies core shell behavior, command dispatch, `ps`, `ls`, `readln`, `./tool`, multicall dispatch, external `echo`/`cat`, failure handling, and `waitpid`
  - `make qemu-smoke-reliability` — verifies `uptime`, `sleep`, pipes, redirection, and `diag`
- **Run**: `make run` or `qemu-system-i386 -cdrom skezos.iso`
- **Includes**: serial console, keyboard, VGA output, interrupts/IDT, paging, physical allocator, `kmalloc`, panic logging, and page-fault handling
- **Layout**: `boot/` has the Multiboot header/loader, `kernel/` has the sources, `iso/boot/grub/grub.cfg` defines the menu entry, and the `Makefile` builds `skezos.iso`

### Build & run

1. Install the required toolchain and utilities.
2. Run `make` to build userland ELFs, regenerate `kernel/initramfs_demo_blob.c`, build `kernel.elf`, and package `skezos.iso`.
3. Run `make check` for the per-PR validation chain.
4. Run `make check-release` for release/nightly validation.
5. Boot with `make run` or `qemu-system-i386 -cdrom skezos.iso`.

<img width="1124" height="858" alt="Screenshot from 2026-02-13 00-15-09" src="https://github.com/user-attachments/assets/a12490a1-f83a-4b03-827b-6973b0909c65" />

**Framebuffer support**

<img width="1052" height="863" alt="image" src="https://github.com/user-attachments/assets/8d7cdf98-b555-4784-826e-2409a56afc64" />

### Keep tinkering

It is intentionally minimal. The shell is still bootstrap-grade: whitespace parsing, foreground-only execution, real `argc/argv` for spawned tools, blocking `/dev/console` reads, bounded `ps`, basic line editing, and `uptime`/`sleep` via `SYS_TIME_INFO` and the tick scheduler.

Display output now goes through a thin abstraction layer. The kernel can map a higher-half framebuffer window safely, and the default boot path uses a framebuffer text shell when a direct-RGB surface is available, with VGA fallback otherwise. The framebuffer shell has a compact header, a framed content area, color-coded prompts and common line types, a scrolling gutter, a fixed prompt lane, and a footer HUD that keeps core operator-state hints visible. The current font is still a bootstrap font, now rendered on a denser 5x7 grid with full printable-ASCII coverage verification at boot.

It also supports basic external-command pipelines and redirection: `|`, `<`, `>`, `>>`, `2>`, and `2>>`. It still has no quoting, `2>&1`, or background jobs.

Add a better shell, disk drivers, a filesystem, or whatever you want next.

### Project docs

- `technical_considerations.md`
- `project_plan.md`
- `current_milestone.md`
- `docs/kernel_architecture_and_coding_rules.md`
- `docs/kernel_memory_layout.md`
- `docs/phase4_loader_vfs_design_note.md`
- `docs/process_fd_lifecycle_design_note.md`
- `docs/lifecycle_smoke_marker_contract.md`
- `docs/phase6_userland_workflow_design_note.md`

### License

MIT. Fork it, remix it, or break it. See [`LICENSE`](LICENSE:1).
