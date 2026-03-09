TARGET = i686-elf
CC     = gcc
AS     = as
LD     = ld
QEMU   ?= qemu-system-i386

# Flags for building a freestanding 32‑bit kernel.  We disable stack
# protector and all floating point support.  We also disable PIE and
# built‑ins so that no assumptions are made about the runtime
# environment.
 CFLAGS = -ffreestanding -fno-pic -m32 -O2 -Wall -Wextra \
          -nostdlib -fno-builtin -fno-stack-protector -mno-sse \
          -mno-sse2 -mno-red-zone -mno-80387
ASFLAGS = --32
LDFLAGS = -m elf_i386

# Build directories
BUILD = build
ISO   = skezos.iso
SMOKE_LOG = $(BUILD)/qemu-smoke.log
LIFECYCLE_SMOKE_LOG = $(BUILD)/qemu-smoke-lifecycle.log
SMOKE_TIMEOUT_SECS ?= 18
SHELL_SMOKE_TIMEOUT_SECS ?= 35
SMOKE_MARKER = SKEZOS_SMOKE_READY
PHASE4_REPEAT ?= 3
USERLAND_BUILD = $(BUILD)/userland
INITRAMFS_STAGING = $(BUILD)/initramfs_root
INITRAMFS_TAR = $(BUILD)/initramfs_demo.tar
USERLAND_ASFLAGS = $(ASFLAGS) -I userland
USERLAND_CFLAGS = $(CFLAGS) -I userland
USERLAND_LDFLAGS = $(LDFLAGS) -nostdlib -N -e _start

