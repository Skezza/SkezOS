# Repository Guidelines

## Project Structure & Module Organization
- `boot/`: Multiboot2 header and early loader assembly.
- `kernel/`: 32-bit x86 kernel code (memory, scheduler, syscalls, VFS, display, drivers).
- `userland/`: tiny ELF programs (`*_slotN.c/.S`) packaged into initramfs as `/bin/*.elf`.
- `scripts/`: smoke helpers, reliability parsers, framebuffer capture/baseline tools.
- `docs/`: design notes and contracts (architecture, lifecycle markers, visual regression strategy).
- `build/` and `skezos.iso`: generated artifacts (do not commit).

## Build, Test, and Development Commands
- `make toolchain-check`: verify required tools (`gcc/as/ld`, `grub-mkrescue`, `qemu-system-i386`, etc.).
- `make`: build kernel, userland ELFs, initramfs blob, and `skezos.iso`.
- `make run`: boot interactively in QEMU (GTK + serial console).
- `make check`: baseline validation chain (build + core smokes).
- `make check-release`: `check` plus lifecycle coverage.
- `make check-nightly`: full matrix (replay, fuzz-lite, storage, fork/COW, GUI visual baseline).
- Targeted examples: `make qemu-smoke-shell-core`, `make qemu-smoke-storage-persist`.

## Coding Style & Naming Conventions
- Language mix: C + x86 assembly; use 4-space indentation and keep lines readable.
- Prefer `snake_case` for functions/variables, `UPPER_SNAKE_CASE` for macros/constants.
- Kernel APIs return `0` on success and `-KERR_*` on failure (`kernel/kerrno.h`).
- Use `KLOGD/KLOGI/KLOGW/KLOGP` for diagnostics; keep logs deterministic for smoke parsing.
- Keep changes scoped: avoid mixing behavior changes, refactors, and formatting churn.

## Testing Guidelines
- Tests are Make/QEMU smoke targets with log assertions (no separate unit-test framework).
- For kernel, scheduler, syscall, VFS, or memory changes, run at least `make check`.
- For cross-subsystem or release-critical changes, run `make check-release` or `make check-nightly`.
- Include exact commands and PASS/fail evidence in PR descriptions.

## Commit & Pull Request Guidelines
- Follow existing history style: short, imperative subjects (e.g., `Harden ...`, `Update ...`, `gui: ...`, `ci: ...`).
- Keep commits logically grouped; prefer implementation and docs in separate commits.
- PRs should include:
  - What changed and why.
  - Risk/rollback notes.
  - Test commands run and outcomes.
  - For framebuffer/UI changes, attach relevant artifact paths or screenshots.
