# SkezOS

## Overview

Tiny 32-bit x86 kernel. Boots through GRUB’s Multiboot2 entry, lives mostly in C, and was assembled with a mix of AI prompts, curiosity, and a bit of stubbornness. The goal? Show that you can cobble together something runnable without needing to write a dissertation.

### Highlights

- **Build**: `make` (you need an `i686-elf` toolchain plus `nasm`, `grub-mkrescue`, `xorriso`).
- **Run**: `make run` or `qemu-system-i386 -cdrom skezos.iso` if you want to keep control of the command line.
- **What’s inside**: serial console, keyboard, VGA output, interrupts/IDT setup, paging, physical allocator, `kmalloc`, panic logging, and the obligatory page fault to show the fault handler works.
- **Layout**: `boot/` holds the Multiboot header/loader, `kernel/` contains the sources, `iso/boot/grub/grub.cfg` houses the menu entry, and `Makefile` ties it all together into `skezos.iso`.

### Build & run (again, because clarity matters)

1. Install the toolchain (`i686-elf` gcc/ld/binutils), `nasm`, `grub-mkrescue`, `xorriso`.
2. `make` – compiles the kernel, links `kernel.elf`, copies it into `iso/boot`, and packages `skezos.iso`.
3. `make run` or `qemu-system-i386 -cdrom skezos.iso` to boot it. Expect boot logs on serial/VGA and a confident page fault at the end.

### Keep tinkering

It’s intentionally minimal. Add a shell, a scheduler, disk drivers, a filesystem, whatever keeps you wired. It’s a creative experiment, so break stuff, fix it, and share what you learn.

### License

MIT License. Fork it, remix it, flame it, or teach your cat to compile it. See [`LICENSE`](LICENSE:1) for the full text.
