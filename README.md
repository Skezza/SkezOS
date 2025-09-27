# TinyOS

## Overview

TinyOS is a hobby operating system for the 32-bit x86 architecture. It uses the Multiboot2 specification to boot via GRUB and is written primarily in C with small assembly stubs.

### Features

- **Bootloader integration**: A Multiboot2 header and a simple assembly stub allow GRUB to load the kernel, so there is no custom bootloader.
- **Interrupts and the IDT**: The kernel sets up an Interrupt Descriptor Table (IDT) and remaps the Programmable Interrupt Controller (PIC). It includes default handlers for CPU exceptions and hardware interrupts, as well as a page-fault handler.
- **Paging and virtual memory**: Paging is enabled and the lower 16 MiB of memory is identity-mapped. The kernel runs in the higher-half (0xC0000000), with helper functions to translate between physical and virtual addresses and handle page faults.
- **Physical memory manager (PMM)**: A bitmap-based allocator manages free 4 KiB frames based on the memory map provided by the bootloader. Reserved regions and the kernel image are skipped.
- **Dynamic memory allocator**: A simple bump allocator (kmalloc) provides dynamic memory allocation within the kernel.
- **Device drivers**: Drivers are provided for the 8250 serial port (COM1), PS/2 keyboard, programmable interval timer (PIT), and VGA text mode.
- **Logging and panic handling**: Kernel logs are sent to the serial port and VGA console. A panic function displays an error message and halts the CPU.
- **Multiboot2 memory map parsing**: The loader passes the Multiboot magic and a pointer to the Multiboot info. The kernel parses this structure to build the PMM.

### Directory structure

- `boot/` – Assembly stubs and the Multiboot2 header. `multiboot2_header.S` defines GRUB magic constants and `loader.S` sets up a temporary stack and calls `kmain`.
- `kernel/` – C and assembly sources for the kernel: IDT (`idt.c/h`, `idt_load.S`), interrupts (`interrupts.c/h`), paging (`paging.c/h`), physical memory management (`pmm.c/h`), dynamic allocation (`kmalloc.c/h`), memory map parsing (`memmap.c/h`), PIC remapping (`pic.c/h`), serial port (`serial.c/h`), timer (`timer.c/h`), keyboard (`keyboard.c/h`), utils (`utils.c/h`), panic handling (`panic.c/h`), VGA text output (`vga.c/h`) and more. `kernel.c` contains the `kmain` entry point that initializes these subsystems and triggers a test page fault.
- `kernel/linker.ld` – Linker script that places the kernel at 1 MiB, defines section layout, and aligns sections on page boundaries.
- `iso/boot/grub/grub.cfg` – GRUB configuration used when building a bootable ISO. It defines a menu entry for TinyOS and loads `kernel.elf`.
- `Makefile` – Build script that assembles and compiles sources, links the ELF kernel, and packages it into a bootable ISO via `grub-mkrescue`. A `run` target launches QEMU.

### Build and run

You need a cross-compiler targeting `i686-elf` because a host GCC (targeted at Linux) cannot correctly produce a bare-metal kernel. GRUB only loads 32-bit Multiboot kernels, so build for 32-bit even on 64-bit systems. Required tools include `nasm` or `gas`, `ld`, `gcc`, `grub-mkrescue`, `xorriso` and `qemu-system-i386`.

Steps to build and run:

1. **Install dependencies** (example on Debian/Ubuntu):

   ```bash
   sudo apt-get install build-essential nasm xorriso grub-pc-bin qemu-system-x86
   ```

2. **Clone the repository**:

   ```bash
   git clone https://github.com/Skezza/Tinyos
   cd Tinyos
   ```

3. **Build the kernel and ISO**:

   ```bash
   make
   ```

   This compiles all sources, links `kernel.elf`, copies it into the `iso` directory and creates `tinyos.iso` using `grub-mkrescue`.

4. **Run in QEMU**:

   ```bash
   make run
   ```

   QEMU will boot the ISO with `-cdrom tinyos.iso`. You can also run QEMU manually, for example:

   ```bash
   qemu-system-i386 -cdrom tinyos.iso
   ```

   QEMU can also boot the ELF kernel directly using `-kernel` if you prefer.

5. When QEMU boots, the kernel prints initialization messages. After setup it intentionally triggers a page fault to demonstrate the fault handler.

### Extending TinyOS

TinyOS is deliberately minimal but designed as a foundation. Possible improvements include:

- Implementing a more advanced memory allocator (e.g., buddy allocator).
- Adding multitasking and a scheduler.
- Developing a file system and disk drivers (ATA/AHCI).
- Implementing user mode and a system call interface.
- Improving the keyboard driver and adding a basic shell.
- Porting the kernel to x86-64 or other architectures.

### Contributing

Contributions are welcome! Fork the repository, create a feature branch and open a pull request. Follow the existing coding style (e.g., no dynamic memory allocation in interrupt context, careful use of inline assembly) and document your changes.

### License

Specify your preferred license here (e.g., MIT, BSD or Apache-2.0).
