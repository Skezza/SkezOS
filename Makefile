TARGET = i686-elf
CC     = gcc
AS     = nasm
LD     = ld

# Flags for building a freestanding 32‑bit kernel.  We disable stack
# protector and all floating point support.  We also disable PIE and
# built‑ins so that no assumptions are made about the runtime
# environment.
CFLAGS = -ffreestanding -fno-pic -m32 -O2 -Wall -Wextra \
         -nostdlib -fno-builtin -fno-stack-protector -mno-sse \
         -mno-sse2 -mno-red-zone
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386

# Build directories
BUILD = build
ISO   = tinyos.iso

SRCS  = $(wildcard kernel/*.c)
OBJS  = $(patsubst kernel/%.c,$(BUILD)/%.o,$(SRCS))

ASM_OBJS = $(BUILD)/idt_load.o

all: $(ISO)

$(BUILD)/%.o: kernel/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/multiboot2_header.o: boot/multiboot2_header.S
	@mkdir -p $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/loader.o: boot/loader.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/kernel.elf: $(BUILD)/multiboot2_header.o $(BUILD)/loader.o $(ASM_OBJS) $(OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) -T kernel/linker.ld -o $@ $^

$(ISO): $(BUILD)/kernel.elf iso/boot/grub/grub.cfg
	@mkdir -p iso/boot
	cp $(BUILD)/kernel.elf iso/boot/kernel.elf
	grub-mkrescue -o $(ISO) iso > /dev/null 2>&1

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO)

clean:
	rm -rf $(BUILD) $(ISO) iso/boot/kernel.elf

# Build object from assembly in the kernel directory
$(BUILD)/idt_load.o: kernel/idt_load.S
	$(AS) $(ASFLAGS) $< -o $@