SRCS  = $(wildcard kernel/*.c)
OBJS  = $(patsubst kernel/%.c,$(BUILD)/%.o,$(SRCS))

ASM_OBJS = $(BUILD)/idt_load.o $(BUILD)/sched_switch.o $(BUILD)/gdt_flush.o $(BUILD)/syscall_entry.o $(BUILD)/user_demo_blob.o

USERLAND_OBJS = \
	$(USERLAND_BUILD)/hello_slot0.o \
	$(USERLAND_BUILD)/hello_slot1.o \
	$(USERLAND_BUILD)/hello_slot2.o \
	$(USERLAND_BUILD)/hello_slot3.o \
	$(USERLAND_BUILD)/sh_slot4.o \
	$(USERLAND_BUILD)/cat_slot5.o \
	$(USERLAND_BUILD)/echo_slot6.o \
	$(USERLAND_BUILD)/readln_slot7.o \
	$(USERLAND_BUILD)/uptime_slot8.o \
	$(USERLAND_BUILD)/sleep_slot9.o \
	$(USERLAND_BUILD)/diag_slot10.o \
	$(USERLAND_BUILD)/fault_slot11.o \
	$(USERLAND_BUILD)/pwd_slot12.o \
	$(USERLAND_BUILD)/ls_slot13.o \
	$(USERLAND_BUILD)/busybox_slot14.o

USERLAND_ELFS = \
	$(USERLAND_BUILD)/hello.elf \
	$(USERLAND_BUILD)/hello2.elf \
	$(USERLAND_BUILD)/hello3.elf \
	$(USERLAND_BUILD)/hello4.elf \
	$(USERLAND_BUILD)/sh.elf \
	$(USERLAND_BUILD)/cat.elf \
	$(USERLAND_BUILD)/echo.elf \
	$(USERLAND_BUILD)/readln.elf \
	$(USERLAND_BUILD)/uptime.elf \
	$(USERLAND_BUILD)/sleep.elf \
	$(USERLAND_BUILD)/diag.elf \
	$(USERLAND_BUILD)/fault.elf \
	$(USERLAND_BUILD)/pwd.elf \
	$(USERLAND_BUILD)/ls.elf \
	$(USERLAND_BUILD)/busybox.elf

.PHONY: all run clean toolchain-check qemu-smoke qemu-smoke-userfault qemu-smoke-phase4 qemu-smoke-phase4-repeat qemu-smoke-lifecycle qemu-smoke-shell-core qemu-smoke-reliability check check-pr check-release check-nightly

all: $(ISO)

$(BUILD)/%.o: kernel/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/multiboot2_header.o: boot/multiboot2_header.S
	@mkdir -p $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/loader.o: boot/loader.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/kernel.elf: $(BUILD)/multiboot2_header.o $(BUILD)/loader.o $(ASM_OBJS) $(OBJS)
	$(LD) $(LDFLAGS) -T kernel/linker.ld -o $@ $^

$(BUILD)/initramfs_demo_blob.o: kernel/initramfs_demo_blob.c

$(USERLAND_BUILD)/%.o: userland/%.S userland/runtime.inc userland/syscall_abi.inc
	@mkdir -p $(USERLAND_BUILD)
	$(AS) $(USERLAND_ASFLAGS) $< -o $@

$(USERLAND_BUILD)/%.o: userland/%.c userland/userlib.h
	@mkdir -p $(USERLAND_BUILD)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(USERLAND_BUILD)/hello.elf: $(USERLAND_BUILD)/hello_slot0.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01410000 -o $@ $<

$(USERLAND_BUILD)/hello2.elf: $(USERLAND_BUILD)/hello_slot1.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01422000 -o $@ $<

$(USERLAND_BUILD)/hello3.elf: $(USERLAND_BUILD)/hello_slot2.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01434000 -o $@ $<

$(USERLAND_BUILD)/hello4.elf: $(USERLAND_BUILD)/hello_slot3.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01445000 -o $@ $<

$(USERLAND_BUILD)/sh.elf: $(USERLAND_BUILD)/sh_slot4.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01456000 -o $@ $<

$(USERLAND_BUILD)/cat.elf: $(USERLAND_BUILD)/cat_slot5.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01467000 -o $@ $<

$(USERLAND_BUILD)/echo.elf: $(USERLAND_BUILD)/echo_slot6.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01478000 -o $@ $<

$(USERLAND_BUILD)/readln.elf: $(USERLAND_BUILD)/readln_slot7.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01489000 -o $@ $<

$(USERLAND_BUILD)/uptime.elf: $(USERLAND_BUILD)/uptime_slot8.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x0149A000 -o $@ $<

$(USERLAND_BUILD)/sleep.elf: $(USERLAND_BUILD)/sleep_slot9.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x014AB000 -o $@ $<

$(USERLAND_BUILD)/diag.elf: $(USERLAND_BUILD)/diag_slot10.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x014BC000 -o $@ $<

$(USERLAND_BUILD)/fault.elf: $(USERLAND_BUILD)/fault_slot11.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x014CD000 -o $@ $<

$(USERLAND_BUILD)/pwd.elf: $(USERLAND_BUILD)/pwd_slot12.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x014DE000 -o $@ $<

$(USERLAND_BUILD)/ls.elf: $(USERLAND_BUILD)/ls_slot13.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x014EF000 -o $@ $<

$(USERLAND_BUILD)/busybox.elf: $(USERLAND_BUILD)/busybox_slot14.o
	$(LD) $(USERLAND_LDFLAGS) -Ttext 0x01500000 -o $@ $<

$(INITRAMFS_STAGING)/.stamp: $(USERLAND_ELFS) userland/readme.txt
	@rm -rf $(INITRAMFS_STAGING)
	@mkdir -p $(INITRAMFS_STAGING)/bin
	cp $(USERLAND_BUILD)/hello.elf $(INITRAMFS_STAGING)/bin/hello.elf
	cp $(USERLAND_BUILD)/hello2.elf $(INITRAMFS_STAGING)/bin/hello2.elf
	cp $(USERLAND_BUILD)/hello3.elf $(INITRAMFS_STAGING)/bin/hello3.elf
	cp $(USERLAND_BUILD)/hello4.elf $(INITRAMFS_STAGING)/bin/hello4.elf
	cp $(USERLAND_BUILD)/sh.elf $(INITRAMFS_STAGING)/bin/sh.elf
	cp $(USERLAND_BUILD)/cat.elf $(INITRAMFS_STAGING)/bin/cat.elf
	cp $(USERLAND_BUILD)/echo.elf $(INITRAMFS_STAGING)/bin/echo.elf
	cp $(USERLAND_BUILD)/readln.elf $(INITRAMFS_STAGING)/bin/readln.elf
	cp $(USERLAND_BUILD)/uptime.elf $(INITRAMFS_STAGING)/bin/uptime.elf
	cp $(USERLAND_BUILD)/sleep.elf $(INITRAMFS_STAGING)/bin/sleep.elf
	cp $(USERLAND_BUILD)/diag.elf $(INITRAMFS_STAGING)/bin/diag.elf
	cp $(USERLAND_BUILD)/fault.elf $(INITRAMFS_STAGING)/bin/fault.elf
	cp $(USERLAND_BUILD)/pwd.elf $(INITRAMFS_STAGING)/bin/pwd.elf
	cp $(USERLAND_BUILD)/ls.elf $(INITRAMFS_STAGING)/bin/ls.elf
	cp $(USERLAND_BUILD)/busybox.elf $(INITRAMFS_STAGING)/bin/busybox.elf
	cp userland/readme.txt $(INITRAMFS_STAGING)/bin/readme.txt
	@touch $@

$(INITRAMFS_TAR): $(INITRAMFS_STAGING)/.stamp
	cd $(INITRAMFS_STAGING) && tar --format=ustar --owner=0 --group=0 --numeric-owner -cf ../$(notdir $(INITRAMFS_TAR)) bin

kernel/initramfs_demo_blob.c: $(INITRAMFS_TAR) scripts/generate_initramfs_blob.sh kernel/initramfs_demo_blob.h
	./scripts/generate_initramfs_blob.sh $(INITRAMFS_TAR) $@

$(ISO): $(BUILD)/kernel.elf iso/boot/grub/grub.cfg
	@mkdir -p iso/boot
	cp $(BUILD)/kernel.elf iso/boot/kernel.elf
	grub-mkrescue -o $(ISO) iso > /dev/null 2>&1

run: $(ISO)
	# Runs QEMU with graphical UI (GTK) and forward COM1 to this terminal.
	# Using "mon:stdio" keeps stdin input reliable when piped input at launch.
	$(QEMU) \
		-cdrom $(ISO) \
		-display gtk \
		-serial mon:stdio \
		-monitor none \

toolchain-check:
	@missing=0; \
	for tool in $(CC) $(AS) $(LD) grub-mkrescue xorriso $(QEMU) timeout tar od; do \
		if ! command -v $$tool >/dev/null 2>&1; then \
			echo "Missing required tool: $$tool"; \
			missing=1; \
		fi; \
	done; \
	if [ $$missing -ne 0 ]; then \
		echo "Install missing tools and rerun."; \
		exit 1; \
	fi

qemu-smoke: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(SMOKE_LOG)
	@echo "[qemu-smoke] Booting headless VM for up to $(SMOKE_TIMEOUT_SECS)s..."
	@rc=0; \
	timeout -s INT -k 2s $(SHELL_SMOKE_TIMEOUT_SECS)s $(QEMU) \
		-cdrom $(ISO) \
		-display none \
		-serial file:$(SMOKE_LOG) \
		-monitor none \
		-no-reboot \
		-no-shutdown >/dev/null 2>&1 || rc=$$?; \
	if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then \
		echo "[qemu-smoke] QEMU failed (exit $$rc)."; \
		exit $$rc; \
	fi; \
	if ! grep -Fq "$(SMOKE_MARKER)" $(SMOKE_LOG); then \
		echo "[qemu-smoke] Missing readiness marker: $(SMOKE_MARKER)"; \
		if [ -f $(SMOKE_LOG) ]; then tail -n 40 $(SMOKE_LOG); fi; \
		exit 1; \
	fi; \
	echo "[qemu-smoke] PASS"

qemu-smoke-userfault: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-userfault.log
	@echo "[qemu-smoke-userfault] Booting headless VM with scripted fault trigger..."
	@rc=0; \
	{ sleep 9; \
		printf 'fault\n'; \
		sleep 0.25; \
		printf 'exit\n'; \
	} | \
	timeout -s INT -k 2s $(SHELL_SMOKE_TIMEOUT_SECS)s $(QEMU) \
		-cdrom $(ISO) \
		-display none \
		-serial mon:stdio \
		-monitor none \
		-no-reboot \
		-no-shutdown > $(BUILD)/qemu-smoke-userfault.log 2>&1 || rc=$$?; \
	if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then \
		echo "[qemu-smoke-userfault] QEMU failed (exit $$rc)."; \
		exit $$rc; \
	fi
	@if ! grep -Fq "$(SMOKE_MARKER)" $(BUILD)/qemu-smoke-userfault.log; then \
		echo "[qemu-smoke-userfault] Missing readiness marker: $(SMOKE_MARKER)"; \
		tail -n 120 $(BUILD)/qemu-smoke-userfault.log; \
		exit 1; \
	fi
	@if ! grep -Fq "fault: triggering user page fault" $(BUILD)/qemu-smoke-userfault.log; then \
		echo "[qemu-smoke-userfault] Missing fault trigger output"; \
		tail -n 120 $(BUILD)/qemu-smoke-userfault.log; \
		exit 1; \
	fi
	@if ! grep -Fq "user page fault:" $(BUILD)/qemu-smoke-userfault.log; then \
		echo "[qemu-smoke-userfault] Missing user page fault log"; \
		tail -n 120 $(BUILD)/qemu-smoke-userfault.log; \
		exit 1; \
	fi
	@if ! grep -Fq "fault recovery: terminating user task pid=" $(BUILD)/qemu-smoke-userfault.log; then \
		echo "[qemu-smoke-userfault] Missing task termination log"; \
		tail -n 120 $(BUILD)/qemu-smoke-userfault.log; \
		exit 1; \
	fi
	@if ! grep -Fq "task=user-shell waited_pid=" $(BUILD)/qemu-smoke-userfault.log; then \
		echo "[qemu-smoke-userfault] Missing shell waitpid recovery log"; \
		tail -n 160 $(BUILD)/qemu-smoke-userfault.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-userfault] PASS"

qemu-smoke-phase4:
	@$(MAKE) qemu-smoke SMOKE_LOG=$(BUILD)/qemu-smoke-phase4.log
	@if ! grep -Fq "tarfs: self-check pass (/bin/readme.txt)" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing tarfs self-check log"; \
		tail -n 120 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-b: sys_read stdin -> 0 bytes" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing SYS_READ smoke log"; \
		tail -n 120 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-b: sys_spawn /bin/hello3.elf ok" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing spawn hello3 success log"; \
		tail -n 120 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-b: sys_spawn /bin/hello4.elf ok" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing spawn hello4 success log"; \
		tail -n 120 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-b: sys_spawn /bin/hello3.elf reuse ok" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing spawn-slot reuse success log"; \
		tail -n 160 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-c: spawned via sys_spawn" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing spawned child C log"; \
		tail -n 120 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'elf-c: spawned via sys_spawn' $(BUILD)/qemu-smoke-phase4.log)" -lt 2 ]; then \
		echo "[qemu-smoke-phase4] Expected elf-c to spawn at least twice (slot reuse)"; \
		tail -n 180 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-d: spawned via sys_spawn" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing spawned child D log"; \
		tail -n 120 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sys_open:" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing SYS_OPEN kernel log"; \
		tail -n 140 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-d: readme: SkezOS tarfs demo" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing open/read file-content smoke log"; \
		tail -n 140 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if ! grep -Fq "elf-d: close fd ok" $(BUILD)/qemu-smoke-phase4.log; then \
		echo "[qemu-smoke-phase4] Missing SYS_CLOSE smoke log"; \
		tail -n 140 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'usermode: released spawn slot task=user-spawn-c' $(BUILD)/qemu-smoke-phase4.log)" -lt 2 ]; then \
		echo "[qemu-smoke-phase4] Expected user-spawn-c slot release at least twice"; \
		tail -n 200 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'syscall: stdio fd bind pid=5 task=user-demo fd=1 path=/dev/console' $(BUILD)/qemu-smoke-phase4.log)" -ne 1 ]; then \
		echo "[qemu-smoke-phase4] Unexpected user-demo stdio bind count (fd cache regression?)"; \
		tail -n 160 $(BUILD)/qemu-smoke-phase4.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-phase4] PASS"

qemu-smoke-phase4-repeat:
	@i=1; \
	while [ $$i -le $(PHASE4_REPEAT) ]; do \
		echo "[qemu-smoke-phase4-repeat] Run $$i/$(PHASE4_REPEAT)"; \
		$(MAKE) qemu-smoke-phase4 || exit $$?; \
		i=$$((i + 1)); \
	done; \
	echo "[qemu-smoke-phase4-repeat] PASS ($(PHASE4_REPEAT) runs)"

qemu-smoke-lifecycle: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(LIFECYCLE_SMOKE_LOG)
	@echo "[qemu-smoke-lifecycle] Booting headless VM with scripted lifecycle input..."
	$(call RUN_SCRIPTED_SHELL_SMOKE,qemu-smoke-lifecycle,$(LIFECYCLE_SMOKE_LOG),$(LIFECYCLE_SMOKE_SCRIPT))
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-lifecycle,$(LIFECYCLE_SMOKE_LOG))
	@if ! grep -Fq "SMOKE_LIFECYCLE_SPAWN_HELLO3_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing spawn hello3 marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_SPAWN_HELLO4_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing spawn hello4 marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_SPAWN_HELLO3_REUSE_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing spawn reuse marker"; \
		tail -n 200 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_WAIT_HELLO3_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing waitpid hello3 marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_WAIT_HELLO4_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing waitpid hello4 marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_WAIT_HELLO3_REUSE_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing waitpid reuse marker"; \
		tail -n 200 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_FD_OPEN_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing fd open marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_FD_READ_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing fd read marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_FD_CLOSE_OK" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing fd close marker"; \
		tail -n 180 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'SMOKE_LIFECYCLE_WAIT_REAP' $(LIFECYCLE_SMOKE_LOG))" -lt 3 ]; then \
		echo "[qemu-smoke-lifecycle] Expected at least three wait/reap kernel markers"; \
		tail -n 220 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'SMOKE_LIFECYCLE_ELF_SCRATCH_RECLAIM' $(LIFECYCLE_SMOKE_LOG))" -lt 5 ]; then \
		echo "[qemu-smoke-lifecycle] Expected scratch reclamation markers for all ELF loads"; \
		tail -n 220 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_STACK_RECLAIM" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing waited-child stack reclaim marker"; \
		tail -n 220 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "SMOKE_LIFECYCLE_STACK_DEFERRED live_large=" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing final deferred stack reclaim watermark"; \
		tail -n 240 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@if ! grep -Fq "sh: exit" $(LIFECYCLE_SMOKE_LOG); then \
		echo "[qemu-smoke-lifecycle] Missing shell exit line"; \
		tail -n 200 $(LIFECYCLE_SMOKE_LOG); \
		exit 1; \
	fi
	@echo "[qemu-smoke-lifecycle] PASS"

define LIFECYCLE_SMOKE_SCRIPT
printf 'cd /bin\n'; \
sleep 0.25; \
printf './hello2.elf\n'; \
sleep 0.25; \
printf '\n'; \
sleep 4.00; \
printf 'exit\n';
endef

define SHELL_CORE_SMOKE_SCRIPT
printf 'pwd\n'; \
sleep 0.25; \
printf 'cd /bin\n'; \
sleep 0.25; \
printf 'pwd\n'; \
sleep 0.25; \
printf 'ls\n'; \
sleep 0.25; \
printf './busybox echo via busybox\n'; \
sleep 0.25; \
printf './busybox pwd\n'; \
sleep 0.25; \
printf 'readlq\177n\n'; \
sleep 0.75; \
printf 'stdin handoff worzz\b\bks\n'; \
sleep 0.25; \
printf 'ps\n'; \
sleep 0.25; \
printf 'echo shell core interactive echo\n'; \
sleep 0.25; \
printf 'cat readme.txt\n'; \
sleep 0.25; \
printf 'missing\n'; \
sleep 0.25; \
printf 'exit\n';
endef

define RELIABILITY_SMOKE_SCRIPT
printf 'cd /\n'; \
sleep 0.25; \
printf 'echo hello > test.txt\n'; \
sleep 0.25; \
printf 'cat /test.txt\n'; \
sleep 0.25; \
printf 'cd /bin\n'; \
sleep 0.25; \
printf 'uptime\n'; \
sleep 0.25; \
printf 'sleep 2\n'; \
sleep 0.25; \
printf 'echo pipeline smoke | cat\n'; \
sleep 0.25; \
printf 'cat < readme.txt | cat | cat\n'; \
sleep 0.25; \
printf 'echo drop-me > /dev/null\n'; \
sleep 0.25; \
printf 'echo reliability-v1 > /reliability.txt\n'; \
sleep 0.25; \
printf 'echo reliability-v2 >> /reliability.txt\n'; \
sleep 0.25; \
printf 'cat /reliability.txt\n'; \
sleep 0.25; \
printf 'echo should-fail > /bin/readme.txt\n'; \
sleep 0.25; \
printf 'pwd | cat\n'; \
sleep 0.25; \
printf 'diag\n'; \
sleep 0.25; \
printf 'exit\n';
endef

define RUN_SCRIPTED_SHELL_SMOKE
@rc=0; \
{ sleep 12; \
$(3) \
} | \
timeout -s INT -k 2s $(SHELL_SMOKE_TIMEOUT_SECS)s $(QEMU) \
	-cdrom $(ISO) \
	-display none \
	-serial mon:stdio \
	-monitor none \
	-no-reboot \
	-no-shutdown > $(2) 2>&1 || rc=$$?; \
if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then \
	echo "[$(1)] QEMU failed (exit $$rc)."; \
	exit $$rc; \
fi
endef

define ASSERT_SHELL_BOOT_READY
@if ! grep -Fq "$(SMOKE_MARKER)" $(2); then \
	echo "[$(1)] Missing readiness marker: $(SMOKE_MARKER)"; \
	tail -n 220 $(2); \
	exit 1; \
fi
@if ! grep -Fq "SkezOS shell ready" $(2); then \
	echo "[$(1)] Missing shell bootstrap banner"; \
	tail -n 220 $(2); \
	exit 1; \
fi
endef

qemu-smoke-shell-core: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-shell-core.log
	@echo "[qemu-smoke-shell-core] Booting headless VM with scripted shell input..."
	$(call RUN_SCRIPTED_SHELL_SMOKE,qemu-smoke-shell-core,$(BUILD)/qemu-smoke-shell-core.log,$(SHELL_CORE_SMOKE_SCRIPT))
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-shell-core,$(BUILD)/qemu-smoke-shell-core.log)
	@if ! awk 'BEGIN{in_shell=0;bad=0} /SkezOS shell ready/{in_shell=1} /sh: exit/{in_shell=0} in_shell && /sched demo:/{bad=1} END{exit bad ? 1 : 0}' $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Unexpected worker demo log spam while shell was active"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fo 'sh> ' $(BUILD)/qemu-smoke-shell-core.log | wc -l)" -lt 11 ]; then \
		echo "[qemu-smoke-shell-core] Missing repeated shell prompts"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "/"; then \
		echo "[qemu-smoke-shell-core] Missing root pwd output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxq "/bin"; then \
		echo "[qemu-smoke-shell-core] Missing changed pwd output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "busybox.elf" $(BUILD)/qemu-smoke-shell-core.log || ! grep -Fq "readme.txt" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing ls directory output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'via busybox' $(BUILD)/qemu-smoke-shell-core.log)" -lt 2 ]; then \
		echo "[qemu-smoke-shell-core] Missing busybox multicall output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(tr -d '\r' < $(BUILD)/qemu-smoke-shell-core.log | grep -Fxc '/bin')" -lt 2 ]; then \
		echo "[qemu-smoke-shell-core] Missing inherited child cwd output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "readln: stdin handoff works" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing foreground stdin handoff output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "PID  PPID  MODE" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing ps task snapshot output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'shell core interactive echo' $(BUILD)/qemu-smoke-shell-core.log)" -lt 2 ]; then \
		echo "[qemu-smoke-shell-core] Missing echo output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "SkezOS tarfs demo" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing cat output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "run: launch failed" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing unknown-command failure output"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sys_waitpid: parent_pid=" $(BUILD)/qemu-smoke-shell-core.log || ! grep -Fq "task=user-shell waited_pid=" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing user-shell waitpid log"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sh: exit" $(BUILD)/qemu-smoke-shell-core.log; then \
		echo "[qemu-smoke-shell-core] Missing shell exit line"; \
		tail -n 220 $(BUILD)/qemu-smoke-shell-core.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-shell-core] PASS (interactive shell)"


qemu-smoke-reliability: $(ISO)
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/qemu-smoke-reliability.log
	@echo "[qemu-smoke-reliability] Booting headless VM with scripted reliability input..."
	$(call RUN_SCRIPTED_SHELL_SMOKE,qemu-smoke-reliability,$(BUILD)/qemu-smoke-reliability.log,$(RELIABILITY_SMOKE_SCRIPT))
	$(call ASSERT_SHELL_BOOT_READY,qemu-smoke-reliability,$(BUILD)/qemu-smoke-reliability.log)
	@if ! grep -Fq "uptime: " $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing uptime output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sleep: requested=2 observed=" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing sleep output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "pipeline smoke" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing basic pipe output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'SkezOS tarfs demo' $(BUILD)/qemu-smoke-reliability.log)" -lt 1 ]; then \
		echo "[qemu-smoke-reliability] Missing redirected pipeline cat output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-reliability.log | grep -Fxq "hello"; then \
		echo "[qemu-smoke-reliability] Missing root redirect round-trip output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-reliability.log | grep -Fxq "reliability-v1"; then \
		echo "[qemu-smoke-reliability] Missing redirect create output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! tr -d '\r' < $(BUILD)/qemu-smoke-reliability.log | grep -Fxq "reliability-v2"; then \
		echo "[qemu-smoke-reliability] Missing redirect append output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if [ "$$(grep -Fc 'run: redirect open failed' $(BUILD)/qemu-smoke-reliability.log)" -ne 1 ]; then \
		echo "[qemu-smoke-reliability] Unexpected redirect failure count"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "run: builtin does not support pipes/redirection" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing builtin pipeline rejection output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: pipe bad ptr ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag pipe bad-pointer check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: dup invalid fd ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag dup invalid-fd check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: dup2 invalid target fd ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag dup2 invalid-target check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: spawn_ex bad flags ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag spawn_ex bad-flags check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: spawn missing path ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag spawn missing-path check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: spawn/wait hello ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag spawn/wait hello check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: waitpid reap again ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag waitpid reap-again check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: waitpid non-child ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag waitpid non-child check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: read invalid fd ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag read invalid-fd check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: close invalid fd ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag close invalid-fd check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: open missing path ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag open missing-path check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: fd open/read/close flow ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag open/read/close flow check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: pipe eof after writer close ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag pipe EOF check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: pipe broken write ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag broken-pipe check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: time_info bad ptr ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag time_info bad-pointer check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: time_info valid ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag time_info valid check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: waitpid options notsup ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag waitpid options-not-supported check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: task_snapshot bad ptr ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag task_snapshot bad-pointer check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: task_snapshot zero cap ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag task_snapshot zero-cap check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: task_snapshot cap one ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag task_snapshot cap-one check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: list_dir bad req ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag list_dir bad-request check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: list_dir root zero cap ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag list_dir root zero-cap check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: list_dir root cap one ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag list_dir root cap-one check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: getcwd bad ptr ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag getcwd bad-pointer check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: getcwd zero len ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag getcwd zero-length check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: chdir file notsup ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag chdir file-not-supported check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: cwd root roundtrip ok" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag cwd round-trip check"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "diag: PASS" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing diag syscall self-check output"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@if ! grep -Fq "sh: exit" $(BUILD)/qemu-smoke-reliability.log; then \
		echo "[qemu-smoke-reliability] Missing shell exit line"; \
		tail -n 220 $(BUILD)/qemu-smoke-reliability.log; \
		exit 1; \
	fi
	@echo "[qemu-smoke-reliability] PASS"

check-pr:
	@$(MAKE) toolchain-check
	@$(MAKE) clean
	@$(MAKE) all
	@$(MAKE) qemu-smoke-userfault
	@$(MAKE) qemu-smoke-shell-core
	@$(MAKE) qemu-smoke-reliability

check: check-pr

check-release: check-pr
	@$(MAKE) qemu-smoke-lifecycle

check-nightly: check-release

clean:
	rm -rf $(BUILD) $(ISO) iso/boot/kernel.elf

# Build object from assembly in the kernel directory
$(BUILD)/idt_load.o: kernel/idt_load.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/sched_switch.o: kernel/sched_switch.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/gdt_flush.o: kernel/gdt_flush.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/syscall_entry.o: kernel/syscall_entry.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/user_demo_blob.o: kernel/user_demo_blob.S
	$(AS) $(ASFLAGS) $< -o $@
